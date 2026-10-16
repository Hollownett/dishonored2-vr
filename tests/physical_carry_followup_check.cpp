#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_grab.hpp"
#include <cassert>
#include <map>
#include <iostream>
namespace {
constexpr std::uintptr_t fixture_base=0x140000000,fixture_game=0x2000000,fixture_player=0x2100000,fixture_camera=0x2200000,
    fixture_body=0x2300000,fixture_controller=0x2400000,fixture_physics=0x2500000,fixture_delegate=0x2600000,
    fixture_entity=0x2700000,fixture_component=0x2800000,fixture_entries=0x2900000,fixture_world=0x3000000,fixture_pool=0x3100000,fixture_model=0x3200000;
std::map<std::uintptr_t,unsigned char> fixture_bytes;
template<class T>void put(std::uintptr_t at,const T& value) {
    const auto* source=reinterpret_cast<const unsigned char*>(&value);
    for(std::size_t byte_index=0;byte_index<sizeof(value);++byte_index)fixture_bytes[at+byte_index]=source[byte_index];
}
bool read(std::uintptr_t at,void* out,std::size_t size) {
    for(std::size_t byte_index=0;byte_index<size;++byte_index) {const auto it=fixture_bytes.find(at+byte_index);if(it==fixture_bytes.end())return false;static_cast<unsigned char*>(out)[byte_index]=it->second;}
    return true;
}
std::array<float,3> last_velocity{}; unsigned fixture_velocity_writes{};
void __fastcall fixture_velocity(void*,UINT,const float* linear,const float*,int) {++fixture_velocity_writes;std::copy(linear,linear+3,last_velocity.begin());}
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
namespace d2vr {
struct NativeGrabCheck {
    static void setup(NativeGrab& grab) {grab.active_=true;grab.base_=fixture_base;grab.read_=read;grab.logic_=logic;grab.velocity_=fixture_velocity;}
    static void consume(NativeGrab& grab,UINT64 now) {std::lock_guard<std::mutex> lock(grab.mutex_);grab.update_lane(0,fixture_world,now);}
};
}
void set_pose(float x) {
    d2vr::rig::Transform transform{};transform.f[0]=transform.f[5]=transform.f[10]=transform.f[15]=1;transform.f[12]=x;
    put(fixture_pool+2*0xb0,transform);
    put(fixture_model+0x1860,std::array<float,3>{x,0,0});
    put(fixture_model+0x1840,std::array<float,8>{x-.05f,-.05f,-.05f,0,x+.05f,.05f,.05f,0});
}
int main(int argc,char** argv) {
    using namespace d2vr;assert(argc==2);fixture();set_pose(.1f);fixture_velocity_writes=0;
    const auto owner=*native_player_camera(read,fixture_base,fixture_camera);
    WorldTrackedPose hand{{},{{{1,0,0},{0,1,0},{0,0,1}}}};
    NativeGrab grab;NativeGrabCheck::setup(grab);
    grab.publish(0,owner,hand,0,1,1000,true,false,1000);grab.filter_native_grip(0,0,1,1000,true,true,false);
    grab.publish(0,owner,hand,.9f,1,1010,true,false,1010);grab.filter_native_grip(0,.9f,1,1010,true,true,false);
    NativeGrabCheck::consume(grab,1010);assert(grab.holding(0));
    const std::string mode=argv[1];
    if(mode=="geometry") {
        set_pose(.4f);NativeGrabCheck::consume(grab,1020);
        const auto current=grab.held(0);assert(current && current->bounds);
        std::cout<<"held_x="<<current->pose.position.x<<" bounds_min_x="<<current->bounds->minimum.x<<std::endl;
        assert(std::abs(current->pose.position.x-.4)<1e-6);
        assert(std::abs(current->bounds->minimum.x-.35)<1e-6);
    } else {
        hand.position.x=.12;set_pose(.22f);
        grab.publish(0,owner,hand,.9f,1,1030,true,false,1030);NativeGrabCheck::consume(grab,1030);
        const auto nominal=last_velocity[0];assert(nominal>2.5f && nominal<2.7f);
        if(mode=="fresh") {
            for(const UINT64 now:{1030ull,1040ull,1050ull,1062ull}) {NativeGrabCheck::consume(grab,now);assert(last_velocity[0]==nominal);}
        } else if(mode=="stale") {
            for(UINT64 now=1040;now<=1230;now+=10)NativeGrabCheck::consume(grab,now);
            std::cout<<"stalled200_ms_speed="<<last_velocity[0]<<std::endl;
            assert(std::abs(last_velocity[0])<.03f);
        } else if(mode=="source") {
            grab.publish(0,owner,hand,.9f,1,1040,true,false,940);NativeGrabCheck::consume(grab,1040);
            std::cout<<"source100_ms_speed="<<last_velocity[0]<<std::endl;
            assert(std::abs(last_velocity[0])<.4f);
        } else if(mode=="throw") {
            NativeGrabCheck::consume(grab,1040);
            grab.publish(0,owner,hand,0,1,1050,true,false,1050);grab.filter_native_grip(0,0,1,1050,true,true,false);
            NativeGrabCheck::consume(grab,1050);assert(!grab.holding(0));
            const double expected=nominal*std::exp(-.02/.035);
            assert(std::abs(last_velocity[0]-expected)<1e-5);
        } else assert(false);
    }
    assert(fixture_velocity_writes);std::cout<<"PASS physical carry "<<mode<<"\n";
}
