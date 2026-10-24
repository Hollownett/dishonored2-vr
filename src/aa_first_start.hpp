#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace d2vr {
// v609: the AA choice moved into vr-settings.cfg (the "AA chosen" marker); the old build/ knob files are removed once and the first start picks the default.
struct LegacyAaKnob {std::string name,value;}; // value: first line, trimmed, at most 64 chars ("" for an empty file)
inline std::vector<LegacyAaKnob> remove_legacy_aa_knobs(const std::filesystem::path& build_dir) noexcept {
    std::vector<LegacyAaKnob> removed;
    try {
        for(const char* name:{"aa-mode","force-taa","fsr","fsr4"}) { // never fsr4.off: that is a different switch
            std::error_code ec;const auto path=build_dir/name;
            if(!std::filesystem::is_regular_file(path,ec)) continue;
            std::string line;{std::ifstream in(path,std::ios::binary);std::getline(in,line);}
            const auto space=[](char c) {return c==' ' || c=='\t' || c=='\r' || c=='\n';};
            while(!line.empty() && space(line.back())) line.pop_back();
            line.erase(0,static_cast<std::size_t>(std::find_if_not(line.begin(),line.end(),space)-line.begin()));
            if(line.size()>64) line.resize(64);
            if(std::filesystem::remove(path,ec) && !ec) removed.push_back({name,line});
        }
        std::sort(removed.begin(),removed.end(),[](const LegacyAaKnob& a,const LegacyAaKnob& b) {return a.name<b.name;});
    } catch(...) {}
    return removed;
}
enum class FirstStartAa {wait,keep,dlaa,fsr3_native};
// Decided at Present once the adapter vendor is known. Saved settings win; only the one-time v609 pass may change the mode.
inline FirstStartAa first_start_aa(bool aa_chosen,bool file_existed,std::uint32_t vendor,bool dlss_offered) noexcept {
    if(aa_chosen) return FirstStartAa::keep;
    if(vendor==0) return FirstStartAa::wait;
    if(dlss_offered) return FirstStartAa::dlaa;
    if(!file_existed) return FirstStartAa::fsr3_native;
    return FirstStartAa::keep;
}
} // namespace d2vr
