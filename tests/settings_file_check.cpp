#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/vr_settings_native.hpp"
#include <cstdio>
#include <stdexcept>

static void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv) {
    try {
        const std::filesystem::path file=argc>1?argv[1]:"tests/fixtures/vr-settings-v10.cfg";
        const auto loaded=d2vr::load_preferences(file);require(loaded.has_value(),"preferences missing");
        const auto original=std::filesystem::file_size(file);
        for(int mode=0;mode<=8;++mode) {
            auto p=*loaded;p.temporal_aa=mode;p.aa_chosen=true;
            d2vr::VrSettings settings;
            settings.initialize(p.resolution,p.antialiasing,p.turning,p.mirror,p.sharpness,p.temporal_aa,p.hud_percent,p.controls_hint,
                                p.player_measurements,p.aa_chosen,p.crosshair,p.scripted_recenter,p.cutscene_mode);
            settings.performance_mode(true);settings.effective_temporal_aa(0);settings.dlss_available(true);
            const auto rows=d2vr::native_settings::rows(settings);
            require(rows[d2vr::native_settings::temporal].value==mode,"native page lost the requested AA mode");
            std::vector<int> values;for(const auto& row:rows)values.push_back(row.kind==d2vr::NativeOptionsPage::Kind::action?-1:row.value);
            values[d2vr::native_settings::hud]=settings.hud_percent()==150?1:6;
            require(d2vr::native_settings::apply(settings,values).hud_percent.has_value(),"native HUD edit missing");
            const auto saved=settings.preferences();
            require(saved.temporal_aa==mode && saved.aa_chosen,"unrelated native edit lost the chosen AA mode");
            require(saved.resolution==p.resolution && saved.mirror==p.mirror && saved.sharpness==p.sharpness,"unrelated native edit lost launcher settings");
            require(saved.crosshair==p.crosshair && saved.scripted_recenter==p.scripted_recenter && saved.cutscene_mode==p.cutscene_mode &&
                    saved.controls_hint==p.controls_hint,"production snapshot lost saved switches");
            auto output=file;output+=L".roundtrip";
            d2vr::save_preferences(output,saved);
            const auto roundtrip=d2vr::load_preferences(output);
            require(roundtrip && roundtrip->temporal_aa==mode && roundtrip->hud_percent==saved.hud_percent,"production settings file roundtrip failed");
            std::filesystem::remove(output);
        }
        require(std::filesystem::file_size(file)==original,"input preferences changed");
        std::puts("PASS: real settings file -> native VR rows -> unrelated edit -> production persistence (all AA modes)");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
