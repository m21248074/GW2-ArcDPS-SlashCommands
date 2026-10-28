#include "locator.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
int main() {
    try {
        auto require=[](bool ok) { if(!ok) throw std::runtime_error("locator assertion failed"); };
        std::vector<std::uint8_t> image(4096);
        const std::array<slash::Range,1> code{{{256,1024}}};
        auto add=[&](std::size_t at,std::int32_t text,std::int32_t target) {
            const std::uint8_t bytes[]{0x48,0x8d,0x0d,0,0,0,0,0x48,0x83,0xc4,0x28,0xe9,0,0,0,0};
            std::memcpy(image.data()+at,bytes,sizeof(bytes));
            auto disp=text-static_cast<std::int32_t>(at+7);
            std::memcpy(image.data()+at+3,&disp,4);
            disp=target-static_cast<std::int32_t>(at+16);
            std::memcpy(image.data()+at+12,&disp,4);
        };
        const char16_t text[]=u"/.disconnect";
        std::memcpy(image.data()+2048,text,sizeof(text));
        add(400,2048,800);
        require(slash::locate(image,code)==std::vector<std::size_t>{800});
        image[2048]='x'; require(slash::locate(image,code).empty());
        std::memcpy(image.data()+2048,text,sizeof(text));
        add(400,-1,800); require(slash::locate(image,code).empty());
        add(400,4090,800); require(slash::locate(image,code).empty());
        add(400,2048,3000); require(slash::locate(image,code).empty());
        add(400,2048,800); add(500,2048,900);
        require(slash::locate(image,code).size()==2); // caller must reject ambiguity
        // Signed backward displacements, and final possible scan position.
        std::fill(image.begin()+256,image.begin()+1280,std::uint8_t{});
        add(1264,2048,800);
        require(slash::locate(image,code)==std::vector<std::size_t>{800});
        const std::array<slash::Range,1> invalid{{{4090,1024}}};
        require(slash::locate(image,invalid).empty());
        std::vector<std::uint8_t> eventImage(4096);
        const std::uint8_t prologue[]{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,
            0x48,0x8d,0x6c,0x24,0xf1,0x48,0x81,0xec,0xd0,0,0,0};
        auto eventFixture=[&](std::size_t at,std::int32_t textAt) {
            std::memcpy(eventImage.data()+at,prologue,sizeof(prologue));
            const std::uint8_t cmp[]{0x48,0x83,0xfb,0x31,0x72,0x19};
            std::memcpy(eventImage.data()+at+0xc3,cmp,sizeof(cmp));
            eventImage[at+0xd6]=0x48; eventImage[at+0xd7]=0x8d; eventImage[at+0xd8]=0x0d;
            auto disp=textAt-static_cast<std::int32_t>(at+0xdd);
            std::memcpy(eventImage.data()+at+0xd9,&disp,4);
            const std::uint8_t bucket[]{0x49,0xc1,0xe6,0x05};
            std::memcpy(eventImage.data()+at+0xe9,bucket,sizeof(bucket));
            eventImage[at+0x1ac]=0xff; eventImage[at+0x1ad]=0xd0;
        };
        constexpr char assertion[]="id < arrsize(m_handler)";
        std::memcpy(eventImage.data()+2000,assertion,sizeof(assertion));
        eventFixture(300,2000);
        require(slash::locateEventDispatch(eventImage,code)==std::vector<std::size_t>{300});
        eventImage[2000]='x'; require(slash::locateEventDispatch(eventImage,code).empty());
        std::memcpy(eventImage.data()+2000,assertion,sizeof(assertion));
        eventFixture(300,-40); require(slash::locateEventDispatch(eventImage,code).empty());
        eventFixture(300,4090); require(slash::locateEventDispatch(eventImage,code).empty());
        eventFixture(300,2000); eventImage[300+0x1ac]=0;
        require(slash::locateEventDispatch(eventImage,code).empty());
        eventFixture(300,2000); eventFixture(1000,2000);
        const std::array<slash::Range,1> longer{{{256,1500}}};
        require(slash::locateEventDispatch(eventImage,longer).size()==2);
        require(slash::locateEventDispatch(eventImage,invalid).empty());
        std::cout<<"Signature, string, bounds, signed displacement and ambiguity checks passed.\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
