#include "../src/layer_ledger.hpp"
#include <cstdio>
#include <vector>
static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}
using d2vr::LayerLedger;using d2vr::LayerEntry;using P=LayerLedger::Probe;using W=LayerLedger::Write;

// A fake engine: one live object per entity address with an id, a handle and a body identity; writes are recorded.
struct World {
    struct Obj {unsigned id{},handle{};int generation{};bool readable{true};};
    Obj obj[8]{};unsigned writes{};unsigned last_write_entity{};bool write_ok{true};unsigned fail_writes{};
    P probe(const LayerEntry& e) const {
        const Obj& o=obj[e.entity];
        if(!o.readable) return P::unreadable;
        return o.id==e.entity_id && o.handle==e.handle && o.generation==e.generation?P::match:P::mismatch;
    }
    W write(const LayerEntry& e) {
        if(probe(e)!=P::match) return W::mismatch;   // the final re-read
        if(fail_writes) {--fail_writes;return W::failed;}
        ++writes;last_write_entity=unsigned(e.entity);return W::ok;
    }
};
static LayerEntry entry(World& w,unsigned entity,unsigned id,unsigned handle,int gen) {
    w.obj[entity]={id,handle,gen,true};
    LayerEntry e;e.entity=entity;e.entity_id=id;e.handle=handle;e.generation=gen;e.filter=3;return e;
}
static int pass(LayerLedger& l,World& w,std::vector<bool>* done=nullptr) {
    int n=0;
    l.service([&](const LayerEntry& e){return w.probe(e);},[&](const LayerEntry& e){return w.write(e);},
              [&](const LayerEntry&,bool r){++n;if(done) done->push_back(r);});
    return n;
}
int main() {
    {   // every_release_path_restores_once: throw, drop, open hand, cancel and level change all end in one restore or one give-up
        LayerLedger l;World w;
        for(unsigned path=0;path<4;++path) {
            auto e=entry(w,1,10+path,100+path,1);l.swapped(e);
            expect(pass(l,w)==0,"a held object is not restored");
            l.released(1);expect(pass(l,w)==1,"a release restores");
            expect(pass(l,w)==0 && pass(l,w)==0,"no second restore");
        }
        auto e=entry(w,2,20,200,1);l.swapped(e);l.released(2);l.clear_all();   // level change before the restore ran
        expect(pass(l,w)==0 && w.writes==4,"level change restores nothing");
        expect(l.swaps()==5 && l.restores()==4 && l.abandoned()==1 && l.releases()==5 && l.restores_per_release()==0.8,"counts");
    }
    {   // recycled_handle_never_written: the address, the id, or the body's handle/identity changed after the swap
        for(int kind=0;kind<3;++kind) {
            LayerLedger l;World w;auto e=entry(w,1,10,100,1);l.swapped(e);l.released(1);
            if(kind==0) w.obj[1].id=11;else if(kind==1) w.obj[1].handle=101;else w.obj[1].generation=2;
            std::vector<bool> done;
            expect(pass(l,w,&done)==1 && done.size()==1 && !done[0],"a recycled object is given up");
            expect(w.writes==0 && l.restores()==0 && l.abandoned()==1,"never written");
        }
        {   // it changes between the first (failed) attempt and the retry
            LayerLedger l;World w;auto e=entry(w,1,10,100,1);l.swapped(e);l.released(1);w.fail_writes=1;
            expect(pass(l,w)==0 && l.pending()==1,"failed write stays pending");
            w.obj[1].handle=555;expect(pass(l,w)==1 && w.writes==0 && l.abandoned()==1,"recycled between retries: not written");
        }
    }
    {   // epoch_change_clears_pending_without_write
        LayerLedger l;World w;for(unsigned i=1;i<=3;++i) {auto e=entry(w,i,10+i,100+i,1);l.swapped(e);l.released(i);}
        w.fail_writes=100;pass(l,w);l.clear_all();
        expect(l.pending()==0 && pass(l,w)==0 && w.writes==0 && l.abandoned()==3 && l.restores()==0,"epoch change drops all pending, no write");
    }
    {   // unreadable_frame_counts_one_attempt: unreadable does not end the retries; the budget is spent exactly
        LayerLedger l;World w;auto e=entry(w,1,10,100,1);l.swapped(e);l.released(1);
        w.obj[1].readable=false;
        for(unsigned i=0;i<LayerLedger::max_attempts-1;++i) expect(pass(l,w)==0,"still pending while unreadable");
        w.obj[1].readable=true;expect(pass(l,w)==1 && w.writes==1 && l.restores()==1,"restored on the last attempt of the budget");
        LayerLedger m;World v;auto f=entry(v,1,10,100,1);m.swapped(f);m.released(1);v.obj[1].readable=false;
        unsigned n=0;while(m.pending() && n<1000) {pass(m,v);++n;}
        expect(n==LayerLedger::max_attempts && m.abandoned()==1,"gives up after exactly the budget");
    }
    {   // new_grab_keeps_old_restore_pending
        LayerLedger l;World w;auto a=entry(w,1,10,100,1);l.swapped(a);l.released(1);w.fail_writes=3;
        auto b=entry(w,2,20,200,1);l.swapped(b);   // the new grab
        expect(pass(l,w)==0 && l.pending()==2 && l.abandoned()==0,"old restore still pending beside the new swap");
        l.released(2);pass(l,w);pass(l,w);
        expect(l.restores()==2 && w.writes==2 && l.abandoned()==0,"both restored");
        // a full list abandons the oldest, counted
        LayerLedger f;World x;for(unsigned i=1;i<=5;++i) {auto g=entry(x,i,10+i,100+i,1);f.swapped(g);}
        expect(f.pending()==4 && f.abandoned()==1,"full list abandons the oldest and counts it");
    }
    {   // transient_restore_does_not_start_watch: the done callback reports the restore; only the caller's real-release flag starts a watch
        LayerLedger l;World w;auto a=entry(w,1,10,100,1);l.swapped(a);l.released(1);
        unsigned entity_real_release=2;bool started=false;
        l.service([&](const LayerEntry& e){return w.probe(e);},[&](const LayerEntry& e){return w.write(e);},
                  [&](const LayerEntry& e,bool restored){if(restored && entity_real_release==e.entity) started=true;});
        expect(l.restores()==1 && !started,"a transient restore starts no watch");
        auto b=entry(w,2,20,200,1);l.swapped(b);l.released(2);
        l.service([&](const LayerEntry& e){return w.probe(e);},[&](const LayerEntry& e){return w.write(e);},
                  [&](const LayerEntry& e,bool restored){if(restored && entity_real_release==e.entity) started=true;});
        expect(started,"a real release starts it");
        LayerLedger g;auto c=entry(w,3,30,300,1);g.swapped(c);g.released(3);w.obj[3].id=99;started=false;entity_real_release=3;
        g.service([&](const LayerEntry& e){return w.probe(e);},[&](const LayerEntry& e){return w.write(e);},
                  [&](const LayerEntry& e,bool restored){if(restored && entity_real_release==e.entity) started=true;});
        expect(!started,"a give-up starts no watch");
    }
    if(!failures) std::printf("PASS: layer ledger: one restore per release, never writes a recycled object, bounded retries, pending list\n");
    return failures?1:0;
}
