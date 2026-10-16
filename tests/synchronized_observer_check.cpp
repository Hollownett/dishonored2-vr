#include "../src/synchronized_observer.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <fstream>

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
    assert(observer.begin(1,1000));assert(observer.begin(2,1000));assert(!observer.begin(5,1000));
    for(unsigned kind=3;kind<=4;++kind) {
        assert(!observer.begin_palette(kind,1000));assert(!observer.pop());
        auto reader=observer.begin(kind,1000);assert(reader && reader->source_kind==kind);
        assert(!observer.begin(kind,1099));assert(observer.begin(kind,1100));
        reader->socket_token=41;reader->socket_pose_epoch=11;reader->socket_reference_epoch=12;reader->socket_mapping_revision=13;
        reader->socket_tick=990;reader->socket_source_tick=980;reader->palette_pose.tracking_revision=14;
        reader->animation=4096;reader->socket_joint=59;reader->socket_stage=6;reader->replaced=true;
        reader->consumer_returned=true;reader->palette_valid=true;reader->palette[0]=fullbody::to_transform({});
        reader->palette[0].f[12]=.25f;
        std::ostringstream serialized;serialized.precision(3);write_synchronized(serialized,*reader,123,0);
        const auto text=serialized.str();assert(serialized.precision()==3 && text.back()=='\n');
        for(const auto* field:{"\"source\":\"attachment_socket_reader\"","\"token\":41","\"pose_epoch\":11","\"reference_epoch\":12",
                "\"mapping_revision\":13","\"tracking_revision\":14","\"joint\":59","\"stage\":6","\"substituted\":true",
                "\"consumer_returned\":true","\"consumer_frame_verified\":false","\"model_local_joint\":["})assert(text.find(field)!=std::string::npos);
        assert(text.find("\"final_palette\"")==std::string::npos && text.find("\"consumer_landmarks\"")==std::string::npos);
        reader->palette_valid=false;reader->replaced=false;reader->consumer_returned=false;reader->socket_stage=2;
        std::ostringstream rejected;write_synchronized(rejected,*reader,123,0);
        assert(rejected.str().find("\"model_local_joint\":null")!=std::string::npos);
        assert(rejected.str().find("\"stage\":2,\"substituted\":false,\"consumer_returned\":false")!=std::string::npos);
        observer.publish(*reader);assert(observer.pop());
    }
    assert(!observer.begin_palette(5,1200));assert(!observer.pop());
    observer.continue_compact();
    assert(!observer.begin_palette(0,1200));assert(!observer.begin_palette(2,1200));assert(observer.begin_palette(1,1200));
    assert(observer.begin(3,1200));assert(observer.begin(4,1200));
    first->palette_valid=true;first->count=20;first->model=0x1234;first->source=0x5678;
    for(unsigned i=0;i<20;++i) {first->palette[i].f[0]=first->palette[i].f[5]=first->palette[i].f[10]=first->palette[i].f[15]=1;
        first->palette[i].f[12]=static_cast<float>(i);}
    for(unsigned i=0;i<128;++i) {first->callback_sequence=i+1;observer.publish(*first);}
    observer.publish(*first);assert(observer.dropped()==1);
    for(unsigned i=0;i<128;++i) {const auto v=observer.pop();assert(v && v->callback_sequence==i+1 && v->raw.revision==9);}
    assert(!observer.pop());observer.publish(*first);assert(observer.pop());
    observer.enable(false);observer.publish(*first);assert(!observer.pop());
    assert(!observer.begin(3,1200));assert(!observer.begin(4,1200));observer.enable(true);
    assert(observer.dropped()==0);
    observer.publish(*first);assert(!observer.pop());
    const auto fresh=observer.begin(1,1200);assert(fresh && fresh->observer_epoch!=first->observer_epoch);
    observer.publish(*fresh);assert(observer.pop());observer.stop();observer.publish(*fresh);assert(!observer.pop());observer.enable(true);

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
    fullbody::Tracking tracking{};tracking.eye=b.bind[b.find("camera_jnt")];tracking.epoch=1;tracking.tracked_hands=3;
    tracking.pose_epoch=tracking.reference_epoch=1;tracking.mapping_revision=17;
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
        tracking.tick=e.tick;tracking.source_tick=e.tick;tracking.seconds=e.tick/1000.;tracking.native_frame=e.present;tracking.tracking_revision=++revision;tracking.epoch=e.epoch;
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
    {
        const auto captured=driver.arm_frozen(0,arms[0],identity,tracking.tick,91827);assert(captured && captured->tracking.tracked_hands==3);
        auto newer=tracking;newer.tick+=10;newer.tracking_revision+=10;newer.tracked_hands=0;driver.publish(newer,{});
        const auto latched=driver.arm_frozen(1,arms[1],identity,newer.tick,91827);assert(latched);
        assert(latched->tracking.tracking_revision==captured->tracking.tracking_revision && latched->tracking.tracked_hands==3);
        assert(driver.diagnostic().tracking.tracking_revision==newer.tracking_revision && driver.diagnostic().tracking.tracked_hands==0);
        first->palette_pose={latched->tracking.tick,latched->tracking.source_tick,latched->tracking.tracking_revision,latched->tracking.pose_epoch,
            latched->tracking.reference_epoch,latched->tracking.mapping_revision,latched->fit_revision,latched->tracking.native_frame,latched->tracking.tracked_hands,latched->owned,true};
        first->raw.tick=first->palette_pose.tick;first->raw.source_tick=first->palette_pose.source_tick;first->raw.mapping_revision=17;
    }
    first->grab_decision_tick[0]=950;first->grab_source_tick[0]=900;first->grab_source_age_ms[0]=100;
    first->grab_age_valid[0]=true;first->grab_gate[0]=12;first->grab_raw[0]=.75;
    first->socket_token=41;first->socket_pose_epoch=11;first->socket_reference_epoch=12;first->socket_mapping_revision=13;
    first->socket_tick=990;first->socket_source_tick=980;first->socket_consumer_tick=1000;
    first->socket_stage=3;first->socket_hand_empty=true;first->socket_tracked_submitted=false;
    first->raw.native_frame=events[3].present;first->tick=events[3].tick;
    for(unsigned kind=0;kind<3;++kind) {
        first->source_kind=kind;first->count=kind?69u:20u;
        first->palette.fill(fullbody::to_transform({}));
        first->hand_proxies_valid=true;first->consumer_returned=true;first->arm_owner_rechecked=true;first->binding_snapshot_current=true;first->model_frame_current=true;
        for(unsigned index=0;index<hand_proxy_count;++index)first->hand_proxies[index]={{static_cast<double>(index),1,2},.01};
        first->raw_hand_proxies=first->hand_proxies;first->raw_hand_proxies_valid=true;first->raw_hand_reached=true;
        std::ostringstream out;write_synchronized(out,*first,123,7);const auto text=out.str();
        assert(text.find("\"schema_version\":1")!=std::string::npos);
        assert(text.find("\"source_kind\":"+std::to_string(kind))!=std::string::npos);
        assert(text.find("\"source_address\":22136")!=std::string::npos);
        assert(text.find("\"dropped\":7")!=std::string::npos);
        assert(text.find("\"source_tick\":"+std::to_string(first->raw.source_tick))!=std::string::npos && text.find("\"grip_value\":0.75")!=std::string::npos);
        assert(text.find("\"decision_tick\":950")!=std::string::npos && text.find("\"source_age_ms\":100")!=std::string::npos);
        const auto start=text.find("\"final_palette\":["),end=text.find("],\"",start);assert(start!=std::string::npos && end!=std::string::npos);
        unsigned matrices=0;for(auto pos=start;pos<end;++pos)if(text[pos]=='[')++matrices;
        assert(matrices==first->count+1 && text.back()=='\n');
        assert(text.find("\"standing_eye_height\":null")!=std::string::npos);
        assert(text.find("\"observation_only\":true,\"raw_intent\":false")!=std::string::npos);
        assert(text.find("\"consumer_returned\":true")!=std::string::npos && text.find("\"tracked_hands\":3")!=std::string::npos);
        assert(text.find("\"consumer_frame_verified\":false")!=std::string::npos);
        for(const auto* field:{"\"socket_terminal\":{\"observation_only\":true,\"token\":41,\"stage\":3",
                "\"publication_tick\":990,\"source_tick\":980,\"consumer_tick\":1000",
                "\"hand_empty\":true,\"tracked_submitted\":false", "\"native_attachment_world\":null"})assert(text.find(field)!=std::string::npos);
        const auto proxy_start=text.find("\"proxies\":["),proxy_end=text.find("],\"palette_pose\"",proxy_start);
        assert(proxy_start!=std::string::npos && proxy_end!=std::string::npos);
        unsigned sphere_arrays{};for(auto position=proxy_start;position<proxy_end;++position)if(text[position]=='[')++sphere_arrays;
        assert(sphere_arrays==(kind?25u:1u));
        const auto raw_start=text.find("\"raw_hand_candidate\":");assert(raw_start!=std::string::npos);
        const auto raw_proxies=text.find("\"proxies\":[",raw_start),raw_end=text.find("]}",raw_proxies);
        assert(raw_proxies!=std::string::npos && raw_end!=std::string::npos);
        unsigned raw_arrays{};for(auto position=raw_proxies;position<raw_end;++position)if(text[position]=='[')++raw_arrays;
        assert(raw_arrays==(kind?25u:1u));
        assert(text.find("\"authorizes_response\":false")!=std::string::npos);
        assert(text.find("\"source\":\"forward_raw_grip_same_frozen_body\"")!=std::string::npos);
        assert(text.find(kind?"\"valid\":true,\"reached\":true":"\"valid\":false,\"reached\":false",raw_start)!=std::string::npos);
        first->consistent=false;std::ostringstream rejected;write_synchronized(rejected,*first,123,0);
        assert(rejected.str().find("\"palm_residual_metres\":null")!=std::string::npos);first->consistent=true;
        assert(text.find("\"solver_palm\":null")==std::string::npos);
        first->solver.arm_valid[1]=false;std::ostringstream stale;write_synchronized(stale,*first,123,0); // the right arm not yet exported for this publication
        assert(stale.str().find("\"solver_palm\":null,\"solver_wrist\":null")!=std::string::npos && stale.str().find("\"solver_palm\":{")!=std::string::npos);
        first->solver.arm_valid[1]=true;
    }
    first->anatomical_shadow.admission_epoch=91;first->anatomical_shadow.revision=92;
    first->anatomical_shadow.tracking_revision=93;first->anatomical_shadow.frame=94;
    first->anatomical_shadow.request_tick=95;first->anatomical_shadow.source_tick=96;
    first->anatomical_shadow.query_tick=97;first->anatomical_shadow.lifecycle_revision=98;
    first->anatomical_shadow.picked=99;first->anatomical_shadow.source_body=0x12345678u;
    first->anatomical_shadow.state={1,2};first->anatomical_shadow.casts={24,23};
    first->anatomical_shadow.witnesses={7,8};first->anatomical_shadow.shift={Vec3{.1,-.2,.3},Vec3{0,std::numeric_limits<double>::quiet_NaN(),0}};
    first->anatomical_shadow.valid=true;first->anatomical_shadow.empty_verified_at_query=false;
    std::ostringstream shadow_output;write_synchronized(shadow_output,*first,123,0);const auto shadow_text=shadow_output.str();
    const auto shadow_start=shadow_text.find("\"anatomical_shadow\":");assert(shadow_start!=std::string::npos);
    assert(shadow_text.find("\"anatomical_shadow\":{\"observation_only\":true,\"authorizes_response\":false,\"valid\":true,\"empty_verified_at_query\":false",shadow_start)!=std::string::npos);
    for(const auto& field:{"\"admission_epoch\":91","\"revision\":92","\"tracking_revision\":93","\"frame\":94",
                           "\"request_tick\":95","\"source_tick\":96","\"query_tick\":97","\"lifecycle_revision\":98",
                           "\"picked\":99","\"source_body\":305419896","\"state\":1","\"casts\":24","\"witnesses\":7",
                           "\"shift\":[0.10000000000000001,-0.20000000000000001,0.29999999999999999]","\"state\":2","\"casts\":23","\"witnesses\":8","\"shift\":[0,null,0]"}) {
        assert(shadow_text.find(field,shadow_start)!=std::string::npos);
    }
    assert(shadow_text.find("nan",shadow_start)==std::string::npos && shadow_text.find("inf",shadow_start)==std::string::npos);
    first->source_kind=1;first->landmarks_valid=true;
    first->actual_palm=first->solver.tracking.grip[0];first->actual_palm.p.x+=.25;
    first->count=70;first->palette_valid=true;
    std::ostringstream bounded;bounded.precision(3);write_synchronized(bounded,*first,123,0);
    assert(bounded.precision()==3 && bounded.str().find("\"final_palette\":[]")!=std::string::npos);
    assert(bounded.str().find("\"palm_residual_metres\":0.25")!=std::string::npos);
    first->palette_pose.tracking_revision++;
    std::ostringstream mismatched;write_synchronized(mismatched,*first,123,0);
    assert(mismatched.str().find("\"palm_residual_metres\":0.25")==std::string::npos);
    assert(mismatched.str().find("\"palm_residual_metres\":null")!=std::string::npos);
    first->palette_pose.tracking_revision--;
    assert(bounded.str().find("\"proxies\":[]")!=std::string::npos);
    assert(bounded.str().find("\"torso_width_measured\":false")!=std::string::npos);
    first->count=69;first->raw_hand_proxies[0].radius=std::numeric_limits<double>::quiet_NaN();
    std::ostringstream bad_raw;write_synchronized(bad_raw,*first,123,0);
    const auto bad_raw_start=bad_raw.str().find("\"raw_hand_candidate\":");
    assert(bad_raw_start!=std::string::npos && bad_raw.str().find("\"valid\":false",bad_raw_start)!=std::string::npos);
    assert(bad_raw.str().find("nan")==std::string::npos);
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
    {
        auto terminal=*first;terminal.source_kind=1;terminal.socket_tick=1000;terminal.socket_consumer_tick=1100;
        terminal.socket_grip={{.123456789,.234567891,.345678912,.456789123},{1234.123456789,-2345.234567891,3456.345678912}};
        terminal.actual_wrist=terminal.actual_palm=terminal.actual_attachment=terminal.socket_native_joint=terminal.socket_grip;
        terminal.landmarks_valid=true;terminal.socket_native_joint_valid=true;terminal.socket_tracking_revision=17;
        terminal.socket_grip.p.x=1234.123456789;
        std::ostringstream compact;compact.precision(3);write_socket_terminal(compact,terminal,123,0);
        assert(compact.precision()==3 && compact.str().find("\"source\":\"arm_socket_terminal\"")!=std::string::npos);
        assert(compact.str().find("final_palette")==std::string::npos && compact.str().find("xyz_xyzw")!=std::string::npos);
        auto reader=terminal;reader.source_kind=3;reader.palette_valid=true;reader.palette[0].f.fill(.123456789f);
        std::ostringstream reader_line;write_synchronized(reader_line,reader,123,0);
        SynchronizedLogBudget budget{};assert((compact.str().size()+reader_line.str().size())*36000ull<budget.compact_limit);
        std::cout<<"PASS compact continuation: terminal "<<compact.str().size()<<" + reader "<<reader_line.str().size()
                 <<" bytes; one synthetic hour at 10 Hz each remains below 64 MiB\n";
        budget.palette_bytes=budget.palette_limit-1;
        const auto palette_before=budget.palette_bytes;std::ostringstream destination;
        assert(write_synchronized_bounded(destination,terminal,123,0,budget)==SynchronizedWrite::written);
        assert(budget.compact_only && budget.palette_bytes==palette_before && budget.compact_bytes==compact.str().size());
        assert(destination.str().find("arm_socket_terminal")!=std::string::npos && destination.str().find("final_palette")==std::string::npos);
        const auto after_transition=destination.str().size();
        for(unsigned iteration=0;iteration<12;++iteration) {
            assert(write_synchronized_bounded(destination,terminal,123,0,budget)==SynchronizedWrite::written);
            assert(write_synchronized_bounded(destination,reader,123,0,budget)==SynchronizedWrite::written);
        }
        assert(destination.str().size()>after_transition && budget.palette_bytes==palette_before);
        auto excluded=terminal;excluded.source_kind=0;const auto before_skip=destination.str().size();
        assert(write_synchronized_bounded(destination,excluded,123,0,budget)==SynchronizedWrite::skipped);
        excluded.source_kind=2;assert(write_synchronized_bounded(destination,excluded,123,0,budget)==SynchronizedWrite::skipped);
        assert(budget.skipped==2 && destination.str().size()==before_skip);
        budget.compact_bytes=budget.compact_limit-1;
        assert(write_synchronized_bounded(destination,reader,123,0,budget)==SynchronizedWrite::exhausted);
        assert(destination.str().size()==before_skip && budget.compact_bytes==budget.compact_limit-1);
        budget.compact_bytes=0;std::ostringstream failed;failed.setstate(std::ios::badbit);
        assert(write_synchronized_bounded(failed,terminal,123,0,budget)==SynchronizedWrite::failed && budget.compact_bytes==0);
        SynchronizedLogBudget exact{};std::ostringstream raw_line;write_synchronized(raw_line,terminal,123,0);
        exact.palette_limit=raw_line.str().size();std::ostringstream exact_output;
        assert(write_synchronized_bounded(exact_output,terminal,123,0,exact)==SynchronizedWrite::written && !exact.compact_only);
        assert(exact.palette_bytes==exact.palette_limit && exact.compact_bytes==0);
        const auto binary_path=std::filesystem::current_path()/"build"/
            ("d2vr-socket-budget-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
        SynchronizedLogBudget binary_budget{};binary_budget.palette_limit=0;
        std::ofstream binary_output(binary_path,std::ios::binary|std::ios::trunc);assert(binary_output);
        assert(write_synchronized_bounded(binary_output,terminal,123,0,binary_budget)==SynchronizedWrite::written);
        assert(write_synchronized_bounded(binary_output,reader,123,0,binary_budget)==SynchronizedWrite::written);
        binary_output.flush();assert(binary_output);binary_output.close();assert(binary_output);
        assert(std::filesystem::file_size(binary_path)==binary_budget.palette_bytes+binary_budget.compact_bytes);
        std::ifstream binary_input(binary_path,std::ios::binary);std::string binary_bytes((std::istreambuf_iterator<char>(binary_input)),{});
        binary_input.close();assert(binary_bytes.find('\r')==std::string::npos);
        assert(std::count(binary_bytes.begin(),binary_bytes.end(),'\n')==2);assert(std::filesystem::remove(binary_path));
    }
    tracking.epoch=2;tracking.tick+=1000;tracking.source_tick=tracking.tick;driver.publish(tracking,{});assert(driver.arm(0,arms[0],identity));
    assert(driver.body(body,identity));assert(driver.diagnostic().wall_dt<.02);
    std::cout<<"PASS: bounded synchronized observer, rate/capacity/drop, serialized native palette schema, captured fit-event/synthetic-pose sequence\n";
}
