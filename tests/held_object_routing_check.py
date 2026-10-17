"""Source-only production routing guard, not native execution or headset proof."""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
source=(root/'src/dxgi_probe.cpp').read_text(encoding='utf-8-sig')
held=(root/'src/native_held_object.hpp').read_text(encoding='utf-8-sig')
carry=(root/'src/native_carry_snapshot.hpp').read_text(encoding='utf-8-sig')
socket=(root/'src/native_left_weapon_socket.hpp').read_text(encoding='utf-8-sig')
compact=lambda value:''.join(value.split())
assert 'indices{49,45,50}'in carry
assert 'hand_holding_object=state->carry.hold_object();hand_hold_state=state->carry.hold_state();hand_carry_snapshot=state->carry;'in compact(source)
assert 'hand_holding_object=state->carry.active[0]'not in source
assert 'hand_hold_state=state->carry.states[0]'not in source
context=source.index('NativeHeldPlacement::process_owner().context(hands.epoch,')
publication=source.index('publish_held_object(side,hands.grips[side],state,hands.epoch,hands.tick,hands.reference_epoch,acquisition)',context)
context_block=source[context:source.index(');',context)+2]
for condition in ('held_object_installed','full_body_requested','tracked_arms_requested','!full_body_stopping.load()',
                  'avatar_pose_gate.load()','!authored_camera','game_logic_mask()==2','XR_SESSION_STATE_FOCUSED','XR_SUCCESS'):
    assert condition in context_block
assert context<publication<source.index('availability.placement_serial=held_placement_serial.load(',publication)
assert 'else {held_placement_session.clear();d2vr::NativeHeldObject::clear();}'in source[publication:publication+350]
producer=source[source.index('static void publish_held_object('):source.index('// HoldObject draws the object as a view-model')]
for condition in ('NativeHeldPlacement::qualify(image_base,&read_self,*carry,side,pose_epoch,pose_tick,t.tick)',
                  'proof->entity==*e','proof->rf==*rf','proof->carry.hold_state()==state','placement.publish(*proof,t.tick)',
                  't.pose_epoch=pose_epoch','t.source_side=side','else placement.clear()',
                  'held_placement_serial.store(t.placement_serial','NativeHeldObject::publish(t)'):
    assert condition in producer
assert producer.index('placement.publish(')<producer.index('NativeHeldObject::publish(t)')
refusal=producer[producer.index('if(!t.placement_serial)'):producer.index('NativeHeldObject::publish(t)')]
for condition in ('held_placement_serial.store(0','NativeHeldObject::clear()','held_entity=0;return;'):
    assert condition in refusal
assert 'held_placement_session.clear()' in refusal
assert 'held_placement_session.update({*e,*rf,state,*id,side,pose_epoch,reference_epoch,acquisition})' in producer
assert 'if(!geometry_captured)' in producer and 'held_placement_mesh_centre(model_pose,published_pose,' in producer
assert producer.index('held_placement_session.update(')<producer.index('held_fitter.reset()')
release=source[source.index('static d2vr::HeldRelease held_release;'):source.index('// Holster: a B press')]
for condition in ('hand_holding_object','side=hand_hold_side&1u','held_release.update(holding && controls_focus',
                  'NativeHeldObject::arm_throw(entity,f3)','else if(r.drop)'):
    assert condition in release
