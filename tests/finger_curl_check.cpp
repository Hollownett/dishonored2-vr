#include "../src/native_hand_reference.hpp"
#include "../src/arm_tracking.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
namespace r=d2vr::rig;
int main() {
    for(float mirror:{-1.f,1.f}) {
        r::ArmHierarchy parents{};parents.fill(0);parents[0]=-1;
        std::array<r::Transform,69> bind{};
        for(auto& t:bind) {t.f[0]=t.f[5]=t.f[10]=1;}
        r::FingerChains chains{};
        for(unsigned finger=0;finger<5;++finger) for(unsigned segment=0;segment<3;++segment) {
            const auto joint=5+finger*4+segment;chains[finger][segment]=joint;
            parents[joint]=segment?static_cast<std::int16_t>(joint-1):0;
            bind[joint].f[12]=mirror*(.05f-.02f*finger);bind[joint].f[13]=.1f+.025f*segment;
            if(finger && !segment) {
                parents[joint]=static_cast<std::int16_t>(joint-1);
                bind[joint-1]=bind[joint];bind[joint-1].f[13]=.075f;
            }
        }
        for(unsigned finger=0;finger<5;++finger) {
            const auto root=chains[finger][0],helper=30+finger;
            parents[helper]=static_cast<std::int16_t>(root);bind[helper]=bind[root];bind[helper].f[14]=.005f;
        }
        const auto mapping=r::finger_binding(bind,parents,chains);
        const auto uncalibrated=r::curl_fingers(bind,parents,mapping,1);
        for(unsigned joint=0;joint<69;++joint) assert(uncalibrated[joint].f==bind[joint].f);
        auto closed=bind;
        // Synthetic authored endpoints exercise interpolation only; runtime
        // references are captured from unmodified native animation palettes.
        for(const auto& chain:chains) for(auto root:chain) {
            const auto before=closed[root];
            const auto after=r::turn_root(before,{mirror*std::sin(.25),0,0,std::cos(.25)},r::position(before));
            for(unsigned joint=0;joint<69;++joint) if(r::in_subtree(parents,joint,root))
                closed[joint]=r::rigid_retarget(closed[joint],before,after);
        }
        const auto calibration=r::native_finger_binding(bind,closed,parents,chains);
        const auto zero=r::curl_fingers(bind,parents,calibration,0);
        for(unsigned joint=0;joint<69;++joint) for(unsigned i=0;i<16;++i) assert(std::abs(zero[joint].f[i]-bind[joint].f[i])<1e-5);
        for(float amount:{.1f,.5f,1.f}) {
            const auto posed=r::curl_fingers(bind,parents,calibration,amount);
            for(unsigned joint=0;joint<69;++joint) {
                assert(r::valid(posed[joint]));
                if(!calibration.mask[joint]) assert(posed[joint].f==bind[joint].f);
                const auto parent=parents[joint];
                if(parent>=0) assert(std::abs(d2vr::length(r::position(posed[joint])-r::position(posed[parent]))-
                                             d2vr::length(r::position(bind[joint])-r::position(bind[parent])))<1e-5);
            }
        }
        auto bad=chains;bad[1][0]=100;
        bool rejected=false;try {r::finger_binding(bind,parents,bad);} catch(const std::invalid_argument&) {rejected=true;}assert(rejected);
        rejected=false;try {r::curl_fingers(bind,parents,calibration,1.1f);} catch(const std::invalid_argument&) {rejected=true;}assert(rejected);
        rejected=false;try {r::native_finger_binding(bind,bind,parents,chains);} catch(const std::invalid_argument&) {rejected=true;}assert(rejected);
        auto stretched=closed;stretched[chains[1][1]].f[13]+=.1f;
        rejected=false;try {r::native_finger_binding(bind,stretched,parents,chains);} catch(const std::invalid_argument&) {rejected=true;}assert(rejected);
        const auto index_only=r::curl_fingers(bind,parents,calibration,std::array<float,5>{0,1,0,0,0});
        assert(d2vr::length(r::position(index_only[chains[1][2]])-r::position(closed[chains[1][2]]))<1e-5);
        assert(d2vr::length(r::position(index_only[chains[2][2]])-r::position(bind[chains[2][2]]))<1e-5);
        r::Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=1;
        const auto target=r::turn_root(identity,{0,0,std::sin(.4),std::cos(.4)},{1,2,3});
        auto current=bind;for(auto& t:current) t=r::rigid_retarget(t,identity,target);
        const auto moved=r::curl_fingers(current,parents,calibration,1);
        for(unsigned joint=0;joint<69;++joint) if(calibration.mask[joint]) {
            const auto expected=r::rigid_retarget(closed[joint],identity,target);
            assert(d2vr::length(r::position(moved[joint])-r::position(expected))<1e-5);
        }
        std::stringstream archive;
        archive.write("D2VRHP01",8);archive.write("b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180",64);
        std::array<std::array<float,12>,69> inverse{};
        for(auto& rows:inverse) rows[0]=rows[5]=rows[10]=1;
        const r::FingerPalmHelpers helpers{69,69};
        const auto write=[&](const auto& data) {archive.write(reinterpret_cast<const char*>(&data),sizeof(data));};
        for(std::uint32_t side=0;side<2;++side) {write(side);write(parents);write(chains);write(helpers);write(inverse);write(bind);write(closed);}
        const auto refs=r::read_native_hand_references(archive);assert(refs[0].fingers.native_reference && refs[1].fingers.native_reference);
        auto bytes=archive.str();bytes[8]='0';std::stringstream bad_archive(bytes);
        rejected=false;try {(void)r::read_native_hand_references(bad_archive);} catch(const std::invalid_argument&) {rejected=true;}assert(rejected);
        bytes=archive.str();bytes.resize(bytes.size()-1);std::stringstream truncated(bytes);
        rejected=false;try {(void)r::read_native_hand_references(truncated);} catch(const std::invalid_argument&) {rejected=true;}assert(rejected);
        // Procedural fist: identity at zero squeeze, rigid bones, fingertips
        // toward the palm, and a finger already at the target is not curled further.
        const r::HandJoints palm{};
        const auto relaxed=r::procedural_curl(bind,parents,chains,palm,0,{0,0,0,0,0});
        for(unsigned joint=0;joint<69;++joint) assert(relaxed[joint].f==bind[joint].f);
        const auto fist=r::procedural_curl(bind,parents,chains,palm,0,{1,1,1,1,1});
        const d2vr::Vec3 palm_in{0,0,-mirror};
        for(unsigned joint=0;joint<69;++joint) {
            assert(r::valid(fist[joint]));
            const auto parent=parents[joint];
            // Knuckle helpers take half the hinge about the knuckle; bones stay rigid.
            if(parent>=0 && joint<30) assert(std::abs(d2vr::length(r::position(fist[joint])-r::position(fist[parent]))-
                                         d2vr::length(r::position(bind[joint])-r::position(bind[parent])))<1e-5);
        }
        for(unsigned finger=1;finger<5;++finger)
            assert(d2vr::dot(r::position(fist[chains[finger][2]])-r::position(bind[chains[finger][2]]),palm_in)>.01);
        const auto again=r::procedural_curl(fist,parents,chains,palm,0,{1,1,1,1,1});
        for(unsigned finger=1;finger<5;++finger) for(auto joint:chains[finger])
            assert(d2vr::length(r::position(again[joint])-r::position(fist[joint]))<1e-4);
        const auto pointing=r::procedural_curl(bind,parents,chains,palm,0,{1,0,1,1,1});
        assert(d2vr::length(r::position(pointing[chains[1][2]])-r::position(bind[chains[1][2]]))<1e-6);
    }
    std::cout<<"PASS: native reference blending, procedural fist (rigid, palm-ward, no over-curl, independent index) and invalid references\n";
}
