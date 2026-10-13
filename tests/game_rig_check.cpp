#include "../src/arm_tracking.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>

#include "native_rig_fixture.hpp"

int main() {
    static_assert(sizeof(r::List)==16 && sizeof(r::Transform)==64);
    auto memory=fixture();const auto arms=r::player_arms(memory,base_address);assert(arms);
    for(unsigned side=0;side<2;++side) {
        auto upload=r::arm_upload(memory,base_address,(*arms)[side],poses[side]+137*64,69);
        assert(upload && upload->first_transform==137 && upload->joints[68].f[12]==68);
        assert(!r::arm_upload(memory,base_address,(*arms)[side],poses[side],69));
        assert(!r::arm_upload(memory,base_address,(*arms)[side],poses[side]+137*64,68));
    }
    const auto original=memory;
    // Relative cooked-declaration remap is not necessarily a dense model list.
    const auto arm=(*arms)[0];const std::uintptr_t hierarchy_blob=0x20c000000;
    memory.put(arm.skeleton,base_address+0x1b0bd08);
    memory.put(base_address+0x1b0bd08+0x88,base_address+0x24f240);
    memory.put(arm.skeleton+0x158,hierarchy_blob);memory.put(hierarchy_blob+0x1c,std::uint32_t{0x500});
    std::array<std::uint16_t,90> record_data{};
    for(unsigned i=0;i<69;++i) {
        const auto record=static_cast<std::uint16_t>(89-i);
        memory.put(hierarchy_blob+0x51c+i*2,record);
        record_data[record]=i?static_cast<std::uint16_t>(0x8000|(i-1)):0xffff;
    }
    for(unsigned i=0;i<90;++i) {memory.put(hierarchy_blob+0x40+i*4,std::uint16_t{0});memory.put(hierarchy_blob+0x42+i*4,record_data[i]);}
    auto hierarchy=r::arm_hierarchy(memory,base_address,arm);assert(hierarchy && (*hierarchy)[0]==-1 && (*hierarchy)[68]==67);
    // Native joint names select semantic roles independently of fixed indices.
    const std::uintptr_t name_pool=0x20d000000,name_ids=0x20e000000,name_text=0x20f000000,inverse_at=0x210000000;
    memory.put(base_address+0x22995c0,base_address+0x22995d0);memory.put(base_address+0x22995d0,base_address+0x1b093c8);
    memory.list(base_address+0x22995d8,name_pool,69,69);memory.put(arm.skeleton+0x70,name_ids);
    memory.put(arm.skeleton+0x120,inverse_at);
    const std::array<unsigned,4> metacarpals{15,26,37,48};
    const std::array<std::string,4> fingers{"index","medium","ring","pinky"};
    for(unsigned joint=0;joint<69;++joint) {
        memory.put(name_ids+joint*2,static_cast<std::uint16_t>(joint));
        const auto text=name_text+joint*128;memory.put(name_pool+joint*0x48+0x10,text);
        std::string name=joint==2?"left_arm_jnt":joint==3?"left_elbow_jnt":joint==4?"left_wrist_jnt":"helper_"+std::to_string(joint);
        std::array<float,12> inverse{1,0,0,0,0,1,0,0,0,0,1,0};
        if(joint>=64 && joint<=67) {
            name=joint==67?"left_wrist_rotation_jnt":"left_lower_armhelper"+std::to_string(joint-63)+"_jnt";
            memory.put(hierarchy_blob+0x42+(89-joint)*4,std::uint16_t{0x8003});
        }
        if(joint>=5 && joint<=7) {
            name="left_thumb_0"+std::to_string(joint-4)+"_jnt";
            if(joint==5) memory.put(hierarchy_blob+0x42+(89-joint)*4,std::uint16_t{0x8004});
        }
        for(unsigned finger=0;finger<4;++finger) {
            if(joint==metacarpals[finger]) {
                name="left_"+fingers[finger]+"_01_jnt";
                // Metacarpal roots lead to distinct MCP knuckles (_02).
                // Named palm fitting requires real width and finger direction.
                inverse[3]=-.075f;inverse[11]=-.03f+.02f*finger;
                memory.put(hierarchy_blob+0x42+(89-joint)*4,std::uint16_t{0x8004});
            }
            if(joint>metacarpals[finger] && joint<=metacarpals[finger]+3) {
                name="left_"+fingers[finger]+"_0"+std::to_string(joint-metacarpals[finger]+1)+"_jnt";
                inverse[3]=-.1f-.02f*(joint-metacarpals[finger]-1);inverse[11]=-.03f+.02f*finger;
            }
        }
        memory.put(inverse_at+joint*48,inverse);
        for(std::size_t i=0;i<128;++i) memory.put(text+i,static_cast<unsigned char>(0));
        for(std::size_t i=0;i<=name.size();++i) memory.put(text+i,static_cast<unsigned char>(name[i]));
    }
    auto binding=r::arm_binding(memory,base_address,arm);assert(binding && binding->chain.upper==2 && binding->chain.wrist==4);
    assert(r::valid(binding->bind_grip) && d2vr::length(r::position(binding->bind_grip)-r::position(binding->bind_wrist))>.04);
    memory.put(arm.skeleton+0x120,std::uintptr_t{0});assert(!r::arm_binding(memory,base_address,arm));
    memory.put(arm.skeleton+0x120,inverse_at);
    memory.put(inverse_at+16*48,std::numeric_limits<float>::quiet_NaN());assert(!r::arm_binding(memory,base_address,arm));
    memory.put(inverse_at+16*48,1.f);
    memory.put(name_ids+2,std::uint16_t{2});memory.put(name_ids+4,std::uint16_t{3});memory.put(name_ids+6,std::uint16_t{4});memory.put(name_ids+8,std::uint16_t{1});
    memory.put(inverse_at+3*48+3*4,-.02f); // The newly named wrist has its own bind origin.
    for(auto joint:metacarpals) memory.put(hierarchy_blob+0x42+(89-joint)*4,std::uint16_t{0x8003});
    for(unsigned joint=64;joint<=67;++joint) memory.put(hierarchy_blob+0x42+(89-joint)*4,std::uint16_t{0x8002});
    // Moving the named wrist must also move the thumb root; an old-wrist
    // thumb is a mixed rig, even if the other four digits were reparented.
    assert(!r::arm_binding(memory,base_address,arm));
    memory.put(hierarchy_blob+0x42+(89-5)*4,std::uint16_t{0x8003});
    binding=r::arm_binding(memory,base_address,arm);assert(binding && binding->chain.upper==1 && binding->chain.elbow==2 && binding->chain.wrist==3);
    assert(std::abs(r::position(binding->bind_wrist).x-.02)<1e-6 && r::valid(binding->bind_grip));
    memory.put(name_ids+8,std::uint16_t{4});assert(!r::arm_binding(memory,base_address,arm));
    memory.put(hierarchy_blob+0x42+88*4,std::uint16_t{0x8002});assert(!r::arm_hierarchy(memory,base_address,arm));
    memory.put(hierarchy_blob+0x42+88*4,std::uint16_t{0x8000});
    memory.put(hierarchy_blob+0x51c,std::uint16_t{2048});assert(!r::arm_hierarchy(memory,base_address,arm));
    memory=original;
    memory.put(body_address+0x10,player_address+8);assert(!r::player_arms(memory,base_address));memory=original;
    memory.put(animations[0]+0x18,r::Identity{-1,10});assert(!r::player_arms(memory,base_address));memory=original;
    memory.put(animations[0]+0x18,r::Identity{300,10});
    assert(!r::arm_upload(memory,base_address,(*arms)[0],poses[0]+137*64,69));memory=original;
    memory.put(models[0],base_address+0x1b0d630);assert(!r::player_arms(memory,base_address));memory=original;
    memory.list(player_address+0x268,components_address,2,1);assert(!r::player_arms(memory,base_address));memory=original;
    memory.put(maps[0]+10*2,std::int16_t{148});
    assert(!r::arm_upload(memory,base_address,(*arms)[0],poses[0]+137*64,69));memory=original;
    memory.list(animations[0]+0x2e8,states_address[0],3,4);
    assert(!r::arm_upload(memory,base_address,(*arms)[0],poses[0]+137*64,69));memory=original;
    memory.put(poses[0]+137*64,2.f);
    assert(!r::arm_upload(memory,base_address,(*arms)[0],poses[0]+137*64,69));memory=original;
    memory.put(poses[0]+137*64+12*4,std::numeric_limits<float>::infinity());
    assert(!r::arm_upload(memory,base_address,(*arms)[0],poses[0]+137*64,69));memory=original;
    memory.recycle_at=animations[0]+0x18;assert(!r::player_arms(memory,base_address));
    std::cout<<"PASS: owned dual-arm discovery, state-3 upload source, recycled identity, bounds and rigid transforms\n";
}
