#include "locator.hpp"
#include <cstring>
namespace slash {
std::vector<std::size_t> locate(std::span<const std::uint8_t> image,
                               std::span<const Range> executable) {
    const std::uint8_t pattern[]{0x48,0x8d,0x0d,0,0,0,0,0x48,0x83,0xc4,0x28,0xe9,0,0,0,0};
    const char16_t expected[] = u"/.disconnect";
    std::vector<std::size_t> results;
    auto valid = [&](std::int64_t offset, std::size_t size) {
        return offset >= 0 && static_cast<std::uint64_t>(offset) <= image.size()
            && size <= image.size() - static_cast<std::size_t>(offset);
    };
    auto isCode = [&](std::size_t offset) {
        for(const auto& r : executable)
            if(offset >= r.offset && offset-r.offset < r.size) return true;
        return false;
    };
    for(const auto& range : executable) {
        if(!valid(static_cast<std::int64_t>(range.offset), range.size) || range.size < sizeof(pattern)) continue;
        for(auto i=range.offset; i <= range.offset+range.size-sizeof(pattern); ++i) {
            bool match=true;
            for(std::size_t j=0;j<sizeof(pattern);++j) {
                if((j>=3 && j<=6) || j>=12) continue;
                if(image[i+j]!=pattern[j]) { match=false; break; }
            }
            if(!match) continue;
            std::int32_t stringDisp{}, targetDisp{};
            std::memcpy(&stringDisp, image.data()+i+3, 4);
            std::memcpy(&targetDisp, image.data()+i+12, 4);
            auto text=static_cast<std::int64_t>(i)+7+stringDisp;
            auto target=static_cast<std::int64_t>(i)+16+targetDisp;
            if(!valid(text,sizeof(expected)) || !valid(target,32)) continue;
            if(std::memcmp(image.data()+text,expected,sizeof(expected))!=0) continue;
            if(isCode(static_cast<std::size_t>(target))) results.push_back(static_cast<std::size_t>(target));
        }
    }
    return results;
}
std::vector<std::size_t> locateEventDispatch(std::span<const std::uint8_t> image,
                                            std::span<const Range> executable) {
    // EventApi dispatch: R9D is event ID, [rsp+28/30/38] are size/data/extra.
    constexpr std::uint8_t prologue[]{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,
        0x48,0x8d,0x6c,0x24,0xf1,0x48,0x81,0xec,0xd0,0,0,0};
    constexpr char assertion[]="id < arrsize(m_handler)";
    constexpr std::uint8_t cmp[]{0x48,0x83,0xfb,0x31,0x72,0x19};
    std::vector<std::size_t> results;
    for(auto range:executable) {
        if(range.offset>image.size() || range.size>image.size()-range.offset || range.size<0x20b) continue;
        for(auto i=range.offset;i<=range.offset+range.size-0x20b;++i) {
            if(std::memcmp(image.data()+i,prologue,sizeof(prologue))!=0) continue;
            if(std::memcmp(image.data()+i+0xc3,cmp,sizeof(cmp))!=0) continue;
            if(image[i+0xd6]!=0x48 || image[i+0xd7]!=0x8d || image[i+0xd8]!=0x0d) continue;
            std::int32_t disp{}; std::memcpy(&disp,image.data()+i+0xd9,4);
            auto text=static_cast<std::int64_t>(i)+0xdd+disp;
            if(text<0 || static_cast<std::uint64_t>(text)>image.size()
                || sizeof(assertion)>image.size()-static_cast<std::size_t>(text)) continue;
            if(std::memcmp(image.data()+text,assertion,sizeof(assertion))!=0) continue;
            // shl r14,5 and indirect callback call rax in dispatch's handler loop.
            constexpr std::uint8_t bucket[]{0x49,0xc1,0xe6,0x05};
            if(std::memcmp(image.data()+i+0xe9,bucket,sizeof(bucket))!=0) continue;
            if(image[i+0x1ac]!=0xff || image[i+0x1ad]!=0xd0) continue;
            results.push_back(i);
        }
    }
    return results;
}
}
