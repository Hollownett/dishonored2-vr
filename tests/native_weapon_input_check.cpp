#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_weapon_input.hpp"
#include <cassert>
#include <stdexcept>
#include <iostream>
int main() {
    using namespace d2vr;
    using D=NativeWeaponDryLease;
    auto owned=std::make_shared<D::Lease>();
    owned->source.source=0x100000;owned->candidate.arm.side=1;owned->candidate.joint=59;
    auto equipment=std::make_shared<NativeEquipmentOwnership>();equipment->attachment=owned->candidate;
    equipment->guards.push_back({0x200000,{1}});owned->equipment=equipment;
    for(unsigned i=0;i<69;++i) {
        auto& f=owned->posed.joints[i].f;f[0]=f[5]=f[10]=1;
        owned->source.joints[i]=owned->posed.joints[i];
    }
    owned->posed.joints[59].f[12]=2;
    const auto select=[&](unsigned site,std::uintptr_t source,unsigned count,std::uintptr_t caller,bool requested=true,bool qualified=true) {
        return native_weapon_input(owned,site,source,count,caller,requested,qualified);
    };
    const auto primary=select(D::primary,0x100000+59*64,1,0x53d539);
    const auto gpu=select(D::gpu,0x100000,69,0x541310);
    assert(primary && gpu && primary.lease==gpu.lease && primary.value_changed && gpu.value_changed);
    assert(reinterpret_cast<std::uintptr_t>(gpu.source)%16==0 && sizeof(owned->posed.joints)==4416);
    assert(static_cast<const rig::Transform*>(gpu.source)+59==primary.source);
    assert(!select(D::primary,0x100000+59*64,1,0x53d493));
    assert(!select(D::gpu,0x100000,69,0x541311));
    assert(!select(D::gpu,0x100000,68,0x541310));
    assert(!select(D::primary,0x100000,1,0x53d539));
    assert(!select(D::primary,0x100000+59*64,1,0x53d539,false));
    assert(!select(D::primary,0x100000+59*64,1,0x53d539,true,false));
    assert(select(D::secondary,0x100000+59*64,1,0x53d705).reason==NativeWeaponInput::secondary_forbidden);
    equipment->attachment.entity=8;assert(!select(D::gpu,0x100000,69,0x541310));equipment->attachment=owned->candidate;
    owned->equipment.reset();assert(!select(D::gpu,0x100000,69,0x541310));owned->equipment=equipment;
    owned->posed.joints[59].f[0]=NAN;assert(!select(D::primary,0x100000+59*64,1,0x53d539));owned->posed.joints[59].f[0]=1;
    alignas(16) rig::Transform native{};native.f[0]=native.f[5]=native.f[10]=1;
    const auto untouched=native;unsigned calls{};float native_output{};
    native_weapon_input_call(primary,&native,[&](const void* input) {
        ++calls;assert(input==primary.source);native_output=static_cast<const rig::Transform*>(input)->f[12];
    });
    assert(calls==1 && native_output==2 && !std::memcmp(&native,&untouched,sizeof(native)));
    native_weapon_input_call({},&native,[&](const void* input){++calls;assert(input==&native);});assert(calls==2);
    // Revocation removes publication; the active call still owns copied values.
    auto isolated=std::make_shared<D::Lease>(*owned);std::weak_ptr<const D::Lease> weak=isolated;
    auto retained=native_weapon_input(isolated,D::primary,0x100000+59*64,1,0x53d539,true,true);
    try {
        native_weapon_input_call(std::move(retained),&native,[&](const void* input) {
            ++calls;isolated.reset();assert(!weak.expired());
            assert(static_cast<const rig::Transform*>(input)->f[12]==2);throw std::runtime_error("native original unwind");
        });assert(false);
    } catch(const std::runtime_error&) {}
    assert(calls==3 && weak.expired());
    std::cout<<"numeric input selection/alignment/original-once/unwind/lifetime fixture passed\n";
}
