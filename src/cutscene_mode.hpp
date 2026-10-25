#pragma once
// G3 cutscene comfort setting: VR comfort (default), VR full, or Theater (the flat screen). The VR settings row is the source (saved as
// cutscene_mode in vr-settings.cfg); the dev knob build/cutscene-mode holds the same text and overrides it while present.
#include <optional>
#include <string_view>

namespace d2vr {
enum class CutsceneMode {vr_comfort,vr_full,theater};

inline std::optional<CutsceneMode> parse_cutscene_mode(std::string_view text) noexcept {
    const auto blank=[](char c) {return c==' ' || c=='\t' || c=='\r' || c=='\n';};
    while(!text.empty() && blank(text.front())) text.remove_prefix(1);
    while(!text.empty() && blank(text.back())) text.remove_suffix(1);
    if(text=="vr_comfort") return CutsceneMode::vr_comfort;
    if(text=="vr_full") return CutsceneMode::vr_full;
    if(text=="theater") return CutsceneMode::theater;
    return std::nullopt;
}

inline const char* to_string(CutsceneMode mode) noexcept {
    switch(mode) {
    case CutsceneMode::vr_comfort: return "vr_comfort";
    case CutsceneMode::vr_full: return "vr_full";
    case CutsceneMode::theater: return "theater";
    }
    return "vr_comfort";
}
} // namespace d2vr
