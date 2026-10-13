#define NOMINMAX
#include "../src/native_interaction_aim.hpp"
#include <map>
#include <cassert>
#include <iostream>
namespace {
constexpr std::uintptr_t fixture_base=0x140000000,fixture_sensor=0x200000,fixture_camera=0x300000,fixture_player=0x400000;
std::map<std::uintptr_t,std::byte> memory;
std::array<float,4> native_eye{0,0,1.7f,0};unsigned originals{};
template<class T>void put(std::uintptr_t at,const T& value){const auto* p=reinterpret_cast<const std::byte*>(&value);for(std::size_t i=0;i<sizeof(T);++i)memory[at+i]=p[i];}
bool read(std::uintptr_t at,void* out,std::size_t size){if(at==reinterpret_cast<std::uintptr_t>(native_eye.data()) && size<=sizeof(native_eye)){std::memcpy(out,native_eye.data(),size);return true;}auto* p=static_cast<std::byte*>(out);for(std::size_t i=0;i<size;++i){const auto found=memory.find(at+i);if(found==memory.end())return false;p[i]=found->second;}return true;}
const float* __fastcall original(void*){++originals;return native_eye.data();}
}
namespace d2vr {
struct NativeInteractionRangeFixture {
    static void run(){
        NativeInteractionAim aim;aim.base_=fixture_base;aim.read_=read;aim.route_=true;aim.active_=true;aim.range_origin_=original;
        put(fixture_sensor,fixture_base+0x1b403b8);put(fixture_sensor+0x40,fixture_camera);put(fixture_sensor+0x10,fixture_player);
        put(fixture_camera,fixture_base+camera_vtable);put(fixture_camera+0x20,fixture_base+camera_secondary_vtable);
        const rig::Identity id{7,10};put(fixture_camera+0x18,id);put(fixture_camera+0x10,fixture_player);
        aim.routing_.next({true,false},GetTickCount64());
        const auto revision=aim.routing_.revision();aim.queued_revision_[0]=revision;
        NativeInteractionAim::Context scope{};scope.camera.player=fixture_player;scope.camera.camera=fixture_camera;scope.camera.camera_identity=id;
        scope.hand={GetTickCount64(),3,fixture_player,fixture_camera,7,{},true,{.65,0,1.25}};scope.side=0;scope.stage=0;scope.sensor=fixture_sensor;scope.revision=revision;
        NativeInteractionAim::context_=&scope;
        const auto call=[&](std::uintptr_t caller=fixture_base+0x3b81d8){return aim.range_origin(reinterpret_cast<void*>(fixture_camera),caller);};
        const auto* hand=call();assert(hand!=native_eye.data() && hand[0]==.65f && hand[2]==1.25f && originals==1);
        // Native-equivalent descriptor range/vertical checks for a guard or
        // prop action: native permissions and the later LOS still decide.
        const Vec3 hit{.8,0,1.25};
        const auto eligible=[&](const float* origin,bool permission,bool los){return permission && los && length(hit-Vec3{origin[0],origin[1],origin[2]})<=.45 && std::abs(origin[2]-1.25)<=.5;};
        assert(!eligible(native_eye.data(),true,true) && eligible(hand,true,true));
        assert(!eligible(hand,false,true) && !eligible(hand,true,false));
        assert(native_eye[0]==0 && native_eye[2]==1.7f); // shared fixture_camera untouched
        assert(call(fixture_base+0x3b81d9)==native_eye.data());
        scope.stage=1;assert(call()==native_eye.data());scope.stage=0;
        scope.hand.tick=GetTickCount64()-251;assert(call()==native_eye.data());scope.hand.tick=GetTickCount64();
        ++scope.revision;assert(call()==native_eye.data());--scope.revision;
        put(fixture_camera+0x18,rig::Identity{8,10});assert(call()==native_eye.data());put(fixture_camera+0x18,id);
        put(fixture_sensor+0x40,fixture_camera+8);assert(call()==native_eye.data());put(fixture_sensor+0x40,fixture_camera);
        aim.queued_revision_[0]=revision+1;assert(call()==native_eye.data());aim.queued_revision_[0]=revision;
        scope.hand.volume_centre={2,0,1.7};assert(call()==native_eye.data());
        NativeInteractionAim::context_=nullptr;assert(call()==native_eye.data());
        assert(originals==10); // original getter exactly once in every path
        constexpr std::uintptr_t target_object=0x500000,target_owner=0x600000,game=0x700000,model=0x800000;
        constexpr unsigned spawn=0x10001;
        put(fixture_base+0x39a5290,game);put(game,fixture_base+0x1c940a8);
        put(game+0x3130b0,fixture_player);put(game+0x3130b0+8,target_owner);put(target_owner+0xa8,spawn);
        put(fixture_camera+0x25200,0);put(fixture_camera+0x25204,0);
        put(fixture_camera+0x251b9,static_cast<unsigned char>(0));put(fixture_camera+0x25260,static_cast<unsigned char>(0));
        put(target_object+0x18,rig::Identity{11,10});put(target_object+0x10,target_owner);
        aim.published_[0]=true;aim.hands_[0]=scope.hand;aim.hands_[0].tick=GetTickCount64();
        aim.target_object_[0]=target_object;aim.target_owner_[0]=target_owner;aim.target_spawn_[0]=spawn;aim.target_generation_[0]=11;aim.target_tick_[0]=GetTickCount64();aim.ready_revision_[0]=revision;
        const auto target=aim.target_identity(GetTickCount64());assert(target && target->object==target_object && target->generation==11 && target->epoch==3 && target->side==0 && target->revision==revision);
        const auto native_guard=aim.qualified_target(GetTickCount64());
        assert(native_guard && native_guard->owner==target_owner && native_guard->spawn==spawn && !native_guard->point);
        put(target_owner+0xf0,model);put(model,fixture_base+0x1b0dd88);
        put(model+0x1840,std::array<float,8>{.7f,-.1f,1.2f,0,.9f,.1f,1.4f,0});
        aim.hands_[0].volume_centre={.65,0,1.25};
        const auto paired=aim.qualified_target(GetTickCount64());
        assert(paired && paired->identity==*target && paired->point && std::abs(paired->point->x-.7)<1e-6);
        put(game+0x3130b0+8,target_owner+8);assert(!aim.qualified_target(GetTickCount64()));put(game+0x3130b0+8,target_owner);
        put(target_owner+0xa8,spawn+1);assert(!aim.has_target(0,GetTickCount64()));put(target_owner+0xa8,spawn);
        put(fixture_camera+0x25200,1);assert(!aim.target_side(GetTickCount64()));put(fixture_camera+0x25200,0);
        put(fixture_camera+0x18,rig::Identity{8,10});assert(!aim.target_identity(GetTickCount64()));put(fixture_camera+0x18,id);
        aim.hands_[0].tick=GetTickCount64()-251;assert(!aim.qualified_target(GetTickCount64()));aim.hands_[0].tick=GetTickCount64();
        put(target_object+0x18,rig::Identity{12,10});assert(!aim.target_identity(GetTickCount64()));
        put(target_object+0x18,rig::Identity{11,10});aim.target_object_[0]=0;assert(!aim.target_identity(GetTickCount64()));
        // Routing flip wipes the live selection: the prompt path keeps the remembered target (no flat fallback),
        // the strict path does not, and the held target expires with its grace.
        const auto flip=GetTickCount64();
        assert(!aim.target_identity(flip) && aim.has_target_or_held(flip) && aim.prompt_identity(flip)==*target);
        assert(aim.prompt_target(flip) && aim.prompt_target(flip)->owner==target_owner);
        assert(!aim.second_target(*target,flip)); // only one hand has a target
        assert(!aim.has_target_or_held(flip+NativeInteractionAim::held_grace_ms+1));
        put(game+0x3130b0+8,target_owner+8);assert(!aim.has_target_or_held(flip));put(game+0x3130b0+8,target_owner);
        // The game's own selected object with no hand target (typewriter): world anchor from entity+0x300.
        const auto n=flip+NativeInteractionAim::held_grace_ms+10;
        aim.native_object_=target_object;aim.native_tick_=n;aim.epoch_hint_=3;put(target_owner+0x300,std::array<float,3>{1,2,3});
        const auto nat=aim.prompt_target(n);
        assert(nat && nat->native_only && nat->owner==target_owner && nat->spawn==spawn && nat->point && nat->point->z==3 && nat->identity.epoch==3 && nat->identity.object==target_object);
        assert(static_cast<bool>(nat->identity) && aim.prompt_identity(n)==nat->identity && !aim.target_identity(n));
        assert(!aim.prompt_target(n+251)); // stale
        aim.epoch_hint_=0;assert(!aim.prompt_target(n) && !aim.prompt_identity(n));aim.epoch_hint_=3;
        put(game+0x3130b0+8,target_owner+8);assert(!aim.prompt_target(n));put(game+0x3130b0+8,target_owner);
        put(target_owner+0x300,std::array<float,3>{1,std::nanf(""),3});assert(!aim.prompt_target(n));
    }
};
}
int main(){d2vr::NativeInteractionRangeFixture::run();std::cout<<"PASS: exact scoped hand eligibility origin, native range/height, permission and LOS rejection, fixture_camera unchanged and owner/freshness fallback\n";}



