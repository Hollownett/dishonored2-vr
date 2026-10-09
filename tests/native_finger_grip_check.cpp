#include "../src/native_finger_grip.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

namespace r=d2vr::rig;

int main() {
    r::ArmHierarchy parents{};parents.fill(0);parents[0]=-1;
    // Two hands of five digits, five joints each (three named chain joints, one
    // knuckle helper and one helper leaf), contiguous per digit the way the
    // cooked native arrays are; the metacarpals hang off the wrist unmasked.
    const auto layout=[&](unsigned first,r::FingerChains& chains) {
        for(unsigned finger=0;finger<5;++finger) {
            const auto base=first+finger*5;
            for(unsigned segment=0;segment<3;++segment) {
                chains[finger][segment]=base+segment;
                parents[base+segment]=segment?static_cast<std::int16_t>(base+segment-1):
                    static_cast<std::int16_t>(finger?first+24+finger:0);
            }
            parents[base+3]=static_cast<std::int16_t>(base+2);
            parents[base+4]=static_cast<std::int16_t>(base+3);
        }
    };
    r::FingerChains right{},left{};layout(5,right);layout(34,left);
    assert(r::valid_hierarchy(parents));
    r::Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=1;
    const auto local_of=[](d2vr::Vec3 axis,double radians,d2vr::Vec3 at) {
        r::Transform base{};base.f[0]=base.f[5]=base.f[10]=1;
        const auto half=radians*.5;
        return r::turn_root(base,{axis.x*std::sin(half),axis.y*std::sin(half),axis.z*std::sin(half),std::cos(half)},at);
    };
    const auto hand_pose=[&](const r::FingerChains& chains,double curl,double yaw,bool moved) {
        std::array<r::Transform,69> pose{};
        for(auto& t:pose) {t.f[0]=t.f[5]=t.f[10]=1;}
        pose[0]=local_of({0,1,0},yaw,moved?d2vr::Vec3{.12,.03,.05}:d2vr::Vec3{});
        for(unsigned finger=1;finger<5;++finger) {
            const auto metacarpal=static_cast<unsigned>(parents[chains[finger][0]]);
            pose[metacarpal]=r::rigid_retarget(local_of({0,1,0},0,{.01*finger,.02,0}),identity,pose[0]);
        }
        for(unsigned finger=0;finger<5;++finger) {
            const auto& chain=chains[finger];
            for(unsigned segment=0;segment<3;++segment) {
                const auto joint=chain[segment],parent=static_cast<unsigned>(parents[joint]);
                const d2vr::Vec3 axis=finger?d2vr::Vec3{1,0,0}:d2vr::Vec3{.6,0,.8};
                const auto bend=curl*(.35+.2*segment)*(finger?.8:1.);
                pose[joint]=r::rigid_retarget(local_of(axis,bend,{0,.028+.002*finger,.004*segment}),identity,pose[parent]);
            }
            const auto helper=chain[0]+3;
            pose[helper]=r::rigid_retarget(local_of({0,0,1},curl*.5,{0,.012,0}),identity,pose[chain[2]]);
            pose[chain[0]+4]=r::rigid_retarget(local_of({0,1,0},curl*.25,{0,.009,0}),identity,pose[helper]);
        }
        return pose;
    };
    const auto close=[](const r::Transform& a,const r::Transform& b,double tolerance) {
        for(unsigned i=0;i<16;++i) if(!(std::abs(static_cast<double>(a.f[i]-b.f[i]))<tolerance)) return false;
        return true;
    };
    const auto local_at=[&](const std::array<r::Transform,69>& pose,unsigned joint) {
        const auto parent=parents[joint];
        return parent<0?pose[joint]:r::rigid_retarget(pose[joint],pose[static_cast<unsigned>(parent)],identity);
    };
    const auto reflect=[](const r::Transform& t,int axis) {
        r::Transform out=t;
        for(unsigned row=0;row<3;++row) for(unsigned col=0;col<4;++col)
            out.f[col*4+row]=t.f[col*4+row]*static_cast<float>(
                (row==static_cast<unsigned>(axis)?-1.:1.)*(col==static_cast<unsigned>(axis)?-1.:1.));
        return out;
    };
    // Capture the held sword fist and check the stored locals against the
    // parent-relative transforms of the source palette.
    const auto held=hand_pose(right,1.,-.3,false),relaxed=hand_pose(right,.15,.6,true);
    const auto grip=r::capture_finger_grip(held,parents,right);
    assert(grip.valid);
    unsigned masked=0;
    for(unsigned joint=0;joint<69;++joint) if(grip.mask[joint]) ++masked;
    assert(masked==25);
    for(unsigned finger=1;finger<5;++finger) assert(!grip.mask[static_cast<unsigned>(parents[right[finger][0]])]);
    for(unsigned finger=0;finger<5;++finger) for(unsigned k=0;k<5;++k) {
        const auto joint=right[finger][0]+k;
        assert(grip.mask[joint]);
        assert(close(local_at(held,joint),grip.local[joint],1e-5));
    }
    const auto round=r::apply_finger_grip(held,parents,grip,std::array<float,5>{1,1,1,1,1});
    for(unsigned joint=0;joint<69;++joint) assert(close(round[joint],held[joint],1e-5));
    // Amount one on a differently posed (moved/rotated wrist) open hand: the
    // finger locals equal the captured ones and the fingers follow the wrist.
    const auto closed=r::apply_finger_grip(relaxed,parents,grip,std::array<float,5>{1,1,1,1,1});
    for(unsigned joint=0;joint<69;++joint) {
        if(!grip.mask[joint]) {assert(closed[joint].f==relaxed[joint].f);continue;}
        assert(close(local_at(closed,joint),grip.local[joint],1e-4));
        assert(close(closed[joint],r::rigid_retarget(held[joint],held[0],relaxed[0]),1e-4));
    }
    // Amount zero is bit-identical; a partial amount only moves its own digit.
    const auto zero=r::apply_finger_grip(relaxed,parents,grip,std::array<float,5>{});
    for(unsigned joint=0;joint<69;++joint) assert(zero[joint].f==relaxed[joint].f);
    const auto index_only=r::apply_finger_grip(relaxed,parents,grip,std::array<float,5>{0,1,0,0,0});
    for(unsigned finger=0;finger<5;++finger) for(unsigned k=0;k<5;++k) {
        const auto joint=right[finger][0]+k;
        if(finger==1) assert(close(local_at(index_only,joint),grip.local[joint],1e-4));
        else assert(close(index_only[joint],relaxed[joint],1e-5));
    }
    const auto partial=r::apply_finger_grip(relaxed,parents,grip,std::array<float,5>{0,0,.5f,0,0});
    for(unsigned joint=0;joint<69;++joint) if(!grip.mask[joint]) assert(partial[joint].f==relaxed[joint].f);
    for(unsigned finger=0;finger<5;++finger) for(unsigned k=0;k<5;++k) {
        const auto joint=right[finger][0]+k;
        const auto travel=d2vr::length(r::position(partial[joint])-r::position(relaxed[joint]));
        if(finger!=2) {assert(travel<1e-5);continue;}
        assert(!close(local_at(partial,joint),local_at(relaxed,joint),1e-6));
        assert(!close(local_at(partial,joint),local_at(closed,joint),1e-6));
        // The proximal joint shares its pivot with the grip rotation, so only a
        // joint beyond it translates; every digit local must still change.
        const auto full=d2vr::length(r::position(closed[joint])-r::position(relaxed[joint]));
        if(k>=2) assert(travel>1e-5 && travel<full);
    }
    for(unsigned joint=0;joint<69;++joint) {
        const auto parent=parents[joint];
        if(parent<0) continue;
        const auto at=static_cast<unsigned>(parent);
        const auto rest=d2vr::length(r::position(relaxed[joint])-r::position(relaxed[at]));
        assert(std::abs(d2vr::length(r::position(closed[joint])-r::position(closed[at]))-rest)<1e-4);
        assert(std::abs(d2vr::length(r::position(partial[joint])-r::position(partial[at]))-rest)<1e-4);
    }
    // Mirroring maps digit by digit, chain position by chain position, and
    // reflects every local on the chosen axis; doing it twice is the identity.
    const auto held_left=hand_pose(left,1.,-.3,false);
    const auto grip_left=r::capture_finger_grip(held_left,parents,left);
    assert(grip_left.valid);
    for(int axis:{0,1,2}) {
        const auto mirrored=r::mirror_finger_grip(grip,right,left,parents,axis);
        assert(mirrored.valid);
        for(unsigned finger=0;finger<5;++finger) for(unsigned k=0;k<5;++k) {
            const auto from=right[finger][0]+k,to=left[finger][0]+k;
            assert(mirrored.mask[to] && r::valid(mirrored.local[to]));
            assert(close(mirrored.local[to],reflect(grip.local[from],axis),1e-6));
        }
    }
    const auto mirrored=r::mirror_finger_grip(grip,right,left,parents);
    assert(mirrored.valid);
    for(unsigned finger=0;finger<5;++finger) for(unsigned k=0;k<5;++k)
        assert(close(mirrored.local[left[finger][0]+k],reflect(grip.local[right[finger][0]+k],2),1e-6));
    const auto other=r::mirror_finger_grip(grip_left,left,right,parents);
    assert(other.valid);
    for(unsigned finger=0;finger<5;++finger) for(unsigned k=0;k<5;++k)
        assert(close(other.local[right[finger][0]+k],reflect(grip_left.local[left[finger][0]+k],2),1e-6));
    const auto back=r::mirror_finger_grip(mirrored,left,right,parents);
    assert(back.valid);
    for(unsigned joint=0;joint<69;++joint) {
        assert(back.mask[joint]==grip.mask[joint]);
        assert(close(back.local[joint],grip.local[joint],1e-5));
    }
    // The mirrored grip drives the other hand's own hierarchy and keeps its
    // unmasked joints untouched.
    const auto left_open=hand_pose(left,.15,.6,true);
    const auto left_closed=r::apply_finger_grip(left_open,parents,mirrored,std::array<float,5>{1,1,1,1,1});
    for(unsigned joint=0;joint<69;++joint) {
        if(!mirrored.mask[joint]) {assert(left_closed[joint].f==left_open[joint].f);continue;}
        assert(close(local_at(left_closed,joint),mirrored.local[joint],1e-4));
    }
    auto bad_chains=right;bad_chains[2][1]=100;
    assert(!r::capture_finger_grip(held,parents,bad_chains).valid);
    auto nan_pose=held;nan_pose[right[1][2]].f[13]=std::nanf("");
    assert(!r::capture_finger_grip(nan_pose,parents,right).valid);
    auto bad_parents=parents;bad_parents[0]=3;
    assert(!r::valid_hierarchy(bad_parents));
    assert(!r::capture_finger_grip(held,bad_parents,right).valid);
    auto short_chains=left;short_chains[4][2]=69;
    assert(!r::mirror_finger_grip(grip,right,short_chains,parents).valid);
    assert(!r::mirror_finger_grip(grip,right,left,parents,3).valid);
    assert(!r::mirror_finger_grip(r::FingerGrip{},right,left,parents).valid);
    const auto none=r::apply_finger_grip(relaxed,parents,r::FingerGrip{},std::array<float,5>{1,1,1,1,1});
    for(unsigned joint=0;joint<69;++joint) assert(none[joint].f==relaxed[joint].f);
    bool rejected=false;
    try {r::apply_finger_grip(relaxed,parents,grip,std::array<float,5>{0,0,1.5f,0,0});}
    catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);
    rejected=false;
    try {r::apply_finger_grip(relaxed,parents,grip,std::array<float,5>{0,0,std::nanf(""),0,0});}
    catch(const std::invalid_argument&) {rejected=true;}
    assert(rejected);
    std::cout<<"PASS: native sword-grip capture on the finger subtrees, rigid expansion onto the posed wrist, "
        "per-digit amounts, order-preserving local mirror (twice identity) and invalid-input rejection\n";
}
