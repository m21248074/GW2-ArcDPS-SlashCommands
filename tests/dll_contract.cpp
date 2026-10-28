#include <Windows.h>
#include <imgui.h>
#include <cstdint>
#include <cstdlib>
#include <iostream>
struct Exports {
    std::uint64_t size; std::uint32_t sig,imguivers; const char* name; const char* build;
    void* wnd; void* combat; void* imgui; void* options; void* local; void* filtered; void* windows;
};
void* alloc(std::size_t n,void*) { return std::malloc(n); }
void dealloc(void* p,void*) { std::free(p); }
int main(int argc,char** argv) {
    if(argc!=2) return 1;
    auto module=LoadLibraryA(argv[1]); if(!module) return 2;
    using Entry=void* (*)(char*,void*,void*,HMODULE,void*,void*,std::uint32_t);
    using Release=void* (*)(std::uint32_t);
    auto entry=reinterpret_cast<Entry>(GetProcAddress(module,"get_init_addr"));
    auto release=reinterpret_cast<Release>(GetProcAddress(module,"get_release_addr"));
    if(!entry || !release) return 3;
    auto init=reinterpret_cast<Exports* (*)()>(entry(nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,0));
    auto rejected=init(); if(rejected->sig!=0 || !rejected->size) return 4;
    auto context=ImGui::CreateContext();
    init=reinterpret_cast<Exports* (*)()>(entry(nullptr,context,nullptr,nullptr,
        reinterpret_cast<void*>(&alloc),reinterpret_cast<void*>(&dealloc),IMGUI_VERSION_NUM));
    auto exp=init();
    if(exp->sig==0 || exp->size!=sizeof(Exports) || exp->imguivers!=IMGUI_VERSION_NUM || !exp->imgui || !exp->options) return 5;
    auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize=ImVec2(1280,720); io.DeltaTime=1.0f/60;
    unsigned char* pixels{}; int width{},height{};
    io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    ImGui::NewFrame();
    reinterpret_cast<void (*)(std::uint32_t,std::uint32_t)>(exp->imgui)(0,0);
    ImGui::Begin("Options"); reinterpret_cast<void (*)()>(exp->options)(); ImGui::End();
    ImGui::Render();
    reinterpret_cast<void (*)()>(release(0))();
    ImGui::DestroyContext(context);
    FreeLibrary(module);
    std::cout<<"DLL exports, version rejection, shared ImGui frame, options and release passed.\n";
}
