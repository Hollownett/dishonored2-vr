#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/menu_panel.hpp"
#include <iostream>

int main() {
    try {
        auto require=[](bool value) {if(!value) throw std::runtime_error("Menu placement check failed");};
        d2vr::MenuPanel panel;
        const auto first=panel.place({{1,1.7,2},{0,0,0,1}});
        require(std::abs(first.position.x-1)<1e-6 && std::abs(first.position.y-1.7)<1e-6 && std::abs(first.position.z-.85)<1e-6);
        // Small head motion inside the 30 degree cone leaves the panel exactly where it is.
        const auto steady=panel.place({{1.05,1.72,2.02},{0,std::sin(.15),0,std::cos(.15)}});
        require(steady.position.x==first.position.x && steady.position.z==first.position.z && steady.orientation.w==1);
        // A 90 degree turn starts a glide that ends in front of the new view.
        d2vr::TrackedPose followed{};
        for(int frame=0;frame<120;++frame) followed=panel.place({{3,1.8,4},{0,std::sqrt(.5),0,std::sqrt(.5)}});
        require(std::abs(followed.position.x-1.85)<.02 && std::abs(followed.position.z-4)<.02);
        // Head roll and pitch never tilt the panel: it stays upright (yaw only).
        panel.reset();const auto upright=panel.place({{0,1.7,0},{std::sin(.3),0,std::sin(.2),std::sqrt(1-std::sin(.3)*std::sin(.3)-std::sin(.2)*std::sin(.2))}});
        require(std::abs(upright.orientation.x)<1e-9 && std::abs(upright.orientation.z)<1e-9 && std::abs(upright.position.y-1.7)<1e-9);
        // summon() re-places a panel that is still inside the dead zone.
        d2vr::MenuPanel close_panel;close_panel.place({{0,1.7,0},{0,0,0,1}});close_panel.summon();
        d2vr::TrackedPose summoned{};for(int frame=0;frame<120;++frame) summoned=close_panel.place({{.3,1.7,0},{0,0,0,1}});
        require(std::abs(summoned.position.x-.3)<.02);
        panel.reset();const auto reopened=panel.place({{3,1.8,4},{0,std::sqrt(.5),0,std::sqrt(.5)}});
        require(std::abs(reopened.position.x-1.85)<1e-6 && std::abs(reopened.position.z-4)<1e-6);
        {   // hold(): the gameplay settings panel is placed once, stays put while open, and is placed afresh on the next open
            d2vr::MenuPanel world;
            const auto at_open=world.hold({{0,1.7,0},{0,0,0,1}});
            require(std::abs(at_open.position.z+1.15)<1e-6);
            const auto turned=world.hold({{2,1.7,3},{0,std::sqrt(.5),0,std::sqrt(.5)}}); // walked and turned 90 degrees
            require(turned.position.x==at_open.position.x && turned.position.z==at_open.position.z && turned.orientation.w==at_open.orientation.w);
            world.summon();
            for(int frame=0;frame<60;++frame) world.hold({{2,1.7,3},{0,std::sqrt(.5),0,std::sqrt(.5)}});
            require(std::abs(world.hold({{9,1.7,9},{0,0,0,1}}).position.x-at_open.position.x)<1e-9);
            world.reset();
            const auto again=world.hold({{2,1.7,3},{0,std::sqrt(.5),0,std::sqrt(.5)}});
            require(std::abs(again.position.x-.85)<1e-6 && std::abs(again.position.z-3)<1e-6);
        }
        const auto wide=d2vr::MenuPanel::extent(1920,1080),portrait=d2vr::MenuPanel::extent(2496,2688);
        require(std::abs(wide[0]/wide[1]-1920.f/1080.f)<1e-5 && wide[0]<=1.8f && wide[1]<=1.2f &&
            std::abs(portrait[0]/portrait[1]-2496.f/2688.f)<1e-5 && portrait[0]<=1.8f && portrait[1]<=1.2f);
        std::cout << "PASS: menu placement, stable within the view cone, lazy follow after a turn, reopen and image aspect\n";
        return 0;
    } catch(const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
