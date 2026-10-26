#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_grab.hpp"
#include <cassert>
#include <iostream>
namespace d2vr {struct NativeGrabCheck {static void active(NativeGrab& grab) {grab.active_=true;}};}
int main() {
    d2vr::NativeGrabDiscovery discovery;
    unsigned lookups=0,validations=0;
    bool alive=true;
    d2vr::NativeRigidBody cached{};cached.entity=1;cached.entity_id=0x10001;cached.pose.position={.2,0,0};
    const auto lookup=[&](unsigned index)->std::optional<d2vr::NativeRigidBody> {++lookups;return index==1 && alive?std::optional<d2vr::NativeRigidBody>{cached}:std::nullopt;};
    const auto validate=[&](const auto&)->std::optional<d2vr::NativeRigidBody> {++validations;return alive?std::optional<d2vr::NativeRigidBody>{cached}:std::nullopt;};
    assert(discovery.advance({},100,lookup,validate).size()==1 && lookups==1024 && validations==0);
    discovery.advance({},100,lookup,validate);assert(lookups==1024 && validations==0); // One slice per pose publication.
    assert(discovery.advance({},101,lookup,validate).size()==1 && lookups==2048 && validations==1);
    ++cached.entity_id;assert(discovery.advance({},102,lookup,validate).empty()); // Reused entity lifetime is not the cached body.
    discovery.reset();lookups=0;assert(discovery.advance({},103,lookup,validate).size()==1);
    alive=false;assert(discovery.advance({},104,lookup,validate).empty());
    for(unsigned tick=105;tick<170;++tick)discovery.advance({},tick,lookup,validate);
    assert(discovery.cursor()>0 && discovery.cursor()<d2vr::NativeGrabDiscovery::registry_size);
    assert(lookups==67*1024); // Registry wrap stays bounded, no full-registry fallback.
    // A lane occupied by native carrying/action state must hand the raw
    // squeeze back without touching the native read ABI, which is not installed
    // in this process: any read here would fault.
    d2vr::NativeGrab grab;d2vr::NativePlayerCamera owner{};
    d2vr::WorldTrackedPose hand{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
    grab.publish(0,owner,hand,.9f,1,100,true,true);
    assert(grab.filter_native_grip(0,.9f,1,100,true,true,false)==.9f);
    assert(grab.filter_native_grip(0,.4f,1,101,true,true,false)==.4f);
    assert(grab.filter_native_grip(0,.9f,1,102,false,false,true)==.9f); // Raw even unfocused or while Use is held.
    assert(!grab.holding(0));
    const auto occupied_diagnostic=grab.diagnostics(110);assert(occupied_diagnostic);
    assert((*occupied_diagnostic)[0].gate==d2vr::NativeGrab::Gate::occupied);
    assert((*occupied_diagnostic)[0].raw_grip==.9f && (*occupied_diagnostic)[0].source_age_ms==10);
    assert((*occupied_diagnostic)[0].source_age_valid && !(*occupied_diagnostic)[0].released_armed);
    grab.publish(1,owner,hand,.7f,1,100,true,false); // Defaulted occupied=false still compiles.
    assert(!grab.holding(1));
    // Uninstalled native reader: pending decisions must return before attempting
    // any native owner query, and may resume only after caller clears deferral.
    for(unsigned tick=101;tick<350;++tick) {
        assert(grab.filter_native_grip(1,.9f,1,tick,true,true,false,true)==0);
        grab.publish(1,owner,hand,.9f,1,tick,true,false);
        assert(grab.waiting_native_query(1)); // Render pose updates must preserve pending ownership.
        const auto pending=grab.diagnostics(tick);assert(pending);
        assert((*pending)[1].gate==d2vr::NativeGrab::Gate::deferred && (*pending)[1].raw_grip==.9f);
        assert((*pending)[1].decision_tick==tick && (*pending)[1].source_age_ms==0);
    }
    assert(!grab.holding(1));
    // Native occupancy continues to win even while a query decision is deferred.
    assert(grab.filter_native_grip(0,.9f,1,350,true,true,false,true)==.9f);
    // Combined owner model: pending keeps the release-armed role untouched;
    // resolution can still reserve physics on the SAME squeeze, without release.
    d2vr::RightGripOwner pending_owner;
    assert(pending_owner.filter(0,true,false)==0);
    for(unsigned tick=100;tick<160;++tick) assert(pending_owner.role()==d2vr::RightGripOwner::Role::ready);
    assert(pending_owner.filter(.9f,true,true)==0 && pending_owner.physical());
    // A second hand resolving native Use cannot revoke this independent owner.
    d2vr::RightGripOwner use_owner;use_owner.filter(0,true,false);
    assert(use_owner.filter(.9f,true,false)==.9f && pending_owner.physical());
    assert(!grab.focus_point(0,100) && !grab.focus_point(1,100) && !grab.focus_point(2,100));
    // The hover and grip share this selector. AABB surface distance wins over
    // body-origin distance, and the other hand's held entity is excluded.
    d2vr::NativeRigidBody small_body{},wide_body{},distant_body{};
    small_body.entity=1;small_body.pose.position={.2,0,0};
    wide_body.entity=2;wide_body.pose.position={.7,0,0};wide_body.bounds=d2vr::NativeGrabBounds{{.1,-.1,-.1},{.8,.1,.1}};
    distant_body.entity=3;distant_body.pose.position={1,0,0};
    const std::vector<d2vr::NativeRigidBody> candidates{small_body,wide_body,distant_body};
    const auto focused=d2vr::grab_focus_candidate(candidates,{0,0,0},0);
    assert(focused && focused->entity==2 && focused->contact({0,0,0}).x==.1);
    const auto excluded=d2vr::grab_focus_candidate(candidates,{0,0,0},2);
    assert(excluded && excluded->entity==1);
    assert(!d2vr::grab_focus_candidate({distant_body},{0,0,0},0));
    assert(!d2vr::grab_focus_candidate({small_body},{0,0,0},1));
    assert(!d2vr::grab_focus_candidate(candidates,{0,0,0},0,true)); // Native Use owns this press even with an eligible prop.
    assert(!grab.focus_point(0,100,true));
    d2vr::NativeRigidBody old_target{},challenger{};old_target.entity=4;challenger.entity=5;
    old_target.pose.position={.21,0,0};challenger.pose.position={.19,0,0};
    assert(!d2vr::grab_focus_candidate({old_target},{0,0,0},0)); // Outside new-acquisition radius.
    assert(d2vr::grab_focus_candidate({old_target,challenger},{0,0,0},0,false,4)->entity==5);
    challenger.pose.position={.15,0,0};
    assert(d2vr::grab_focus_candidate({old_target,challenger},{0,0,0},0,false,4)->entity==5);
    old_target.pose.position={.201,0,0};
    assert(!d2vr::grab_focus_candidate({old_target},{0,0,0},0,false,4)); // Beyond retention margin.
    {   // G3 (plan Task 5): the palm picks the physical candidate. In clutter the box the palm faces wins over an overlapping box listed
        // first that is nearer (under the knuckles): the nearest-AABB pick above (build/legacy-palm-select) takes the nearer one.
        d2vr::PalmProbe probe;probe.palm={0,0,0};probe.forward={0,0,1};probe.direction={1,0,0};probe.valid=true;   // palm point (0,0,.015)
        d2vr::NativeRigidBody under{},faced{};
        under.entity=10;under.entity_id=0x10010;under.pose.position={0,0,-.1};under.bounds=d2vr::NativeGrabBounds{{-.3,-.1,-.3},{.3,.1,-.015}};
        faced.entity=11;faced.entity_id=0x20011;faced.pose.position={.15,0,0};faced.bounds=d2vr::NativeGrabBounds{{.05,-.1,-.1},{.3,.1,.1}};
        assert(under.bounds->maximum.x>faced.bounds->minimum.x && under.bounds->maximum.z>faced.bounds->minimum.z);   // the boxes overlap
        const std::vector<d2vr::NativeRigidBody> clutter{under,faced};
        assert(under.contact_distance({0,0,.015})<faced.contact_distance({0,0,.015}));
        d2vr::GrabTuning tuning;d2vr::PalmSelection selection;d2vr::PalmPick pick;
        const auto palm=d2vr::grab_focus_candidate(clutter,probe,selection,tuning,0,0,d2vr::grab_acquire_distance,false,100,&pick);
        assert(palm && palm->entity==10 && pick.index && selection.current()==0x10010);
        d2vr::PalmSelection legacy_selection;
        const auto nearest=d2vr::grab_focus_candidate(clutter,probe,legacy_selection,tuning,0,0,d2vr::grab_acquire_distance,true);
        assert(nearest && nearest->entity==10);   // legacy_nearest: the v611 nearest-AABB pick
        assert(d2vr::grab_focus_candidate(clutter,probe,selection,tuning,11,0,d2vr::grab_acquire_distance)->entity==10);   // the other hand's
        d2vr::PalmSelection invalid;d2vr::PalmProbe bad=probe;bad.valid=false;   // an invalid probe: nothing new, scoring_fallback counted
        assert(!d2vr::grab_focus_candidate(clutter,bad,invalid,tuning,0,0,d2vr::grab_acquire_distance) && invalid.scoring_fallback()==1);
        const d2vr::PalmTarget bone=d2vr::palm_body_target([]{d2vr::NativeRigidBody b{};b.entity_id=7;b.bone=1;b.pose.position={1,2,3};return b;}(),tuning);
        assert(bone.kind==d2vr::PalmKind::bone && bone.priority==tuning.priority_torso && bone.id==7 && bone.minimum.x==1 && bone.maximum.z==3);
    }
    // An inactive/unusable lane needs no owner memory query; diagnostics still
    // distinguish future/missing source age from a real zero-age publication.
    grab.publish(1,owner,hand,.2f,1,400,false,false);
    assert(grab.filter_native_grip(1,.2f,1,399,true,true,false)==.2f);
    const auto inactive=grab.diagnostics(399);assert(inactive);
    assert((*inactive)[1].gate==d2vr::NativeGrab::Gate::inactive && !(*inactive)[1].source_age_valid);
    assert((*inactive)[1].role==d2vr::RightGripOwner::Role::ready && (*inactive)[1].released_armed);
    assert((*inactive)[1].candidates==0);
    d2vr::NativeGrabCheck::active(grab);
    grab.publish(1,owner,hand,.2f,1,1000,true,false,749);
    assert(grab.filter_native_grip(1,.2f,1,1000,true,true,false)==.2f); // Must reject before uninstalled native reader.

    const auto stale_xr=grab.diagnostics(1000);assert(stale_xr);
    assert((*stale_xr)[1].publication_tick==1000 && (*stale_xr)[1].source_tick==749);
    assert((*stale_xr)[1].source_age_ms==251 && (*stale_xr)[1].source_age_valid);
    assert((*stale_xr)[1].gate==d2vr::NativeGrab::Gate::source_stale);
    grab.publish(1,owner,hand,.2f,1,1000,true,false,1001);
    assert(grab.filter_native_grip(1,.2f,1,1000,true,true,false)==.2f);
    const auto future_xr=grab.diagnostics(1000);assert(future_xr);
    assert((*future_xr)[1].gate==d2vr::NativeGrab::Gate::source_future && !(*future_xr)[1].source_age_valid);
    // Press/release transitions shorter than observer cadence remain available.
    d2vr::NativeGrab decisions;decisions.publish(0,owner,hand,0,1,1000,true,true);
    assert(decisions.filter_native_grip(0,0,1,1000,true,true,false)==0);
    assert(decisions.filter_native_grip(0,.9f,1,1010,true,true,false)==.9f);
    assert(decisions.filter_native_grip(0,0,1,1020,true,true,false)==0);
    const auto released=decisions.pop_decision(),pressed=decisions.pop_decision(),released_again=decisions.pop_decision();
    assert(released && pressed && released_again && !decisions.pop_decision());
    assert(released->raw_grip==0 && pressed->raw_grip==.9f && released_again->raw_grip==0);
    assert(pressed->gate==d2vr::NativeGrab::Gate::occupied && pressed->source_tick==1000 && pressed->source_age_ms==10);
    decisions.filter_native_grip(0,0,1,1021,true,true,false);assert(!decisions.pop_decision());
    decisions.filter_native_grip(0,0,1,1120,true,true,false);assert(decisions.pop_decision()); // Bounded steady cadence.
    for(unsigned frame=0;frame<300;++frame) decisions.filter_native_grip(0,frame%2?0:.9f,1,1200+frame,true,true,false);
    assert(decisions.decision_dropped()==44);
    unsigned drained=0;while(decisions.pop_decision()) ++drained;assert(drained==256);
    std::cout<<"PASS: native carrying/Use priority, shared surface selector, .45 acquisition/.55 retention, .10 switch margin and other-hand exclusion\n";
}
