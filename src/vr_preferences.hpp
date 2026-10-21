#pragma once
#include "turn_control.hpp"
#include "avatar_calibration.hpp"
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
    MirrorMode mirror=MirrorMode::right_eye; // one eye: the desktop copy of both eyes costs render-thread time (1.5 ms on Windows)
    double sharpness=.5;
    int temporal_aa=2; // anti-aliasing: 0 off (SMAA row only), 1 game TAA, 2 FSR 3 native, 3 FSR 3 quality (77% render), 4 FSR 3 balanced (67%), 5 FSR 4 native, 6 FSR 4 quality (77%)
    int hud_percent=65; // HUD drawn at this size about the view centre (native_hud_rect.hpp)
    bool controls_hint=true;
    AvatarPlayerMeasurements player_measurements{};
};
inline void validate_preferences(const VrPreferences& p) {
    if(!std::isfinite(p.resolution) || p.resolution<.5 || p.resolution>2)
        throw std::invalid_argument("VR preference resolution must be 0.5 to 2.0");
    if(p.mirror<MirrorMode::right_eye || p.mirror>MirrorMode::both_eyes)
        throw std::invalid_argument("VR preference mirror mode must be right eye, left eye or both eyes");
    if(!std::isfinite(p.sharpness) || p.sharpness<0 || p.sharpness>1)
        throw std::invalid_argument("VR preference sharpness must be 0.0 to 1.0");
    if(p.hud_percent<50 || p.hud_percent>200) throw std::invalid_argument("VR preference HUD size must be 50 to 200 percent");
    if(p.temporal_aa<0 || p.temporal_aa>6) throw std::invalid_argument("VR preference anti-aliasing must be off, TAA, an FSR 3 or an FSR 4 mode");
    const auto bounded=[](const std::optional<double>& value,double low,double high) {return !value || (std::isfinite(*value) && *value>=low && *value<=high);};
    const auto& m=p.player_measurements;
    if(!bounded(m.standing_eye_height_metres,.9,2.2) || !bounded(m.shoulder_span_metres,.15,.65) ||
       !bounded(m.torso_width_metres,.15,.8) || !bounded(m.arm_reach_metres[0],.25,1.) || !bounded(m.arm_reach_metres[1],.25,1.))
        throw std::invalid_argument("Invalid player measurements");
    TurnControl check;check.configure(p.turning);
}
inline std::optional<VrPreferences> load_preferences(const std::filesystem::path& file) {
    if(!std::filesystem::exists(file)) return {};
    if(std::filesystem::file_size(file)>512) throw std::runtime_error("VR preferences exceed the format limit");
    std::ifstream in(file);VrPreferences p;int version{},aa{},mode{},mirror{};
    if(!(in>>version>>p.resolution>>aa>>mode>>p.turning.snap_degrees>>p.turning.smooth_degrees_per_second) ||
       (version<1 || version>7) || (aa!=0 && aa!=1) || mode<0 || mode>2)
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
        int taa{};if(!(in>>taa) || taa<0 || taa>(version>=5?6:1)) throw std::runtime_error("Invalid VR preferences; the saved file was preserved");
        // Version 5 adds FSR 3 (2), later FSR 4 Native/Quality (5/6); older on/off choices predate it and move to FSR 3.
        p.temporal_aa=version>=5?taa:2;
    }
    // Version 4 adds the aim reticle and controls hint rows; older files keep
    // both overlays enabled.
    if(version>=4) {
        // Version 6 replaces the retired aim-reticle switch with the HUD size in percent.
        int hud{},hint{};
        if(!(in>>hud>>hint) || (version>=6?(hud<50 || hud>200):(hud!=0 && hud!=1)) || (hint!=0 && hint!=1))
            throw std::runtime_error("Invalid VR preferences; the saved file was preserved");
        p.hud_percent=version>=6?hud:65;p.controls_hint=hint!=0;
    }
    if(version>=7) {
        double values[5]{};for(auto& value:values)if(!(in>>value) || !std::isfinite(value) || value<0)throw std::runtime_error("Invalid saved player measurements");
        auto& m=p.player_measurements;
        const auto measured=[](double value) {return value?std::optional<double>(value):std::nullopt;};
        m.standing_eye_height_metres=measured(values[0]);m.shoulder_span_metres=measured(values[1]);m.torso_width_metres=measured(values[2]);
        m.arm_reach_metres={measured(values[3]),measured(values[4])};
    }
    in>>std::ws;if(!in.eof()) throw std::runtime_error("Unexpected VR preference fields");
    p.antialiasing=version>=3?aa==1:true;p.turning.mode=static_cast<TurnControl::Mode>(mode);validate_preferences(p);return p;
}
inline void save_preferences(const std::filesystem::path& file,const VrPreferences& p) {
    validate_preferences(p);auto temporary=file;temporary+=L".tmp";
    {std::ofstream out(temporary,std::ios::trunc);
     out<<std::setprecision(17)<<7<<' '<<p.resolution<<' '<<p.antialiasing<<' '<<static_cast<int>(p.turning.mode)<<' '
        <<p.turning.snap_degrees<<' '<<p.turning.smooth_degrees_per_second<<' '<<static_cast<int>(p.mirror)<<' '<<p.sharpness<<' '
        <<p.temporal_aa<<' '<<p.hud_percent<<' '<<p.controls_hint<<' '
        <<p.player_measurements.standing_eye_height_metres.value_or(0)<<' '<<p.player_measurements.shoulder_span_metres.value_or(0)<<' '
        <<p.player_measurements.torso_width_metres.value_or(0)<<' '<<p.player_measurements.arm_reach_metres[0].value_or(0)<<' '
        <<p.player_measurements.arm_reach_metres[1].value_or(0)<<'\n';
     out.flush();if(!out) throw std::runtime_error("Cannot save VR preferences; previous settings retained");}
    if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish VR preferences; previous settings retained");
}
} // namespace d2vr
