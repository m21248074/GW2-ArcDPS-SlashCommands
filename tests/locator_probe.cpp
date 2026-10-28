#include <Windows.h>
#include "locator.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstring>
int wmain(int argc,wchar_t** argv) {
    if(argc!=2) return 1;
    std::ifstream file(argv[1],std::ios::binary);
    std::vector<std::uint8_t> disk{std::istreambuf_iterator<char>(file),{}};
    if(disk.size()<sizeof(IMAGE_DOS_HEADER)) return 2;
    auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(disk.data());
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<0
        || static_cast<std::size_t>(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64)>disk.size()) return 3;
    auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(disk.data()+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC) return 4;
    auto sections=IMAGE_FIRST_SECTION(nt);
    if(reinterpret_cast<const std::uint8_t*>(sections+nt->FileHeader.NumberOfSections)>disk.data()+disk.size()) return 5;
    std::vector<std::uint8_t> mapped(nt->OptionalHeader.SizeOfImage);
    std::vector<slash::Range> code;
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) {
        auto& s=sections[i];
        if(s.PointerToRawData>disk.size() || s.SizeOfRawData>disk.size()-s.PointerToRawData
            || s.VirtualAddress>mapped.size() || s.SizeOfRawData>mapped.size()-s.VirtualAddress) return 6;
        std::memcpy(mapped.data()+s.VirtualAddress,disk.data()+s.PointerToRawData,s.SizeOfRawData);
        if(s.Characteristics&IMAGE_SCN_MEM_EXECUTE) code.push_back({s.VirtualAddress,s.Misc.VirtualSize});
    }
    auto handlers=slash::locate(mapped,code), events=slash::locateEventDispatch(mapped,code);
    for(auto r:handlers) std::cout<<"Command handler RVA: 0x"<<std::hex<<r<<'\n';
    for(auto r:events) std::cout<<"Event dispatcher RVA: 0x"<<std::hex<<r<<'\n';
    return handlers.size()==1 && events.size()==1?0:7;
}
