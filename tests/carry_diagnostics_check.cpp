// Offline pin of the carry diagnostics counters (C7, G6): logic only, no engine. Each candidate's signal must discriminate.
#include "carry_diagnostics.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace d2vr;
static CarryFrame frame(unsigned i,std::uint32_t layer,float scale=0.f) {
    CarryFrame f;f.tick=1000+16ull*i;f.entity=0x1234;f.hold_serial=7;f.layer=layer;f.render_scale=scale;
    f.rf_quat_norm=f.rf_quat_norm2=1.f;f.rf_joint=-1;f.entity_err=0.f;f.hand_speed=0.f;f.master_valid=true;f.residual_age_ms=20;return f;
}
static bool has(const std::string& s,const char* t) {return s.find(t)!=std::string::npos;}
static size_t fields(const std::string& s) {size_t n=1;for(const char c:s) n+=c==',';return n;}
int main() {
    {   // C1 refuted: a healthy tank hold is RIGID_BODY until the swap lands, then DEBRIS; a clear mid-hold that is re-swapped is fine
        CarryDiagnostics d;
        for(unsigned i=0;i<10;++i) d.note(frame(i,i<2?3u:4u));
        const auto j=d.json();
        assert(has(j,"\"rigid_frames\":2") && has(j,"\"rigid_after_swap\":0") && has(j,"\"layer_flips\":0"));
        CarryDiagnostics b;for(unsigned i=0;i<10;++i) b.note(frame(i,4));   // a bottle: DEBRIS natively
        assert(has(b.json(),"\"rigid_after_swap\":0") && has(b.json(),"\"rigid_frames\":0"));
    }
    {   // C1 confirmed: swapped, a clear, then RIGID_BODY again; the clear reason is stamped on the next row
        CarryDiagnostics d;
        for(unsigned i=0;i<5;++i) d.note(frame(i,4));
        d.clear_seen(2);
        auto r=d.note(frame(5,3));
        assert(r.clear_reason==2 && d.note(frame(6,3)).clear_reason==0);
        for(unsigned i=7;i<10;++i) d.note(frame(i,3));
        const auto j=d.json();
        assert(has(j,"\"rigid_after_swap\":5") && has(j,"\"layer_flips\":1") && has(j,"\"clears\":[0,0,1,0,0]") && has(j,"\"holds\":1"));
    }
    {   // a layer that cannot be read is not a healthy layer
        CarryDiagnostics d;d.note(frame(0,CarryFrame::layer_unread));
        assert(has(d.json(),"\"layer_unknown_frames\":1"));
    }
    {   // C4: the clear request needs time to land (async game thread); a reading after that counts, and a re-entry is flagged
        CarryDiagnostics d;
        for(unsigned i=0;i<3;++i) d.note(frame(i,4,1.f));
        for(unsigned i=3;i<10;++i) d.note(frame(i,4));
        auto j=d.json();assert(has(j,"\"scale_frames\":0") && has(j,"\"scale_clear_frames_max\":3") && has(j,"\"scale_reentries\":0"));
        d.note(frame(10,4,1.f));
        j=d.json();assert(has(j,"\"scale_frames\":1") && has(j,"\"scale_reentries\":1"));
        CarryDiagnostics e;for(unsigned i=0;i<8;++i) e.note(frame(i,4,1.f));   // never cleared
        assert(has(e.json(),"\"scale_frames\":5"));
    }
    {   // C3: only two agreeing reads away from 1 count; a torn probe read does not
        CarryDiagnostics d;
        auto f=frame(0,4);f.rf_quat_norm=.5f;f.rf_quat_norm2=1.f;d.note(f);                     // disagree: the probe tore
        f=frame(1,4);f.rf_quat_norm=f.rf_quat_norm2=1.00005f;d.note(f);                          // within 1e-4
        auto j=d.json();assert(has(j,"\"bad_quat_reads\":0") && has(j,"\"quat_read_disagree\":1"));
        f=frame(2,4);f.rf_quat_norm=f.rf_quat_norm2=.75f;d.note(f);
        j=d.json();assert(has(j,"\"bad_quat_reads\":1") && has(j,"\"max_rf_norm_dev\":0.25"));
    }
    {   // read failures are counted, never taken for healthy values
        CarryDiagnostics d;auto f=frame(0,4);f.rf_quat_norm=CarryFrame::unread;f.rf_joint=CarryFrame::joint_unread;f.render_scale=CarryFrame::unread;f.entity_err=CarryFrame::unread;
        d.note(f);
        const auto j=d.json();assert(has(j,"\"failed_reads\":4") && has(j,"\"bad_quat_reads\":0") && has(j,"\"max_rf_norm_dev\":0"));
        auto g=frame(1,4);g.rf_quat_norm=g.rf_quat_norm2=NAN;CarryDiagnostics e;e.note(g);assert(has(e.json(),"\"failed_reads\":1"));
    }
    {   // C5: age only while applied; none counted
        CarryDiagnostics d;auto f=frame(0,4);f.residual_age_ms=300;d.note(f);f=frame(1,4);f.residual_age_ms=-1;d.note(f);
        const auto j=d.json();assert(has(j,"\"max_residual_age_ms\":300") && has(j,"\"residual_none_frames\":1"));
        CarryDiagnostics e;e.note(f);assert(has(e.json(),"\"max_residual_age_ms\":null"));
    }
    {   // C6: entity_err only counts while the hand is slow (normal drive lag at speed is not a break); master drift is across frames
        CarryDiagnostics d;auto f=frame(0,4);f.entity_err=.3f;f.hand_speed=.1f;d.note(f);
        f=frame(1,4);f.entity_err=.5f;f.hand_speed=1.2f;d.note(f);d.note(frame(2,4));
        auto j=d.json();assert(has(j,"\"max_entity_err\":0.5") && has(j,"\"max_entity_err_slow\":0.3") && has(j,"\"entity_err_frames\":1"));
        f=frame(3,4);f.entity_err=.4f;f.hand_speed=CarryFrame::unread;d.note(f);
        assert(has(d.json(),"\"entity_err_frames\":1"));   // an unknown hand speed is not a slow hand
        CarryDiagnostics m;   // the master walks 1 cm per frame, then the engine re-bases it by 0.5 m
        for(unsigned i=0;i<4;++i) {auto g=frame(i,4);g.master_pos[0]=.01f*i;m.note(g);}
        j=m.json();assert(has(j,"\"max_master_drift\":0.01"));
        auto g=frame(4,4);g.master_pos[0]=.53f;const auto r=m.note(g);
        assert(std::abs(r.master_drift-.5f)<1e-4f && has(m.json(),"\"max_master_drift\":0.5"));
        auto first=frame(5,4);first.hold_serial=99;first.master_pos[0]=50.f;assert(m.note(first).master_drift<0.f);   // a new hold has no previous frame
        auto invalid=frame(6,4);invalid.hold_serial=99;invalid.master_valid=false;assert(m.note(invalid).master_drift<0.f);
    }
    {   // C2 per hold: rebinds are lifetime counters on the frame; the row carries the hold's share
        CarryDiagnostics d;auto f=frame(0,4);f.rebinds=10;assert(d.note(f).rebinds_hold==0);
        f=frame(1,4);f.rebinds=12;assert(d.note(f).rebinds_hold==2);
        f=frame(2,4);f.rebinds=12;f.hold_serial=8;assert(d.note(f).rebinds_hold==0);   // the next carry does not inherit the total
        f=frame(3,4);f.rebinds=13;f.hold_serial=8;d.note(f);
        assert(has(d.json(),"\"rebinds_hold_max\":2"));
    }
    {   // a hitch longer than 250 ms does not end the hold; a new publication serial or entity does
        CarryDiagnostics d;d.note(frame(0,3));d.note(frame(1,4));
        auto f=frame(2,3);f.tick+=5000;d.note(f);
        assert(has(d.json(),"\"holds\":1") && has(d.json(),"\"layer_flips\":1"));
        f=frame(3,3);f.hold_serial=8;d.note(f);assert(has(d.json(),"\"holds\":2"));
        f=frame(4,3);f.entity=0x99;d.note(f);assert(has(d.json(),"\"holds\":3"));
        assert(d.note(frame(5,3)).hold_id==4);
    }
    {   // clears: only after a published hold; reasons 0..4, others fold to 0; rejected publishes and engine counters
        CarryDiagnostics d;d.clear_seen(3);assert(has(d.json(),"\"clears\":[0,0,0,0,0]"));
        d.note(frame(0,4));d.clear_seen(4);d.clear_seen(4);
        d.note(frame(1,4));d.clear_seen(9);
        d.rejected();d.rejected();
        CarryEngineCounters c;c.swaps=2;c.restores=1;c.rebind_overwrites=3;c.anchored=9;c.raw_clears={1,2,3,4,5};d.engine(c);
        const auto j=d.json();
        assert(has(j,"\"clears\":[1,0,0,0,1]") && has(j,"\"raw_clears\":[1,2,3,4,5]") && has(j,"\"swaps\":2") && has(j,"\"restores\":1"));
        assert(has(j,"\"rebind_overwrites\":3") && has(j,"\"anchored\":9") && has(j,"\"rejected_publishes\":2"));
    }
    {   // csv: 15 fields, null for unread values, no newline
        auto f=frame(3,4,1.f);f.residual_age_ms=-1;f.rf_joint=CarryFrame::joint_unread;f.entity_err=CarryFrame::unread;
        const auto row=CarryDiagnostics::csv_row(f);
        assert(fields(CarryDiagnostics::csv_header())==17 && fields(row)==17 && row.find('\n')==std::string::npos);
        assert(has(row,",null,1,1,") && row.size()>21 && row.substr(row.size()-21)==",0,null,null,null,0,0");
        f.residual_age_ms=42;assert(has(CarryDiagnostics::csv_row(f),",42,"));
        CarryFrame none;assert(has(CarryDiagnostics::csv_row(none),",null,0,0,null,null,null,null,") && fields(CarryDiagnostics::csv_row(none))==17);
        assert(CarryDiagnostics::csv_max_rows==20000);
    }
    {   // the trace writer: rows reach the file off the calling thread, stop() flushes everything, a stopped writer ignores frames
        const std::string path="carry_trace_writer_check.csv";
        CarryTraceWriter w;assert(!w.start("no_such_directory_for_carry/x.csv"));
        assert(w.start(path));
        for(unsigned i=0;i<200;++i) {auto f=frame(i,4);f.hold_id=1;w.push(f);}
        w.stop();w.push(frame(300,4));
        std::ifstream in(path);std::string line;unsigned rows=0;bool first=true;
        while(std::getline(in,line)) {if(first) {assert(line==CarryDiagnostics::csv_header());first=false;continue;}assert(fields(line)==17);++rows;}
        assert(rows==w.written() && rows+w.dropped()==200 && rows>0);
        in.close();std::remove(path.c_str());
    }
    {   // lifetime: the destructor stops and joins (a static writer must never be destroyed joinable); a failed start closes out and can be retried
        const std::string path="carry_trace_lifetime_check.csv";
        {
            CarryTraceWriter w;assert(w.start(path));
            for(unsigned i=0;i<50;++i) {auto f=frame(i,4);f.hold_id=1;w.push(f);}
        }   // no stop(): the destructor drains and closes
        std::ifstream in(path);std::string line;unsigned lines=0;while(std::getline(in,line)) ++lines;
        assert(lines>=2);in.close();
        CarryTraceWriter w;assert(!w.start("no_such_directory_for_carry/x.csv"));
        w.push(frame(0,4));w.stop();                    // inert after a failed start
        assert(w.start(path));w.stop();w.stop();        // retry works; stop is idempotent
        std::remove(path.c_str());
    }
    std::puts("carry_diagnostics_check passed");
    return 0;
}
