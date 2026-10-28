// Test plugin queue on an intercepted synthetic EngineTick, never the game.
#include "../src/plugin.cpp"
#include <iostream>
#include <stdexcept>
#include <set>
namespace {
std::mutex seenMutex;
std::vector<std::wstring> seen;
std::atomic<unsigned> eventsSeen{};
std::atomic<DWORD> commandThread{};
bool nestEvent=true;
using Dispatch=slash::EventDispatch;
__declspec(noinline) std::uintptr_t fakeEvent(void*,void*,std::uint32_t flags,std::uint32_t event,
                                             std::uint32_t size,void*,void*) {
    eventsSeen.fetch_add(1); return 0x5000+flags+event+size;
}
__declspec(noinline) std::uintptr_t fakeHandler(const wchar_t* text) {
    commandThread=GetCurrentThreadId();
    if(nestEvent) {
        nestEvent=false;
        slash::EventDispatch volatile nested=&fakeEvent;
        nested(nullptr,nullptr,0,2,0,nullptr,nullptr);
    }
    std::lock_guard lock(seenMutex); seen.emplace_back(text); return 1;
}
void require(bool ok,const char* reason) { if(!ok) throw std::runtime_error(reason); }
}
int main() {
    try {
        std::set<std::string_view> unique(slash::commands.begin(),slash::commands.end());
        require(unique.size()==62 && !unique.contains("/rc"),"catalog mismatch or removed rc present");
        require(slash::commands[0]=="/gg" && slash::commands[49]=="/sleep","catalog mismatch");
        require(slash::website(L"/rc extra").empty(),"removed rc still intercepted");
        require(slash::website(L"/sc")==L"https://snowcrows.com","sc dispatch mismatch");
        require(slash::website(L"/scammer").empty(),"prefix incorrectly intercepted");
        handler=fakeHandler; active=true; allowCommands=true; renderHeartbeat=GetTickCount64();
        std::string tickError;
        require(engineTick.start(&fakeEvent,&onEngineTick,tickError),"tick hook failed");
        Dispatch volatile tick=&fakeEvent;
        enqueue(49); enqueue(14); enqueue(0);
        require(tick(nullptr,nullptr,0,1,0,nullptr,nullptr)==0x5001,"non-tick return changed");
        require(seen.empty(),"non-tick event executed a command");
        for(int i=0;i<3;++i) {
            nextDispatch=0;
            require(tick(nullptr,nullptr,3,2,16,nullptr,nullptr)==0x5015,"event return changed");
            require(state.load()==2,"tick dispatch failed");
            require(commandThread.load()==GetCurrentThreadId(),"command escaped event thread");
        }
        require(seen==std::vector<std::wstring>{L"/sleep",L"/dance",L"/gg"},"queue order/payload changed");
        for(int i=0;i<12;++i) enqueue(49);
        require(queue.size()==8,"queue limit not enforced"); queue.clear();
        enqueue(49); renderHeartbeat=GetTickCount64()-2000; nextDispatch=0;
        tick(nullptr,nullptr,0,2,0,nullptr,nullptr);
        require(queue.empty() && seen.size()==3,"stale render heartbeat executed command");
        renderHeartbeat=GetTickCount64(); enqueue(49); allowCommands=false;
        tick(nullptr,nullptr,0,2,0,nullptr,nullptr);
        require(queue.empty() && seen.size()==3,"loading-state request executed");
        allowCommands=true;
        active=false; enqueue(49); require(queue.empty(),"inactive request accepted");
        active=true; state=3; enqueue(49); require(queue.empty(),"request after exception accepted");
        state=0;
        installHook(); require(hookEnabled && original,"passthrough hook failed");
        Handler volatile invoke=handler;
        require(invoke(L"/wave")==1,"noncustom command not passed to original");
        require(seen.back()==L"/wave","passthrough payload changed");
        require(invoke(L"/rc")==1 && seen.back()==L"/rc","removed rc not forwarded to game");
        active=false;
        require(invoke(L"/rc")==1 && seen.back()==L"/rc","inactive hook did not pass through");
        require(MH_DisableHook(reinterpret_cast<void*>(handler))==MH_OK,"hook disable failed");
        hookEnabled=false;
        require(engineTick.stop(),"tick hook disable failed");
        auto stopped=engineTick.count();
        tick(nullptr,nullptr,0,2,0,nullptr,nullptr);
        require(engineTick.count()==stopped,"disabled hook ran callback");
        wchar_t temp[MAX_PATH]{}; GetTempPathW(MAX_PATH,temp);
        ini=std::filesystem::path(temp)/(L"slashcommands-test-"+std::to_wstring(GetCurrentProcessId())+L".ini");
        WritePrivateProfileStringW(L"bindings",L"/rc",L"123",ini.c_str());
        WritePrivateProfileStringW(L"general",L"hotkeys",L"1",ini.c_str());
        WritePrivateProfileStringW(L"bindings",L"/sleep",L"843",ini.c_str());
        visible=false; save();
        wchar_t removed[16]{};
        GetPrivateProfileStringW(L"bindings",L"/rc",L"missing",removed,16,ini.c_str());
        require(std::wstring_view(removed)==L"missing","old rc setting not removed");
        require(GetPrivateProfileIntW(L"general",L"visible",1,ini.c_str())==0,"visibility not persisted");
        require(GetPrivateProfileIntW(L"general",L"hotkeys",-1,ini.c_str())==static_cast<UINT>(-1),"obsolete hotkey option retained");
        GetPrivateProfileStringW(L"bindings",L"/sleep",L"missing",removed,16,ini.c_str());
        require(std::wstring_view(removed)==L"missing","obsolete binding retained");
        DeleteFileW(ini.c_str()); ini.clear(); active=false;
        std::cout<<"Catalog, aliases, queue, EngineTick thread, non-tick passthrough, stale/loading cancellation, hook stop and INI persistence passed.\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
