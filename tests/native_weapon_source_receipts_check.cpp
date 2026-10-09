#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_weapon_source_receipts.hpp"
#include "native_rig_fixture.hpp"
#include <cassert>
#include <iostream>
static Memory* memory{};
static unsigned original_calls{},mode{};
static d2vr::NativeWeaponSourceReceipts* current{};
static r::ArmTracking* current_tracking{};
namespace d2vr {
struct NativeWeaponSourceReceiptsCheck {
    static void invoke(NativeWeaponSourceReceipts& trace,std::uintptr_t animation,std::uintptr_t caller) {
        trace.invoke(reinterpret_cast<void*>(animation),3,caller);
    }
    static void stage(NativeWeaponSourceReceipts& trace,int stage,std::uintptr_t caller) {
        trace.invoke(reinterpret_cast<void*>(animations[1]),stage,caller);
    }
    static void setup(NativeWeaponSourceReceipts& trace) {
        trace.base_=base_address;
        trace.read_=+[](std::uintptr_t at,void* out,std::size_t size){return (*memory)(at,out,size);};
        trace.logic_=+[]()->UINT{return 2;};trace.active_=true;
        trace.original_=+[](void* animation,int stage) {
            ++original_calls;assert(animation==reinterpret_cast<void*>(animations[1]) && stage>=0 && stage<=3);
            if(mode==1) memory->put(game_address+0x2e7428,std::uint32_t{43});
            if(mode==2) {mode=0;invoke(*current,animations[1],base_address+0x5411e7);}
            if(mode==3) {mode=0;invoke(*current,animations[1],base_address+0x111111);}
            if(mode==4) throw std::runtime_error("Native test exception");
        };
    }
};
}
int main() {
    auto native=fixture();memory=&native;native.put(game_address+0x2e7428,std::uint32_t{42});
    const auto arms=r::player_arms(native,base_address);assert(arms);
    const std::array<float,15> model_base{1,2,3,1,0,0,0,1,0,0,0,1,1,1,1};
    native.put(models[1]+0x1860,model_base);
    auto snapshot=std::make_shared<const r::NativeArmBindingSnapshot>(r::NativeArmBindingSnapshot{*arms,{},{}});
    const auto source=poses[1]+137*64;
    const auto content=d2vr::weapon_source_content(native,base_address,arms->at(1));
    assert(content && content->frame==42 && content->source==source && content->remap[59]==196);
    assert(d2vr::same_weapon_source(*content,*d2vr::weapon_source_content(native,base_address,arms->at(1))));
    d2vr::NativeWeaponSourceReceipts trace;current=&trace;d2vr::NativeWeaponSourceReceiptsCheck::setup(trace);
    r::ArmTracking tracking;current_tracking=&tracking;r::ArmTrackingFrame tracking_frame{};
    tracking_frame.tick=GetTickCount64();tracking_frame.epoch=1;tracking_frame.hands=3;tracking.publish(tracking_frame);
    trace.tracking_reader(+[](){return current_tracking->stamp();});
    trace.publish(snapshot,7,GetTickCount64(),3,true);
    assert(!trace.match(arms->at(1),source).matched);
    d2vr::NativeWeaponSourceReceiptsCheck::invoke(trace,animations[1],base_address+0x5411e7);
    assert(original_calls==1 && trace.receipts()==1);
    auto matched=trace.match(arms->at(1),source);assert(matched.matched && matched.native_frame==42 && matched.receipt_frame==42 && matched.epoch==7);
    assert(matched.model_matched && matched.tracking_matched);
    auto captured=trace.capture_current(arms->at(1));assert(captured && d2vr::same_weapon_source(captured->content,*content));
    captured->content.joints[59].f[12]+=.1f;
    assert(trace.match(arms->at(1),source).matched); // Full capture is owned; mutation cannot alter the receipt/native source.
    tracking.publish(tracking_frame);
    matched=trace.match(arms->at(1),source);assert(matched.matched && !matched.tracking_matched);
    auto moved_base=model_base;moved_base[0]+=.01f;native.put(models[1]+0x1860,moved_base);
    matched=trace.match(arms->at(1),source);assert(matched.matched && !matched.model_matched && matched.model.valid);
    moved_base[3]=2;native.put(models[1]+0x1860,moved_base);
    matched=trace.match(arms->at(1),source);assert(matched.matched && !matched.model_matched && !matched.model.valid);
    native.put(models[1]+0x1860,model_base);
    unsigned model_reads{};auto model_race=[&](std::uintptr_t at,void* out,std::size_t size) {
        if(at==models[1]+0x1860 && ++model_reads==2) native.put(at,moved_base);
        return native(at,out,size);
    };
    assert(!d2vr::weapon_model_base(model_race,base_address,arms->at(1)).valid);
    native.put(models[1]+0x1860,model_base);
    assert(trace.match(arms->at(1),source+59*64,59).matched);
    assert(!trace.match(arms->at(1),source+58*64,59).matched);
    for(int stage=0;stage<3;++stage) {
        d2vr::NativeWeaponSourceReceiptsCheck::stage(trace,stage,base_address+0x54ebca);
        assert(!trace.match(arms->at(1),source).matched && trace.stage_calls(static_cast<unsigned>(stage),true)>0);
    }
    d2vr::NativeWeaponSourceReceiptsCheck::stage(trace,3,base_address+0x54ebca);
    assert(trace.match(arms->at(1),source).matched && trace.match(arms->at(1),source).producer_caller_rva==0x54ebca);
    d2vr::NativeWeaponSourceReceiptsCheck::stage(trace,3,base_address+0x5508f8);assert(trace.match(arms->at(1),source).matched);
    d2vr::NativeWeaponSourceReceiptsCheck::stage(trace,0,base_address+0x5508f8);assert(!trace.match(arms->at(1),source).matched);
    assert(trace.noncanonical()==1);
    d2vr::NativeWeaponSourceReceiptsCheck::stage(trace,3,base_address+0x5411e7);
    auto changed=content->joints[59];changed.f[12]+=.01f;native.put(source+59*64,changed);
    assert(trace.match(arms->at(1),source).reason==4);native.put(source+59*64,content->joints[59]);
    native.put(game_address+0x2e7428,std::uint32_t{43});assert(trace.match(arms->at(1),source).reason==3);
    native.put(game_address+0x2e7428,std::uint32_t{42});
    mode=1;d2vr::NativeWeaponSourceReceiptsCheck::invoke(trace,animations[1],base_address+0x5411e7);
    assert(!trace.match(arms->at(1),source).matched);mode=0;
    d2vr::NativeWeaponSourceReceiptsCheck::invoke(trace,animations[1],base_address+0x5411e7);assert(trace.match(arms->at(1),source).matched);
    mode=2;d2vr::NativeWeaponSourceReceiptsCheck::invoke(trace,animations[1],base_address+0x5411e7);
    assert(trace.overlaps()==1 && !trace.match(arms->at(1),source).matched);
    d2vr::NativeWeaponSourceReceiptsCheck::invoke(trace,animations[1],base_address+0x5411e7);assert(trace.match(arms->at(1),source).matched);
    mode=3;d2vr::NativeWeaponSourceReceiptsCheck::invoke(trace,animations[1],base_address+0x5411e7);
    assert(trace.noncanonical()==2 && !trace.match(arms->at(1),source).matched);
    d2vr::NativeWeaponSourceReceiptsCheck::invoke(trace,animations[1],base_address+0x5411e7);
    trace.publish(snapshot,8,GetTickCount64(),3,true);assert(!trace.match(arms->at(1),source).matched);
    trace.publish(snapshot,8,GetTickCount64()-251,3,true);
    d2vr::NativeWeaponSourceReceiptsCheck::invoke(trace,animations[1],base_address+0x5411e7);assert(!trace.match(arms->at(1),source).matched);
    trace.publish(snapshot,8,GetTickCount64(),3,true);
    d2vr::NativeWeaponSourceReceiptsCheck::invoke(trace,animations[1],base_address+0x5411e7);
    trace.publish(snapshot,8,GetTickCount64(),1,true);assert(trace.match(arms->at(1),source).reason==5);
    trace.publish(snapshot,8,GetTickCount64(),3,true);mode=4;const auto before=original_calls;
    try {d2vr::NativeWeaponSourceReceiptsCheck::invoke(trace,animations[1],base_address+0x5411e7);assert(false);}
    catch(const std::runtime_error&) {}mode=0;assert(original_calls==before+1 && !trace.match(arms->at(1),source).matched);
    d2vr::NativeWeaponSourceReceiptsCheck::invoke(trace,animations[1],base_address+0x5411e7);assert(trace.match(arms->at(1),source).matched);
    native.put(animations[1]+0x18,r::Identity{999,10});assert(!d2vr::weapon_source_content(native,base_address,arms->at(1)));
    native=fixture();native.put(game_address+0x2e7428,std::uint32_t{42});
    unsigned payload_reads{};auto racing=[&](std::uintptr_t at,void* out,std::size_t size) {
        if(at==source && ++payload_reads==2) native.put(source+59*64,changed);
        return native(at,out,size);
    };
    assert(!d2vr::weapon_source_content(racing,base_address,arms->at(1)));
    trace.clear();assert(!trace.match(arms->at(1),source).matched);
    d2vr::NativeWeaponSourceReceipts unused;assert(unused.disable());
    bool rejected=false;
    try {unused.install(base_address,+[](std::uintptr_t at,void* out,std::size_t size){return (*memory)(at,out,size);},+[]()->UINT{return 2;});}
    catch(const std::runtime_error&) {rejected=true;}assert(rejected && unused.disable()); // No hook creation for wrong ABI.
    std::cout<<"PASS: exact owned stage3 receipt, native frame/content/remap identity, source socket, original once, overlap/noncanonical/focus/epoch/recycle/racing rejection; no native writes\n";
}
