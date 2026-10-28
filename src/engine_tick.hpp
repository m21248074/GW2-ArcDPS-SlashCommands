#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <string>
namespace slash {
using EventDispatch=std::uintptr_t (*)(void*,void*,std::uint32_t,std::uint32_t,
                                     std::uint32_t,void*,void*);
class EngineTick {
public:
    using Callback=void (*)();
    bool start(EventDispatch target,Callback callback,std::string& error);
    bool stop();
    std::uint64_t count() const { return ticks_.load(); }
    DWORD threadId() const { return thread_.load(); }
    unsigned inFlight() const { return inFlight_.load(); }
    bool running() const { return running_.load(); }
private:
    static std::uintptr_t detour(void*,void*,std::uint32_t,std::uint32_t,std::uint32_t,void*,void*);
    static EngineTick* instance_;
    EventDispatch target_{},original_{};
    Callback callback_{};
    std::atomic<bool> running_{};
    std::atomic<std::uint64_t> ticks_{};
    std::atomic<DWORD> thread_{};
    std::atomic<unsigned> inFlight_{};
    bool created_{};
};
}
