#include "../src/avatar_calibration.hpp"
#include <cassert>
#include <iostream>
namespace {
using namespace d2vr;
constexpr double kPi=3.14159265358979323846;
bool near(double a,double b,double tolerance=1e-6) {return std::abs(a-b)<tolerance;}
// Feed a still pose every 25 ms for `count` samples starting at `tick`.
void hold(AvatarCalibration& c,const TrackedPose& head,const std::array<TrackedPose,2>& hands,std::uint64_t tick,unsigned count,std::uint64_t epoch) {
    for(unsigned i=0;i<count;++i) c.observe(head,hands,3,true,true,tick+i*25,epoch);
}
}
int main() {
    TrackedPose head{{0,1.7,0},{}};
    std::array<TrackedPose,2> hands{{{{-.8,1.48,0},{}},{{.8,1.48,0},{}}}};
    // Shoulders sit 8 cm behind the eyes (+Z is backward in OpenXR).
    const double level_reach=std::sqrt(.65*.65+.08*.08);
    {
        // Level T pose without a floor: drop .22 is used, reach from HMD-derived shoulders.
        AvatarCalibration c;c.begin(1,{1.5,.3});hold(c,head,hands,0,41,1);
        const auto fit=c.result(1);assert(fit && c.state()==AvatarCalibrationState::ready && c.progress()==1);
        assert(!fit->standing_eye_height_metres && near(fit->eye_to_shoulder_metres,.22));
        assert(fit->shoulder_span_metres==.3 && !fit->shoulder_span_measured && fit->arm_lengths_measured && !fit->torso_width_measured);
        assert(near(*fit->arm_reach_metres[0],level_reach) && near(*fit->arm_reach_metres[1],level_reach));
        assert(near(fit->palm_span_metres,.3+2*level_reach) && near(fit->arm_scale,fit->palm_span_metres/1.5));
    }
    {
        // v228 failure: a relaxed 30-degree A pose put the grips .55 m below the eyes.
        // Shoulders stay at the eye-height proportion and the reach is unchanged.
        const double eye=1.7,drop=.13*eye,s=std::sin(kPi/6),co=std::cos(kPi/6);
        auto relaxed=hands;
        for(unsigned side=0;side<2;++side) relaxed[side].position={(side?1:-1)*(.15+.65*co),eye-drop-.65*s,.08};
        AvatarCalibration c;c.begin(2,{1.5,.3},0.);hold(c,head,relaxed,0,41,2);
        const auto fit=c.result(2);assert(fit && near(*fit->standing_eye_height_metres,eye));
        assert(near(fit->eye_to_shoulder_metres,drop) && near(*fit->arm_reach_metres[0],.65) && near(*fit->arm_reach_metres[1],.65));
        assert(near(fit->palm_span_metres,1.6) && near(fit->arm_scale,1.6/1.5));
        // Hands held above the nominal shoulder line lower the drop by at most 4 cm.
        auto high=relaxed;for(auto& hand:high) hand.position={hand.position.x>0?.8:-.8,eye-.15,.08};
        AvatarCalibration h;h.begin(3,{1.5,.3},0.);hold(h,head,high,0,41,3);
        assert(h.result(3) && near(h.result(3)->eye_to_shoulder_metres,drop-.04));
        auto low=high;for(auto& hand:low) hand.position.y=eye-.3;
        AvatarCalibration l;l.begin(4,{1.5,.3},0.);hold(l,head,low,0,41,4);
        assert(l.result(4) && near(l.result(4)->eye_to_shoulder_metres,drop));
    }
    {
        // Natural sway completes; slow drift cannot hide inside the average.
        AvatarCalibration natural;natural.begin(11,{1.5,.3});
        for(unsigned i=0;i<=40;++i) {
            auto h=head;auto p=hands;const double sway=.04*std::sin(i*.3);h.position.x+=sway;
            for(auto& hand:p) {hand.position.x+=sway;hand.position.z+=.05*std::sin(i*.2);}
            natural.observe(h,p,3,true,true,100+i*25,11);
        }
        assert(natural.result(11));
        AvatarCalibration drift;drift.begin(12,{1.5,.3});
        for(unsigned i=0;i<=120;++i) {
            auto h=head;auto p=hands;h.position.x+=i*.004;for(auto& hand:p) hand.position.x+=i*.004;
            drift.observe(h,p,3,true,true,100+i*25,12);
        }
        assert(!drift.result(12));
    }
    {
        // Brief faults pause the hold; sustained faults or released grips restart it.
        AvatarCalibration c;c.begin(9,{1.5,.3});
        for(unsigned i=0;i<=40;++i) c.observe(head,hands,3,true,false,i*25,9);
        assert(c.state()==AvatarCalibrationState::grips_released && c.progress()==0 && !c.result(9));
        hold(c,head,hands,1100,21,9);assert(near(c.progress(),.5) && !c.result(9));
        auto bad=hands;bad[0].position.y-=.2;
        assert(c.observe(head,bad,3,true,true,1625,9)==AvatarCalibrationState::pose_invalid && near(c.progress(),.5));
        assert(c.observe(head,hands,1,true,true,1650,9)==AvatarCalibrationState::tracking_lost && near(c.progress(),.5));
        auto moved=hands;moved[0].position.x-=.15;
        assert(c.observe(head,moved,3,true,true,1675,9)==AvatarCalibrationState::pose_moving && near(c.progress(),.5));
        assert(c.observe(head,hands,3,true,true,1700,9)==AvatarCalibrationState::hold_pose && c.progress()>.5);
        // Same-millisecond callbacks add nothing.
        const auto progress=c.progress();c.observe(head,hands,3,true,true,1700,9);assert(c.progress()==progress);
        for(unsigned i=1;i<=20;++i) c.observe(head,bad,3,true,true,1700+i*25,9);
        assert(c.state()==AvatarCalibrationState::pose_invalid && c.progress()==0);
        assert(c.observe(head,hands,3,true,false,2300,9)==AvatarCalibrationState::grips_released && c.progress()==0);
        hold(c,head,hands,2400,41,9);assert(c.result(9) && c.progress()==1);
        const auto accepted=c.result(9);
        // A cancelled retry keeps the previous fit.
        c.begin(9,{1.5,.3});hold(c,head,hands,5500,10,9);
        assert(c.state()==AvatarCalibrationState::hold_pose && c.result(9)->palm_span_metres==accepted->palm_span_metres);
        assert(c.synchronize(9,false)==AvatarCalibrationState::ready && c.result(9));
        c.begin(9,{1.5,.3});c.observe(head,hands,3,true,true,6100,9);
        assert(c.observe(head,hands,3,true,true,6125,10)==AvatarCalibrationState::reference_changed && c.progress()==0 && !c.result(9) && !c.result(10));
        // Player body survives the reference change and refits without a pose.
        assert(c.restore(10,{1.5,.3}) && c.state()==AvatarCalibrationState::ready);
        auto restored=*c.result(10);restored.reference_epoch=9;assert(same_avatar_calibration(restored,*accepted));
        assert(!c.restore(10,{1.5,.3}));
    }
    {
        // Head turned 30 degrees: the side axis comes from the hands.
        AvatarCalibration c;c.begin(13,{1.5,.3});auto turned=head;turned.orientation={0,std::sin(kPi/12),0,std::cos(kPi/12)};
        hold(c,turned,hands,0,41,13);assert(c.result(13) && near(*c.result(13)->arm_reach_metres[0],level_reach));
        AvatarCalibration swapped;swapped.begin(14,{1.5,.3});auto invalid=hands;std::swap(invalid[0],invalid[1]);
        assert(swapped.observe(head,invalid,3,true,true,0,14)==AvatarCalibrationState::pose_invalid);
        auto down=hands;for(auto& hand:down) hand.position={hand.position.x*.3,1.0,0};
        assert(swapped.observe(head,down,3,true,true,25,14)==AvatarCalibrationState::pose_invalid);
    }
    {
        // Entered shoulder width and reaches, asymmetric hands.
        AvatarPlayerMeasurements player;player.standing_eye_height_metres=1.71;player.shoulder_span_metres=.44;player.torso_width_metres=.47;
        AvatarCalibration c;c.begin(20,{1.5,.3},{},player);
        auto asymmetric=hands;asymmetric[0].position.x=-.75;asymmetric[1].position.x=.85;
        hold(c,head,asymmetric,100,41,20);
        const auto fit=c.result(20);assert(fit);
        assert(fit->standing_eye_height_metres==player.standing_eye_height_metres && fit->torso_width_metres==player.torso_width_metres);
        assert(fit->shoulder_span_measured && fit->shoulder_span_metres==.44 && fit->torso_width_measured && fit->arm_lengths_measured);
        assert(near(*fit->arm_reach_metres[0],std::sqrt(.53*.53+.0064)) && near(*fit->arm_reach_metres[1],std::sqrt(.63*.63+.0064)));
        player.arm_reach_metres={.62,.62};c.begin(20,{1.5,.3},{},player);hold(c,head,hands,2000,41,20);
        assert(c.result(20)->arm_reach_metres==player.arm_reach_metres && near(c.result(20)->palm_span_metres,1.68));
        // Contradictory entered reach: measurement keeps running, baseline kept.
        player.arm_reach_metres={.9,.9};c.begin(20,{1.5,.3},{},player);hold(c,head,hands,4000,41,20);
        assert(c.state()==AvatarCalibrationState::dimensions_unsupported && c.result(20)->arm_reach_metres[0]==.62);
        player.torso_width_metres=std::numeric_limits<double>::quiet_NaN();bool rejected=false;
        try {c.begin(20,{1.5,.3},{},player);} catch(const std::invalid_argument&) {rejected=true;}
        assert(rejected && c.result(20));
    }
    {
        // Avatar change: same player body refits to the new native avatar.
        AvatarCalibration c;c.begin(30,{1.5,.3});hold(c,head,hands,0,41,30);assert(c.result(30));
        c.invalidate_rig();assert(c.state()==AvatarCalibrationState::rig_changed && !c.result(30));
        assert(!c.restore(30,{9,.3}) && c.restore(30,{1.4,.35}));
        const auto fit=c.result(30);assert(fit && fit->shoulder_span_metres==.35 && near(*fit->arm_reach_metres[0],level_reach));
        assert(near(fit->palm_span_metres,.35+2*level_reach) && near(fit->arm_scale,fit->palm_span_metres/1.4));
        // A native refusal of a restored body is never restored again.
        c.reject_dimensions();assert(!c.result(30) && c.state()==AvatarCalibrationState::dimensions_unsupported);
        c.invalidate_rig();assert(!c.restore(30,{1.5,.3}));
        // Unsupported size keeps capturing so the player can simply adjust.
        AvatarCalibration big;big.begin(31,{2.4,.3});hold(big,head,hands,0,41,31);
        assert(big.state()==AvatarCalibrationState::dimensions_unsupported && !big.result(31));
        auto wide=hands;wide[0].position.x=-1.1;wide[1].position.x=1.1;hold(big,head,wide,1100,41,31);assert(big.result(31));
        big.clear();assert(big.state()==AvatarCalibrationState::idle && !big.restore(31,{2.4,.3}));
        big.invalidate_rig();assert(big.state()==AvatarCalibrationState::idle);
    }
    {
        // Floor datum gives standing eye height; capture invisible cancels.
        AvatarCalibration c;assert(c.observe(head,hands,3,true,true,0,1)==AvatarCalibrationState::idle);
        c.begin(2,{1.5,.3},.1);hold(c,head,hands,0,41,2);
        assert(c.result(2) && near(*c.result(2)->standing_eye_height_metres,1.6));
        assert(c.synchronize(3,false)==AvatarCalibrationState::reference_changed && !c.result(2));
        c.begin(3,{1.5,.3});hold(c,head,hands,0,10,3);
        assert(c.synchronize(3,false)==AvatarCalibrationState::cancelled && !c.result(3));
        hold(c,head,hands,4000,41,3);assert(c.state()==AvatarCalibrationState::cancelled && !c.result(3));
    }
    std::cout<<"PASS: single forgiving T/A-pose capture, droop-independent reach, shoulders never below eye-height proportion, grace pauses, retained body refit across avatar/reference changes\n";
}
