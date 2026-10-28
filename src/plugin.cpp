#include <Windows.h>
#include <imgui.h>
#include <MinHook.h>
#include <shellapi.h>
#include "locator.hpp"
#include "commands.hpp"
#include "engine_tick.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace {
struct Exports {
    std::uint64_t size; std::uint32_t sig, imguivers;
    const char* name; const char* build;
    void* wnd; void* combat; void* imgui; void* options;
    void* combatLocal; void* wndFilter; void* windows;
};
static_assert(sizeof(Exports)==88);
Exports exports{};
bool handoff{}, visible=true;
bool madKingOnly{};
slash::Category selectedCategory=slash::Category::Basic;
std::atomic<bool> active{};
std::string error;
using Handler=std::uintptr_t (*)(const wchar_t*);
Handler handler{};
Handler original{};
bool hookCreated{}, hookEnabled{};
std::string hookStatus;
using Log=void (*)(const char*);
Log arcLog{};
slash::EngineTick engineTick;
slash::EventDispatch eventDispatch{};
std::atomic<bool> allowCommands{};
std::atomic<ULONGLONG> renderHeartbeat{};
std::atomic_flag consuming=ATOMIC_FLAG_INIT;
std::atomic<unsigned> state{}; // 0 ready, 1 running, 2 returned, 3 exception
std::atomic<DWORD> exceptionCode{};
std::uintptr_t handlerRva{};
std::filesystem::path ini;
std::mutex queueMutex;
std::deque<std::wstring> queue;
std::string lastCommand;
std::string notice;
char filter[80]{};
ULONGLONG nextDispatch{};
void log(const char* text) { if(arcLog) arcLog(text); }
std::string utf8(std::wstring_view text) {
    if(text.empty()) return {};
    auto length=WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    std::string result(static_cast<std::size_t>(length),'\0');
    if(length) WideCharToMultiByte(CP_UTF8,0,text.data(),static_cast<int>(text.size()),result.data(),length,nullptr,nullptr);
    return result;
}

void save() {
    if(ini.empty()) return;
    bool ok=WritePrivateProfileStringW(L"general",L"visible",visible?L"1":L"0",ini.c_str())!=FALSE;
    ok=WritePrivateProfileStringW(L"general",L"hotkeys",nullptr,ini.c_str()) && ok;
    ok=WritePrivateProfileStringW(L"bindings",nullptr,nullptr,ini.c_str()) && ok;
    if(!ok) notice="無法儲存設定，請檢查資料夾權限。";
}
void load() {
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,32768);
    ini=std::filesystem::path(path).parent_path()/L"addons/arcdps/arcdps_slashcommands.ini";
    std::error_code ec; std::filesystem::create_directories(ini.parent_path(),ec);
    visible=GetPrivateProfileIntW(L"general",L"visible",1,ini.c_str())!=0;
    save(); // Remove obsolete shortcut settings during migration.
}
bool openWebsite(const wchar_t* text) {
    auto url=slash::website(text);
    if(url.empty()) return false;
    auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",url.data(),nullptr,nullptr,SW_SHOWNORMAL));
    if(result<=32) OutputDebugStringA("Slash Commands: website launch failed.\n");
    return true;
}
std::uintptr_t onCommand(const wchar_t* text) {
    if(active.load() && text && openWebsite(text)) return 1;
    return original(text);
}
void installHook() {
    if(!handler) return;
    auto result=MH_Initialize();
    if(result!=MH_OK && result!=MH_ERROR_ALREADY_INITIALIZED) { hookStatus="聊天指令無法使用：攔截初始化失敗。"; return; }
    if(!hookCreated) {
        result=MH_CreateHook(reinterpret_cast<void*>(handler),reinterpret_cast<void*>(&onCommand),reinterpret_cast<void**>(&original));
        if(result!=MH_OK) { hookStatus="聊天指令無法使用：指令攔截失敗。"; return; }
        hookCreated=true;
    }
    result=MH_EnableHook(reinterpret_cast<void*>(handler));
    hookEnabled=result==MH_OK || result==MH_ERROR_ENABLED;
    hookStatus=hookEnabled?"聊天指令 /sc 已啟用。":"聊天指令無法使用：啟用攔截失敗。";
}

