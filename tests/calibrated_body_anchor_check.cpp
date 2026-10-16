#include "../src/full_body_driver.hpp"
#include <iostream>
#include <cmath>
using namespace d2vr;
using namespace d2vr::fullbody;
namespace {
int failures=0;
void expect(bool condition,const char* message) {if(!condition){++failures;std::cerr<<"FAIL "<<message<<'\n';}}
struct Fixture {
    Driver driver;
    body::Skeleton b=skeleton(data::kBody),a[2]{skeleton(data::kLeftArm),skeleton(data::kRightArm)};
    std::array<Transform,20> native_body{};
    std::array<Transform,20> output_body{};
    std::array<Transform,69> native_arm[2]{};
    std::array<float,15> body_frame{},arm_frame[2]{};
    Tracking tracking{};
    AvatarCalibrationResult fit{};
    Fixture(double yaw,double eye_height) {
        fit.reference_epoch=1;fit.palm_span_metres=1.5376323527553446;fit.shoulder_span_metres=.31135770827676856;
        fit.eye_to_shoulder_metres=.282828964636876;fit.arm_scale=1.0478091908198655;
        fit.standing_eye_height_metres=1.645879692501492;fit.shoulder_span_measured=fit.arm_lengths_measured=true;
        fit.arm_reach_metres={.6811952818642661,.6787665947295677};
        driver.configure_calibration(fit);driver.tune(.08,.02);
        const float c=static_cast<float>(std::cos(yaw)),s=static_cast<float>(std::sin(yaw));
        body_frame={3,-4,2.3301475048065186f,c,s,0,-s,c,0,0,0,1,1,1,1};
        for(unsigned j=0;j<20;++j)native_body[j]=to_transform(b.bind[j]);
        tracking.eye={body::rot_z(yaw),{3,-4,eye_height}};tracking.epoch=1;tracking.reference_epoch=1;
        tracking.tick=tracking.source_tick=1000;
        for(unsigned side=0;side<2;++side) {
            for(unsigned j=0;j<69;++j)native_arm[side][j]=to_transform(a[side].bind[j]);
            arm_frame[side]=body_frame;
            // Independently translated/rotated native arm model frames affect only conversion.
            arm_frame[side][0]+=side?.27f:-.19f;arm_frame[side][1]+=side?-.11f:.16f;
            const double arm_yaw=yaw+(side?-.21:.17);
            arm_frame[side][3]=static_cast<float>(std::cos(arm_yaw));arm_frame[side][4]=static_cast<float>(std::sin(arm_yaw));
            arm_frame[side][6]=-arm_frame[side][4];arm_frame[side][7]=arm_frame[side][3];
            tracking.grip_valid[side]=true;
            tracking.grip[side]={tracking.eye.q,tracking.eye.p+rotated(tracking.eye.q,{.3,side?-.3:.3,-.5})};
            (void)driver.arm_frozen(side,native_arm[side],arm_frame[side]);
        }
    }
    Vec3 step(body::Ownership ownership={}) {
        tracking.tick+=11;tracking.source_tick=tracking.tick;tracking.seconds=tracking.tick/1000.;driver.publish(tracking,ownership);
        const auto body_output=driver.body(native_body,body_frame);
        expect(body_output.has_value(),"body production solve missing");
        if(body_output)output_body=*body_output;
        Vec3 shoulders{};
        for(unsigned side=0;side<2;++side) {
            const auto output=driver.arm_frozen(side,native_arm[side],arm_frame[side],tracking.tick);
            expect(output.has_value(),"arm production solve missing");
            if(output)shoulders=shoulders+rig::ModelFrame(arm_frame[side]).world_point(rig::position(output->joints[2]))*.5;
        }
        return shoulders;
    }
    void anchor(const char* label,double tolerance=.005) {
        Vec3 shoulders{};for(unsigned i=0;i<90;++i)shoulders=step();
        const auto drop=tracking.eye.p.z-shoulders.z;
        std::cout<<label<<" measured_drop="<<fit.eye_to_shoulder_metres<<" solved_drop="<<drop<<" error="<<drop-fit.eye_to_shoulder_metres<<'\n';
        expect(std::abs(drop-fit.eye_to_shoulder_metres)<tolerance,"measured upperarm shoulder centres lost to leg feasibility");
    }
};
}
int main() {
    constexpr double ground=2.3301475048065186,measured_height=1.645879692501492;
    Fixture control(0,ground+measured_height);control.anchor("normal ground");
    Fixture rejected(0,4.100638675689697);rejected.anchor("v223 raised eye/native ground");
    Fixture rotated(.83,4.100638675689697);rotated.anchor("rotated body and independent arm frames");
    for(const double yaw:{0.,body::kPi*.5}) {
        Fixture heading(yaw,ground+measured_height);
        for(unsigned frame=0;frame<90;++frame)heading.step();
        const Vec3 forward{std::cos(yaw),std::sin(yaw),0},left{-std::sin(yaw),std::cos(yaw),0};
        const auto check_heading=[&] {
            const auto diagnostic=heading.driver.diagnostic();
            const auto flatten=[](Vec3 axis) {axis.z=0;return unit(axis);};
            expect(dot(flatten(diagnostic.pelvis_forward),forward)>.999,"pelvis forward does not follow body yaw");
            expect(dot(flatten(diagnostic.pelvis_left),left)>.999,"pelvis left does not follow body yaw");
            const auto pelvis=heading.b.find("center_root_jnt");
            const auto output_position=rig::ModelFrame(heading.body_frame).world_point(rig::position(heading.output_body[pelvis]));
            expect(length(diagnostic.pelvis.p-output_position)<1e-5,"pelvis diagnostic position differs from rendered output");
        };
        check_heading();
        const body::Ownership fixed_body{1,1,{1,1},.15,.35};
        for(unsigned frame=0;frame<180;++frame)heading.step(fixed_body);
        check_heading();
        for(const double head_turn:{body::kPi*.5,-body::kPi*.5}) {
            heading.tracking.eye.q=body::rot_z(yaw+head_turn);
            for(unsigned frame=0;frame<30;++frame)heading.step(fixed_body);
            check_heading();
        }
    }
    // A pitched eye can move anatomical shoulders about the neck; floor mismatch must
    // not introduce an additional drop at the same pitch and local tracked hand pose.
    Fixture pitch_control(0,ground+measured_height),pitch_raised(0,4.100638675689697);
    pitch_control.tracking.eye.q=pitch_raised.tracking.eye.q=body::axis_angle({0,1,0},.25);
    Vec3 low{},high{};for(unsigned i=0;i<90;++i){low=pitch_control.step();high=pitch_raised.step();}
    const auto pitch_error=(pitch_raised.tracking.eye.p.z-high.z)-(pitch_control.tracking.eye.p.z-low.z);
    std::cout<<"pitched floor mismatch extra_drop="<<pitch_error<<'\n';
    expect(std::abs(pitch_error)<.005,"pitch introduced additional shoulder drop from floor mismatch");
    // Authored animation deliberately owns the torso/arms; it is not forced to a
    // tracked calibration anchor. Return to tracked ownership must restore the fit.
    const body::Ownership authored{1,1,{1,1},.15,.35};
    Vec3 authored_shoulders{};for(unsigned i=0;i<180;++i)authored_shoulders=rejected.step(authored);
    const auto owned=rejected.driver.diagnostic();
    expect(owned.owned[1]>.99 && owned.owned[2]>.99 && owned.owned[3]>.99,"authored ownership not retained");
    const auto chest=rejected.b.bind[rejected.b.find("center_spine03_jnt")];
    const double native_shoulder_z=(body::mul(chest,rejected.a[0].bind[2]).p.z+body::mul(chest,rejected.a[1].bind[2]).p.z)*.5;
    const double authored_drop=(rejected.b.bind[rejected.b.find("camera_jnt")].p.z-native_shoulder_z)*owned.scale;
    expect(std::abs(rejected.tracking.eye.p.z-authored_shoulders.z-authored_drop)<.005,"authored native shoulder centres changed by tracked anchor policy");
    rejected.anchor("after authored release");
    expect(rejected.driver.diagnostic().owned[1]<.01,"tracked torso ownership did not return");
    if(failures)return 1;
    std::cout<<"PASS: production calibrated shoulder centres survive native ground conflict, model conversion, pitch and authored return\n";
}
