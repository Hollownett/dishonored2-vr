#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_interaction_aim.hpp"
#include <cassert>
#include <map>
#include <iostream>
#include <memory>
namespace {
constexpr std::uintptr_t fixture_base=0x140000000,fixture_game=0x2000000,fixture_player=0x2100000,fixture_camera=0x2200000,
    fixture_body=0x2300000,fixture_controller=0x2400000,fixture_physics=0x2500000,fixture_delegate=0x2600000,
    fixture_entity=0x2700000,fixture_component=0x2800000,fixture_entries=0x2900000,fixture_world=0x3000000,fixture_pool=0x3100000,fixture_model=0x3200000;
std::map<std::uintptr_t,unsigned char> fixture_bytes;
template<class T>void put(std::uintptr_t at,const T& value) {
    const auto* source=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t i=0;i<sizeof(value);++i)fixture_bytes[at+i]=source[i];
}
bool read(std::uintptr_t at,void* out,std::size_t size) {
    for(std::size_t i=0;i<size;++i) {const auto it=fixture_bytes.find(at+i);if(it==fixture_bytes.end())return false;static_cast<unsigned char*>(out)[i]=it->second;}
    return true;
}
UINT logic() {return 2;}
void fixture() {
    using namespace d2vr;fixture_bytes.clear();
    put(fixture_base+0x39a5290,fixture_game);put(fixture_game,fixture_base+0x1c940a8);put(fixture_game+0x3130b0,fixture_player);put(fixture_player,fixture_base+0x1c5e428);
    put(fixture_camera+0x25150,std::array<std::byte,0x200>{});
    for(auto [at,table]:std::array<std::pair<std::uintptr_t,std::uintptr_t>,9>{{
        {fixture_camera,0x1c72218},{fixture_camera+0x20,0x1c722f8},{fixture_body,0x1c5a8d8},{fixture_body+0x20,0x1c5a9b0},
        {fixture_controller,0x1c72918},{fixture_physics,0x1c73268},{fixture_physics+0x20,0x1c73340},{fixture_physics+0x30,0x1c73378},{fixture_delegate+0x160,0x1b02768}}})put(at,fixture_base+table);
    for(auto at:{fixture_camera,fixture_body,fixture_controller,fixture_physics}) {put(at+0x10,fixture_player);put(at+0x18,rig::Identity{5,10});}
    put(fixture_camera+0x25158,fixture_controller);put(fixture_camera+0x25160,fixture_physics);put(fixture_camera+0x25170,fixture_body);
    put(fixture_physics+0x48,fixture_delegate);put(fixture_delegate+0x168,fixture_physics);put(fixture_delegate+0x120,fixture_player+0x300);put(fixture_delegate+0x1e0,fixture_delegate);
    put(fixture_delegate+0xd0,0.f);put(fixture_delegate+0xbc,0.f);put(fixture_delegate+0x40,0.f);
    put(fixture_controller+0xd0,std::array<float,2>{});put(fixture_player+0x30c,std::array<float,9>{1,0,0,0,1,0,0,0,1});
    put(fixture_game+0x3130b8,fixture_entity);put(fixture_entity,fixture_base+0x1c886a8);put(fixture_entity+0xa8,std::uint32_t{0x10001});
    put(fixture_entity+0x300,std::array<float,3>{.1f,0,0});put(fixture_entity+0x268,rig::List{fixture_entries,1,1<<8});put(fixture_entries,fixture_component);
    put(fixture_entity+0xf0,fixture_model);put(fixture_model,fixture_base+0x1b0dd88);put(fixture_component,fixture_base+0x1c745c8);
    put(fixture_component+0x10,fixture_entity);put(fixture_component+0x18,rig::Identity{7,10});put(fixture_component+0xe8,fixture_base+0x1c74798);
    constexpr std::uint32_t handle=0x1000002;put(fixture_component+0xf8,handle);put(fixture_component+0xc8,std::uintptr_t{});put(fixture_component+0xd2,static_cast<unsigned char>(0));
    put(fixture_game+0x60410,fixture_world);put(fixture_world+0x28,fixture_pool);put(fixture_world+0x30,std::uint32_t{3});
    rig::Transform transform{};transform.f[0]=transform.f[5]=transform.f[10]=transform.f[15]=1;transform.f[12]=.1f;
    const auto record=fixture_pool+2*0xb0;put(record,transform);put(record+0x44,std::uint32_t{130});put(record+0x40,std::uint32_t{1});put(record+0x70,handle);put(record+0x74,handle);
}
}

