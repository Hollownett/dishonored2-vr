#pragma once
#include "turn_control.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <windows.h>

namespace d2vr {
enum class MirrorMode {right_eye=0,left_eye=1,both_eyes=2};
struct VrPreferences {
    double resolution=1;
    bool antialiasing=true;
    TurnControl::Config turning{TurnControl::Mode::snap,45,90};
    MirrorMode mirror=MirrorMode::both_eyes;
    double sharpness=.5;
    bool temporal_aa=false;
    bool aim_reticle=true;
    bool controls_hint=true;
};
inline void validate_preferences(const VrPreferences& p) {
    if(!std::isfinite(p.resolution) || p.resolution<.5 || p.resolution>2)
        throw std::invalid_argument("VR preference resolution must be 0.5 to 2.0");
    if(p.mirror<MirrorMode::right_eye || p.mirror>MirrorMode::both_eyes)
        throw std::invalid_argument("VR preference mirror mode must be right eye, left eye or both eyes");
    if(!std::isfinite(p.sharpness) || p.sharpness<0 || p.sharpness>1)
        throw std::invalid_argument("VR preference sharpness must be 0.0 to 1.0");
    TurnControl check;check.configure(p.turning);
}
inline std::optional<VrPreferences> load_preferences(const std::filesystem::path& file) {
    if(!std::filesystem::exists(file)) return {};
    if(std::filesystem::file_size(file)>256) throw std::runtime_error("VR preferences exceed the format limit");
    std::ifstream in(file);VrPreferences p;int version{},aa{},mode{},mirror{};
    if(!(in>>version>>p.resolution>>aa>>mode>>p.turning.snap_degrees>>p.turning.smooth_degrees_per_second) ||
       (version<1 || version>4) || (aa!=0 && aa!=1) || mode<0 || mode>2)
        throw std::runtime_error("Invalid VR preferences; the saved file was preserved");
    // Version 2 predates the mirror/sharpness rows.
    if(version>=2) {
        if(!(in>>mirror>>p.sharpness) || mirror<0 || mirror>2 || !std::isfinite(p.sharpness) || p.sharpness<0 || p.sharpness>1)
            throw std::runtime_error("Invalid VR preferences; the saved file was preserved");
        p.mirror=static_cast<MirrorMode>(mirror);
    }
    // Version 3 adds the temporal AA row and is the first to record the edge
    // smoothing choice; older files migrate to the accepted SMAA-on/no-TAA image.
    if(version>=3) {
        int taa{};if(!(in>>taa) || (taa!=0 && taa!=1)) throw std::runtime_error("Invalid VR preferences; the saved file was preserved");
        p.temporal_aa=taa!=0;
    }
    // Version 4 adds the aim reticle and controls hint rows; older files keep
    // both overlays enabled.
    if(version>=4) {
        int reticle{},hint{};if(!(in>>reticle>>hint) || (reticle!=0 && reticle!=1) || (hint!=0 && hint!=1))
            throw std::runtime_error("Invalid VR preferences; the saved file was preserved");
        p.aim_reticle=reticle!=0;p.controls_hint=hint!=0;
    }
    in>>std::ws;if(!in.eof()) throw std::runtime_error("Unexpected VR preference fields");
    p.antialiasing=version>=3?aa==1:true;p.turning.mode=static_cast<TurnControl::Mode>(mode);validate_preferences(p);return p;
}
inline void save_preferences(const std::filesystem::path& file,const VrPreferences& p) {
    validate_preferences(p);auto temporary=file;temporary+=L".tmp";
    {std::ofstream out(temporary,std::ios::trunc);
     out<<std::setprecision(17)<<4<<' '<<p.resolution<<' '<<p.antialiasing<<' '<<static_cast<int>(p.turning.mode)<<' '
        <<p.turning.snap_degrees<<' '<<p.turning.smooth_degrees_per_second<<' '<<static_cast<int>(p.mirror)<<' '<<p.sharpness<<' '
        <<p.temporal_aa<<' '<<p.aim_reticle<<' '<<p.controls_hint<<'\n';
     out.flush();if(!out) throw std::runtime_error("Cannot save VR preferences; previous settings retained");}
    if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish VR preferences; previous settings retained");
}
} // namespace d2vr
