#include "../src/hand_bind.hpp"
#include <cassert>
#include <iostream>
#include <limits>

namespace r=d2vr::rig;
int main() {
    for(unsigned side=0;side<2;++side) {
        const double direction=side?-1.:1.;
        r::Transform wrist{};wrist.f[0]=wrist.f[5]=wrist.f[10]=1;wrist.f[12]=static_cast<float>(direction*.7);
        std::array<r::Transform,4> knuckles{};
        for(unsigned i=0;i<4;++i) {
            knuckles[i]=wrist;knuckles[i].f[12]+=static_cast<float>(direction*.1);
            knuckles[i].f[14]=.045f-.03f*i;
        }
        const auto grip=r::hand_bind_grip(wrist,knuckles);
        assert(r::valid(grip));
        assert(d2vr::length(r::position(grip)-(r::position(wrist)+d2vr::Vec3{direction*.05,0,0}))<1e-6);
        assert(d2vr::length(r::column(grip,0)-d2vr::Vec3{0,0,1})<1e-6);
        assert(d2vr::length(r::column(grip,1)-d2vr::Vec3{0,direction,0})<1e-6);
        // World forward=-XR Z; world left=-XR X. Mirroring therefore changes
        // +XR X from outward left to inward right without a controller angle.
        const auto xr_x=r::column(grip,1)*-1;
        assert(d2vr::length(xr_x-d2vr::Vec3{0,-direction,0})<1e-6);
        assert(d2vr::length(d2vr::cross(r::column(grip,0),r::column(grip,1))-r::column(grip,2))<1e-6);
        const auto neutral=r::rigid_retarget(wrist,grip,grip);
        assert(d2vr::length(r::position(neutral)-r::position(wrist))<1e-6);
        auto moved=grip;moved.f[12]+=static_cast<float>(direction*.08);
        const auto target=r::rigid_retarget(wrist,grip,moved);
        assert(d2vr::length(r::position(target)-r::position(wrist)-d2vr::Vec3{direction*.08,0,0})<1e-6);
        // A changed skeleton/world orientation must rotate the bind frame,
        // including its palm offset, rather than use hard-coded bone axes.
        r::Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=1;
        auto rotated=identity;rotated.f[0]=0;rotated.f[1]=1;rotated.f[4]=-1;rotated.f[5]=0;
        rotated.f[12]=3;rotated.f[13]=4;rotated.f[14]=5;
        const auto rotated_wrist=r::rigid_retarget(wrist,identity,rotated);
        auto rotated_knuckles=knuckles;
        for(auto& k:rotated_knuckles) k=r::rigid_retarget(k,identity,rotated);
        const auto actual=r::hand_bind_grip(rotated_wrist,rotated_knuckles),expected=r::rigid_retarget(grip,identity,rotated);
        assert(d2vr::length(r::position(actual)-r::position(expected))<1e-6);
        for(unsigned axis=0;axis<3;++axis) assert(d2vr::length(r::column(actual,axis)-r::column(expected,axis))<1e-5);
        const auto inverse=r::affine_rows(r::rigid_retarget(identity,rotated_wrist,identity));
        const auto decoded=r::bind_transform(inverse);
        assert(d2vr::length(r::position(decoded)-r::position(rotated_wrist))<1e-5);
        for(unsigned axis=0;axis<3;++axis) assert(d2vr::length(r::column(decoded,axis)-r::column(rotated_wrist,axis))<1e-6);
        bool rejected=false;knuckles.fill(wrist);
        try {r::hand_bind_grip(wrist,knuckles);} catch(const std::invalid_argument&) {rejected=true;}
        assert(rejected);rejected=false;
        auto bad=inverse;bad[0]=std::numeric_limits<float>::quiet_NaN();
        try {r::bind_transform(bad);} catch(const std::invalid_argument&) {rejected=true;}
        assert(rejected);
    }
    std::cout<<"PASS: mirrored palm/wrist offsets, grip axes under translated/rotated binds, rigid inverse and degenerate/non-finite rejection\n";
}
