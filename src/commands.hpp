#pragma once
#include <array>
#include <cstdint>
#include <string_view>
namespace slash {
inline constexpr std::array<std::string_view,62> commands{
    "/gg", "/barbecue", "/beckon", "/bless", "/bloodstoneboogie", "/blowkiss", "/bow",
    "/breakdance", "/channel", "/cheer", "/cower", "/crabdance", "/crossarms", "/cry",
    "/dance", "/drink", "/facepalm", "/geargrind", "/heroic", "/hiss", "/kneel", "/laugh",
    "/magicjuggle", "/magictrick", "/no", "/paper", "/petalthrow", "/playdead", "/point",
    "/ponder", "/posecover", "/posehigh", "/poselow", "/posetwist", "/possessed", "/readbook",
    "/rock", "/rockout", "/sad", "/salute", "/scissors", "/serve", "/shiver", "/shiverplus",
    "/shocked", "/shrug", "/shuffle", "/sipcoffee", "/sit", "/sleep", "/stretch", "/step",
    "/surprised", "/talk", "/thank", "/threaten", "/thumbsdown", "/thumbsup", "/unleash",
    "/wave", "/yes", "/sc"
};
inline constexpr std::array<std::string_view,commands.size()> labels{
    "戰敗", "烤肉", "招手示意", "祝福", "血石熱舞", "飛吻", "鞠躬",
    "霹靂舞", "引導", "歡呼", "畏縮", "螃蟹舞", "雙手抱胸", "哭泣",
    "跳舞", "喝酒", "扶額", "齒輪舞", "英雄姿勢", "嘶聲", "跪下", "大笑",
    "魔法雜耍", "魔術", "否定", "布", "撒花瓣", "裝死", "指向",
    "思考", "遮擋姿勢", "高姿勢", "低姿勢", "扭轉姿勢", "附身", "看書",
    "石頭", "搖滾", "難過", "敬禮", "剪刀", "端盤", "發抖", "加強發抖",
    "震驚", "聳肩", "曳步舞", "喝咖啡", "坐下", "睡覺", "伸展", "踏步",
    "驚訝", "說話", "感謝", "威脅", "倒讚", "讚", "釋放力量",
    "揮手", "肯定", "Snow Crows 網站"
};
constexpr bool isMadKingEmote(std::string_view command) {
    constexpr std::array<std::string_view,18> emotes{
        "/beckon", "/bow", "/cheer", "/cower", "/dance", "/kneel",
        "/laugh", "/no", "/point", "/ponder", "/salute", "/shrug",
        "/sit", "/sleep", "/surprised", "/threaten", "/wave", "/yes"
    };
    for(auto emote:emotes) if(command==emote) return true;
    return false;
}
enum class Category { Basic, Unlock, Other };
constexpr Category category(std::string_view command) {
    if(command=="/gg" || command=="/sc") return Category::Other;
    if(isMadKingEmote(command) || command=="/crossarms" || command=="/cry"
        || command=="/facepalm" || command=="/sad" || command=="/talk" || command=="/thank")
        return Category::Basic;
    return Category::Unlock;
}
constexpr std::wstring_view website(std::wstring_view text) {
    auto keyword=text.substr(0,text.find(L' '));
    if(keyword==L"/sc") return L"https://snowcrows.com";
    return {};
}
}
