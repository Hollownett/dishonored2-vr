#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_hud_child_draw.hpp"
#include <cassert>
#include <iostream>
#include <limits>
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_OK;}
namespace {
alignas(8) std::array<std::byte,0x1668> fixture_file{};
alignas(8) std::array<std::byte,0xe0> fixture_node{};
alignas(8) std::array<std::byte,0x100> fixture_iggy{};
const char fixture_path[]="ui/hud.iggy";
unsigned originals{},before{},after{},begins{},ends{};
float* fixture_matrix{};bool changed{},capture_enabled{},eye_owner_ok=true;float expect_x=-1.f,glyph_factor=-1.f;
d2vr::NativeHudChildKind last_kind{};UINT64 last_epoch{},last_tick{};std::uintptr_t last_owner{};bool last_talk_offer{},last_talk_glyph{};
bool read(std::uintptr_t at,void* out,std::size_t size){SIZE_T n{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),out,size,&n) && n==size;}
std::uint32_t __fastcall original(void* iggy,float* matrix,void* node,void* aux,void* p5,void* p6,std::uint32_t clip,float depth){
    ++originals;assert(iggy==fixture_iggy.data() && node==fixture_node.data() && aux==reinterpret_cast<void*>(1) && p5==reinterpret_cast<void*>(2) && p6==reinterpret_cast<void*>(3) && clip==123 && depth==4.5f);
    if(glyph_factor>=0.f) {
        assert(matrix!=fixture_matrix && matrix[4]==fixture_matrix[4] && matrix[5]==fixture_matrix[5]);
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
    assert(observed()==NativeHudChildKind::panel && last_talk_glyph);
    NativeHudChildDraw::refresh_prompt(file,iggy,9,target_tick-1000,target,true);
    assert(observed()==NativeHudChildKind::panel && !last_talk_glyph);
    NativeHudChildDraw::refresh_prompt(file,iggy,9,GetTickCount64(),target,false);
    assert(observed()==NativeHudChildKind::panel && !last_talk_glyph);
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
    assert(observed()==NativeHudChildKind::panel && !last_talk_glyph);
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
    NativeHudChildDraw::nesting_=0;assert(observed()==NativeHudChildKind::none); // root (nesting 1) is not the panel
    NativeHudChildDraw::tutorial_epoch=0;NativeHudChildDraw::nesting_=1;assert(observed()==NativeHudChildKind::none);
    NativeHudChildDraw::nesting_=0;NativeHudChildDraw::tutorial_movie=nullptr;
    assert(native_hud_keep_flat(NativeHudChildKind::tutorial,false,false,false) && !native_hud_keep_flat(NativeHudChildKind::tutorial,false,true,false));
    // HUD window: prompts become panel content and are never drawn flat; vitals keep their kind.
    NativeHudChildDraw::clear();NativeHudChildDraw::panel_active=true;
    const auto panel_tick=GetTickCount64();NativeWristHud::observe_draw(iggy,panel_tick);
    assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,4,panel_tick));
    const auto prior_panel_children=NativeHudChildDraw::panel_children.load();
    assert(observed()==NativeHudChildKind::panel && NativeHudChildDraw::panel_children.load()==prior_panel_children+1);
    {const auto before_orig=originals;NativeHudChildDraw::begin=begin;NativeHudChildDraw::end=end;
     capture_enabled=false;assert(call()==0 && originals==before_orig); // capture refused: suppressed, not flat
     capture_enabled=true;assert(call()==456 && originals==before_orig+1);
     NativeHudChildDraw::begin=nullptr;NativeHudChildDraw::end=nullptr;}
    NativeHudChildDraw::clear(); // a target-bound prompt is HUD-screen content too: no separate world capture to fail
    assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,4,panel_tick,{0x500000,7,1,4,3}));
    assert(observed()==NativeHudChildKind::panel);
    NativeHudChildDraw::clear();assert(NativeHudChildDraw::publish(NativeHudChildKind::health,geometry,91,4,panel_tick));
    assert(observed()==NativeHudChildKind::health);
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
     assert(NativeHudChildDraw::publish(NativeHudChildKind::prompt,geometry,99,4,t));
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
    assert(NativeHudChildDraw::publish_eye(node,eye_binding));assert(NativeHudChildDraw::disable());assert(call()==456);
    std::cout<<"native child draw ABI/return, capture/matrix, kind clearing, stale capacity reclaim and exact-owner wrist refresh checks passed\n";
}
