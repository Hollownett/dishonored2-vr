#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_hud_child_draw.hpp"
#include "../src/native_prompt_anchor.hpp"
#include "../src/selection_policy.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <map>
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_OK;}
namespace {
alignas(8) std::array<std::byte,0x1668> fixture_file{};
alignas(8) std::array<std::byte,0xe0> fixture_node{};
alignas(8) std::array<std::byte,0x100> fixture_iggy{};
const char fixture_path[]="ui/hud.iggy";
unsigned originals{},before{},after{},begins{},ends{};
float eye_dx{},eye_dy{};float* fixture_matrix{};bool changed{},capture_enabled{},eye_owner_ok=true;float expect_x=-1.f,glyph_factor=-1.f;
d2vr::NativeHudChildKind last_kind{};UINT64 last_epoch{},last_tick{};std::uintptr_t last_owner{};bool last_talk_offer{},last_talk_glyph{};
unsigned read_count{},eye_live_calls{};
std::map<std::uintptr_t,unsigned char> retained_memory;
d2vr::NativeHudChildScope last_scope{};
template<class T> void put(std::uintptr_t at,const T& value) {
    const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t i=0;i<sizeof(value);++i)retained_memory[at+i]=bytes[i];
}
bool read(std::uintptr_t at,void* out,std::size_t size){++read_count;
    if(retained_memory.count(at)) {
        auto* bytes=static_cast<unsigned char*>(out);
        for(std::size_t i=0;i<size;++i){const auto it=retained_memory.find(at+i);if(it==retained_memory.end())return false;bytes[i]=it->second;}
        return true;
    }
    SIZE_T n{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&n) && n==size;
}
std::uint32_t __fastcall original(void* iggy,float* matrix,void* node,void* aux,void* p5,void* p6,std::uint32_t clip,float depth){
    ++originals;assert(iggy==fixture_iggy.data() && node==fixture_node.data() && aux==reinterpret_cast<void*>(1) && p5==reinterpret_cast<void*>(2) && p6==reinterpret_cast<void*>(3) && clip==123 && depth==4.5f);
    if(glyph_factor>=0.f) {
        assert(matrix!=fixture_matrix && std::fabs(matrix[4]-(fixture_matrix[4]+eye_dx))<1e-4f && std::fabs(matrix[5]-(fixture_matrix[5]+eye_dy))<1e-4f);
        for(unsigned i=0;i<4;++i)assert(std::fabs(matrix[i]-fixture_matrix[i]*glyph_factor)<1e-6f);
    }
    else if(changed)assert(matrix!=fixture_matrix && matrix[4]==103);
    else if(expect_x>=0)assert(expect_x==100.f?matrix==fixture_matrix:(matrix!=fixture_matrix && std::fabs(matrix[4]-expect_x)<1e-3f));
    else assert(matrix==fixture_matrix);
    return 456;
}
void observe(const d2vr::NativeHudChildScope& scope,bool entering,std::uint32_t result)noexcept {assert(scope.node==reinterpret_cast<std::uintptr_t>(fixture_node.data()) && scope.matrix_valid && scope.bounds_valid);if(entering){++before;assert(!result);}else{++after;assert(result==456);}}
bool begin(const d2vr::NativeHudChildScope&)noexcept{++begins;return capture_enabled;}
void end(const d2vr::NativeHudChildScope&)noexcept{++ends;}
bool transform(const d2vr::NativeHudChildScope&,std::array<float,6>& matrix)noexcept{matrix[4]+=3;return true;}
alignas(8) std::array<std::byte,0xe0> ancestor_node{};
unsigned nested_glyphs{};
float nested_scale{};
std::uint32_t __fastcall nested_original(void* iggy,float* matrix,void* node,void* aux,void* p5,void* p6,std::uint32_t clip,float depth) {
    assert(aux==reinterpret_cast<void*>(1) && p5==reinterpret_cast<void*>(2) && p6==reinterpret_cast<void*>(3) && clip==123 && depth==4.5f);
    const auto root=reinterpret_cast<std::uintptr_t>(fixture_iggy.data())+0x26b0;
    if(reinterpret_cast<std::uintptr_t>(node)==root)
        return d2vr::NativeHudChildDraw::invoke_fixture(iggy,matrix,ancestor_node.data(),aux,p5,p6,clip,depth);
    if(node==ancestor_node.data()) {
        // Native drawing composes this child's local transform with the incoming parent.
        std::array<float,6> child{matrix[0],matrix[1],matrix[2],matrix[3],
            matrix[4]+matrix[0]*50.f+matrix[2]*60.f,matrix[5]+matrix[1]*50.f+matrix[3]*60.f};
        return d2vr::NativeHudChildDraw::invoke_fixture(iggy,child.data(),fixture_node.data(),aux,p5,p6,clip,depth);
    }
    assert(node==fixture_node.data());++nested_glyphs;
    assert(std::fabs(matrix[0]-.65f*nested_scale)<1e-5f && std::fabs(matrix[3]-.65f*nested_scale)<1e-5f);
    assert(std::fabs(matrix[4]-150.f)<1e-3f && std::fabs(matrix[5]-260.f)<1e-3f);
    return 456;
}

}
int main(){
    using namespace d2vr;const auto iggy=reinterpret_cast<std::uintptr_t>(fixture_iggy.data()),node=reinterpret_cast<std::uintptr_t>(fixture_node.data()),file=reinterpret_cast<std::uintptr_t>(fixture_file.data()),path=reinterpret_cast<std::uintptr_t>(fixture_path),root=iggy+0x26b0;
    NativeHudChildScope identity{};identity.kind=NativeHudChildKind::prompt;identity.file=file;identity.iggy=iggy;identity.node=node;identity.owner=99;identity.epoch=1;identity.tick=1000;
    identity.target={0x500000,7,0,1,4};
    assert(native_hud_child_replacement_ready(identity,identity,1000,1001));
    auto replaced=identity;replaced.node+=8;assert(!native_hud_child_replacement_ready(identity,replaced,1000,1001));
    replaced=identity;replaced.owner++;assert(!native_hud_child_replacement_ready(identity,replaced,1000,1001));
    replaced=identity;replaced.epoch++;assert(!native_hud_child_replacement_ready(identity,replaced,1000,1001));
    replaced=identity;replaced.target.object+=8;assert(!native_hud_child_same_identity(identity,replaced));
    replaced=identity;replaced.target.generation++;assert(!native_hud_child_same_identity(identity,replaced));
    assert(!native_hud_child_replacement_ready(identity,identity,749,1000));
    assert(!native_hud_child_replacement_ready(identity,identity,1001,1000));
    NativeHudPromptHold hold;assert(!hold.holds(1000));hold.good(1000,Vec3{},.24);
    assert(hold.holds(1600) && !hold.holds(1601) && !hold.holds(999));hold.reset();assert(!hold.holds(1000));
    hold.good(1000,Vec3{},.3,300);assert(hold.width_for(100)>.0999 && hold.width_for(100)<.1001 && hold.width_for(0)==.3); // v692: the scale is held, not the width
    { // v1220 native_prompt_keep_grace: the takedown slot and a slot-0 frame without a same-object offer keep the v1146 rule exactly
        const auto old_rule=[](bool live,bool same,bool current,bool bound,bool offer,bool take)->UINT64{
            const bool same_live=live && same && current;const bool gap=take && offer && !same_live && (!live || same || !current);
            return gap || (live && same && (same_live || bound))?(gap?1300:600):0;};
        for(unsigned m=0;m<64;++m) {const bool l=m&1,s=m&2,c=m&4,b=m&8,o=m&16,t=m&32;
            if(t || !o) assert(native_prompt_keep_grace(l,s,c,b,o,t)==old_rule(l,s,c,b,o,t));}
        // Replay of the v1216 headset log (native-prompt-blinks.jsonl, a body prompt): every ~750 ms the retained HUD entry is withdrawn and the
        // same window rebinds to the hand target; 3-5 frames (15-78 ms) pass before the new binding's art lands. Frame = 11 ms (90 Hz).
        unsigned old_hides{},new_hides{};
        for(UINT64 t=0;t<10000;t+=11) {const UINT64 phase=t%750;
            if(phase>=50) continue;   // good frames: nothing fails
            const bool snapshot_old=phase<30;   // the capture still holds the withdrawn binding's image, then the new binding is captured
            const bool live=true,same=true,current=!snapshot_old,bound=false,offer=true;   // the hand target still names the body
            old_hides+=old_rule(live,same,current,bound,offer,false)==0;new_hides+=native_prompt_keep_grace(live,same,current,bound,offer,false)==0;}
        assert(old_hides>=30 && new_hides==0);
        assert(native_prompt_keep_grace(true,false,true,false,true,false)==0); // a fresh capture of ANOTHER element replaces it at once
        assert(native_prompt_keep_grace(true,true,false,false,false,false)==0); // no target any more: the prompt goes as before
        assert(native_prompt_keep_grace(false,false,false,false,true,false)==250 && native_prompt_keep_grace(false,false,false,false,true,true)==1300);
        // v1225, sim v1222 rows: 187292500 hide retained owner ...266112 (crop 459, hand target present), 187292562 show hand-route owner ...254976
        // (crop 372): the same window rebound to ANOTHER object; its fresh binding is current, its art lands 62 ms later. v1222 cleared (blank 62 ms).
        {const bool live=true,same=false,current=true,bound=false,offer=false;   // retained owner != hand target owner: not the same object
         assert(old_rule(live,same,current,bound,offer,false)==0);
         const UINT64 g=native_prompt_keep_grace(live,same,current,bound,offer,false,true);assert(g==100 && 62<=g);
         assert(native_prompt_keep_grace(live,same,current,bound,offer,true,true)==0);     // takedown slot: another hint replaces at once, as before
         assert(native_prompt_keep_grace(live,same,current,bound,offer,false,false)==0);   // another window (node): replaced at once, as before
         assert(native_prompt_keep_grace(live,same,false,bound,offer,false,true)==0);}     // the new binding not current: nothing to hand over to
        // v1228 NativeBodyPromptStats on the logged v1209 interval at 179860156 (4694 ms on one body, first shown at 78 ms, three
        // retained->hand re-bind gaps of 15-63 ms): v1216 = one flicker interval; with the gap hold (no gaps) = clean; frames 11 ms.
        const auto replay=[](bool gaps){NativeBodyPromptStats s;const std::uintptr_t body=0x7181600;
            for(UINT64 t=0;t<=4694;t+=11) {const bool gap=gaps && ((t>=1000 && t<1063) || (t>=2500 && t<2515) || (t>=3700 && t<3747));
                s.update(1000000+t,body,t>=78 && !gap);}
            for(UINT64 t=4705;t<5400;t+=11) s.update(1000000+t,0,t<4900);   // hand leaves: the prompt fades once (no flicker)
            return s;};
        {const auto s=replay(true);assert(s.intervals==1 && s.flicker==1 && s.hides_total==3 && !s.never && !s.late);}
        {const auto s=replay(false);assert(s.intervals==1 && s.flicker==0 && s.hides_total==0 && s.shown_ms*100>=s.target_ms*95);}
        {NativeBodyPromptStats s;for(UINT64 t=0;t<=800;t+=11) s.update(5000+t,0x10,false);s.update(7000,0,false);   // never shown
         assert(s.intervals==1 && s.never==1);}
        {NativeBodyPromptStats s;for(UINT64 t=0;t<=800;t+=11) s.update(5000+t,0x10,t>=400);s.update(7000,0,false);  // first shown at 400 ms
         assert(s.intervals==1 && s.late==1 && !s.never);}
        {NativeBodyPromptStats s;for(UINT64 t=0;t<=200;t+=11) s.update(5000+t,0x10,true);s.update(7000,0,false);    // a 200 ms brush: not counted
         assert(s.intervals==0);}
        {NativeBodyPromptStats s;for(UINT64 t=0;t<=800;t+=11) s.update(5000+t,t<400?0x10:0x20,true);s.update(7000,0,false); // two bodies: two runs
         assert(s.intervals==2);}
        // The logged dropouts: the body leaves the hand query for 0.2-2.3 s; the LOS that dropped it is judged from the palm now for downed characters.
        assert(hand_judged_los(true,false,0) && hand_judged_los(false,true,0x08) && hand_judged_los(false,true,0x20) &&
               !hand_judged_los(false,true,0x80) && !hand_judged_los(false,false,0x28));   // loot, dead, unconscious; standing NPC and objects keep the eye
    }
    assert(!native_hud_keep_flat(NativeHudChildKind::prompt,true,false,false)); // target exists: never flat
    assert(!native_hud_keep_flat(NativeHudChildKind::prompt,false,false,true));
    assert(!native_hud_keep_flat(NativeHudChildKind::prompt,false,false,false)); // VR-only: never flat, even without a target
    assert(!native_hud_keep_flat(NativeHudChildKind::health,true,false,false) && !native_hud_keep_flat(NativeHudChildKind::equipment,false,false,false)); // wrist only
    assert(native_hud_keep_flat(NativeHudChildKind::objective,false,false,false) && !native_hud_keep_flat(NativeHudChildKind::objective,false,true,false));
    std::memcpy(fixture_file.data()+0x1608,&iggy,8);std::memcpy(fixture_file.data()+0x1658,&path,8);std::memcpy(fixture_iggy.data()+0x78,&file,8);std::memcpy(fixture_node.data()+0x80,&root,8);
    const std::array<float,4> bounds{10,20,100,200};std::memcpy(fixture_node.data()+0xc8,bounds.data(),sizeof(bounds));
    NativeWristHud::observe_draw(iggy,GetTickCount64());NativeHudChildDraw::fixture(read,0x140000000,original);
    NativeHudArtGeometry geometry{};geometry.file=file;geometry.iggy=iggy;geometry.display_node=node;geometry.visible=true;geometry.fields=31;
    assert(!NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,0,4,GetTickCount64()));
    assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,4,GetTickCount64()));
    std::array<float,6> matrix{1,0,0,1,100,200};fixture_matrix=matrix.data();
    const auto call=[&]{return NativeHudChildDraw::invoke_fixture(fixture_iggy.data(),matrix.data(),fixture_node.data(),reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),123,4.5f);};
    NativeHudChildDraw::observer=observe;assert(call()==456 && originals==1 && before==1 && after==1 && matrix[4]==100);
    NativeHudChildDraw::transform=transform;changed=true;assert(call()==456 && originals==2 && matrix[4]==100 && NativeHudChildDraw::transforms==1);
    NativeHudChildDraw::begin=begin;NativeHudChildDraw::end=end;capture_enabled=true;changed=false;
    assert(call()==456 && originals==3 && begins==1 && ends==1 && NativeHudChildDraw::captures==1);
    NativeHudChildDraw::clear();NativeHudChildDraw::observer=nullptr;assert(call()==456 && originals==4 && before==3 && after==3);
    NativeHudChildDraw::begin=nullptr;NativeHudChildDraw::end=nullptr;NativeHudChildDraw::transform=nullptr;
    NativeHudChildDraw::observer=+[](const NativeHudChildScope& scope,bool entering,std::uint32_t)noexcept{
        if(entering){last_kind=scope.kind;last_epoch=scope.epoch;last_tick=scope.tick;last_owner=scope.owner;last_talk_offer=scope.talk_offer;
            last_talk_glyph=native_hud_talk_glyph_context(NativeHudChildDraw::current);}
    };
    const auto observed=[&]{last_kind=NativeHudChildKind::none;last_talk_glyph=false;assert(call()==456);return last_kind;};
    const auto fresh_tick=GetTickCount64();NativeWristHud::observe_draw(iggy,fresh_tick);
    assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,90,4,fresh_tick));
    assert(NativeHudChildDraw::publish(NativeHudChildKind::health,geometry,91,4,fresh_tick));
    assert(observed()==NativeHudChildKind::prompt);
    NativeHudChildDraw::clear_kind(NativeHudChildKind::prompt);
    assert(observed()==NativeHudChildKind::health && last_owner==91); // only requested kind removed
    NativeHudChildDraw::clear_kind(NativeHudChildKind::mana);
    assert(observed()==NativeHudChildKind::health); // absent-kind clear leaves health
    NativeHudChildDraw::clear();
    // Sparse wrist values can refresh from an actual owned movie Draw. Prompt
    // bindings remain short-lived and must not inherit wrist refreshes.
    const auto stale_tick=fresh_tick-1000;
    assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,90,4,stale_tick));
    assert(NativeHudChildDraw::publish(NativeHudChildKind::health,geometry,91,4,stale_tick));
    assert(observed()==NativeHudChildKind::prompt); // sticky prompt root: stale binding still never flat
    NativeHudChildDraw::refresh_wrist(file,iggy,5,fresh_tick);
    assert(observed()==NativeHudChildKind::health && last_epoch==5 && last_tick==fresh_tick);
    NativeHudChildDraw::clear_kind(NativeHudChildKind::health);
    assert(observed()==NativeHudChildKind::prompt); // prompt root is sticky
    for(const auto kind:{NativeHudChildKind::mana,NativeHudChildKind::equipment}){
        NativeHudChildDraw::clear();assert(NativeHudChildDraw::publish(kind,geometry,92,4,stale_tick));
        NativeHudChildDraw::refresh_wrist(file,iggy,6,fresh_tick);assert(observed()==kind && last_epoch==6);
    }
    NativeHudChildDraw::clear();
    assert(NativeHudChildDraw::publish(NativeHudChildKind::health,geometry,91,4,stale_tick));
    NativeHudChildDraw::refresh_wrist(file+8,iggy,5,fresh_tick);assert(observed()==NativeHudChildKind::none);
    NativeHudChildDraw::refresh_wrist(file,iggy,0,fresh_tick);assert(observed()==NativeHudChildKind::none);
    const auto cycle=node;std::memcpy(fixture_node.data()+0x80,&cycle,8);
    NativeHudChildDraw::refresh_wrist(file,iggy,5,fresh_tick);std::memcpy(fixture_node.data()+0x80,&root,8);
    assert(observed()==NativeHudChildKind::none); // detached/cyclic child was not renewed
    WristHudDrawIdentity::clear();NativeHudChildDraw::refresh_wrist(file,iggy,5,fresh_tick);
    NativeWristHud::observe_draw(iggy,GetTickCount64());assert(observed()==NativeHudChildKind::none);
    const auto replaced_iggy=iggy+8;
    std::memcpy(fixture_file.data()+0x1608,&replaced_iggy,8);NativeWristHud::observe_draw(replaced_iggy,GetTickCount64());
    assert(wrist_hud_file_owned(read,0x140000000,file)); // path/current new movie still valid
    NativeHudChildDraw::refresh_wrist(file,iggy,5,GetTickCount64());
    assert(!NativeHudChildDraw::publish(NativeHudChildKind::health,geometry,91,5,GetTickCount64()));
    std::memcpy(fixture_file.data()+0x1608,&iggy,8);NativeWristHud::observe_draw(iggy,GetTickCount64());
    assert(observed()==NativeHudChildKind::none); // old identity cannot be refreshed through replacement file
    assert(NativeHudChildDraw::publish(NativeHudChildKind::health,geometry,91,5,GetTickCount64()));
    std::memcpy(fixture_file.data()+0x1608,&replaced_iggy,8);NativeWristHud::observe_draw(replaced_iggy,GetTickCount64());
    assert(observed()==NativeHudChildKind::none); // identify rechecks identity even for fresh binding
    std::memcpy(fixture_file.data()+0x1608,&iggy,8);NativeWristHud::observe_draw(iggy,GetTickCount64());
    NativeHudChildDraw::clear();
    const auto capacity_tick=GetTickCount64();
    for(unsigned i=0;i<40;++i)assert(NativeHudChildDraw::publish(NativeHudChildKind::objective,geometry,100+i,7,capacity_tick));
    assert(!NativeHudChildDraw::publish(NativeHudChildKind::objective,geometry,999,7,capacity_tick));
    assert(NativeHudChildDraw::publish(NativeHudChildKind::objective,geometry,999,8,capacity_tick)); // prior epoch reclaim
    NativeHudChildDraw::clear();
    for(unsigned i=0;i<40;++i)assert(NativeHudChildDraw::publish(NativeHudChildKind::objective,geometry,100+i,7,capacity_tick-1000));
    assert(NativeHudChildDraw::publish(NativeHudChildKind::objective,geometry,999,7,capacity_tick)); // expired slot reclaim
    assert(observed()==NativeHudChildKind::objective && last_owner==999);
    NativeHudChildDraw::clear();
    { // Native Add/Show happens before stage0 publishes its new selection.
        const auto event_tick=GetTickCount64();
        const NativeInteractionTargetIdentity new_target{0x600000,8,1,9,5};
        NativeWristHud::observe_draw(iggy,event_tick);
        assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,9,event_tick,{},true,10));
        const NativeInteractionTargetIdentity old_target{0x500000,7,0,9,4};
        NativePromptCompletions completions;completions.begin(10);
        NativeHudChildDraw::refresh_prompt(file,iggy,9,event_tick,old_target,false,completions);
        NativeHudChildDraw::observer=nullptr;
        NativeHudChildDraw::begin=+[](const NativeHudChildScope& scope) noexcept {assert(!scope.target && scope.prompt_event_pending);return true;};
        NativeHudChildDraw::end=+[](const NativeHudChildScope&) noexcept {};
        assert(call()==456); // a Draw before stage0 completes cannot bind the old remembered/current target
        completions.target(10,new_target);completions.finish(10);
        NativeHudChildDraw::refresh_prompt(file,iggy,9,event_tick,new_target,false,completions);
        NativeHudChildDraw::begin=+[](const NativeHudChildScope& scope) noexcept {
            assert(scope.target.object==0x600000 && scope.target.side==1 && !scope.prompt_event_pending);return true;
        };
        NativeHudChildDraw::end=+[](const NativeHudChildScope&) noexcept {};
        assert(call()==456); // first actual child Draw captures the NEW target, with no additional Add event
        NativeHudChildDraw::begin=nullptr;NativeHudChildDraw::end=nullptr;
        NativeHudChildDraw::clear();
        assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,9,event_tick-1000,{},true));
        NativeHudChildDraw::refresh_prompt(file,iggy,9,event_tick,new_target);
        NativeHudChildDraw::begin=+[](const NativeHudChildScope& scope) noexcept {assert(!scope.target);return true;};
        NativeHudChildDraw::end=+[](const NativeHudChildScope&) noexcept {};
        assert(call()==456); // stale event cannot relabel old artwork as a fresh target
        NativeHudChildDraw::begin=nullptr;NativeHudChildDraw::end=nullptr;
        NativeHudChildDraw::clear();
        assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,9,event_tick,{},true,11));
        completions.begin(11);completions.finish(11); // rejected/empty exact owning invocation
        completions.begin(12);completions.target(12,new_target);completions.finish(12); // unrelated completion before Draw
        NativeHudChildDraw::refresh_prompt(file,iggy,9,event_tick,new_target,false,completions);
        NativeHudChildDraw::begin=+[](const NativeHudChildScope& scope) noexcept {assert(!scope.target && !scope.prompt_event_pending);return true;};
        NativeHudChildDraw::end=+[](const NativeHudChildScope&) noexcept {};
        assert(call()==456); // rejected owner + unrelated completion before Draw cannot relabel event
        NativeHudChildDraw::clear();
        assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,9,event_tick,{},true,13));
        completions.begin(13);completions.target(13,old_target);completions.finish(13);
        completions.begin(14);completions.target(14,new_target);completions.finish(14);
        NativeHudChildDraw::refresh_prompt(file,iggy,9,event_tick,new_target,false,completions);
        assert(call()==456); // B then C before Draw: C never receives artwork emitted by B
        NativeHudChildDraw::retire_prompt_node(iggy,node);
        assert(NativeHudChildDraw::publish(NativeHudChildKind::panel,geometry,99,9,event_tick));
        NativeHudChildDraw::begin=+[](const NativeHudChildScope& scope) noexcept {assert(scope.kind==NativeHudChildKind::panel && !scope.target);return true;};
        assert(call()==456); // no-token native event uses panel pixels, not the stale same-node world prompt root
        NativeHudChildDraw::begin=nullptr;NativeHudChildDraw::end=nullptr;
        NativeHudChildDraw::observer=+[](const NativeHudChildScope& scope,bool entering,std::uint32_t) noexcept {
            if(entering){last_kind=scope.kind;last_epoch=scope.epoch;last_tick=scope.tick;last_owner=scope.owner;last_talk_offer=scope.talk_offer;
                last_talk_glyph=native_hud_talk_glyph_context(NativeHudChildDraw::current);}
        };
        NativeHudChildDraw::clear();
    }
    { // Retained native prompt A survives hover B and the hand query clearing
      // when carrying begins. Its compound child still contains the gauge.
        constexpr std::uintptr_t element=0x210000,entries=0x220000,game=0x230000,entity=0x240000;
        constexpr std::uint32_t spawn=0x1230007,key=42;
        constexpr std::uintptr_t base=0x140000000;
        const auto tick=GetTickCount64();
        put(element+0x80,entries);put(element+0x88,std::uint32_t{1});
        put(entries,std::array<unsigned char,0x40>{});put(entries,key);put(entries+0x3a,std::uint8_t{1});put(entries+0x3c,spawn);
        put(base+0x39a5290,game);put(game+0x3130b0+(spawn>>16)*8ull,entity);put(entity+0xa8,spawn);
        const native_prompt_entry::Receipt source{element,entries,entity,key,spawn};
        const NativeInteractionTargetIdentity object_a{0x500000,7,0,9,4},object_b{0x600000,8,1,9,5};
        const auto saved_observer=NativeHudChildDraw::observer.load();
        NativeHudChildDraw::fixture(read,base,original,true);
        NativeHudChildDraw::observer=+[](const NativeHudChildScope& scope,bool entering,std::uint32_t) noexcept {
            if(entering){last_scope=scope;last_kind=scope.kind;}
        };
        assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,element,9,tick,object_a,false,0,source));
        assert(!NativeHudChildDraw::has_retained_prompt(9,tick));
        assert(observed()==NativeHudChildKind::prompt && !last_scope.retained_entry); // pending1 cannot authorize Draw
        put(entries+0x3a,std::uint8_t{4});
        NativeHudChildDraw::refresh_prompt(file,iggy,9,tick,object_b,true);
        assert(observed()==NativeHudChildKind::prompt && last_scope.retained_entry && last_scope.retained_entry->owner==entity && !last_scope.talk_offer);
        assert(NativeHudChildDraw::has_retained_prompt(9,tick) && !NativeHudChildDraw::has_retained_prompt(10,tick));
        const auto captured_a=last_scope;
        assert(NativeHudChildDraw::scope_current(captured_a,9,tick));
        assert(!NativeHudChildDraw::scope_current(captured_a,10,tick));
        assert(native_prompt_anchor(object_a,object_b,{},source)==NativePromptAnchor::retained);
        NativeHudChildDraw::refresh_prompt(file,iggy,9,tick,{});
        assert(observed()==NativeHudChildKind::prompt && last_scope.retained_entry);
        // Successive native Gauge updates recapture the compound node, even
        // though carry cleared its hand-selected interaction component.
        for(unsigned gauge=0;gauge<3;++gauge) {
            assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,element,9,GetTickCount64(),{},false,0,source));
            NativeHudChildDraw::refresh_prompt(file,iggy,9,GetTickCount64(),{});
            assert(observed()==NativeHudChildKind::prompt && last_scope.node==node && last_scope.retained_entry);
            assert(native_hud_child_same_identity(last_scope,captured_a));
        }
        auto stale_pixels=last_scope;stale_pixels.tick=tick+1000;
        assert(!native_hud_child_replacement_ready(stale_pixels,stale_pixels,tick,tick+1000)); // retained source never renews old pixels
        auto other_source=captured_a;other_source.retained_entry->key++;
        assert(!native_hud_child_same_identity(captured_a,other_source));
        other_source=captured_a;other_source.retained_entry->spawn++;
        assert(!native_hud_child_same_identity(captured_a,other_source));
        other_source=captured_a;other_source.retained_entry->owner+=8;
        assert(!native_hud_child_same_identity(captured_a,other_source));
        auto panel_a=captured_a,panel_b=other_source;panel_a.kind=panel_b.kind=NativeHudChildKind::panel;
        assert(!native_hud_child_same_identity(panel_a,panel_b)); // readbacks keep source identity through panel routing too
        std::array<unsigned char,0x40> active{};assert(read(entries,active.data(),active.size()));
        put(entries+0x40,active);put(entries+0x40,std::uint32_t{43});put(element+0x88,std::uint32_t{2});
        const native_prompt_entry::Receipt source_b{element,entries+0x40,entity,43,spawn};
        assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,element,9,GetTickCount64(),{},false,0,source_b));
        assert(!NativeHudChildDraw::scope_current(captured_a,9,GetTickCount64())); // same window now belongs to B; last-good A must disappear
        assert(observed()==NativeHudChildKind::prompt && last_scope.retained_entry && last_scope.retained_entry->key==43);
        assert(NativeHudChildDraw::scope_current(last_scope,9,GetTickCount64()));
        NativeHudChildDraw::clear_prompt_entry(element,key);
        assert(NativeHudChildDraw::has_retained_prompt(9,GetTickCount64())); // Remove A cannot erase current B
        assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,element,9,GetTickCount64(),{},false,0,source));
        put(entries+0x40,active);put(entries,std::array<unsigned char,0x40>{});put(entries,std::uint32_t{99});put(element+0x88,std::uint32_t{2});
        NativeHudChildDraw::refresh_prompt(file,iggy,9,GetTickCount64(),object_b);
        assert(observed()==NativeHudChildKind::prompt && last_scope.retained_entry && last_scope.retained_entry->entry==entries+0x40);
        assert(native_hud_child_same_identity(last_scope,captured_a)); // compaction changes no artwork identity
        NativeHudChildDraw::clear_prompt_entry(element,99);
        assert(NativeHudChildDraw::has_retained_prompt(9,GetTickCount64())); // Remove another label leaves the gauge source live
        NativeHudChildDraw::clear_prompt_entry(element,key);
        assert(!NativeHudChildDraw::has_retained_prompt(9,GetTickCount64()));
        assert(!NativeHudChildDraw::scope_current(captured_a,9,GetTickCount64()));
        assert(observed()==NativeHudChildKind::prompt && !last_scope.retained_entry); // sticky root is suppression only
        assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,element,9,GetTickCount64(),{},false,0,source));
        put(entries+0x40+0x3a,std::uint8_t{12});
        NativeHudChildDraw::refresh_prompt(file,iggy,9,GetTickCount64(),object_b);
        assert(!NativeHudChildDraw::has_retained_prompt(9,GetTickCount64()));
        assert(observed()==NativeHudChildKind::prompt && !last_scope.retained_entry);
        put(entries+0x40+0x3a,std::uint8_t{4});
        assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,element,9,GetTickCount64(),{},false,0,source));
        put(entries+0x40+0x3c,spawn+1);put(entity+0xa8,spawn+1);
        NativeHudChildDraw::refresh_prompt(file,iggy,9,GetTickCount64(),{});
        assert(!NativeHudChildDraw::has_retained_prompt(9,GetTickCount64()));
        assert(observed()==NativeHudChildKind::prompt && !last_scope.retained_entry);
        put(entries+0x40+0x3c,spawn);put(entity+0xa8,spawn);
        NativeHudChildDraw::clear();
        assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,element,9,tick-1000,{},false,0,source));
        for(unsigned i=0;i<39;++i)assert(NativeHudChildDraw::publish(NativeHudChildKind::objective,geometry,100+i,9,GetTickCount64()));
        assert(!NativeHudChildDraw::publish(NativeHudChildKind::objective,geometry,999,9,GetTickCount64()));
        assert(observed()==NativeHudChildKind::prompt && last_scope.retained_entry); // sparse event is not a reclaimable slot while its entry is active
        NativeHudChildDraw::fixture(read,base,original);NativeHudChildDraw::observer=saved_observer;retained_memory.clear();
    }
    const NativeInteractionTargetIdentity target{0x500000,7,0,9,4};const auto target_tick=GetTickCount64();
    NativeWristHud::observe_draw(iggy,target_tick);
    assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,9,target_tick-1000,target));
    assert(observed()==NativeHudChildKind::prompt && last_owner==99); // sticky root, no flat gap before refresh
    NativeHudChildDraw::refresh_prompt(file,iggy,9,target_tick-1000,target,true);
    assert(observed()==NativeHudChildKind::prompt && !last_talk_offer); // stale offer timestamp never grants the scoped glyph
    assert(!last_talk_glyph);
    NativeHudChildDraw::refresh_prompt(file,iggy,9,GetTickCount64(),target,true);
    assert(observed()==NativeHudChildKind::prompt && last_talk_offer); // exact current Talk offer follows this target scope
    assert(last_talk_glyph);
    NativeHudChildDraw::panel_active=true;
    assert(observed()==NativeHudChildKind::prompt && last_talk_glyph); // v698: in the HUD window a prompt stays world art, never panel content
    NativeHudChildDraw::refresh_prompt(file,iggy,9,target_tick-1000,target,true);
    assert(observed()==NativeHudChildKind::prompt && !last_talk_glyph);
    NativeHudChildDraw::refresh_prompt(file,iggy,9,GetTickCount64(),target,false);
    assert(observed()==NativeHudChildKind::prompt && !last_talk_glyph);
    NativeHudChildDraw::refresh_prompt(file,iggy,9,GetTickCount64(),target,true);
    std::memcpy(fixture_node.data()+0x80,&cycle,8);
    assert(observed()==NativeHudChildKind::none && !last_talk_glyph);
    std::memcpy(fixture_node.data()+0x80,&root,8);
    NativeHudChildDraw::panel_active=false;
    assert(observed()==NativeHudChildKind::prompt && last_talk_glyph);
    NativeHudChildScope unrelated{};unrelated.kind=NativeHudChildKind::panel;unrelated.target=target;unrelated.talk_offer=true;
    assert(!native_hud_talk_glyph_context(&unrelated));
    unrelated.panel_prompt=true;unrelated.target={};
    assert(!native_hud_talk_glyph_context(&unrelated));
    assert(!native_hud_talk_glyph_context(nullptr));
    assert(observed()==NativeHudChildKind::prompt); // held event-driven native prompt stays usable
    auto next=target;next.object+=8;
    NativeHudChildDraw::refresh_prompt(file,iggy,9,GetTickCount64(),next);
    assert(observed()==NativeHudChildKind::prompt && !last_talk_offer); // old target retired; sticky root keeps it prompt (never flat)
    assert(!last_talk_glyph);
    NativeHudChildDraw::panel_active=true;
    assert(observed()==NativeHudChildKind::prompt && !last_talk_glyph); // v698: world art in the HUD window too
    NativeHudChildDraw::panel_active=false;
    assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,9,GetTickCount64(),next));
    assert(observed()==NativeHudChildKind::prompt);
    NativeHudChildDraw::clear_kind(NativeHudChildKind::prompt);assert(observed()==NativeHudChildKind::prompt); // native Remove keeps sticky root
    assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,9,GetTickCount64(),next));
    NativeHudChildDraw::refresh_prompt(file,iggy,9,GetTickCount64(),{},true);assert(observed()==NativeHudChildKind::prompt && !last_talk_offer);
    // Suppression: identified prompt that is not captured is never passed to the native draw.
    NativeHudChildDraw::begin=+[](const NativeHudChildScope&)noexcept{return false;};NativeHudChildDraw::end=+[](const NativeHudChildScope&)noexcept{};
    const auto before_calls=originals;call();assert(originals==before_calls);
    NativeHudChildDraw::begin=nullptr;NativeHudChildDraw::end=nullptr;
    NativeHudChildDraw::clear();
    // Tutorial movie: identified at nesting 2 without any published binding; owner is the node.
    NativeHudChildDraw::clear();
    NativeHudChildDraw::tutorial_movie=+[](void*)noexcept{return true;};NativeHudChildDraw::tutorial_epoch=5;
    NativeHudChildDraw::nesting_=1;assert(observed()==NativeHudChildKind::tutorial && last_epoch==5 && last_owner==node);
    // v814 (headset v812: "I should not see this huge white tutorial notification, there is a VR copy"): a message whose capture is
    // refused this draw is not drawn flat across the eye either
    NativeHudChildDraw::begin=+[](const NativeHudChildScope&)noexcept{return false;};NativeHudChildDraw::end=+[](const NativeHudChildScope&)noexcept{};
    {const auto drawn=originals;NativeHudChildDraw::nesting_=1;call();assert(originals==drawn);}
    NativeHudChildDraw::begin=nullptr;NativeHudChildDraw::end=nullptr;
    NativeHudChildDraw::nesting_=0;assert(observed()==NativeHudChildKind::none); // root (nesting 1) is not the panel
    NativeHudChildDraw::tutorial_epoch=0;NativeHudChildDraw::nesting_=1;assert(observed()==NativeHudChildKind::none);
    NativeHudChildDraw::nesting_=0;NativeHudChildDraw::tutorial_movie=nullptr;
    assert(!native_hud_keep_flat(NativeHudChildKind::tutorial,false,false,false) && !native_hud_keep_flat(NativeHudChildKind::tutorial,false,true,false));
    // HUD window (v696): prompts stay world art (never panel content, never drawn flat); vitals keep their kind.
    NativeHudChildDraw::clear();NativeHudChildDraw::panel_active=true;
    const auto panel_tick=GetTickCount64();NativeWristHud::observe_draw(iggy,panel_tick);
    assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,4,panel_tick));
    const auto prior_panel_children=NativeHudChildDraw::panel_children.load();
    assert(observed()==NativeHudChildKind::prompt && NativeHudChildDraw::panel_children.load()==prior_panel_children);
    {const auto before_orig=originals;NativeHudChildDraw::begin=begin;NativeHudChildDraw::end=end;
     capture_enabled=false;assert(call()==0 && originals==before_orig); // capture refused: suppressed, not flat
     capture_enabled=true;assert(call()==456 && originals==before_orig+1);
     NativeHudChildDraw::begin=nullptr;NativeHudChildDraw::end=nullptr;}
    NativeHudChildDraw::clear(); // a target-bound prompt too
    assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,4,panel_tick,{0x500000,7,1,4,3}));
    assert(observed()==NativeHudChildKind::prompt);
    NativeHudChildDraw::clear();assert(NativeHudChildDraw::publish(NativeHudChildKind::health,geometry,91,4,panel_tick));
    assert(observed()==NativeHudChildKind::health);
    {   // v720: a script message laid out in the takedown hint's stage region stays a message while a takedown is offered.
        NativeHudChildDraw::clear();NativeHudChildDraw::tutorial_epoch=5;const auto saved=matrix;matrix={1,0,0,1,960,700};
        NativeHudChildDraw::master_=true;NativeHudChildDraw::nesting_=2;NativeHudChildDraw::root_={1,0,0};
        NativeHudChildDraw::set_takedown({0x500000,7,1,4,3},GetTickCount64());
        assert(observed()==NativeHudChildKind::prompt); // the takedown hint itself
        NativeHudChildDraw::nesting_=2;NativeHudChildDraw::root_={1,0,0};
        assert(NativeHudChildDraw::publish(NativeHudChildKind::tutorial,geometry,node,4,GetTickCount64()));
        assert(observed()==NativeHudChildKind::tutorial);
        NativeHudChildDraw::set_takedown({},0);NativeHudChildDraw::master_=false;NativeHudChildDraw::nesting_=0;matrix=saved;NativeHudChildDraw::tutorial_epoch=0;
    }
    // Generic panel child only at nesting 3 beneath the qualified MasterHUD node.
    NativeHudChildDraw::clear();NativeHudChildDraw::panel_generic=true;NativeHudChildDraw::tutorial_epoch=5;
    NativeHudChildDraw::nesting_=2;assert(observed()==NativeHudChildKind::none); // not under MasterHUD
    // Nesting-2 children of another gameplay HUD movie (notifications) become panel content.
    NativeHudChildDraw::panel_movie=+[](void*)noexcept{return true;};
    NativeHudChildDraw::nesting_=1;assert(observed()==NativeHudChildKind::panel);
    NativeHudChildDraw::nesting_=0;assert(observed()==NativeHudChildKind::none); // root stays out
    NativeHudChildDraw::panel_movie=nullptr;NativeHudChildDraw::nesting_=1;assert(observed()==NativeHudChildKind::none);
    // Tutorial centring: 400-wide box at x=0..400 shifts to be centred on 960; unusable bounds stay put.
    assert(native_hud_centre_shift({1,0,0,1,0,0},{0,0,400,100})==760.f);
    assert(native_hud_centre_shift({1,0,0,1,0,0},{-5e6f,-5e6f,5e6f,5e6f})==0.f);
    assert(native_hud_centre_shift({1,0,0,1,0,0},{0,0,1900,100})==0.f);
    // HUD screen fit: the root-placed 1920x1080 stage maps exactly onto the texture width, centred vertically.
    {const auto f=native_hud_stage_fit({1.43f,0,0,1.43f,0,706},1.43f,0,706,2496,2688);
     assert(std::fabs(f[0]-1.3f)<1e-5f && std::fabs(f[4])<1e-3f && std::fabs(f[5]-(2688-1080*1.3f)*.5f)<1e-2f);
     const auto right=native_hud_stage_fit({1.43f,0,0,1.43f,1.43f*1900,706},1.43f,0,706,2496,2688); // a widget at stage x=1900
     assert(right[4]<2496.f && std::fabs(right[4]-1900*1.3f)<1e-2f);}
    // Full-screen overlay (measured DamageVignette bounds on a 2746x2957 eye) stays native; a centred hint does not.
    assert(native_hud_full_screen({-2746,-2641.5f,5492,5598.5f},1.43021f,0,706) && native_hud_full_screen({-143,0,2889,2957},1.43021f,0,706));
    assert(!native_hud_full_screen({741,839,2004,2163},1.43021f,0,706) && !native_hud_full_screen({0,706,2746,2251},1.43021f,0,706));
    // Panel relayout: scale about the stage centre (1060,740 from the root matrix) for panel children only.
    {std::array<float,6> m{1,0,0,1,100,200};
     assert(native_hud_panel_relayout(m,{1060.f,740.f},.6f) && std::fabs(m[0]-.6f)<1e-6f && std::fabs(m[4]-484.f)<1e-3f && std::fabs(m[5]-(740.f+.6f*(200.f-740.f)))<1e-3f);
     m={1,0,0,1,100,200};assert(!native_hud_panel_relayout(m,{1060.f,740.f},1.f) && !native_hud_panel_relayout(m,{0.f,0.f},.6f) && m[4]==100);
     NativeHudChildDraw::clear();NativeHudChildDraw::hud_scale=.6f;NativeHudChildDraw::hud_movie=+[](void*)noexcept{return true;};NativeHudChildDraw::nesting_=0;
     NativeHudChildDraw::begin=begin;NativeHudChildDraw::end=end;capture_enabled=true;NativeHudChildDraw::panel_width=2496;NativeHudChildDraw::panel_height=2688;
     expect_x=0.f; // root {1,..,100,200}: the child at the root origin lands at the texture's left edge, scaled 2496/1920
     const auto t=GetTickCount64();NativeWristHud::observe_draw(iggy,t);
     assert(NativeHudChildDraw::publish(NativeHudChildKind::panel,geometry,99,4,t)); // v698: panel content (prompts are world art)
     const auto relayouts=NativeHudChildDraw::hud_relayouts.load();
     assert(observed()==NativeHudChildKind::panel && NativeHudChildDraw::hud_relayouts.load()==relayouts+1 && matrix[4]==100);
     NativeHudChildDraw::clear();expect_x=100.f; // vitals stay on the wrist, untouched
     assert(NativeHudChildDraw::publish(NativeHudChildKind::health,geometry,91,4,t));
     assert(observed()==NativeHudChildKind::health && NativeHudChildDraw::hud_relayouts.load()==relayouts+1);
     expect_x=-1.f;NativeHudChildDraw::hud_movie=nullptr;NativeHudChildDraw::hud_scale=1.f;NativeHudChildDraw::begin=nullptr;NativeHudChildDraw::end=nullptr;}
    NativeHudChildDraw::panel_active=false;NativeHudChildDraw::panel_generic=false;NativeHudChildDraw::tutorial_epoch=0;NativeHudChildDraw::nesting_=0;
    NativeHudChildDraw::clear();NativeHudChildDraw::observer=nullptr;
    // Exact crosshair node is scaled independently of panel layout, with a live owner/movie.
    matrix={1,.2f,-.3f,1,100,200};const auto saved_matrix=matrix;
    NativeHudChildDraw::begin=nullptr;NativeHudChildDraw::end=nullptr;NativeHudChildDraw::transform=nullptr;
    NativeHudChildDraw::eye_live=+[](const NativeCrosshair::Binding& b,UINT64 now) noexcept {
        ++eye_live_calls;
        return eye_owner_ok && b.element==99 && b.movie==88 && b.iggy==reinterpret_cast<std::uintptr_t>(fixture_iggy.data()) &&
            b.tick && now>=b.tick && now-b.tick<=250;
    };
    NativeCrosshair::Binding eye_binding;eye_binding.element=99;eye_binding.movie=88;eye_binding.iggy=iggy;eye_binding.tick=GetTickCount64();
    NativeWristHud::observe_draw(iggy,GetTickCount64());
    assert(NativeHudChildDraw::publish_eye(node,eye_binding));
    for(bool panel:{false,true})for(float scale:{.55f,.65f,1.f,2.f}) {
        NativeHudChildDraw::panel_active=panel;NativeHudChildDraw::hud_scale=scale;glyph_factor=.65f*scale;
        assert(call()==456 && matrix==saved_matrix); // pivot, caller bytes, raw ABI args and return stay unchanged
    }
    glyph_factor=-1.f;expect_x=-1.f;NativeHudChildDraw::panel_active=false;
    NativeHudChildDraw::hud_scale=std::numeric_limits<float>::quiet_NaN();assert(call()==456 && matrix==saved_matrix);
    NativeHudChildDraw::hud_scale=.65f;
    eye_owner_ok=false;assert(call()==456); // stale update/owner, even if the raw hint still matches
    eye_owner_ok=true;
    // Per displayed frame the exact-eye node is validated once (owner, file/iggy back-links, tree walk); the other child draws of the frame
    // are answered from that. Dropped by a new frame, a changed binding, a teardown, or a stale answer.
    {
        const auto drawn=[&](unsigned n) {for(unsigned i=0;i<n;++i)assert(call()==456);};
        glyph_factor=.65f*.65f; // the validated eye node is drawn through the glyph scale (hud_scale .65)
        NativeHudChildDraw::begin_frame(0);
        auto live_before=eye_live_calls;auto reads_before=read_count;
        drawn(40);
        const auto uncached_live=eye_live_calls-live_before,uncached_reads=read_count-reads_before;
        assert(uncached_live==40); // no frame number: every child draw validates (old behaviour)
        NativeHudChildDraw::begin_frame(11);
        live_before=eye_live_calls;reads_before=read_count;auto validations=NativeHudChildDraw::eye_validations();
        drawn(1);
        const auto first_reads=read_count-reads_before;
        assert(eye_live_calls-live_before==1 && NativeHudChildDraw::eye_validations()==validations+1);
        live_before=eye_live_calls;reads_before=read_count;
        drawn(39);
        assert(eye_live_calls==live_before && NativeHudChildDraw::eye_validations()==validations+1);
        // the 39 cached draws still read the glyph matrix (a draw's own work) but none of the ownership reads
        assert(first_reads==uncached_reads/40 && read_count-reads_before==39);
        std::cout<<"native_hud_child_draw engine reads for 40 eye draws: "<<uncached_reads<<" uncached, "<<(first_reads+(read_count-reads_before))<<" with the frame cache; ownership checks "<<uncached_live<<" -> 1\n";
        // a new displayed frame validates again
        NativeHudChildDraw::begin_frame(12);live_before=eye_live_calls;drawn(3);assert(eye_live_calls==live_before+1);
        // a stale owner inside the frame: re-publishing drops the cache, so the loss is seen at once
        eye_owner_ok=false;
        assert(NativeHudChildDraw::publish_eye(node,eye_binding)==false);
        eye_owner_ok=true;assert(NativeHudChildDraw::publish_eye(node,eye_binding));
        live_before=eye_live_calls;drawn(2);assert(eye_live_calls==live_before); // publishing validated it for this frame already
        // clearing the binding drops the cache
        NativeHudChildDraw::clear_eye();NativeHudChildDraw::eye_node=node;glyph_factor=-1.f;live_before=eye_live_calls;drawn(2);assert(eye_live_calls==live_before);
        assert(NativeHudChildDraw::publish_eye(node,eye_binding));
        NativeHudChildDraw::begin_frame(0);glyph_factor=-1.f;
    }
    {   // G4: the in-game switch hides the eye node live; paired stereo's second UI draw is shifted by the per-eye callback
        assert(NativeHudChildDraw::publish_eye(node,eye_binding));
        NativeHudChildDraw::hud_scale=.65f;
        // crosshair_hidden_matrix_zero_linear
        {std::array<float,6> m{1,.2f,-.3f,1,100,200};assert(native_crosshair_hide(m) && m[0]==0.f && m[1]==0.f && m[2]==0.f && m[3]==0.f && m[4]==100.f && m[5]==200.f);}
        // layout_undo_is_identity: the draw path compensates the HUD layout scale itself, so placement must not pre-scale
        for(const float k:{.55f,.65f,.85f}) {
            const std::array<float,6> in{.9f,.1f,-.2f,1.1f,310.f,-140.f};auto m=in;const std::array<float,2> centre{960.f,540.f};
            assert(native_hud_panel_relayout(m,centre,k) && native_crosshair_unlayout(m,k,centre));
            for(unsigned i=0;i<6;++i)assert(std::fabs(m[i]-in[i])<1e-4f);
        }
        // crosshair_toggle_live
        NativeHudChildDraw::begin_frame(40);
        NativeHudChildDraw::crosshair_hidden=true;glyph_factor=0.f;assert(call()==456);
        const auto hidden_draws=NativeHudChildDraw::crosshair_hidden_draws.load();assert(hidden_draws>=1);
        NativeHudChildDraw::crosshair_hidden=false;glyph_factor=.65f*.65f;assert(call()==456);
        assert(NativeHudChildDraw::crosshair_hidden_draws.load()==hidden_draws);
        // second_eye_draw_shifted
        NativeHudChildDraw::eye_shift=+[](unsigned index) noexcept->std::optional<std::array<float,2>> {
            return index==1?std::optional<std::array<float,2>>(std::array<float,2>{30.f,15.f}):std::nullopt;};
        const auto shifts=NativeHudChildDraw::crosshair_eye_shifts.load();
        NativeHudChildDraw::begin_frame(41);
        eye_dx=0.f;eye_dy=0.f;assert(call()==456);
        eye_dx=30.f;eye_dy=15.f;assert(call()==456);
        assert(NativeHudChildDraw::crosshair_eye_shifts.load()==shifts+1);
        NativeHudChildDraw::begin_frame(42);
        eye_dx=0.f;eye_dy=0.f;assert(call()==456 && NativeHudChildDraw::crosshair_eye_shifts.load()==shifts+1); // a new frame's first draw is unshifted
        // target_size: the movie root fits 1920x1080 by width and centres it
        NativeHudChildDraw::publish_root({1.43f,0,0,1.43f,0,706});
        {const auto size=NativeHudChildDraw::target_size();assert(std::fabs(size[0]-1920*1.43f)<1e-2f && std::fabs(size[1]-(1080*1.43f+2*706))<1e-2f);}
        NativeHudChildDraw::eye_shift=nullptr;NativeHudChildDraw::begin_frame(0);glyph_factor=-1.f;eye_dx=eye_dy=0.f;
    }
    std::memcpy(fixture_file.data()+0x1608,&replaced_iggy,8);assert(call()==456);
    std::memcpy(fixture_file.data()+0x1608,&iggy,8);
    std::memcpy(fixture_node.data()+0x80,&cycle,8);assert(call()==456); // no longer in live movie tree
    std::memcpy(fixture_node.data()+0x80,&root,8);
    NativeHudChildDraw::clear_eye();NativeHudChildDraw::eye_node=node;assert(call()==456); // hint alone is never authority
    assert(NativeHudChildDraw::publish_eye(node,eye_binding));NativeHudChildDraw::clear();assert(call()==456);
    assert(NativeHudChildDraw::publish_eye(node,eye_binding));NativeHudChildDraw::fixture(read,0x140000000,original);assert(call()==456);
    // Actual nested hook route: MasterHUD scaling is inherited by the reticle, then undone exactly once.
    NativeHudChildDraw::fixture(read,0x140000000,nested_original);
    NativeHudChildDraw::hud_movie=+[](void* movie) noexcept {return movie==fixture_iggy.data();};
    NativeHudChildDraw::hud_layout_node=+[](void*,void* child) noexcept {return child==ancestor_node.data();};
    std::memcpy(ancestor_node.data()+0x80,&root,8);
    const auto ancestor=reinterpret_cast<std::uintptr_t>(ancestor_node.data());std::memcpy(fixture_node.data()+0x80,&ancestor,8);
    assert(NativeHudChildDraw::publish_eye(node,eye_binding));
    for(float scale:{.55f,.65f,1.f,2.f}) {
        nested_scale=scale;NativeHudChildDraw::hud_scale=scale;
        std::array<float,6> parent{1,0,0,1,100,200};
        assert(NativeHudChildDraw::invoke_fixture(fixture_iggy.data(),parent.data(),reinterpret_cast<void*>(root),
            reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),123,4.5f)==456);
        assert((parent==std::array<float,6>{1,0,0,1,100,200}));
    }
    assert(nested_glyphs==4);std::memcpy(fixture_node.data()+0x80,&root,8);
    NativeHudChildDraw::fixture(read,0x140000000,original);NativeHudChildDraw::hud_movie=nullptr;NativeHudChildDraw::hud_layout_node=nullptr;
    assert(NativeHudChildDraw::publish_eye(node,eye_binding));assert(NativeHudChildDraw::has_live_eye());
    NativeHudChildDraw::clear_eye();assert(!NativeHudChildDraw::has_live_eye());
    assert(NativeHudChildDraw::publish_eye(node,eye_binding));assert(NativeHudChildDraw::disable());assert(call()==456);
    std::cout<<"native child draw ABI/return, capture/matrix, kind clearing, stale capacity reclaim and exact-owner wrist refresh checks passed\n";
}
