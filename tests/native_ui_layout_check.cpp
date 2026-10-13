#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_hud_child_draw.hpp"
#include "../src/native_eye_art.hpp"
#include <cassert>
#include <iostream>
namespace {
std::array<float,6> stage{1,0,0,1,0,0},damage{1,0,0,1,0,0},widget{1,0,0,1,100,100};
std::array<float,6> observed_damage{},observed_widget{};
bool read(std::uintptr_t address,void* out,std::size_t size){SIZE_T n{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&n) && n==size;}
std::uint32_t __fastcall original(void* iggy,float* matrix,void* node,void*,void*,void*,std::uint32_t,float){
    if(node==reinterpret_cast<void*>(1)) {
        d2vr::NativeHudChildDraw::invoke_fixture(iggy,damage.data(),reinterpret_cast<void*>(2),nullptr,nullptr,nullptr,0,0);
        d2vr::NativeHudChildDraw::invoke_fixture(iggy,widget.data(),reinterpret_cast<void*>(3),nullptr,nullptr,nullptr,0,0);
    } else std::copy_n(matrix,6,node==reinterpret_cast<void*>(2)?observed_damage.data():observed_widget.data());
    return 0;
}
}
int main(){
    using namespace d2vr;
    NativeHudChildDraw::fixture(read,0x140000000,original);NativeHudChildDraw::hud_scale=.6f;
    NativeHudChildDraw::hud_movie=+[](void*)noexcept{return true;};
    const auto draw=[] {NativeHudChildDraw::invoke_fixture(reinterpret_cast<void*>(4),stage.data(),reinterpret_cast<void*>(1),nullptr,nullptr,nullptr,0,0);};
    draw();assert(observed_damage==damage && observed_widget==widget);
    NativeHudChildDraw::hud_layout_node=+[](void*,void* node)noexcept{return node==reinterpret_cast<void*>(3);};
    draw();assert(observed_damage==damage && observed_widget[0]==.6f && observed_widget[4]>widget[4]);
    assert(stage[0]==1 && widget[0]==1); // no native matrix mutation
    // A separate tutorials movie relays every child (damage-like node 2 included) about the stage centre.
    NativeHudChildDraw::hud_movie=nullptr;NativeHudChildDraw::hud_layout_node=nullptr;
    draw();assert(observed_widget==widget);
    NativeHudChildDraw::tutorial_movie=+[](void*)noexcept{return true;};
    draw();assert(observed_widget[0]==.6f && observed_damage[0]==.6f && observed_widget[4]>widget[4]);
    std::array<XrView,2> eyes{};
    for(unsigned i=0;i<2;++i){eyes[i].pose.orientation.w=1;eyes[i].pose.position.x=i?.032f:-.032f;eyes[i].fov={-.65f,.75f,.7f,-.6f};}
    TrackedPose panel{{.28,-.1,-.35},{}};
    assert(!native_panel_contained(eyes,panel,.28,.10));
    const auto fitted=native_panel_fit(eyes,panel,.28,.10);
    assert(fitted && native_panel_contained(eyes,*fitted,.28,.10) && fitted->position.x<panel.position.x);
    assert(fitted->position.z==panel.position.z && fitted->orientation.w==1);
    assert(!native_panel_fit(eyes,panel,2.,.10)); // cannot fit at requested size
    eyes[1].fov.angleRight=.15f;
    assert(!native_panel_contained(eyes,panel,.28,.10));
    auto bad=panel;bad.orientation.w=0;assert(!native_panel_fit(eyes,bad,.28,.10));
    std::cout<<"PASS: damage child retains full matrix, positive widget relayout, binocular full-panel asymmetric FOV containment\n";
}
