#include "../src/render_scale_transaction.hpp"
#include <cassert>
#include <iostream>
using d2vr::RenderScaleTransaction;
struct Cache {
    std::array<std::uint32_t,2> values{RenderScaleTransaction::bits(1),RenderScaleTransaction::bits(1)};
    int fail_axis=-1, unread_axis=-1, read_calls{}, unread_at=-1;
    bool enabled{}, enable_ok=true;
    std::optional<std::uint32_t> read(unsigned i) {
        ++read_calls;
        if(static_cast<int>(i)==unread_axis && read_calls>=unread_at) return {};
        return values[i];
    }
    bool cas(unsigned i,std::uint32_t from,std::uint32_t to) {
        if(static_cast<int>(i)==fail_axis||values[i]!=from) return false;
        values[i]=to;return true;
    }
    auto apply(RenderScaleTransaction& state,float f) {
        return state.apply(f,[&](unsigned i){return read(i);},
            [&](unsigned i,std::uint32_t a,std::uint32_t b){return cas(i,a,b);},
            [&](bool on){if(!enable_ok)return false;enabled=on;return true;});
    }
};
int main() {
    const auto full=RenderScaleTransaction::bits(1),half=RenderScaleTransaction::bits(.5f);
    {
        Cache c;RenderScaleTransaction s;
        auto r=c.apply(s,.5f);assert(r.committed&&s.active&&c.enabled&&c.values[0]==half&&c.values[1]==half);
        const int previous_reads=c.read_calls;
        r=c.apply(s,.5f);assert(r.committed&&c.read_calls>=previous_reads+6);
        c.values={full,full};r=c.apply(s,.5f);assert(r.committed&&r.baseline_reset&&c.values[0]==half);
        r=c.apply(s,1);assert(r.committed&&!s.active&&!c.enabled&&c.values[0]==full&&c.values[1]==full);
    }
    {
        Cache c;RenderScaleTransaction s;c.fail_axis=1;
        auto r=c.apply(s,.5f);assert(!r.committed&&r.failed&&!r.rollback_failed&&c.values[0]==full&&c.values[1]==full&&!s.active);
    }
    {
        Cache c;RenderScaleTransaction s;c.unread_axis=1;c.unread_at=4;
        auto r=c.apply(s,.5f);assert(!r.committed&&r.failed&&c.values[0]==full&&c.values[1]==full);
    }
    {
        Cache c;RenderScaleTransaction s;assert(c.apply(s,.5f).committed);
        const auto foreign=RenderScaleTransaction::bits(.7f);c.values[0]=foreign;
        // A game-written cache becomes the new baseline: the scale still applies and full scale restores it.
        auto r=c.apply(s,.6f);assert(r.committed&&r.foreign&&c.values[0]==RenderScaleTransaction::bits(.6f)&&c.values[1]==RenderScaleTransaction::bits(.6f));
        r=c.apply(s,1);assert(r.committed&&c.values[0]==foreign&&c.values[1]==full);
    }
    {
        Cache c;RenderScaleTransaction s;c.enable_ok=false;
        auto r=c.apply(s,.5f);assert(!r.committed&&r.enable_failed&&c.values[0]==full&&c.values[1]==full);
    }
    {
        Cache c;RenderScaleTransaction s;int writes{};
        auto r=s.apply(.5f,[&](unsigned i)->std::optional<std::uint32_t>{return c.values[i];},
            [&](unsigned i,std::uint32_t a,std::uint32_t b){
                ++writes;
                if(writes==2) {c.values[0]=RenderScaleTransaction::bits(.8f);return false;}
                return c.cas(i,a,b);
            },[&](bool){return true;});
        assert(!r.committed&&r.rollback_failed&&c.values[0]==RenderScaleTransaction::bits(.8f)&&s.active);
    }
    {
        Cache c;RenderScaleTransaction s;int enable_calls{};
        auto r=s.apply(.5f,[&](unsigned i){return c.read(i);},
            [&](unsigned i,std::uint32_t a,std::uint32_t b){return c.cas(i,a,b);},
            [&](bool on){++enable_calls;c.enabled=on;if(enable_calls==1)c.values[1]=full;return true;});
        assert(!r.committed&&r.failed&&enable_calls==2&&!c.enabled&&c.values[0]==full&&c.values[1]==full);
    }
    {
        Cache c;RenderScaleTransaction s;
        assert(!c.apply(s,.49f).committed&&!c.apply(s,1.01f).committed);
    }
    {
        Cache c;RenderScaleTransaction s;
        const auto original_x=RenderScaleTransaction::bits(.95f);
        c.values[0]=original_x;assert(c.apply(s,.5f).committed);
        assert(c.apply(s,1).committed&&c.values[0]==original_x&&c.values[1]==full);
    }
    {
        // Actual headset lifecycle: independently own default zero -> one,
        // then Native/Quality/Balanced transitions save and restore that one.
        Cache c;c.values={0,0};RenderScaleTransaction s;
        std::array<bool,2> headset_owned{};
        const auto read=[&](unsigned i){return c.read(i);};
        const auto cas=[&](unsigned i,std::uint32_t a,std::uint32_t b){return c.cas(i,a,b);};
        for(unsigned i=0;i<2;++i) {assert(c.cas(i,0,full));headset_owned[i]=true;}
        assert(c.apply(s,1).committed);
        assert(c.apply(s,.77f).committed&&s.axes[0].saved==full&&s.axes[1].saved==full);
        assert(c.apply(s,.67f).committed);
        // Native zero reset is rebound only under the independently held
        // startup ownership; transaction still remembers baseline one.
        c.values[0]=0;
        for(unsigned i=0;i<2;++i) assert(d2vr::rebind_render_scale_baseline(i,0,full,headset_owned[i],read,cas));
        const auto reset=c.apply(s,.67f);assert(reset.committed&&reset.baseline_reset);
        assert(c.apply(s,1).committed&&c.values[0]==full&&c.values[1]==full);
        assert(c.apply(s,.77f).committed);
        assert(c.apply(s,1).committed);
        c.values[0]=0; // Already-original final cleanup is success without a write.
        for(unsigned i=0;i<2;++i) assert(d2vr::restore_render_scale_baseline(i,0,full,headset_owned[i],read,cas));
        assert(c.values[0]==0&&c.values[1]==0);
        // Restart sees native default zero, rather than an old owned fraction.
        s={};for(unsigned i=0;i<2;++i) assert(c.cas(i,0,full));
        assert(c.apply(s,.67f).committed&&s.axes[0].saved==full);
        // Cvar refusal rolls a mode switch back to the previous fraction.
        c.enable_ok=false;assert(!c.apply(s,.77f).committed&&c.values[0]==RenderScaleTransaction::bits(.67f));
        c.enable_ok=true;assert(c.apply(s,1).committed);
        for(unsigned i=0;i<2;++i) assert(d2vr::restore_render_scale_baseline(i,0,full,true,read,cas));
    }
    {
        Cache c;c.values={RenderScaleTransaction::bits(.8f),0};
        const auto read=[&](unsigned i){return c.read(i);};
        const auto cas=[&](unsigned i,std::uint32_t a,std::uint32_t b){return c.cas(i,a,b);};
        assert(d2vr::rebind_render_scale_baseline(0,0,full,true,read,cas));
        assert(c.values[0]==RenderScaleTransaction::bits(.8f)); // Foreign is untouched; transaction rejects it.
        assert(!d2vr::restore_render_scale_baseline(0,0,full,true,read,cas));
        assert(d2vr::rebind_render_scale_baseline(1,0,full,false,read,cas)&&c.values[1]==0);
        c.fail_axis=1;assert(!d2vr::rebind_render_scale_baseline(1,0,full,true,read,cas)&&c.values[1]==0);
    }
    {
        Cache c;RenderScaleTransaction s;assert(c.apply(s,.77f).committed);
        const auto read=[&](unsigned i){return c.read(i);};
        const auto cas=[&](unsigned i,std::uint32_t a,std::uint32_t b){return c.cas(i,a,b);};
        c.values[0]=0;c.enable_ok=false; // Native reset plus already-retired cvar owner during final cleanup.
        const auto transaction_restore=s.apply(1,read,cas,[](bool){return true;});
        assert(!transaction_restore.committed&&transaction_restore.foreign&&c.values[0]==0&&c.values[1]==full);
        for(unsigned i=0;i<2;++i) assert(d2vr::restore_render_scale_baseline(i,0,full,true,read,cas));
        assert(c.values[0]==0&&c.values[1]==0); // Startup layer proves complete cleanup despite transaction's zero rejection.
    }
    std::cout<<"render scale transaction checks passed\n";
}
