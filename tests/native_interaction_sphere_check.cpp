#define NOMINMAX
#include "../src/native_interaction_aim.hpp"
#include <cassert>
#include <iostream>
#include <map>
#include <stdexcept>
namespace {
constexpr std::uintptr_t image=0x140000000,sensor=0x200000,camera=0x300000,player=0x400000,
    component=0x500000,owner=0x600000,game=0x700000,model=0x800000,owner_table=0x900000;
std::map<std::uintptr_t,std::byte> fixture_memory;
std::array<float,4> native_eye{0,0,1.7f,0};
template<class T> void put(std::uintptr_t at,const T& value) {
    const auto* bytes=reinterpret_cast<const std::byte*>(&value);
    for(std::size_t i=0;i<sizeof(T);++i)fixture_memory[at+i]=bytes[i];
}
bool read_memory(std::uintptr_t at,void* output,std::size_t size) {
    if(at==reinterpret_cast<std::uintptr_t>(native_eye.data()) && size<=sizeof(native_eye)) {
        std::memcpy(output,native_eye.data(),size);return true;
    }
    for(std::size_t i=0;i<size;++i) {
        const auto found=fixture_memory.find(at+i);if(found==fixture_memory.end())return false;
        static_cast<std::byte*>(output)[i]=found->second;
    }
    return true;
}
}
namespace d2vr {
struct NativeInteractionSphereFixture {
    enum class Mode {permission_refused,geometry_refused,wrong_getter,wrong_camera,revoke,revoke_owner,unpublish,move_hand,recycle,recycle_spawn,nested,throw_native,seh_native,seh_after_marker};
    inline static NativeInteractionAim* current{};
    inline static Mode mode=Mode::geometry_refused;
    inline static unsigned native_calls{},getter_calls{};
    inline static unsigned los_calls{};
    inline static bool los_blocked{};inline static unsigned char los_output{};
    inline static bool in_nested{};
    inline static const void* descriptor_expected{};
    inline static const float* point_expected{};
    static constexpr std::uintptr_t raw=0x87654300;
    static const float* __fastcall native_range(void*) {++getter_calls;return native_eye.data();}
    static std::uintptr_t __fastcall native_los(void* passed_sensor,void* entity,const void*,const void*,const void* point,unsigned range,unsigned char* output) {
        ++los_calls;
        assert(passed_sensor==reinterpret_cast<void*>(sensor) && entity==reinterpret_cast<void*>(owner) && point==point_expected && range==0);
        if(output)*output=los_output;
        return raw|(los_blocked?1u:0u);
    }
    static std::uintptr_t __fastcall native_eligibility(void* passed_sensor,const void* descriptor,void* passed_component,const float* point) {
        ++native_calls;
        assert(passed_sensor==reinterpret_cast<void*>(sensor) && descriptor==descriptor_expected &&
            passed_component==reinterpret_cast<void*>(component) && point==point_expected);
        if(mode==Mode::throw_native)throw std::runtime_error("native fixture failure");
        if(mode==Mode::seh_native) {RaiseException(0xe1375350,EXCEPTION_NONCONTINUABLE,0,nullptr);return raw;}
        if(mode==Mode::permission_refused)return raw; // no getter: native semantic refusal
        if(mode==Mode::nested && !in_nested) {
            auto* parent=NativeInteractionAim::sphere_eligibility_;assert(parent && !parent->semantic_passed);
            in_nested=true;
            assert(current->eligibility_result(passed_sensor,descriptor,passed_component,point,image+0x1234)==raw);
            in_nested=false;
            assert(NativeInteractionAim::sphere_eligibility_==parent && !parent->semantic_passed);
            return raw;
        }
        if(in_nested)assert(!NativeInteractionAim::sphere_eligibility_);
        current->range_origin(reinterpret_cast<void*>(mode==Mode::wrong_camera?camera+8:camera),
            image+(mode==Mode::wrong_getter?0x3b81d9:0x3b81d8));
        if(mode==Mode::seh_after_marker) {
            assert(NativeInteractionAim::sphere_eligibility_ && NativeInteractionAim::sphere_eligibility_->semantic_passed);
            RaiseException(0xe1375350,EXCEPTION_NONCONTINUABLE,0,nullptr);return raw;
        }
        if(mode==Mode::revoke)current->routing_.reset();
        if(mode==Mode::revoke_owner)current->routing_.invalidate_owner();
        if(mode==Mode::unpublish)current->published_[0]=false;
        if(mode==Mode::move_hand)current->hands_[0].volume_centre={3,0,1.25};
        if(mode==Mode::recycle)put(component+0x18,rig::Identity{12,10});
        if(mode==Mode::recycle_spawn)put(owner+0xa8,0x10002u); // same registry index and component identity, new native spawn
        return raw; // native geometry rejected; only a qualified semantic marker can change AL
    }
    static bool invoke_seh(NativeInteractionAim& aim) {
        __try {
            aim.eligibility_result(reinterpret_cast<void*>(sensor),descriptor_expected,reinterpret_cast<void*>(component),point_expected,image+0x3b8a10);
        } __except(GetExceptionCode()==0xe1375350?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
        return false;
    }
    static void prepare(NativeInteractionAim& aim,NativeInteractionAim::Context& scope) {
        fixture_memory.clear();current=&aim;mode=Mode::geometry_refused;native_calls=getter_calls=0;in_nested=false;
        aim.base_=image;aim.read_=read_memory;aim.route_=true;aim.active_=true;
        aim.range_origin_=native_range;aim.eligibility_=native_eligibility;
        put(sensor,image+0x1b403b8);put(sensor+0x40,camera);put(sensor+0x10,player);
        put(camera,image+camera_vtable);put(camera+0x20,image+camera_secondary_vtable);
        const rig::Identity camera_identity{7,10};put(camera+0x18,camera_identity);put(camera+0x10,player);
        for(const unsigned offset:{0x25200u,0x25204u})put(camera+offset,0);
        for(const unsigned offset:{0x251b9u,0x25260u})put(camera+offset,static_cast<unsigned char>(0));
        put(image+0x39a5290,game);put(game+0x3130b0,player);put(game+0x3130b8,owner);put(owner+0xa8,0x10001u);
        put(component+0x10,owner);put(component+0x18,rig::Identity{11,10});
        put(component,image+0x1c5bfc8);
        if(descriptor_expected)put(reinterpret_cast<std::uintptr_t>(descriptor_expected),std::array<unsigned char,40>{});
        put(owner+0xf0,model);put(model,image+0x1b0dd88);
        put(model+0x1840,std::array<float,8>{.7f,-.1f,1.2f,0,.9f,.1f,1.4f,0});
        // A generic noncharacter interaction owner: no invented book RTTI or pickup component.
        put(owner,owner_table);put(owner_table,image+0x100000);
        const std::int32_t displacement=static_cast<std::int32_t>(0x3901000-0x100007);
        std::array<unsigned char,8> getter{0x48,0x8d,0x05,0,0,0,0,0xc3};
        std::memcpy(getter.data()+3,&displacement,4);put(image+0x100000,getter);
        put(image+0x3901000+0x60,50);put(image+0x39ce2c0,100);put(image+0x39ce2c4,200);
        aim.routing_.reset();aim.routing_.next({true,false},GetTickCount64());
        scope={};scope.camera.player=player;scope.camera.camera=camera;scope.camera.camera_identity=camera_identity;
        scope.hand={GetTickCount64(),3,player,camera,7,{},true,{.65,0,1.25}};
        scope.side=0;scope.stage=0;scope.sensor=sensor;scope.revision=aim.routing_.revision();
        scope.ownership_revision=aim.routing_.ownership_revision();
        aim.queued_revision_[0]=scope.revision;aim.published_[0]=true;aim.hands_[0]=scope.hand;
        NativeInteractionAim::context_=&scope;NativeInteractionAim::sphere_eligibility_=nullptr;
    }
    static void prepare_hover(NativeInteractionAim& aim,NativeInteractionAim::Context& scope) {
        prepare(aim,scope);
        constexpr std::uintptr_t machine=0xa00000,interaction=0xb00000;
        put(sensor+0x20,machine);put(machine,image+0x1c73d58);put(machine+0x318,interaction);
        put(interaction,image+0x1c72e98);put(interaction+0x10,player);
        put(interaction+0x30,-1);put(interaction+0x38,std::uintptr_t{});put(interaction+0x40,static_cast<unsigned char>(0));
        put(sensor+0x84,std::array<unsigned,3>{0x10001u,0x3fff0010u,0x3fff0010u});
        put(sensor+0x98,std::uintptr_t{});put(sensor+0xa8,reinterpret_cast<std::uintptr_t>(descriptor_expected));put(sensor+0xb0,std::uintptr_t{});
        aim.logic_=+[]()->UINT{return 2;};
    }
    static void run() {
        NativeInteractionAim aim;NativeInteractionAim::Context scope{};
        const std::array<unsigned char,40> descriptor{};const std::array<float,3> point{.8f,0,1.25f};
        descriptor_expected=descriptor.data();point_expected=point.data();
        const auto invoke=[&](std::uintptr_t caller=image+0x3b8a10) {
            const auto before=native_calls;
            const auto result=aim.eligibility_result(reinterpret_cast<void*>(sensor),descriptor.data(),reinterpret_cast<void*>(component),point.data(),caller);
            assert(native_calls==before+1 && !NativeInteractionAim::sphere_eligibility_);return result;
        };
        prepare(aim,scope);mode=Mode::permission_refused;assert(invoke()==raw && getter_calls==0);
        assert(scope.hover_count==0 && scope.eligibility.semantic_refused==1);
        prepare(aim,scope);assert(invoke()==(raw|1) && getter_calls==1);
        prepare(aim,scope);assert(invoke(image+0x3b8a35)==(raw|1));
        for(const auto rejected:{Mode::wrong_getter,Mode::wrong_camera,Mode::revoke,Mode::revoke_owner,Mode::unpublish,Mode::move_hand,Mode::recycle,Mode::recycle_spawn}) {
            prepare(aim,scope);mode=rejected;assert(invoke()==raw);
        }
        prepare(aim,scope);assert(invoke(image+0x1234)==raw);
        prepare(aim,scope);scope.hand.volume_centre={.44,0,1.25};aim.hands_[0]=scope.hand;assert(invoke()==raw); // >25 cm
        prepare(aim,scope);put(image+0x3901000+0x60,150);assert(invoke()==raw); // character
        prepare(aim,scope);scope.hand.tick=GetTickCount64()-251;aim.hands_[0]=scope.hand;assert(invoke()==raw);
        prepare(aim,scope);put(camera+0x18,rig::Identity{8,10});assert(invoke()==raw);
        prepare(aim,scope);put(game+0x3130b8,owner+8);assert(invoke()==raw);
        prepare(aim,scope);scope.hand.reach=true;aim.hands_[0]=scope.hand;assert(invoke()==raw);
        prepare(aim,scope);mode=Mode::nested;
        assert(aim.eligibility_result(reinterpret_cast<void*>(sensor),descriptor.data(),reinterpret_cast<void*>(component),point.data(),image+0x3b8a10)==raw);
        assert(native_calls==2 && getter_calls==1 && !NativeInteractionAim::sphere_eligibility_);
        prepare(aim,scope);mode=Mode::throw_native;bool threw=false;
        try {invoke();}catch(const std::runtime_error&) {threw=true;}
        assert(threw && native_calls==1 && !NativeInteractionAim::sphere_eligibility_);
        for(const auto fault:{Mode::seh_native,Mode::seh_after_marker})for(const bool nested_parent:{false,true}) {
            prepare(aim,scope);mode=fault;
            NativeInteractionAim::SphereEligibility parent{};
            auto* expected_parent=nested_parent?&parent:nullptr;
            NativeInteractionAim::sphere_eligibility_=expected_parent;
            assert(invoke_seh(aim) && native_calls==1 && NativeInteractionAim::sphere_eligibility_==expected_parent);
            assert(!parent.semantic_passed && getter_calls==(fault==Mode::seh_after_marker?1u:0u));
            NativeInteractionAim::sphere_eligibility_=nullptr;
        }
        // Open-hand preview uses the final native scanner's exact surviving
        // action, even while the native FSM has no active interaction component.
        prepare_hover(aim,scope);assert(aim.routing_.locked()==-1 && invoke()==(raw|1));
        assert(scope.hover_count==1 && scope.eligibility.accepted==1);
        aim.target(scope);
        assert(aim.has_target(0,GetTickCount64()) && aim.grip_use_target(0,GetTickCount64()));
        const auto hover=aim.qualified_target(GetTickCount64());
        assert(hover && hover->owner==owner && hover->identity.object==component && hover->identity.generation==11);
        assert(aim.routing_.locked()==-1); // no grip lock, FSM write or synthetic Use is required
        for(unsigned refusal=0;refusal<7;++refusal) {
            prepare_hover(aim,scope);assert(invoke()==(raw|1));
            switch(refusal) {
                case 0:put(sensor+0x84,0x20001u);break; // final scan selected another owner / LOS rejected this owner
                case 1:put(sensor+0xa8,std::uintptr_t{});break; // native discarded the descriptor
                case 2:put(reinterpret_cast<std::uintptr_t>(descriptor_expected)+0x1c,static_cast<unsigned char>(1));break;
                case 3:put(component+0x18,rig::Identity{12,10});break;
                case 4:scope.hand.tick=GetTickCount64()-251;break;
                case 5:scope.hover_overflow=true;break;
                case 6: {
                    constexpr auto second=component+0x10000;
                    auto ambiguous=scope.hover_offers[0];ambiguous.component=second;ambiguous.identity={12,10};
                    put(second+0x10,owner);put(second+0x18,ambiguous.identity);
                    scope.hover_offers[scope.hover_count++]=ambiguous;break;
                }
            }
            aim.target(scope);assert(!aim.has_target(0,GetTickCount64()));
        }
        prepare_hover(aim,scope);mode=Mode::permission_refused;assert(invoke()==raw);
        aim.target(scope);assert(!aim.has_target(0,GetTickCount64()) && scope.hover_count==0);
        // Engine-selected generic readables use the same sphere, then lose permission with their native identity.
        prepare(aim,scope);aim.target_object_[0]=component;aim.target_owner_[0]=owner;aim.target_spawn_[0]=0x10001;
        aim.target_generation_[0]=11;aim.target_tick_[0]=GetTickCount64();aim.ready_revision_[0]=scope.revision;
        assert(!aim.takeable(owner) && aim.grip_use_target(0,GetTickCount64()));
        const auto prompt_tick=GetTickCount64();
        const auto exact_prompt=aim.prompt_identity_for_owner(owner,0x10001,prompt_tick);
        assert(exact_prompt && exact_prompt->object==component && exact_prompt->generation==11);
        assert(!aim.prompt_identity_for_owner(owner+8,0x10001,prompt_tick));
        assert(!aim.prompt_identity_for_owner(owner,0x10002,prompt_tick));
        // A retained entry may join its own other-hand receipt while this hand hovers an unrelated owner.
        constexpr std::uintptr_t unrelated_owner=owner+0x10000,unrelated_component=component+0x10000;
        aim.published_[1]=true;aim.hands_[1]=scope.hand;
        aim.seen_[1]=aim.seen_[0];aim.seen_[1].target.identity.side=1;
        put(unrelated_owner+0xa8,0x20001u);put(game+0x3130b0+16,unrelated_owner);
        put(unrelated_component+0x10,unrelated_owner);put(unrelated_component+0x18,rig::Identity{21,10});
        aim.target_object_[0]=unrelated_component;aim.target_owner_[0]=unrelated_owner;
        aim.target_generation_[0]=21;aim.target_spawn_[0]=0x20001;
        const auto retained_prompt=aim.prompt_identity_for_owner(owner,0x10001,prompt_tick);
        assert(retained_prompt && retained_prompt->object==component && retained_prompt->side==1);
        const auto unrelated_prompt=aim.prompt_identity_for_owner(unrelated_owner,0x20001,prompt_tick);
        assert(unrelated_prompt && unrelated_prompt->object==unrelated_component && unrelated_prompt->side==0);
        put(component+0x10,unrelated_owner);assert(!aim.prompt_identity_for_owner(owner,0x10001,prompt_tick));
        put(component+0x10,owner);put(owner+0xa8,0x10002u);assert(!aim.prompt_identity_for_owner(owner,0x10001,prompt_tick));
        put(owner+0xa8,0x10001u);put(game+0x3130b8,owner+8);assert(!aim.prompt_identity_for_owner(owner,0x10001,prompt_tick));
        put(game+0x3130b8,owner);put(component+0x18,rig::Identity{12,10});
        assert(!aim.prompt_identity_for_owner(owner,0x10001,prompt_tick));
        put(component+0x18,rig::Identity{11,10});
        assert(!aim.prompt_identity_for_owner(owner,0x10001,prompt_tick+NativeInteractionAim::held_grace_ms+1));
        aim.target_object_[0]=component;aim.target_owner_[0]=owner;aim.target_generation_[0]=11;aim.target_spawn_[0]=0x10001;
        put(component+0x18,rig::Identity{12,10});assert(!aim.grip_use_target(0,GetTickCount64()));
        assert((descriptor==std::array<unsigned char,40>{} && point==std::array<float,3>{.8f,0,1.25f}));
        assert((native_eye==std::array<float,4>{0,0,1.7f,0}));
        {
            const auto los=[&](unsigned char& output) {
                return aim.occlusion_result_body(reinterpret_cast<void*>(sensor),reinterpret_cast<void*>(owner),native_eye.data(),nullptr,
                    point_expected,0,&output,image+0x3b8b05);
            };
            // Doors and other native world usables need no invented small
            // model AABB. Preserve native LOS exactly through the final
            // ranked-scan continuation branch, including a native output0.
            for(const auto table:{0x1c569c8u,0x1c5bfc8u})for(unsigned bounds=0;bounds<2;++bounds)
                for(const bool blocked:{false,true})for(const unsigned char out:{static_cast<unsigned char>(0),static_cast<unsigned char>(0x7d)}) {
                    prepare(aim,scope);aim.occlusion_=native_los;put(component,image+table);
                    if(bounds==0) {scope.hand.volume_centre={0,0,1.25};aim.hands_[0]=scope.hand;}
                    else put(owner+0xf0,std::uintptr_t{});
                    assert(!aim.takeable(owner) && !aim.sphere_owner_current(scope,owner));
                    NativeInteractionAim::retrieved_={0,0,0,0,&scope,true,0};
                    los_blocked=blocked;los_output=out;los_calls=0;unsigned char output=0xcc;
                    assert(los(output)==(raw|(blocked?1u:0u)) && output==out && los_calls==1);
                }
            // Existing inside-sphere readable behavior is unchanged, even
            // when native eye LOS is blocked by the surface near the hand.
            prepare(aim,scope);aim.occlusion_=native_los;los_blocked=true;los_output=0x7d;los_calls=0;
            unsigned char output=0xcc;assert(los(output)==raw && output==0 && los_calls==1);
            // Verified pickups still require the very same nearest-hand
            // sphere: outside is refused and inside keeps proximity behavior.
            for(const bool inside:{false,true})for(const bool blocked:{false,true}) {
                prepare(aim,scope);aim.occlusion_=native_los;
                put(owner,image+0x1c5e258);put(image+0x1c5e258,image+0x100000);
                if(!inside) {scope.hand.volume_centre={0,0,1.25};aim.hands_[0]=scope.hand;}
                assert(aim.takeable(owner));los_blocked=blocked;los_output=0x7d;los_calls=0;output=0xcc;
                assert(los(output)==(raw|(inside?0u:1u)) && output==(inside?0:1) && los_calls==1);
            }
            NativeInteractionAim::retrieved_={};
        }
        {
            const WorldTrackedPose raw_hand{{.65,0,1.25},{{{1,0,0},{0,1,0},{0,0,1}}}};
            NativeInteractionAim::PassiveGazePreview preview{{{.03,.04,1.7},raw_hand.basis},true,{false,false}};
            const std::array<std::optional<WorldTrackedPose>,2> both{raw_hand,raw_hand};
            NativePlayerCamera owned_camera{};
            const auto publish=[&](std::uint64_t tick,std::uint64_t epoch=3) {
                aim.publish_hands(owned_camera,both,epoch,tick,{}, {}, {}, {}, {}, {},preview);
            };
            const auto scope_for_selected=[&] {
                scope={};scope.camera=owned_camera;scope.side=aim.routing_.selected();scope.sensor=sensor;scope.stage=0;
                scope.revision=aim.routing_.revision();scope.ownership_revision=aim.routing_.ownership_revision();
                scope.hand=aim.hands_[static_cast<unsigned>(scope.side)];
                aim.queued_revision_[static_cast<unsigned>(scope.side)]=scope.revision;
                NativeInteractionAim::context_=&scope;
            };
            const auto enter_preview=[&] {
                prepare_hover(aim,scope);owned_camera=scope.camera;aim.clear();
                owned_camera.aim_origin={0,0,1.7f};owned_camera.aim_axis={1,0,0,0,1,0,0,0,1};
                preview.allowed=true;preview.moving={false,false};
                const auto now=GetTickCount64();publish(now-400);publish(now-200);
                // Complete owned empty stage0 scans, including final native
                // selection, are required for each hand. Pending scans do not count.
                for(unsigned side=0;side<2;++side) {
                    aim.routing_.next({side==0,side==1},now);scope_for_selected();
                    scope.completed_volumes=side==0?3:7;aim.target(scope);
                }
                publish(now);assert(!aim.passive_preview_active());
                aim.routing_.next({true,false},now);scope_for_selected();scope.completed_volumes=7;aim.target(scope);
                publish(now);assert(aim.passive_preview_active() && aim.routing_.locked()==-1);
                scope_for_selected();
            };
            enter_preview();
            const auto preview_revision=scope.revision;
            const auto preview_side=scope.side;
            assert(scope.hand.preview && scope.hand.reach && length(scope.hand.hand.position-preview.gaze.position)<1e-9);
            InteractionRay native{{0,0,1.7f,0,2,0,0,1,0,0,0,0}},routed{};
            const unsigned flags=0x3f000000;std::memcpy(&native.f[11],&flags,4);
            put(reinterpret_cast<std::uintptr_t>(&native),native);
            assert(aim.ray(&native,routed,image+0x3b867b)==&routed);
            assert(std::abs(routed.f[0]-.03)<1e-6 && std::abs(routed.f[1]-.04)<1e-6);
            mode=Mode::permission_refused;
            const auto before_calls=native_calls;
            assert(invoke()==raw && native_calls==before_calls+1); // gaze never overrides native permission
            put(0xb00000+0x30,11);put(0xb00000+0x38,component);
            aim.target(scope);
            // Even an engine-selected gaze component cannot grant input claims.
            aim.native_object_=component;aim.native_tick_=GetTickCount64();
            assert(!aim.has_target(GetTickCount64()) && !aim.native_selected_target(GetTickCount64()));
            for(int side=0;side<2;++side)
                assert(!aim.has_target(side,GetTickCount64()) && !aim.query_ready(side,GetTickCount64()) && !aim.grip_use_target(side,GetTickCount64()));
            assert(!aim.qualified_target(GetTickCount64()));
            const auto now=GetTickCount64();
            for(unsigned i=0;i<20;++i)publish(now+i); // resting/head-only changes never alternate hand/gaze queries
            assert(aim.passive_preview_active() && aim.routing_.selected()==preview_side && aim.routing_.revision()==preview_revision);
            const auto old_preview=scope;
            aim.lock(preview_side);
            assert(!aim.passive_preview_active() && aim.routing_.revision()!=preview_revision);
            assert(length(aim.hands_[static_cast<unsigned>(preview_side)].hand.position-raw_hand.position)<1e-9);
            assert(!aim.query_ready(preview_side,GetTickCount64()));
            // An outstanding preview query cannot route or publish after the press.
            scope=old_preview;NativeInteractionAim::context_=&scope;
            assert(aim.ray(&native,routed,image+0x3b867b)==&native);
            aim.target(scope);assert(!aim.has_target(preview_side,GetTickCount64()));
            // The normal, newly owned query can select the same exact owner.
            // Input authorization still comes from the ordinary press path.
            scope_for_selected();scope.hand.tick=GetTickCount64();aim.hands_[static_cast<unsigned>(preview_side)].tick=scope.hand.tick;
            aim.target(scope);assert(aim.has_target(preview_side,GetTickCount64()) && aim.target_owner(preview_side,GetTickCount64())==owner);
            enter_preview();preview.moving={false,true};publish(GetTickCount64());
            assert(!aim.passive_preview_active()); // actual controller intent returns to the hand sphere immediately
            enter_preview();preview.allowed=false;publish(GetTickCount64());assert(!aim.passive_preview_active());
            enter_preview();publish(GetTickCount64(),4);assert(!aim.passive_preview_active()); // ownership/epoch discontinuity
            enter_preview();
            aim.publish_hands(owned_camera,{raw_hand,std::nullopt},3,GetTickCount64(),{}, {}, {}, {}, {}, {},preview);
            assert(!aim.passive_preview_active()); // missing tracking cannot enter or keep preview
            aim.clear();
        }
        NativeInteractionAim::context_=nullptr;NativeInteractionAim::sphere_eligibility_=nullptr;current=nullptr;
    }
};
}
int main() {SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);d2vr::NativeInteractionSphereFixture::run();std::cout<<"PASS: hand sphere preserves native semantic refusal and original arguments; qualified geometry override, nested C++/SEH TLS, revoked ownership/spawn, range/character/lifetime guards and generic readable grip\n";}