bool resolve() {
    handler=nullptr; eventDispatch=nullptr;
    auto base=reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"Gw2-64.exe"));
    if(!base) { error="尚未載入 Gw2-64.exe。"; return false; }
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE) { error="遊戲執行檔格式無效。"; return false; }
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        error="需要 64 位元遊戲執行檔。"; return false;
    }
    // This prototype is intentionally limited to the locally analyzed layout.
    // Signature matching is still performed; no function address is hardcoded.
    std::vector<slash::Range> ranges;
    auto section=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
        if(section[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
            ranges.push_back({section[i].VirtualAddress,section[i].Misc.VirtualSize});
    auto found=slash::locate({base,nt->OptionalHeader.SizeOfImage},ranges);
    if(found.size()!=1) { error="無法唯一定位指令處理函式，已停用按鈕。"; return false; }
    // Fail closed on changed function prologue until that version is analyzed.
    constexpr std::uint8_t entry[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf1,0xe8};
    if(std::memcmp(base+found[0],entry,sizeof(entry))!=0) {
        error="指令函式入口不同或已被攔截，已停用按鈕。"; return false;
    }
    handlerRva=found[0]; handler=reinterpret_cast<Handler>(base+handlerRva);
    auto events=slash::locateEventDispatch({base,nt->OptionalHeader.SizeOfImage},ranges);
    if(events.size()!=1) { error="無法唯一定位 EngineTick 分派函式，已停用指令執行。"; return false; }
    eventDispatch=reinterpret_cast<slash::EventDispatch>(base+events[0]);
    return true;
}
bool invokeCommand(const wchar_t* command) {
    // SEH is diagnostic only; it cannot guarantee recovery after a game fault.
    __try {
        if(!openWebsite(command)) (original?original:handler)(command);
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { exceptionCode.store(GetExceptionCode()); state.store(3); }
    return false;
}
void enqueue(std::size_t index) {
    if(!active || !allowCommands || !handler || state.load()==3 || index>=slash::commands.size()) return;
    std::lock_guard lock(queueMutex);
    if(queue.size()>=8) { notice="指令佇列已滿。"; return; }
    auto cmd=slash::commands[index];
    queue.emplace_back(cmd.begin(),cmd.end());
}
void dispatch() {
    if(!handler || state.load()==3 || state.load()==1 || GetTickCount64()<nextDispatch) return;
    std::wstring command;
    {
        std::lock_guard lock(queueMutex);
        if(queue.empty()) return;
        command=std::move(queue.front()); queue.pop_front();
        lastCommand=utf8(command);
    }
    state.store(1);
    nextDispatch=GetTickCount64()+250;
    if(invokeCommand(command.c_str())) state.store(2);
}
void onEngineTick() noexcept {
    if(consuming.test_and_set()) return;
    struct Reset { ~Reset() { consuming.clear(); } } reset;
    try {
        // EngineTick can be nested: never recursively consume the queue.
        if(!active || !allowCommands || GetTickCount64()-renderHeartbeat.load()>1000) {
            std::lock_guard lock(queueMutex); queue.clear(); return;
        }
        dispatch();
    } catch(...) {
        exceptionCode.store(0xe06d7363); state.store(3);
    }
}
void frame(std::uint32_t inGame,std::uint32_t) {
    if(!active) return;
    allowCommands=inGame && handler && engineTick.running();
    renderHeartbeat=GetTickCount64();
    if(!inGame || state.load()==3) { std::lock_guard lock(queueMutex); queue.clear(); }
    if(!visible) return;
    ImGui::SetNextWindowSize(ImVec2(560,500),ImGuiCond_FirstUseEver);
    bool wasVisible=visible;
    if(ImGui::Begin("Slash Commands###Slash Commands",&visible)) {
        if(!error.empty()) ImGui::TextWrapped("%s",error.c_str());
        const float checkboxWidth=ImGui::GetFrameHeight()+ImGui::GetStyle().ItemInnerSpacing.x
            +ImGui::CalcTextSize("瘋王說").x;
        const float searchWidth=ImGui::GetContentRegionAvail().x-checkboxWidth-ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetNextItemWidth(searchWidth>1?searchWidth:1);
        ImGui::InputTextWithHint("##filter","搜尋指令…",filter,sizeof(filter));
        ImGui::SameLine();
        if(ImGui::Checkbox("瘋王說",&madKingOnly)) filter[0]='\0';
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("只顯示該活動表情");
        if(madKingOnly) {
            ImGui::TextDisabled("18 個活動表情");
            ImGui::TextWrapped("聽到「Mad King says…」才做動作；沒有說就不要動作。");
        } else ImGui::TextDisabled("61 個指令與 1 個網站");
        if(!madKingOnly && ImGui::BeginTabBar("categories")) {
            const char* names[]{"基本表情", "需解鎖", "其他指令"};
            for(int tab=0;tab<3;++tab) {
                if(ImGui::BeginTabItem(names[tab])) {
                    selectedCategory=static_cast<slash::Category>(tab);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        if(!madKingOnly && selectedCategory==slash::Category::Unlock)
            ImGui::TextWrapped("包含購買、成就及劇情獎勵；請先解鎖對應表情。");
        if(!inGame) ImGui::TextUnformatted("登入角色後即可使用指令。");
        if(ImGui::BeginChild("commands",ImVec2(0,0))) {
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,ImVec2(0.5f,0.5f));
            const float spacing=ImGui::GetStyle().ItemSpacing.x;
            const float available=ImGui::GetContentRegionAvail().x;
            float targetWidth=ImGui::GetFontSize()*6.0f;
            for(std::size_t i=0;i<slash::commands.size();++i) {
                auto label=std::string(slash::labels[i])+"\n"+std::string(slash::commands[i]);
                float width=ImGui::CalcTextSize(label.c_str()).x+ImGui::GetStyle().FramePadding.x*2;
                if(width>targetWidth) targetWidth=width;
            }
            int columns=static_cast<int>((available+spacing)/(targetWidth+spacing));
            if(columns<1) columns=1;
            const float buttonWidth=(available-spacing*(columns-1))/columns;
            int shown=0;
            for(std::size_t i=0;i<slash::commands.size();++i) {
                if(madKingOnly && !slash::isMadKingEmote(slash::commands[i])) continue;
                if(!madKingOnly && slash::category(slash::commands[i])!=selectedCategory) continue;
                if(filter[0] && slash::commands[i].find(filter)==std::string_view::npos
                    && slash::labels[i].find(filter)==std::string_view::npos) continue;
                if(shown++%columns) ImGui::SameLine();
                ImGui::PushID(static_cast<int>(i));
                ImGui::BeginDisabled(!allowCommands || state.load()==3);
                const float lineHeight=ImGui::GetTextLineHeight();
                if(ImGui::Button("##command",ImVec2(buttonWidth,lineHeight*2+ImGui::GetStyle().FramePadding.y*2))) enqueue(i);
                const auto min=ImGui::GetItemRectMin(), max=ImGui::GetItemRectMax();
                float y=min.y+(max.y-min.y-lineHeight*2)*0.5f;
                for(auto text:{slash::labels[i],slash::commands[i]}) {
                    const float x=min.x+(max.x-min.x-ImGui::CalcTextSize(text.data()).x)*0.5f;
                    ImGui::GetWindowDrawList()->AddText(ImVec2(x,y),ImGui::GetColorU32(ImGuiCol_Text),text.data());
                    y+=lineHeight;
                }
                ImGui::EndDisabled();
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    if(i==0) ImGui::SetTooltip("/gg 會在遊戲支援的區域讓角色戰敗。");
                    else ImGui::SetTooltip("%s",slash::commands[i].data());
                }
                ImGui::PopID();
            }
            if(!shown) ImGui::TextDisabled("找不到符合的指令。");
            ImGui::PopStyleVar();
        }
        ImGui::EndChild();
    }
    ImGui::End();
    if(wasVisible!=visible) save();
}
void options() {
    if(ImGui::Checkbox("顯示指令視窗",&visible)) save();
    if(!hookEnabled && !hookStatus.empty()) ImGui::TextWrapped("%s",hookStatus.c_str());
        std::string shownCommand;
        { std::lock_guard lock(queueMutex); shownCommand=lastCommand; }
        switch(state.load()) {
        case 0: ImGui::TextUnformatted("就緒"); break;
        case 1: ImGui::Text("正在執行 %s…",shownCommand.c_str()); break;
        case 2: ImGui::Text("%s：呼叫已返回。",shownCommand.c_str()); break;
        case 3: ImGui::Text("執行失敗：0x%08lX",exceptionCode.load()); break;
        }
        {
            std::lock_guard lock(queueMutex);
            if(!queue.empty()) { ImGui::SameLine(); ImGui::Text("等待中：%zu",queue.size()); }
        }
        if(!notice.empty()) ImGui::TextWrapped("%s",notice.c_str());
    ImGui::TextDisabled("EngineTick：%llu | 執行緒 %lu",engineTick.count(),engineTick.threadId());
}
Exports* initialize() {
    if(active) return &exports;
    exports={};
    if(state.load()==1 || engineTick.inFlight()) {
        error="前一個指令仍在執行，請重新啟動 GW2 後再載入。";
        exports.size=reinterpret_cast<std::uint64_t>(error.c_str()); return &exports;
    }
    if(!handoff) {
        error="Slash Commands需要 arcdps 提供 ImGui 1.92.7 環境與記憶體配置函式。";
        exports.size=reinterpret_cast<std::uint64_t>(error.c_str()); return &exports;
    }
    error.clear();
    // Detour and trampoline must remain mapped during concurrent game callbacks.
    HMODULE pinned{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                         reinterpret_cast<LPCWSTR>(&initialize),&pinned)) {
        error="無法保留卸載期間所需的攔截程式碼。";
        exports.size=reinterpret_cast<std::uint64_t>(error.c_str()); return &exports;
    }
    load();
    if(resolve()) { installHook(); engineTick.start(eventDispatch,&onEngineTick,error); }
    active=true;
    exports={sizeof(Exports),0x534c5031,IMGUI_VERSION_NUM,"Slash Commands","1.0.0",
        nullptr,nullptr,reinterpret_cast<void*>(&frame),reinterpret_cast<void*>(&options),nullptr,nullptr,nullptr};
    log(engineTick.running()?"Slash Commands 1.0.0: EngineTick hook enabled.":error.c_str());
    return &exports;
}
void release() {
    active=false; allowCommands=false;
    if(!engineTick.stop()) log("Slash Commands: EngineTick hook disable failed; pinned passthrough retained.");
    save();
    { std::lock_guard lock(queueMutex); queue.clear(); }
    if(hookEnabled) {
        if(MH_DisableHook(reinterpret_cast<void*>(handler))==MH_OK) hookEnabled=false;
        else log("Slash Commands: hook disable failed; pinned passthrough retained.");
    }
    // Keep trampoline allocated: a game thread may already be inside the detour.
    log("Slash Commands: stopped; module remains mapped until game exit.");
}
}
extern "C" __declspec(dllexport) void* get_init_addr(char*,void* context,void*,HMODULE arc,
    void* allocate,void* deallocate,std::uint32_t version) {
    arcLog=arc?reinterpret_cast<Log>(GetProcAddress(arc,"e3")):nullptr;
    handoff=version==IMGUI_VERSION_NUM && context && allocate && deallocate;
    if(handoff) {
        ImGui::SetAllocatorFunctions(reinterpret_cast<ImGuiMemAllocFunc>(allocate),reinterpret_cast<ImGuiMemFreeFunc>(deallocate));
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(context));
    }
    return reinterpret_cast<void*>(&initialize);
}
extern "C" __declspec(dllexport) void* get_release_addr(std::uint32_t) { return reinterpret_cast<void*>(&release); }
