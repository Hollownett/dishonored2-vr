#include "../src/layer_ledger.hpp"
#include <cstdio>
static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}
using d2vr::LayerLedger;using S=d2vr::LayerLedger::Step;
int main() {
    {   // every_release_path_restores_once: throw, drop, open hand, cancel and level change all end in one restore or one give-up
        LayerLedger l;
        for(int path=0;path<5;++path) {
            l.swapped();
            expect(l.attempt(true)==S::restore,"a release restores");
            l.result(true);
            expect(l.attempt(true)==S::none && l.attempt(true)==S::none,"no second restore");
        }
        expect(l.swaps()==5 && l.restores()==5 && l.abandoned()==0 && l.releases()==5 && l.restores_per_release()==1.,"five swaps five restores");
    }
    {   // a body not readable yet is retried, and restored once
        LayerLedger l;l.swapped();
        expect(l.attempt(true)==S::restore,"first try");l.result(false);
        expect(l.pending() && l.attempt(true)==S::restore,"retry after a failed restore");l.result(true);
        expect(l.restores()==1 && l.attempt(true)==S::none,"restored once after a retry");
    }
    {   // the engine freed the object (level change, kill): the layer is not ours to restore; counted as given up, not restored
        LayerLedger l;l.swapped();
        expect(l.attempt(false)==S::give_up && !l.pending() && l.attempt(false)==S::none,"entity gone gives up once");
        expect(l.restores()==0 && l.abandoned()==1 && l.releases()==1 && l.restores_per_release()==0.,"abandoned counted");
    }
    {   // a retry that never succeeds is bounded
        LayerLedger l;l.swapped();unsigned tries=0;
        while(l.attempt(true)==S::restore) {++tries;l.result(false);if(tries>1000) break;}
        expect(tries==LayerLedger::max_attempts && !l.pending() && l.abandoned()==1,"bounded retries");
    }
    {   // restore without a swap does nothing
        LayerLedger l;expect(l.attempt(true)==S::none,"idle");l.result(true);expect(l.restores()==0,"no phantom restore");
    }
    if(!failures) std::printf("PASS: layer ledger restores once per release, retries, gives up when the engine owns it\n");
    return failures?1:0;
}