namespace {
constexpr std::uintptr_t fixture_sensor=0x3300000,fixture_machine=0x3400000,fixture_interaction=0x3500000;
std::array<float,3> fixture_point{.2f,.3f,.4f};unsigned fixture_calls{},fixture_mode{};
std::uintptr_t __fastcall native_los(void*,void*,const void*,const void*,const void*,unsigned,unsigned char* output) {
    ++fixture_calls;*output=1;
    if(fixture_mode==2)put(fixture_entity+0xa8,unsigned{0x10002});
    if(fixture_mode==3)fixture_point[0]=.9f;
    if(fixture_mode==4)put(fixture_sensor+0x18,d2vr::rig::Identity{8,10});
    if(fixture_mode==5)put(fixture_camera+0x25200,1);
    if(fixture_mode==15)put(fixture_camera+0x18,d2vr::rig::Identity{6,10});
    if(fixture_mode==16)put(fixture_game+0x60410,fixture_world+0x10000);
    return fixture_mode==1?0xfedcba9876543201ull:0xfedcba9876543200ull;
}
bool anchor_read(std::uintptr_t at,void* output,std::size_t size) {
    if(at==reinterpret_cast<std::uintptr_t>(fixture_point.data()) && size==sizeof(fixture_point)) {std::memcpy(output,fixture_point.data(),size);return true;}
    return read(at,output,size);
}
}
namespace d2vr {
struct NativeInteractionRangeFixture {
    static void run() {
        for(unsigned mode=0;mode<22;++mode) {
            fixture();fixture_point={.2f,.3f,.4f};fixture_calls=0;fixture_mode=mode;
            put(fixture_sensor,fixture_base+0x1b403b8);put(fixture_sensor+0x18,rig::Identity{7,10});put(fixture_sensor+0x10,fixture_player);
            put(fixture_sensor+0x40,fixture_camera);put(fixture_sensor+0x38,static_cast<unsigned char>(1));
            put(fixture_sensor+0x20,fixture_machine);put(fixture_machine,fixture_base+0x1c73d58);put(fixture_machine+0x318,fixture_interaction);
            put(fixture_interaction,fixture_base+0x1c72e98);put(fixture_interaction+0x10,fixture_player);
            put(fixture_interaction+0x30,7);put(fixture_interaction+0x38,fixture_component);put(fixture_interaction+0x40,static_cast<unsigned char>(0));
            put(fixture_sensor+0x84,std::array<unsigned,3>{0x10001,0x3fff0010,0x3fff0010});put(fixture_sensor+0x98,std::uintptr_t{});
            // Nonstatic native Use owner: no static-mesh bounds can supply an anchor.
            put(fixture_model,fixture_base+0x1b00000);
            if(mode==14) {put(fixture_model,fixture_base+0x1b0dd88);put(fixture_model+0x1840,std::array<float,8>{1,1,1,0,1.1f,1.1f,1.1f,0});}
            auto storage=std::make_unique<NativeInteractionAim>();auto& aim=*storage;
            aim.base_=fixture_base;aim.read_=anchor_read;aim.logic_=logic;aim.route_=true;aim.active_=true;aim.occlusion_=native_los;
            const auto camera=*native_player_camera(anchor_read,fixture_base,fixture_camera);const auto now=GetTickCount64();
            WorldTrackedPose hand{{},{{{1,0,0},{0,1,0},{0,0,1}}}};aim.publish(camera,hand,1,now,true,0);
            NativeInteractionAim::Context scope{camera,aim.hands_[0],0,0,fixture_sensor,aim.routing_.revision()};
            scope.sensor_identity={7,10};
            aim.queued_revision_[0]=scope.revision;NativeInteractionAim::context_=&scope;
            if(mode==6)scope.hand.tick=now-251;
            const auto caller=mode==7?fixture_base+0x3b8b47:fixture_base+(mode==12?0x3b8b05:0x3b87b5);
            unsigned char continuation{};std::array<float,9> axes{};
            const auto result=aim.occlusion_result(reinterpret_cast<void*>(fixture_sensor),reinterpret_cast<void*>(fixture_entity),
                fixture_point.data(),axes.data(),fixture_point.data(),mode==8?1u:0u,&continuation,caller);
            assert(fixture_calls==1 && continuation==1 && result==(mode==1?0xfedcba9876543201ull:0xfedcba9876543200ull));
            if(mode==13) {fixture_point[0]=.6f;aim.occlusion_result(reinterpret_cast<void*>(fixture_sensor),reinterpret_cast<void*>(fixture_entity),
                fixture_point.data(),axes.data(),fixture_point.data(),0,&continuation,fixture_base+0x3b8b05);}
            if(mode==17)aim.publish(camera,hand,2,now,true,0);
            if(mode==9)put(fixture_component+0x10,fixture_player); // selection belongs to another owner
            if(mode==10)aim.unlock(),aim.lock(0); // routing changed before promotion
            aim.target(scope);
            const auto point=aim.target_point_locked(0,mode==11?now+251:GetTickCount64());
            if(mode==20 || mode==21) {
                aim.publish(camera,hand,mode==21?2u:1u,now+100,true,0);
                assert(!aim.qualified_target(mode==20?now+1:now+100));
            }else if(mode==18 || mode==19) {
                const auto frozen_query_tick=aim.contact_anchor_[0].tick;
                hand.position.x=.1;
                aim.publish(camera,hand,1,mode==18?now+1:now-1,true,0);
                const auto qualified=aim.qualified_target(now+1);
                if(mode==18) {
                    assert(qualified && qualified->point && std::abs(qualified->point->x-.2)<1e-6);
                    assert(aim.contact_anchor_[0].tick==frozen_query_tick && aim.contact_anchor_[0].publication_tick==now);
                    aim.publish(camera,hand,1,now+100,true,0);
                    assert(aim.qualified_target(now+100)->point);
                    assert(aim.contact_anchor_[0].tick==frozen_query_tick);
                    aim.publish(camera,hand,1,now+251,true,0);
                    assert(!aim.qualified_target(now+251)); // publications do not refresh native selection/query age
                }else assert(!qualified || !qualified->point);
            }else if(mode==0 || mode==12) {
                assert(point && std::abs(point->x-.2)<1e-6 && std::abs(point->y-.3)<1e-6);
                put(fixture_entity+0xa8,unsigned{0x10002});assert(!aim.target_point_locked(0,GetTickCount64()));
            }else if(mode==14)assert(point && point->x==1 && point->y==1 && point->z==1);
            else assert(!point);
            NativeInteractionAim::context_=nullptr;
        }
    }
};
}
int main(){d2vr::NativeInteractionRangeFixture::run();std::cout<<"PASS: real native clear LOS anchor promotion for both verified callers; raw ABI preserved; blocked, mutated, recycled, foreign/range, wrong-owner, routing, stale and authored paths rejected\n";}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*) {return MH_OK;}
extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_OK;}
extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_OK;}
