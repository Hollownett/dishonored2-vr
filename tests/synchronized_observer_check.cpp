#include "../src/synchronized_observer.hpp"
#include <cassert>
#include <iostream>
#include <sstream>

int main() {
    using namespace d2vr;
    // Production stores this bounded diagnostic ring globally, outside the
    // engine thread stack. Match that lifetime as telemetry fields grow.
    static SynchronizedObserver observer;
    assert(!observer.begin(0,1000));observer.enable(true);
    SynchronizedTracking raw{};raw.tick=1000;raw.revision=9;raw.native_frame=44;
    raw.pose_epoch=raw.reference_epoch=1;raw.source_tick=900;raw.grip_value[0]=.75;observer.tracking(raw);
    auto first=observer.begin(0,1000);assert(first && first->raw.revision==9);
    assert(!observer.begin(0,1099));assert(observer.begin(0,1100));
    assert(observer.begin(1,1000));assert(observer.begin(2,1000));assert(!observer.begin(3,1000));
    first->palette_valid=true;first->count=20;first->model=0x1234;first->source=0x5678;
    for(unsigned i=0;i<20;++i) {first->palette[i].f[0]=first->palette[i].f[5]=first->palette[i].f[10]=first->palette[i].f[15]=1;
        first->palette[i].f[12]=static_cast<float>(i);}
    for(unsigned i=0;i<128;++i) {first->callback_sequence=i+1;observer.publish(*first);}
    observer.publish(*first);assert(observer.dropped()==1);
    for(unsigned i=0;i<128;++i) {const auto v=observer.pop();assert(v && v->callback_sequence==i+1 && v->raw.revision==9);}
    assert(!observer.pop());observer.publish(*first);assert(observer.pop());
    observer.enable(false);observer.publish(*first);assert(!observer.pop());observer.enable(true);
    assert(observer.dropped()==0);

    // Exact event time/frame/epochs and accepted dimensions from captured CSV.
    // Head/controller poses below are native-bind SYNTHETIC inputs: v220 did
    // not record raw poses or final palettes and cannot furnish those inputs.
    struct Event {std::uint64_t tick,present,epoch;bool accepted;};
    const Event events[]={{225334218,21490,1,false},{225334234,21491,1,false},
        {225335234,21555,1,true},{225340312,21928,1,true}};
    const AvatarCalibrationResult fit{1,1.46747*1.06811,.311208*1.06811,.148397,1.06811,{}};
    fullbody::Driver driver;driver.tune(0,1);
    const auto b=fullbody::skeleton(fullbody::data::kBody);
    const auto a0=fullbody::skeleton(fullbody::data::kLeftArm),a1=fullbody::skeleton(fullbody::data::kRightArm);
    std::array<rig::Transform,20> body{};std::array<rig::Transform,69> arms[2]{};
    for(unsigned i=0;i<20;++i)body[i]=fullbody::to_transform(b.bind[i]);
    const std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
    fullbody::Tracking tracking{};tracking.eye=b.bind[b.find("camera_jnt")];tracking.epoch=1;
    for(unsigned side=0;side<2;++side) {
        const auto& a=side?a1:a0;const std::string prefix=side?"right":"left";
        for(unsigned i=0;i<69;++i)arms[side][i]=fullbody::to_transform(a.bind[i]);
        std::array<rig::Transform,4> knuckles{};const char* fingers[]={"index","medium","ring","pinky"};
        for(unsigned i=0;i<4;++i)knuckles[i]=arms[side][a.find(prefix+"_"+fingers[i]+"_02_jnt")];
        tracking.grip[side]=body::mul(b.bind[b.find("center_spine03_jnt")],fullbody::to_xf(
            rig::hand_bind_grip(arms[side][a.find(prefix+"_wrist_jnt")],knuckles)));
        tracking.grip_valid[side]=true;(void)driver.arm(side,arms[side],identity);
    }
    std::uint64_t revision=0;bool retained_body=false;
    for(const auto& e:events) {
        tracking.tick=e.tick;tracking.seconds=e.tick/1000.;tracking.native_frame=e.present;tracking.tracking_revision=++revision;tracking.epoch=e.epoch;
        if(e.accepted)driver.configure_calibration(fit);
        driver.publish(tracking,{});
        assert(static_cast<bool>(driver.arm(0,arms[0],identity))==retained_body);
        assert(driver.body(body,identity));assert(driver.arm(0,arms[0],identity));assert(driver.arm(1,arms[1],identity));
        retained_body=true;
        const auto d=driver.diagnostic();assert(d.revision==d.solved_revision && d.tracking.tracking_revision==revision);
        assert(d.tracking.native_frame==e.present && d.body_valid && d.arm_valid[0] && d.arm_valid[1]);
        if(e.accepted)assert(d.fit && !d.fit->standing_eye_height_metres && d.fit_revision==1);
        if(e.tick==225340312)assert(d.gap_reset && d.substeps==1);
    }
    first->solver=driver.diagnostic();first->consistent=true;first->raw.revision=revision;
    first->grab_decision_tick[0]=950;first->grab_source_tick[0]=900;first->grab_source_age_ms[0]=100;
    first->grab_age_valid[0]=true;first->grab_gate[0]=12;first->grab_raw[0]=.75;
    first->raw.native_frame=events[3].present;first->tick=events[3].tick;
    for(unsigned kind=0;kind<3;++kind) {
        first->source_kind=kind;first->count=kind?69u:20u;
        first->palette.fill(fullbody::to_transform({}));
        std::ostringstream out;write_synchronized(out,*first,123,7);const auto text=out.str();
        assert(text.find("\"schema_version\":1")!=std::string::npos);
        assert(text.find("\"source_kind\":"+std::to_string(kind))!=std::string::npos);
        assert(text.find("\"source_address\":22136")!=std::string::npos);
        assert(text.find("\"dropped\":7")!=std::string::npos);
        assert(text.find("\"source_tick\":900")!=std::string::npos && text.find("\"grip_value\":0.75")!=std::string::npos);
        assert(text.find("\"decision_tick\":950")!=std::string::npos && text.find("\"source_age_ms\":100")!=std::string::npos);
        const auto start=text.find("\"final_palette\":["),end=text.find("],\"",start);assert(start!=std::string::npos && end!=std::string::npos);
        unsigned matrices=0;for(auto pos=start;pos<end;++pos)if(text[pos]=='[')++matrices;
        assert(matrices==first->count+1 && text.back()=='\n');
        assert(text.find("\"standing_eye_height\":null")!=std::string::npos);
        first->consistent=false;std::ostringstream rejected;write_synchronized(rejected,*first,123,0);
        assert(rejected.str().find("\"palm_residual_metres\":null")!=std::string::npos);first->consistent=true;
    }
    first->source_kind=1;first->landmarks_valid=true;
    first->actual_palm=first->solver.tracking.grip[0];first->actual_palm.p.x+=.25;
    first->count=70;first->palette_valid=true;
    std::ostringstream bounded;bounded.precision(3);write_synchronized(bounded,*first,123,0);
    assert(bounded.precision()==3 && bounded.str().find("\"final_palette\":[]")!=std::string::npos);
    assert(bounded.str().find("\"palm_residual_metres\":0.25")!=std::string::npos);
    assert(bounded.str().find("\"torso_width_measured\":false")!=std::string::npos);
    first->raw.head.position.x=std::numeric_limits<double>::quiet_NaN();
    first->raw.head.orientation.w=std::numeric_limits<double>::infinity();
    first->solver.wall_dt=std::numeric_limits<double>::infinity();
    first->preparation_ms=std::numeric_limits<double>::quiet_NaN();
    first->model_frame[0]=std::numeric_limits<float>::quiet_NaN();first->count=1;
    first->palette[0].f[0]=std::numeric_limits<float>::infinity();
    first->source_kind=99;first->landmarks_valid=true;
    std::ostringstream invalid;write_synchronized(invalid,*first,123,0);
    assert(invalid.str().find("nan")==std::string::npos && invalid.str().find("inf")==std::string::npos);
    assert(invalid.str().find("\"wall_dt\":null")!=std::string::npos);
    tracking.epoch=2;tracking.tick+=1000;driver.publish(tracking,{});assert(driver.arm(0,arms[0],identity));
    assert(driver.body(body,identity));assert(driver.diagnostic().wall_dt<.02);
    std::cout<<"PASS: bounded synchronized observer, rate/capacity/drop, serialized native palette schema, captured fit-event/synthetic-pose sequence\n";
}
