#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/settings_menu_layout.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    const auto desktop=SettingsMenuLayout::image_rect(1920,1080,0,1080);
    assert(desktop.x==80 && desktop.y==190 && desktop.width==960 && desktop.height==840);
    const auto portrait=SettingsMenuLayout::image_rect(2496,2688,642,1404);
    assert(portrait.x==104 && portrait.y==889 && portrait.width==1248 && portrait.height==1092);
    assert(std::abs(double(portrait.width)/portrait.height-640./560)<1e-9);
    const auto centre=SettingsMenuLayout::pointer({560./1920,610./1080});
    assert(centre && std::abs(centre->u-.5)<1e-9 && std::abs(centre->v-.5)<1e-9);
    const auto top_left=SettingsMenuLayout::pointer({80./1920,190./1080});
    const auto bottom_right=SettingsMenuLayout::pointer({1040./1920,1030./1080});
    assert(top_left && std::abs(top_left->u)<1e-9 && std::abs(top_left->v)<1e-9);
    assert(bottom_right && std::abs(bottom_right->u-1)<1e-9 && std::abs(bottom_right->v-1)<1e-9);
    // Observed 851x499 native pause menu: Resume and VR Settings share the right column.
    assert(!SettingsMenuLayout::pointer({550./851,206./499}));
    assert(!SettingsMenuLayout::pointer({550./851,341./499}));
    assert(!SettingsMenuLayout::pointer({.5,100./1080})); // Mission header stays visible.
    assert(!SettingsMenuLayout::pointer({.7,.02}));
    assert(!SettingsMenuLayout::pointer({NAN,.5}));
    for(const auto& size:std::array<std::array<std::uint32_t,4>,3>{{{1920,1080,0,0},{1920,1080,1000,200},{0,1080,0,1080}}}) {
        bool rejected=false;
        try {SettingsMenuLayout::image_rect(size[0],size[1],size[2],size[3]);}
        catch(const std::invalid_argument&) {rejected=true;}
        assert(rejected);
    }
    VrSettings settings;ControllerInput held{};held.active=true;held.a=true;held.right_trigger=1;
    settings.open_from_menu();
    const auto first=settings.update(held,false,true,PanelPointer{.8,.15});
    assert(first.consume && !first.resolution && !first.temporal_aa && settings.open());
    assert(!settings.update(held,false,true,PanelPointer{.8,.15}).resolution);
    held.a=false;held.right_trigger=0;settings.update(held,false,true);
    held.right_trigger=1;
    assert(settings.update(held,false,true,PanelPointer{.8,.15}).resolution);
    std::cout<<"PASS: native menu content rectangle, portrait eye crop, shared pointer mapping and swallowed opening click\n";
}