hook=held[held.index('static std::uintptr_t __fastcall bind_hook('):held.index('static void place(')]
assert hook.count('bind_(rf,mode,pos,axis,lin,ang,dt)')==1
assert hook.index('bind_(')<hook.index('place(rf,')
place=held[held.index('static void place('):held.index('static unsigned commit(')]
assert place.index('snapshot(t,&revision)')<place.index('process_owner().current(')<place.index('commit(t,revision,')<place.index('process_owner().completed(')
assert 'if(!t.placement_serial || !NativeHeldPlacement::process_owner().current(' in place
assert 'if(result==2)'in compact(place)
assert 'AcquireSRWLock'not in place and 'ReleaseSRWLock'not in place
assert 'clear();'not in place
assert '} __except(EXCEPTION_EXECUTE_HANDLER) {}'in place
commit=held[held.index('static unsigned commit('):held.index('static bool anchor(')]
assert commit.index('AcquireSRWLockExclusive(')<commit.index('__try')<commit.index('revision!=publication_revision_')<commit.index('std::memcpy(pos,t.pos,12)')<commit.index('anchor(t,m)')<commit.index('__finally {ReleaseSRWLockExclusive(')
# commit() validates the immutable published snapshot (published.*), then copies it to a local Target t, applies the master
# carry to t, and re-validates t before any engine-buffer write.
for condition in ('publication_exhausted_','rf_.load(std::memory_order_acquire)!=published.rf','GetTickCount64()-published.tick>150',
                  'if(*reinterpret_cast<const std::uintptr_t*>(published.rf+0x10)!=published.entity)return 0;',
                  'Targett=published;','return anchored_here?2u:1u;'):
    assert compact(condition) in compact(commit)
assert commit.index('carry(t,m)')<commit.index('if(!target_valid(t))')<commit.index('std::memcpy(pos,t.pos,12)')
assert '++rejected_targets;return 0;' in commit
assert 'NativeHeldPlacement'not in commit and 'bind_('not in commit and 'bind_observer'not in commit
assert 'clear();'not in commit
clear=held[held.index('static void clear() noexcept'):held.index('// Render thread: the next set-velocity')]
assert clear.count('NativeHeldPlacement::process_owner().clear()')==1
assert clear.index('process_owner().clear()')<clear.index('AcquireSRWLockExclusive(')<clear.index('advance_publication_locked()')<clear.index('data_={};rf_.store(0,std::memory_order_release)')<clear.index('__finally {ReleaseSRWLockExclusive(')
assert 'current('not in clear and 'completed('not in clear
publish=held[held.index('static void publish('):held.index('static void clear() noexcept')]
assert publish.index('if(!target_valid(t))')<publish.index('++rejected_targets')<publish.index('AcquireSRWLockExclusive(')
assert 'static bool target_valid(const Target& t) noexcept'in held and 'std::isfinite'in held and 'det>0.f'in held
assert publish.index('AcquireSRWLockExclusive(')<publish.index('advance_publication_locked()')<publish.index('data_=t')<publish.index('rf_.store(t.rf')<publish.index('__finally {ReleaseSRWLockExclusive(')
snapshot=held[held.index('static bool snapshot('):held.index('static std::uintptr_t __fastcall bind_hook(')]
assert snapshot.index('AcquireSRWLockShared(')<snapshot.index('out=data_')<snapshot.index('*revision=publication_revision_')<snapshot.index('__finally {ReleaseSRWLockShared(')
assert 'volatile Target'not in held and 'seq_'not in held
advance=held[held.index('static bool advance_publication_locked('):held.index('static bool snapshot(')]
assert advance.index('publication_revision_==(std::numeric_limits<std::uint64_t>::max)()')<advance.index('publication_exhausted_=true')<advance.index('++publication_revision_')
assert '__finally' in hook and hook.index('if(observe)observe(3,')<hook.index('--depth_;')
anchor=held[held.index('static bool anchor('):held.index('static void __fastcall arm_part_hook(')]
assert 'return false;'in anchor and '++anchored;return true;'in anchor
assert anchor.index('std::isfinite(c)')<anchor.index('c/=norm')<anchor.index('std::memcpy(rf+0x3e0,xyzw,16)')<anchor.index('return true;')
assert anchor.index('std::abs(norm-1.f)<1e-3f')<anchor.index('*reinterpret_cast<std::int16_t*>(rf+0x3d0)=-1')
assert 'if(carry.active[2])return false;'in socket
assert 'NativeHeldPlacement::process_owner().current_driven_right('in socket
assert 'availability.placement_serial,availability.epoch,carry,now' in compact(socket)
print('PASS held-object production source routing only: typed49/50 placement/release, serialized target revision/freshness and same-hold acknowledgement; not compiled/runtime/headset proof')
