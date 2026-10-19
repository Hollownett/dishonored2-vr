// Offline pin of the carry diagnostics counters (C7, G6): logic only, no engine.
#include "carry_diagnostics.hpp"
#include <cassert>
#include <cstdio>
#include <string>

using namespace d2vr;
static CarryFrame frame(unsigned i,std::uint32_t layer,float scale=0.f) {
    CarryFrame f;f.tick=1000+16ull*i;f.entity=0x1234;f.layer=layer;f.render_scale=scale;f.rf_quat_norm=1.f;return f;
}
static bool has(const std::string& s,const char* t) {return s.find(t)!=std::string::npos;}
int main() {
    {   // 10 frames of a hold; a clear (reason 2) at frame 4; the layer goes 4 -> 3 from frame 5.
        CarryDiagnostics d;
        for(unsigned i=0;i<10;++i) {
            d.note(frame(i,i<5?4u:3u));
            if(i==4) d.clear_seen(2);
        }
        const auto j=d.json();
        assert(has(j,"\"frames\":10"));assert(has(j,"\"rigid_frames\":5"));assert(has(j,"\"clears\":[0,0,1,0]"));
        assert(has(j,"\"scale_frames\":0"));assert(has(j,"\"max_residual_age_ms\":null"));
    }
    {   // the view-model scale is expected on the first two frames of a hold only
        CarryDiagnostics d;
        for(unsigned i=0;i<2;++i) d.note(frame(i,4,1.f));
        for(unsigned i=2;i<10;++i) d.note(frame(i,4,i==7?1.f:0.f));
        assert(has(d.json(),"\"scale_frames\":1"));
        CarryDiagnostics e;
        for(unsigned i=0;i<2;++i) e.note(frame(i,4,1.f));
        for(unsigned i=2;i<10;++i) e.note(frame(i,4));
        assert(has(e.json(),"\"scale_frames\":0"));
    }
    {   // a new entity or a tick gap restarts the grace window
        CarryDiagnostics d;d.note(frame(0,4));d.note(frame(1,4));d.note(frame(2,4));
        auto f=frame(3,4,1.f);f.tick+=5000;d.note(f);
        assert(has(d.json(),"\"scale_frames\":0"));
    }
    {   // a clear that no hold preceded is not counted; swaps and restores are reported as given
        CarryDiagnostics d;d.clear_seen(3);d.clear_seen(3);
        assert(has(d.json(),"\"clears\":[0,0,0,0]"));
        d.note(frame(0,4));d.clear_seen(3);d.clear_seen(3);d.layers(2,1);
        const auto j=d.json();
        assert(has(j,"\"clears\":[0,0,0,1]"));assert(has(j,"\"swaps\":2"));assert(has(j,"\"restores\":1"));
        d.clear_seen(9);d.note(frame(1,4));d.clear_seen(9);   // out-of-range reasons are unattributed
        assert(has(d.json(),"\"clears\":[1,0,0,1]"));
    }
    {   // residual age, quaternion deviation, clamp hits, joint
        CarryDiagnostics d;
        auto f=frame(0,4);f.residual_age_ms=120;f.anchor_quat_dev=.002f;f.rf_quat_norm=.75f;f.clamp_hit_held=true;d.note(f);
        f=frame(1,4);f.residual_age_ms=-1;f.anchor_quat_dev=.0005f;f.clamp_hit_held=true;d.note(f);
        for(unsigned i=2;i<5;++i) {f=frame(i,4);f.rf_joint=i==4?7:-1;d.note(f);}
        const auto j=d.json();
        assert(has(j,"\"max_residual_age_ms\":120"));assert(has(j,"\"clamp_hits_held\":2"));
        assert(has(j,"\"max_quat_dev\":0.002"));assert(has(j,"\"max_rf_norm_dev\":0.25"));
        assert(has(j,"\"joint_off_frames\":1"));
    }
    {   // csv: header and row have 10 fields; -1 residual age prints null; a trace cap
        const auto fields=[](const std::string& s) {size_t n=1;for(const char c:s) n+=c==',';return n;};
        auto f=frame(3,4,1.f);f.clear_reason=2;
        assert(fields(CarryDiagnostics::csv_header())==10);
        assert(fields(CarryDiagnostics::csv_row(f))==10);
        assert(has(CarryDiagnostics::csv_row(f),",null,"));
        f.residual_age_ms=42;assert(has(CarryDiagnostics::csv_row(f),",42,"));
        assert(CarryDiagnostics::csv_row(f).find('\n')==std::string::npos);
        assert(CarryDiagnostics::csv_max_rows==20000);
    }
    std::puts("carry_diagnostics_check passed");
    return 0;
}
