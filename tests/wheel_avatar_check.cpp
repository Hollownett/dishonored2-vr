#define NOMINMAX
#include "../src/wheel_avatar.hpp"
#include "../src/native_attachment_pose.hpp"
#include <cassert>
#include <limits>
#include <iostream>

int main() {
    using namespace d2vr;using namespace d2vr::rig;
    std::array<float,15> frame{3,4,5,0,1,0,-1,0,0,0,0,1,2,2,2};
    ModelFrame model(frame);
    std::array<Transform,69> arms{};
    for(unsigned i=0;i<arms.size();++i) {auto& j=arms[i];j.f[0]=j.f[5]=j.f[10]=j.f[15]=1;j.f[12]=static_cast<float>(i)*.01f;}
    const auto original=arms;
    assert(!hide_wheel_avatar(arms,frame,2,true) && arms[1].f==original[1].f);
    assert(!hide_wheel_avatar(arms,frame,256,false) && arms[1].f==original[1].f);
    assert(hide_wheel_avatar(arms,frame,256,true));
    for(unsigned i=0;i<arms.size();++i) {
        assert(valid(arms[i]));
        const auto delta=model.world_point(position(arms[i]))-model.world_point(position(original[i]));
        assert(length(delta-Vec3{0,0,-100})<.0001);
        for(unsigned k=0;k<12;++k) assert(arms[i].f[k]==original[i].f[k]);
    }
    std::array<Transform,20> body{};for(auto& j:body) j=original[0];
    assert(hide_wheel_avatar(body,frame,256,true));
    auto invalid=original;invalid[40].f[0]=std::numeric_limits<float>::quiet_NaN();
    assert(!hide_wheel_avatar(invalid,frame,256,true));
    assert(invalid[0].f==original[0].f); // Rejection cannot partially change a palette.
    auto bad_frame=frame;bad_frame[12]=0;auto unchanged=original;
    assert(!hide_wheel_avatar(unchanged,bad_frame,256,true) && unchanged[0].f==original[0].f);
    // v776 (headset v775): the dual-wield hand wheel (logic 256 with the ring at the hand) keeps the avatar and its tracked arms; the head-front wheel hides it.
    assert(wheel_hides_avatar(256,false) && !wheel_hides_avatar(256,true));          // knob off: exactly as before
    assert(!wheel_hides_avatar(2,false) && !wheel_hides_avatar(2,true) && !wheel_hides_avatar(32,true) && !wheel_hides_avatar(512,true)); // only the wheel
    assert(wheel_tracks_arms(2,false) && wheel_tracks_arms(2,true));                   // gameplay is unchanged
    assert(wheel_tracks_arms(256,true) && !wheel_tracks_arms(256,false));
    assert(!wheel_tracks_arms(512,true) && !wheel_tracks_arms(1024,true) && !wheel_tracks_arms(16,true) && !wheel_tracks_arms(32,true)); // other menus keep the native arms
    // The attachment policy the head-view thread publishes: both tracked hands may carry their items in the hand wheel; the head-front wheel none.
    assert(native_attachment_allowed_mask(3,0,wheel_tracks_arms(256,true),true,true)==3);
    assert(native_attachment_allowed_mask(3,0,wheel_tracks_arms(256,false),true,true)==0);
    assert(native_attachment_allowed_mask(3,1,wheel_tracks_arms(256,true),true,true)==2); // an arm the player state owns stays native
    assert(native_attachment_allowed_mask(3,0,wheel_tracks_arms(256,true),false,true)==0 && native_attachment_allowed_mask(3,0,wheel_tracks_arms(256,true),true,false)==0);
    std::cout<<"wheel_avatar_check PASS\n";
}
