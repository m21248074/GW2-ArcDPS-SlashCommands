#include "engine_tick.hpp"
#include <MinHook.h>
namespace slash {
EngineTick* EngineTick::instance_{};
bool EngineTick::start(EventDispatch target,Callback callback,std::string& error) {
    if(!target || !callback || inFlight_.load()) { error="EngineTick 攔截無效或忙碌中。"; return false; }
    if(created_ && target!=target_) { error="EngineTick 目標已變更，請重新啟動 GW2。"; return false; }
    if(instance_ && instance_!=this) { error="EngineTick 攔截已被使用。"; return false; }
    auto result=MH_Initialize();
    if(result!=MH_OK && result!=MH_ERROR_ALREADY_INITIALIZED) { error="無法初始化 EngineTick 攔截。"; return false; }
    callback_=callback; target_=target; instance_=this;
    if(!created_) {
        result=MH_CreateHook(reinterpret_cast<void*>(target_),reinterpret_cast<void*>(&detour),reinterpret_cast<void**>(&original_));
        if(result!=MH_OK) { error="無法建立 EngineTick 分派攔截。"; return false; }
        created_=true;
    }
    running_=true;
    result=MH_EnableHook(reinterpret_cast<void*>(target_));
    if(result!=MH_OK && result!=MH_ERROR_ENABLED) { running_=false; error="無法啟用 EngineTick 攔截。"; return false; }
    return true;
}
bool EngineTick::stop() {
    running_=false;
    if(!created_) return true;
    auto result=MH_DisableHook(reinterpret_cast<void*>(target_));
    // Retain code, instance, callback and trampoline. A suspended thread may
    // have entered the detour before its first instruction. The owner is pinned.
    return result==MH_OK || result==MH_ERROR_DISABLED;
}
std::uintptr_t EngineTick::detour(void* api,void* engine,std::uint32_t flags,std::uint32_t event,
                                std::uint32_t size,void* data,void* extra) {
    auto self=instance_;
    self->inFlight_.fetch_add(1);
    auto result=self->original_(api,engine,flags,event,size,data,extra);
    if(event==2 && self->running_.load()) {
        self->ticks_.fetch_add(1);
        self->thread_.store(GetCurrentThreadId());
        self->callback_();
    }
    self->inFlight_.fetch_sub(1);
    return result;
}
}
