#include "corpse_diagnostics.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
bool has(const std::string& s,const std::string& k) {return s.find(k)!=std::string::npos;}
void json_has_every_listed_key() {
    CorpseDiagnostics d;const auto j=d.json();
    for(const char* k:{"holds","binds","releases","open_fist","lag","lifecycle","runaway","fault","state_changed","refused","motor_samples","capped_writes",
        "cap_fraction_mean","mass_source","mass_verified","mass_read_rejected","lag_m_max","peak_body_speed","peak_bone_speed_all17","tow_writes",
        "tow_disabled_saturated","event_posts_b9","event_posts_suppressed","fault_latch","wake_attempts","wake_refused","body_near_player","bones","limb","collar",
        "drawn_anchor_gap_m","tuning"}) CHECK(has(j,std::string("\"")+k+"\":"));
    CHECK(has(j,"\"mass_source\":\"class\""));CHECK(has(j,"\"mass_verified\":false"));CHECK(has(j,"\"tuning\":\"default\""));
    CHECK(!has(j,"\"mass\":{"));
    d.set_tuning(true,true);d.set_fault_latch(true);
    CHECK(has(d.json(),"\"tuning\":\"file\""));CHECK(has(d.json(),"\"fault_latch\":true"));
}
void releases_split_by_reason_and_side() {
    CorpseDiagnostics d;
    d.note_release(0,ReleaseReason::lag);d.note_release(1,ReleaseReason::lag);d.note_release(1,ReleaseReason::lag);
    d.note_release(0,ReleaseReason::runaway);d.note_release(1,ReleaseReason::refused);d.note_release(0,ReleaseReason::none);
    const auto j=d.json();
    CHECK(has(j,"\"lag\":[1,2]"));CHECK(has(j,"\"runaway\":[1,0]"));CHECK(has(j,"\"refused\":[0,1]"));CHECK(has(j,"\"open_fist\":[0,0]"));
    CHECK(has(j,"],\"runaway\":1,"));
}
void collar_versus_limb_histogram() {
    CorpseDiagnostics d;
    d.note_bind(0,2,MassSource::cls);d.note_bind(0,3,MassSource::cls);d.note_bind(1,7,MassSource::cls);d.note_bind(1,13,MassSource::cls);
    CHECK(has(d.json(),"\"bones\":{\"limb\":2,\"collar\":2}"));CHECK(has(d.json(),"\"binds\":[2,2]"));
}
void cap_fraction_mean() {
    CorpseDiagnostics d;
    for(int i=0;i<4;++i) {CorpseMotorSample s;s.side=0;s.capped=i<3;s.cap_fraction=s.capped?1.0:0.0;d.note_motor(s);}
    CHECK(has(d.json(),"\"cap_fraction_mean\":[0.75,0]"));CHECK(has(d.json(),"\"capped_writes\":[3,0]"));CHECK(has(d.json(),"\"motor_samples\":[4,0]"));
}
void peaks_keep_maximum() {
    CorpseDiagnostics d;
    CorpseMotorSample a;a.side=1;a.lag_m=0.4;a.body_speed=3;a.peak_all17=9;d.note_motor(a);
    CorpseMotorSample b;b.side=1;b.lag_m=0.1;b.body_speed=1;b.peak_all17=2;d.note_motor(b);
    CorpseMotorSample c;c.side=1;c.lag_m=std::nan("");c.body_speed=1e300*1e300;d.note_motor(c);
    d.note_anchor_gap(0,0.2);d.note_anchor_gap(0,0.1);
    const auto j=d.json();
    CHECK(has(j,"\"lag_m_max\":[0,0.4]"));CHECK(has(j,"\"peak_body_speed\":[0,3]"));CHECK(has(j,"\"peak_bone_speed_all17\":[0,9]"));
    CHECK(has(j,"\"drawn_anchor_gap_m\":[0.2,0]"));
}
void holds_never_negative() {
    CorpseDiagnostics d;
    d.note_release(0,ReleaseReason::open_fist);d.note_release(0,ReleaseReason::lag);
    CHECK(has(d.json(),"\"holds\":[0,0]"));
    d.note_bind(0,7,MassSource::cls);d.note_bind(0,7,MassSource::cls);d.note_bind(0,7,MassSource::cls);
    CHECK(has(d.json(),"\"holds\":[1,0]"));
}
void counters_and_mass() {
    CorpseDiagnostics d;
    d.note_mass_rejected();BodyMass m;m.rejected=true;d.note_mass(m);m.rejected=false;d.note_mass(m);
    d.note_post(true);d.note_post(false);d.note_post(false);d.note_wake_attempt();d.note_wake_refused();d.note_near_player(1);
    CorpseMotorSample s;s.tow_written=true;s.mass_source=MassSource::measured;d.note_motor(s);
    const auto j=d.json();
    CHECK(has(j,"\"mass_read_rejected\":2"));CHECK(has(j,"\"event_posts_b9\":1"));CHECK(has(j,"\"event_posts_suppressed\":2"));
    CHECK(has(j,"\"wake_attempts\":1"));CHECK(has(j,"\"wake_refused\":1"));CHECK(has(j,"\"body_near_player\":[0,1]"));
    CHECK(has(j,"\"tow_writes\":1"));CHECK(has(j,"\"mass_source\":\"measured\""));CHECK(has(j,"\"mass_verified\":false"));
}
std::size_t commas(const std::string& s) {std::size_t n=0;for(char c:s) n+=c==',';return n;}
void csv_row_field_count_matches_header() {
    CorpseMotorSample s;s.tick=5;s.side=1;s.bone=3;s.lag_m=.5;s.capped=true;
    CHECK(commas(CorpseDiagnostics::csv_row(s))==commas(CorpseDiagnostics::csv_header()));
    s.lag_m=std::nan("");CHECK(commas(CorpseDiagnostics::csv_row(s))==commas(CorpseDiagnostics::csv_header()));
}
void csv_capped_at_20000_rows() {
    const std::filesystem::path p("corpse_diag_trace.csv");
    std::remove(p.string().c_str());
    CorpseTraceWriter w;CHECK(w.start(p));
    CorpseMotorSample s;
    for(int it=0;it<400 && w.written()<20000;++it) {
        for(int i=0;i<500;++i) {s.tick++;w.push(s);}
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    for(int i=0;i<2000;++i) {s.tick++;w.push(s);}
    w.stop();
    std::ifstream in(p);std::string line;std::size_t lines=0;while(std::getline(in,line)) ++lines;
    in.close();CHECK(lines==CorpseDiagnostics::csv_max_rows+1);
    std::remove(p.string().c_str());
}
}
int main() {
    json_has_every_listed_key();releases_split_by_reason_and_side();collar_versus_limb_histogram();cap_fraction_mean();peaks_keep_maximum();
    holds_never_negative();counters_and_mass();csv_row_field_count_matches_header();csv_capped_at_20000_rows();
    std::puts("corpse_diagnostics_check ok");return 0;
}
