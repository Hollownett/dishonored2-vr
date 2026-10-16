#include "../src/selection_policy.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
NativeUseHit hit(float x,unsigned id,float fraction=0) {
    NativeUseHit h{};std::memcpy(h.data(),&x,4);std::memcpy(h.data()+0x48,&id,4);std::memcpy(h.data()+0x20,&fraction,4);
    for(unsigned i=0x50;i<112;++i)h[i]=std::byte{static_cast<unsigned char>(id)};return h;
}
std::uintptr_t owner(const NativeUseHit& h) {unsigned id{};std::memcpy(&id,h.data()+0x48,4);return id;}
int main() {
    assert(native_use_query_outcome(0x123456789abcde01ull,123,2,2)==NativeUseQueryOutcome::multiple);
    assert(native_use_query_outcome(0x123456789abcde00ull,0,-1,0)==NativeUseQueryOutcome::incomplete);
    assert(native_use_query_outcome(0x123456789abcde01ull,0,1,1)==NativeUseQueryOutcome::rejected);
    assert(native_use_query_outcome(0,0,-1,0)==NativeUseQueryOutcome::incomplete);
    assert(native_use_query_outcome(1,0,0,0)==NativeUseQueryOutcome::empty);
    assert(native_use_query_outcome(1,123,1,1)==NativeUseQueryOutcome::single);
    assert(native_use_query_outcome(1,123,2,2)==NativeUseQueryOutcome::multiple);
    assert(native_use_query_outcome(1,0,1,1)==NativeUseQueryOutcome::rejected);
    assert(native_use_query_outcome(1,123,-1,2)==NativeUseQueryOutcome::rejected);
    assert(native_use_query_outcome(1,123,65,65)==NativeUseQueryOutcome::rejected);
    assert(native_use_query_outcome(1,123,2,1)==NativeUseQueryOutcome::rejected);
    assert(native_use_query_outcome(2,123,2,2)==NativeUseQueryOutcome::rejected);
    const auto safe=hit(.40f,1),loot=hit(.10f,2),door=hit(.30f,3);
    std::array<NativeUseHit,3> records{safe,door,loot};
    assert(order_native_use_hits(records.data(),3,{0,0,0},0,owner));
    assert(records[0]==loot && records[1]==door && records[2]==safe); // complete records unchanged
    records={hit(.11f,1),hit(.10f,2),hit(.3f,3)};
    assert(order_native_use_hits(records.data(),3,{0,0,0},1,owner) && owner(records[0])==1);
    records={hit(.14f,1),hit(.10f,2),hit(.3f,3)};
    assert(order_native_use_hits(records.data(),3,{0,0,0},1,owner) && owner(records[0])==2);
    // Same native geometry fractions still choose nearest actual hand contact.
    records={hit(.4f,1,-.9f),hit(.1f,2,0),hit(.3f,3,-.5f)};
    assert(order_native_use_hits(records.data(),3,{.5,0,0},0,owner) && owner(records[0])==1);
    records={hit(.2f,1),hit(.2f,2),hit(.2f,3)};const auto ties=records;
    assert(order_native_use_hits(records.data(),3,{0,0,0},0,owner) && records==ties);
    {   // Palm-wide beam: the item the hand points at beats one merely nearer the palm but off the beam.
        const auto at=[](float x,float y,unsigned id){auto h=hit(x,id);std::memcpy(h.data()+4,&y,4);return h;};
        records={at(.05f,.12f,1),at(.40f,.01f,2),at(.20f,.30f,3)};
        assert(order_native_use_hits(records.data(),3,{0,0,0},0,owner,{2,0,0}) && owner(records[0])==2 && owner(records[1])==1);
        records={at(.40f,.0f,1),at(.10f,.02f,2),at(.20f,.30f,3)}; // both on the beam: the nearer along it wins
        assert(order_native_use_hits(records.data(),3,{0,0,0},0,owner,{1,0,0}) && owner(records[0])==2);
        // Loot within a palm width beats the usable surface under the palm (coins on a cash machine), not a clearly nearer one.
        const auto coin=[](std::uintptr_t id){return id==2;};
        records={at(.10f,.0f,1),at(.15f,.02f,2),at(.20f,.30f,3)};
        assert(order_native_use_hits(records.data(),3,{0,0,0},0,owner,{1,0,0},coin) && owner(records[0])==2);
        records={at(.05f,.0f,1),at(.60f,.05f,2),at(.20f,.30f,3)};
        assert(order_native_use_hits(records.data(),3,{0,0,0},0,owner,{1,0,0},coin) && owner(records[0])==1);
    }
    records={hit(.2f,1),hit(NAN,2),hit(.1f,3)};const auto invalid=records;
    assert(!order_native_use_hits(records.data(),3,{0,0,0},0,owner) && records==invalid);
    assert(!order_native_use_hits(records.data(),65,{0,0,0},0,owner));
    assert(continue_native_use_scan(1,0,true,false));
    assert(!continue_native_use_scan(0,0,true,false)); // Clear LOS unchanged.
    assert(!continue_native_use_scan(1,1,true,false)); // Native already continues.
    assert(!continue_native_use_scan(1,0,false,false)); // Unowned scan unchanged.
    assert(!continue_native_use_scan(1,0,true,true)); // NPC blocked metadata unchanged.
    assert(!continue_native_use_scan(1,0,true,{})); // Unknown type unchanged.
    // Match verified native stage0's break/accept branches. A hand-near
    // blocked loot must remain rejected while a farther visible safe/front
    // item gets its own native permission and LOS evaluation.
    struct Candidate {unsigned id;bool eligible;std::uintptr_t blocked;};
    const std::array<Candidate,3> closed{{{2,true,1},{3,false,0},{1,true,0}}};
    const auto native_scan=[](const auto& candidates,bool ranked) {
        unsigned checks=0,selected=0;
        for(const auto& c:candidates) {
            if(!c.eligible)continue;++checks;unsigned char flag=0;
            const auto native_result=c.blocked;
            if(continue_native_use_scan(native_result,flag,ranked,false))flag=1;
            if(!(native_result&0xff)){selected=c.id;break;}
            if(!flag)break;
        }
        return std::array<unsigned,2>{selected,checks};
    };
    assert((native_scan(closed,false)==std::array<unsigned,2>{0,1}));
    assert((native_scan(closed,true)==std::array<unsigned,2>{1,2}));
    const std::array<Candidate,2> open{{{2,true,0},{1,true,0}}};
    assert((native_scan(open,true)==std::array<unsigned,2>{2,1}));
    const std::array<Candidate,2> all_blocked{{{2,true,1},{1,true,1}}};
    assert((native_scan(all_blocked,true)==std::array<unsigned,2>{0,2}));
    std::cout<<"PASS: native hit proximity replaces overlap fraction, bounded preference, stable ties, record integrity and invalid-buffer rejection\n";
}
