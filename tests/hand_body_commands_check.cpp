#include "hand_body_commands.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
using namespace d2vr;
using namespace d2vr::hand_body;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)

namespace {
std::uint16_t u16(const CommandRecord& r,std::size_t at){std::uint16_t v;std::memcpy(&v,r.bytes.data()+at,2);return v;}
std::uint32_t u32(const CommandRecord& r,std::size_t at){std::uint32_t v;std::memcpy(&v,r.bytes.data()+at,4);return v;}
float f32(const CommandRecord& r,std::size_t at){float v;std::memcpy(&v,r.bytes.data()+at,4);return v;}
template<class F> bool throws(F f){try{f();}catch(const std::invalid_argument&){return true;}return false;}
constexpr std::uint32_t all_ones=0x07ffffffu;   // 27 mask bits
constexpr std::uint32_t real_player=0x0838c566u;   // B7 runtime: layer 6, mask 0x0041c62b

void set_velocity_layout(){
    const auto r=encode_set_velocity(0x01001cc5u,{1,2,3},{4,5,6});
    CHECK(r.size==0x30);CHECK(u16(r,0)==0x30);CHECK(u16(r,2)==0x0200);CHECK(u16(r,4)==9);CHECK(u32(r,8)==0x01001cc5u);
    CHECK(f32(r,0x10)==1.f&&f32(r,0x14)==2.f&&f32(r,0x18)==3.f&&f32(r,0x1c)==0.f);
    CHECK(f32(r,0x20)==4.f&&f32(r,0x24)==5.f&&f32(r,0x28)==6.f&&f32(r,0x2c)==0.f);
    CHECK(u32(r,0xc)==0);
}
void set_filter_layout(){
    const auto r=encode_set_filter(0x02000007u,0x000887e4u);
    CHECK(r.size==0x20);CHECK(u16(r,0)==0x20);CHECK(u16(r,2)==0x0200);CHECK(u16(r,4)==0x1a);
    CHECK(u32(r,8)==0x02000007u);CHECK(u32(r,0xc)==0x000887e4u);CHECK(u32(r,0x10)==0);
    // the 0x97600 builder stores 0x0200 and 0x1a as one u32 at +2 (local_26 = 0x1a0200 is the 24-bit-shifted view of the same bytes)
    CHECK(u16(r,2)==0x0200&&u16(r,4)==0x001a);
}
void non_finite_never_encodes(){
    const double nan=std::numeric_limits<double>::quiet_NaN(),inf=std::numeric_limits<double>::infinity();
    const double bad[]={nan,inf,-inf,1e300};
    for(const double b:bad){
        CHECK(throws([&]{encode_set_velocity(1,{b,0,0},{0,0,0});}));
        CHECK(throws([&]{encode_set_velocity(1,{0,b,0},{0,0,0});}));
        CHECK(throws([&]{encode_set_velocity(1,{0,0,b},{0,0,0});}));
        CHECK(throws([&]{encode_set_velocity(1,{0,0,0},{b,0,0});}));
        CHECK(throws([&]{encode_set_velocity(1,{0,0,0},{0,b,0});}));
        CHECK(throws([&]{encode_set_velocity(1,{0,0,0},{0,0,b});}));
    }
}
void filter_word_roundtrip(){
    for(unsigned layer=0;layer<27;++layer)for(const std::uint32_t m:{0u,1u,0x443fu,0x0041c62bu,all_ones}){
        const auto w=filter_word(layer,m);
        CHECK(word_layer(w)==layer);CHECK(word_mask(w)==m);
    }
    CHECK(filter_word(6,0x0041c62bu)==real_player);
    CHECK(filter_word(layer_debris,0x443fu)==0x000887e4u);
}
void hand_mask_includes_world_layers(){
    const auto m=hand_collides_with(true);
    for(const unsigned l:{0u,1u,2u,3u,4u,5u,10u,14u}) CHECK((m>>l)&1u);
    CHECK(m==0x443fu);CHECK(hand_filter_word(true)==0x000887e4u);
    // layer 15 (INTERACTABLE) is excluded on purpose, see the header comment
    CHECK(!((m>>layer_interactable)&1u));
    const auto n=hand_collides_with(false);
    for(const unsigned l:{0u,1u,2u,3u,5u,10u,14u}) CHECK((n>>l)&1u);
    // a world body that lists the hand's layer collides with the hand
    for(const unsigned l:{0u,1u,2u,3u,5u,10u,14u}) CHECK(layers_collide(hand_filter_word(true),filter_word(l,all_ones)));
}
void hand_mask_excludes_player_layers_both_directions(){
    for(const bool debris:{false,true}){
        const auto hand=hand_filter_word(debris);
        for(const unsigned l:{layer_player,layer_player_hitbox,layer_player_dead,layer_player_physics,layer_pickup}){
            CHECK(!((hand_collides_with(debris)>>l)&1u));
            CHECK(!layers_collide(hand,filter_word(l,all_ones)));
            CHECK(!layers_collide(filter_word(l,all_ones),hand));
        }
    }
    CHECK(!layers_collide(hand_filter_word(true),real_player));
    CHECK(!layers_collide(real_player,hand_filter_word(true)));
}
void debris_only_when_requested(){
    CHECK(!((hand_collides_with(false)>>layer_debris)&1u));
    CHECK(((hand_collides_with(true)>>layer_debris)&1u));
    CHECK(hand_collides_with(true)==(hand_collides_with(false)|layer_bit(layer_debris)));
    // the Q5 layer-swap fallback word: mask bit 4 cleared is word bit 9
    CHECK(hand_filter_word(true)-hand_filter_word(false)==0x200u);
    CHECK(hand_filter_word(false)==0x000885e4u);
    // a debris body with a full mask meets the hand only when requested
    const auto debris_body=filter_word(layer_debris,all_ones);
    CHECK(layers_collide(hand_filter_word(true),debris_body));
    CHECK(!layers_collide(hand_filter_word(false),debris_body));
}
void mutual_requirement(){
    const auto hand=hand_filter_word(true);
    // a world body whose mask omits the hand layer (4) does not collide
    CHECK(!layers_collide(hand,filter_word(layer_world_low,all_ones&~layer_bit(layer_debris))));
    CHECK(!layers_collide(filter_word(layer_world_low,all_ones&~layer_bit(layer_debris)),hand));
    // and a hand whose mask omits the other body's layer does not collide even if that body lists the hand
    CHECK(!layers_collide(hand,filter_word(layer_npc,all_ones)));
    CHECK(layers_collide(hand,filter_word(layer_world_low,all_ones)));
    CHECK(layers_collide(filter_word(layer_world_low,all_ones),hand)==layers_collide(hand,filter_word(layer_world_low,all_ones)));
}
// Q3: no queued createBody, so no add record; 0x11/6/0x32 have no pinned payload. Assert the header exposes exactly the two proven
// encoders (a source scan, since a missing function cannot be named in C++).
void no_encode_add_without_evidence(){
    std::string text;
    for(const char* p:{"src/hand_body_commands.hpp","../src/hand_body_commands.hpp","../../src/hand_body_commands.hpp"}){
        std::ifstream in(p);if(in){std::stringstream ss;ss<<in.rdbuf();text=ss.str();break;}
    }
    CHECK(!text.empty());
    std::set<std::string> found;
    const std::string key="CommandRecord encode_";
    for(std::size_t at=0;(at=text.find(key,at))!=std::string::npos;at+=key.size()){
        const auto start=at+std::string("CommandRecord ").size();
        const auto end=text.find('(',start);
        found.insert(text.substr(start,end-start));
    }
    CHECK((found==std::set<std::string>{"encode_set_velocity","encode_set_filter"}));
}
}

int main(){
    set_velocity_layout();set_filter_layout();non_finite_never_encodes();filter_word_roundtrip();hand_mask_includes_world_layers();
    hand_mask_excludes_player_layers_both_directions();debris_only_when_requested();mutual_requirement();no_encode_add_without_evidence();
    std::printf("hand_body_commands_check ok\n");
    return 0;
}
