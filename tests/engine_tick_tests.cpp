#include "../src/engine_tick.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
slash::EngineTick tick;
std::array<std::uintptr_t,7> received{};
unsigned originalCalls{},callbacks{};
DWORD callbackThread{};
bool order=true;
std::atomic<bool> blockCallback{},entered{},finish{};
__declspec(noinline) std::uintptr_t fake(void* a,void* b,std::uint32_t c,std::uint32_t d,
                                       std::uint32_t e,void* f,void* g) {
    received={reinterpret_cast<std::uintptr_t>(a),reinterpret_cast<std::uintptr_t>(b),c,d,e,
              reinterpret_cast<std::uintptr_t>(f),reinterpret_cast<std::uintptr_t>(g)};
    ++originalCalls; return 0x12345678abcdef01ull;
}
void callback() {
    ++callbacks; callbackThread=GetCurrentThreadId(); order=order && originalCalls>=callbacks;
    if(blockCallback) { entered=true; while(!finish.load()) Sleep(1); }
}
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
}
int main() {
    try {
        std::string error;
        require(tick.start(&fake,&callback,error),error.c_str());
        slash::EventDispatch volatile invoke=&fake;
        auto result=invoke(reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),3,2,5,
                           reinterpret_cast<void*>(6),reinterpret_cast<void*>(7));
        require(result==0x12345678abcdef01ull,"return register changed");
        require(received==std::array<std::uintptr_t,7>{1,2,3,2,5,6,7},"register/stack arguments changed");
        require(callbacks==1 && order,"callback missing or ran before original");
        DWORD emittedThread{};
        std::thread emitter([&] { emittedThread=GetCurrentThreadId(); invoke(nullptr,nullptr,0,2,0,nullptr,nullptr); });
        emitter.join();
        require(callbackThread==emittedThread && tick.threadId()==emittedThread,"callback ran on wrong thread");
        invoke(nullptr,nullptr,0,35,0,nullptr,nullptr);
        require(callbacks==2,"non-EngineTick event called callback");
        require(tick.inFlight()==0,"callback accounting leaked");
        require(tick.stop(),"disable failed");
        invoke(nullptr,nullptr,0,2,0,nullptr,nullptr);
        require(callbacks==2,"disabled hook invoked callback");
        require(tick.start(&fake,&callback,error),"restart failed");
        invoke(nullptr,nullptr,0,2,0,nullptr,nullptr);
        require(callbacks==3,"restarted hook missing");
        blockCallback=true;
        std::thread pending([&] { invoke(nullptr,nullptr,0,2,0,nullptr,nullptr); });
        auto deadline=GetTickCount64()+2000;
        while(!entered.load() && GetTickCount64()<deadline) Sleep(1);
        bool reached=entered.load();
        bool stopped=tick.stop();
        bool refused=!tick.start(&fake,&callback,error);
        finish=true; pending.join();
        require(reached && stopped && refused,"in-flight disable/reload guard failed");
        require(tick.inFlight()==0,"in-flight callback did not finish safely");
        std::cout<<"Seven-argument ABI, return, original-first order, event filtering, calling thread and stop/restart passed.\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
