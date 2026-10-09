#include "../src/native_arm_binding_snapshot.hpp"
#include <cassert>
#include <atomic>
#include <thread>
#include <iostream>

namespace r=d2vr::rig;
std::array<r::Arm,2> pair(int generation) {
    std::array<r::Arm,2> arms{};
    for(unsigned side=0;side<2;++side) arms[side]={0x100000,0x200000,0x300000+side*0x1000,
        0x400000+side*0x1000,0x500000+side*0x1000,{generation,10},{generation+static_cast<int>(side),10},side};
    return arms;
}
r::ArmBinding binding_for(const r::Arm& arm) {
    r::ArmBinding binding{};binding.chain={0,1,2};binding.parents.fill(-1);
    binding.parents[1]=0;binding.parents[2]=1;
    binding.bind_wrist.f[0]=binding.bind_wrist.f[5]=binding.bind_wrist.f[10]=1;
    binding.bind_wrist.f[12]=static_cast<float>(arm.body_identity.generation);
    binding.bind_grip=binding.bind_wrist;
    return binding;
}
int main() {
    r::NativeArmBindingCache cache;
    auto native=pair(1);
    auto find=[&]() -> std::optional<std::array<r::Arm,2>> {return native;};
    auto bind=[](const r::Arm& arm) -> std::optional<r::ArmBinding> {return binding_for(arm);};
    assert(!cache.load());assert(cache.refresh_from(find,bind));const auto retained=cache.load();assert(retained);
    assert(retained->side_for_model(native[0].model)==0u && retained->side_for_model(native[1].model)==1u && !retained->side_for_model(0x600000));
    native=pair(2);assert(cache.refresh_from(find,bind));
    assert(cache.load()->arms[0].body_identity.generation==2 && retained->arms[0].body_identity.generation==1);
    assert(retained->bindings[0].bind_wrist.f[12]==1 && retained->bindings[1].bind_wrist.f[12]==1);
    unsigned rebound{};auto counted=[&](const r::Arm& arm) -> std::optional<r::ArmBinding> {++rebound;return binding_for(arm);};
    const auto unchanged=cache.load();assert(cache.refresh_from(find,counted) && cache.load()==unchanged && rebound==0);
    auto missing=[]() -> std::optional<std::array<r::Arm,2>> {return {};};
    assert(!cache.refresh_from(missing,bind) && !cache.load());assert(retained->bindings[0].bind_wrist.f[12]==1);
    // A transient read loss makes a new shared allocation on reacquisition,
    // even though every owned native identity/source field is unchanged.
    // Shared-pointer replacement alone cannot prove an avatar identity change.
    assert(cache.refresh_from(find,bind));const auto reacquired=cache.load();
    assert(reacquired!=unchanged && reacquired->arms==unchanged->arms && reacquired->sources==unchanged->sources);
    native=pair(3);assert(cache.refresh_from(find,bind));
    auto incomplete=[](const r::Arm& arm) -> std::optional<r::ArmBinding> {return arm.side?std::nullopt:std::optional<r::ArmBinding>(binding_for(arm));};
    native=pair(4);assert(!cache.refresh_from(find,incomplete) && !cache.load());
    for(unsigned failure=0;failure<5;++failure) {
        native=pair(4);
        if(failure==0) ++native[1].body_identity.generation;
        if(failure==1) native[1].player+=8;
        if(failure==2) native[1].side=0;
        if(failure==3) native[1].model=native[0].model;
        if(failure==4) native[1].animation_identity.generation=-1;
        assert(!cache.refresh_from(find,bind) && !cache.load());
    }
    native=pair(5);unsigned calls{};
    auto recycled=[&](const r::Arm& arm) -> std::optional<r::ArmBinding> {
        auto binding=binding_for(arm);if(++calls==2) native=pair(6);return binding;
    };
    assert(!cache.refresh_from(find,recycled) && calls==2 && !cache.load()); // No mixed generation publication.
    assert(cache.refresh_from(find,bind) && cache.load()->bindings[1].bind_wrist.f[12]==6);
    auto throwing=[](const r::Arm&) -> std::optional<r::ArmBinding> {throw std::runtime_error("native read failed");};
    native=pair(7);
    assert(!cache.refresh_from(find,throwing) && !cache.load());
    // Identical component/skeleton addresses must not hide changed bind data.
    native=pair(20);int inverse_version=20;
    auto capture=[](const std::array<r::Arm,2>&,const r::NativeArmBindingSnapshot*) -> std::optional<std::array<r::NativeArmBindingSource,2>> {return std::array<r::NativeArmBindingSource,2>{};};
    auto revision_bind=[&](const r::Arm& arm) -> std::optional<r::ArmBinding> {
        auto binding=binding_for(arm);binding.bind_wrist.f[12]=static_cast<float>(inverse_version);return binding;
    };
    auto valid=[&](const r::NativeArmBindingSnapshot& snapshot) {return snapshot.bindings[0].bind_wrist.f[12]==inverse_version && snapshot.bindings[1].bind_wrist.f[12]==inverse_version;};
    assert(cache.refresh_from(find,revision_bind,capture,valid));const auto old_inverse=cache.load();inverse_version=21;
    assert(cache.refresh_from(find,revision_bind,capture,valid) && cache.load()!=old_inverse && old_inverse->bindings[0].bind_wrist.f[12]==20);
    unsigned captures{};
    auto changing_source=[&](const std::array<r::Arm,2>&,const r::NativeArmBindingSnapshot*) -> std::optional<std::array<r::NativeArmBindingSource,2>> {
        std::array<r::NativeArmBindingSource,2> sources{};sources[0].inverse=++captures;return sources;
    };
    assert(!cache.refresh_from(find,revision_bind,changing_source,valid) && !cache.load());
    auto unreadable=[](std::uintptr_t,void*,std::size_t) {return false;};
    assert(!cache.refresh(unreadable,0x140000000) && !cache.load()); // Instantiate the production native guards.
    // Callbacks each retain one immutable dual-arm lease across concurrent
    // complete replacement and invalidation; no old/new binding mixtures.
    native=pair(7);assert(cache.refresh_from(find,bind));
    std::atomic<bool> done{false},start{false};std::atomic<unsigned> seen{0};
    auto reader=[&] {
        while(!start.load()) std::this_thread::yield();
        do {
            const auto snapshot=cache.load();
            if(snapshot) {
                const auto stamp=snapshot->arms[0].body_identity.generation;
                assert(snapshot->arms[1].body_identity.generation==stamp);
                std::this_thread::yield();
                assert(snapshot->bindings[0].bind_wrist.f[12]==stamp && snapshot->bindings[1].bind_wrist.f[12]==stamp);
                ++seen;
            }
        } while(!done.load());
    };
    std::thread left(reader),right(reader);start=true;
    while(seen.load()<2) std::this_thread::yield();
    for(int generation=8;generation<1008;++generation) {
        native=pair(generation);assert(cache.refresh_from(find,bind));if(generation%3==0) cache.clear();
    }
    done=true;left.join();right.join();assert(seen>=2);
    assert(retained->arms[0].body_identity.generation==1 && retained->bindings[1].bind_wrist.f[12]==1);
    std::cout<<"PASS: whole dual-arm binding publication, ownership/recycling/incomplete rejection, retained lifetime and concurrent callback coherence\n";
}
