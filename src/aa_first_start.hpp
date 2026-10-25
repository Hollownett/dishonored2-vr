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
enum class FirstStartAa {wait,keep,dlaa,fsr3_native,fsr4_native};
// v613: the default anti-aliasing follows the GPU (the launcher's Prefs.DefaultAa mirrors this): DLSS offered (NVIDIA driver/adapter and the
// user's nvngx_dlss.dll) -> DLAA (7); FSR 4 runs on the game's adapter (the FSR 4 backend's own create check) -> FSR 4 native (5); else FSR 3 native (2).
inline int default_aa_mode(bool dlss_offered,bool fsr4_supported) noexcept {return dlss_offered?7:fsr4_supported?5:2;}
// Decided at Present once the adapter vendor is known. Saved settings win; only this one-time pass may change the mode, and only before the
// first gameplay frame (the caller keeps the mode once a level runs). A file saved before v609 without the "AA chosen" marker keeps its mode
// unless the GPU offers a better upscaler (DLAA, FSR 4).
inline FirstStartAa first_start_aa(bool aa_chosen,bool file_existed,std::uint32_t vendor,bool dlss_offered,bool fsr4_supported=false) noexcept {
    if(aa_chosen) return FirstStartAa::keep;
    if(vendor==0) return FirstStartAa::wait;
    if(dlss_offered) return FirstStartAa::dlaa;
    if(fsr4_supported) return FirstStartAa::fsr4_native;
    if(!file_existed) return FirstStartAa::fsr3_native;
    return FirstStartAa::keep;
}
inline int first_start_mode(FirstStartAa decision) noexcept {
    return decision==FirstStartAa::dlaa?7:decision==FirstStartAa::fsr4_native?5:decision==FirstStartAa::fsr3_native?2:-1;
}
inline const char* first_start_name(FirstStartAa decision) noexcept {
    return decision==FirstStartAa::dlaa?"dlaa":decision==FirstStartAa::fsr4_native?"fsr4_native":decision==FirstStartAa::fsr3_native?"fsr3_native":"keep";
}
} // namespace d2vr
