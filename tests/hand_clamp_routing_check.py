"""Physical hands phase 1 (Task 5, C9/C14) source routing guard for the hand clamp in dxgi_probe.cpp and its casts in
native_interaction_aim.hpp. Source-only: not compiled, native-execution or headset proof."""
from pathlib import Path


def compact(value: str) -> str:
    return ''.join(value.split())


def between(source: str, start: str, end: str, after: int = 0) -> str:
    begin = source.index(start, after)
    return source[begin:source.index(end, begin)]


def check(dxgi: str, aim: str) -> None:
    s = compact(dxgi)
    # The publication block, from the tracked poses to the arm IK feed.
    pub = between(s, 'raw_grips=hands.grips;', 'hands.has_finger_curls=false;')
    clamp = between(pub, 'if(weapon_collision_enabled&&!authored_camera){', 'publish_hand(side,')
    loop = between(clamp, 'for(unsignedside=0;side<2;++side)if(hands.hands&(1u<<side)){', 'hand_clamp_budget.note(')

    # C9 ownership is known before the clamp: the authored arms are hoisted above it.
    assert s.index('constautonative_ownership=d2vr::fullbody::ownership(avatar_states,authored_camera);') < s.index('raw_grips=hands.grips;')
    assert 'constunsignedauthored_hands=(authored_camera?3u:0u)|(native_ownership.arm[0]>0?1u:0u)|(native_ownership.arm[1]>0?2u:0u);' in s
    assert s.count('constautonative_ownership=') == 1 and s.count('constunsignedauthored_hands=') == 1
    assert 'hands.native_hands=authored_hands;if(authored_hands)hands.has_hand_state=true;' in s

    # The raw poses are taken before anything moves the drawn hands.
    assert pub.index('raw_grips=hands.grips;') < pub.index('reach_clamp(') < pub.index('hand_clamp.update(')
    # Yields (C9): authored arms, a native carry on that side (fresh hand state), a physical hold. The reach clamp never applies on a yield.
    assert 'if(hand_holding_object&&hand_state_tick&&GetTickCount64()-hand_state_tick<=300)carry_bits=1u<<(hand_hold_side&1u);' in clamp
    for need in ('constboolclamp_yield=d2vr::clamp_yields(side,authored_hands,carry_bits,physical_grab_requested&&native_grab.holding(side));',
                 'constautotarget=clamp_yield||!reach_fresh?raw:d2vr::reach_clamp(raw,arm_reach.reach[side],&reach_clamped);',
                 'in.yield=clamp_yield;', 'in.target=target;', 'in.root=clamp_root;', 'in.epoch=clamp_epoch;', 'in.chest=chest;',
                 'in.post=d2vr::clamp_posts(clamp_alternate,hand_clamp_frame,side);', 'in.tip_enabled=clamp_tip_on;'):
        assert need in loop, need
    assert 'reach_fresh=full_body_requested&&tracked_arms_requested&&arm_reach.body_valid&&arm_reach.output_age_seconds<.25;' in clamp
    assert 'constautoclamp_root=d2vr::level_frame({anchor.position[0],anchor.position[1],anchor.position[2]},{anchor.basis[0],anchor.basis[1],anchor.basis[2]});' in clamp
    assert 'constautoclamp_epoch=hands.epoch^(hands.reference_epoch<<32)^static_cast<std::uint64_t>(body->player);' in clamp
    assert 'constautoclamp_alternate=hand_clamp_budget.alternate();constautoclamp_tip_on=hand_clamp_budget.tip_enabled();' in clamp
    # The open-hand probe set feeds the leading probes and ClampInput::hand alike (Task 3 carry: else the wall-slide fix is off).
    for need in ('refresh_clamp_probes(side,raw);', 'clamp_tip[side]=d2vr::leading_probe(probes.hand,target,velocity,clamp_tip[side]);',
                 'clamp_wall_tip[side]=d2vr::wall_leading_probe(probes.hand,target,velocity,clamp_wall_tip[side]);',
                 'in.hand=probes.hand;in.hand_count=probes.count;', 'in.tip=clamp_tip[side];in.wall_tip=clamp_wall_tip[side];'):
        assert need in loop, need
    # Last frame's answers, routed by the clamp's last state; then the meet's loose list and exclusion, then the update.
    route = 'd2vr::route_clamp_hits(in,clamp_state[side],answer(0),answer(d2vr::NativeInteractionAim::tip_slot),answer(d2vr::NativeInteractionAim::wall_tip_slot),answer(d2vr::NativeInteractionAim::exclude_slot));'
    assert route in loop
    assert loop.index(route) < loop.index('native_grab.hand_contact().fill_clamp_input(side,in,clamp_exclude[side]);') < loop.index('clamped=hand_clamp.update(side,in);clamp_state[side]=clamped.state;')
    # Posts: palm/chest to slot 0, the tip, the wall tip and the exclusion sweep (with its excluded entity) to their slots; only while drawn.
    posts = between(loop, 'if(clamped.valid&&!clamp_yield){constautopost=', 'constautomet=')
    for need in ('d2vr::NativeInteractionAim::post_weapon_segment(side,slot,sweep.from,sweep.to,sweep.radius,sweep.exclude?*sweep.exclude:0);',
                 'if(!sweep.valid||(sweep.exclude&&!*sweep.exclude))return;', 'post(0,clamped.palm_sweep);',
                 'post(d2vr::NativeInteractionAim::tip_slot,clamped.tip_sweep);', 'post(d2vr::NativeInteractionAim::wall_tip_slot,clamped.wall_tip_sweep);',
                 'post(d2vr::NativeInteractionAim::exclude_slot,clamped.exclude_sweep);'):
        assert need in posts, need
    # The meet composes with the clamp (Task 3: meet_clamp, never the old meet()); the re-acquire frame holds the hand.
    assert '.meet(side,' not in s and 'hand_contact().meet(' not in s
    meet = between(loop, 'constautomet=native_grab.hand_contact().meet_clamp(side,hand_clamp,clamped,target);', 'constautoblade_axis=')
    for need in ('g.position=met.drawn;', 'held_by=met.held_by;', 'clamp_exclude[side]=met.exclude;',
                 'if(clamped.reacquired)if(constautokept=clamp_hold.hold(side,clamp_epoch,clamp_root,target.position,hand_clamp.config().snap_gap)){g.position=*kept;',
                 '}else{clamp_exclude[side].reset();clamp_hold.clear(side);}', 'cast_entities[side]=held_by;'):
        assert need in meet, need
    # The blade sweeps from the drawn grip; note_drawn follows it every frame (missed_notes stays 0).
    blade = 'constautor=weapon_collision.update(side,dt,g.position,blade_axis,blade,query,blade_offset,blade_radius);'
    assert blade in loop and 'weapon_collision.update(side,dt,chest' not in s
    assert loop.index('clamped=hand_clamp.update(side,in);clamp_state[side]=clamped.state;') < loop.index('constautomet=') < loop.index(blade) < loop.index('g.position=r.grip;') \
        < loop.index('hand_clamp.note_drawn(side,clamp_root,g.position);')
    assert loop.index('d2vr::align_weapon_blade(g,*r.blade_dir,side==1&&reverse_sword);') < loop.index('hand_clamp.note_drawn(side,clamp_root,g.position);')
    assert 'if(clamp_updated&&clamped.valid&&!clamp_yield)clamp_hold.note(side,clamp_epoch,clamp_root,g.position);' in loop
    assert 'if(clamp_updated)hand_clamp.note_drawn(side,clamp_root,g.position);' in loop
    # Casts are answered only in gameplay (Stage0, logic 2): elsewhere the clamp is not updated and the hand is held (a yield still resets it).
    assert 'constboolclamp_live=game_logic_mask()==2;' in clamp
    gate = 'd2vr::ClampOutputclamped{};constboolclamp_updated=clamp_live||clamp_yield;if(clamp_updated){d2vr::ClampInputin{};'
    assert gate in loop and loop.index(gate) < loop.index(route)
    paused = between(loop, 'if(!clamp_updated){', '}elseif(clamped.valid&&!clamp_yield){constautopost=')
    assert 'if(constautokept=clamp_hold.hold(side,clamp_epoch,clamp_root,target.position,hand_clamp.config().snap_gap))g.position=*kept;' in paused
    assert 'post_weapon_segment' not in paused and 'hand_clamp.' not in paused.replace('hand_clamp.config()', '')
    # Query lambda: the blade only (slot 1); the palm is the clamp's.
    assert 'd2vr::NativeInteractionAim::post_weapon_segment(side,1,a,b,std::max(.02,blade_radius));' in loop
    assert 'post_weapon_segment(side,k,' not in s
    # Events and haptics from either; contact frames from either.
    for need in ('constautoevent=r.event?r.event:clamped.event;', 'constautohaptic=r.haptic?r.haptic:clamped.haptic;',
                 'if(clamped.state==d2vr::ClampState::contact||r.contact)++weapon_contact_frames;', 'if(event){++weapon_contact_events;',
                 'if(event&&event->entity&&physical_grab_requested&&', 'if(haptic)game_xr->pulse(side,haptic->amplitude,haptic->seconds);'):
        assert need in clamp, need
    for gone in ('r.event->', 'if(r.event)', 'if(r.haptic)', 'cast_held'):
        assert gone not in clamp, gone
    # Swing speed (C9: attack detection) from the raw controller.
    assert 'constautorel=raw.position+raw.basis[0]*(side==1&&reverse_sword?-blade:blade)-' in loop
    # C14: the clamp's cast cost is noted once per game-thread scope; the hands alternate / drop the tip by CastBudget.
    after = clamp[clamp.index('hand_clamp_budget.note('):]
    assert 'clamp_cast_serial_.load();' in clamp and 'hand_clamp_budget.note(d2vr::NativeInteractionAim::clamp_cast_us_.load(),clamp_now);' in clamp
    assert 'hand_clamp.reset(0);hand_clamp.reset(1);' in after
    # Publication after the clamp (C9): drawn hand for the drop position, raw controller for throw velocity, press grips and reach.
    assert 'for(unsignedside=0;side<2;++side)if(hands.hands&(1u<<side))publish_hand(side,hands.grips[side],raw_grips[side]);' in pub
    assert 'staticvoidpublish_hand(unsignedside,constd2vr::WorldTrackedPose&drawn,constd2vr::WorldTrackedPose&raw){' in s
    producer = between(s, 'staticvoidpublish_hand(', 'staticboolnpc_entity(')
    assert 'window.emplace_back(qpc.QuadPart,raw.position);' in producer and '(raw.position-window.front().second)*(1/dt)' in producer
    assert 'hand_world[side&1]=drawn;' in producer and 'g.position' not in producer
    assert 'if((hands.hands>>side)&1u)note_grip(side,raw_grips[side],hands.curls[side]);' in s
    assert 'constauto&p=raw_grips[side].position;reach_hands[side]=' in s
    assert 'note_grip(side,hands.grips' not in s and 'constauto&p=hands.grips[side].position;reach_hands' not in s
    # The drawn consumers keep the drawn hand: the arm IK target and the solid hands' drawn pose (raw for their push velocity).
    assert 't.grip[side]=d2vr::fullbody::from_basis(hands.grips[side].position,' in s
    assert 'solid_hands.publish(side,can_push,hands.grips[side],raw_grips[side],' in s
    assert 'intent.grips=raw_grips;' in s
    # Reset with the weapon collision; the exclusion's +0x14 knob.
    assert 'weapon_collision=d2vr::WeaponCollision{};weapon_collision_qpc=0;hand_clamp=d2vr::HandClamp{};hand_clamp_budget.reset();' in s
    assert 'd2vr::NativeInteractionAim::clamp_exclude_info_zero_=fs::exists(root/L"build"/L"hand-clamp-exclude-info-zero");' in s
    # Status.
    status = between(s, '",\\"hand_clamp\\":{\\"active\\":', '",\\"hud_relayouts\\":')
    for field in ('contact_frames', 'snapbacks', 'reach_clamped', 'yields', 'missed_notes', 'repeated_updates', 'reacquire_blocked',
                  'reacquire_holds', 'unanswered', 'cast_us', 'cast_us_max', 'tip_probe', 'alternate', 'exclusion', 'self_hits', 'unmapped'):
        assert '\\"' + field + '\\":' in status, field
    assert s.index('",\\"weapon_collision\\":{') < s.index('",\\"hand_clamp\\":{\\"active\\":')

    # native_interaction_aim.hpp: slots, exclusion mapping, timing.
    a = compact(aim)
    for need in ('inlinestaticconstexprunsignedweapon_slots=14;', 'staticconstexprunsignedtip_slot=11,wall_tip_slot=12,exclude_slot=13;',
                 'returnslot==0||slot==tip_slot||slot==wall_tip_slot||slot==exclude_slot;',
                 'weapon_req_[side][slot]={from,to,radius,true,exclude};'):
        assert need in a, need
    stage = between(a, 'std::array<std::array<std::optional<WorldTraceHit>,weapon_slots>,2>hits{};', '++weapon_casts_;')
    for need in ('if(constautoprop=native_rigid_body(self->read_,self->base_,entity,true);prop&&prop->world==*world){',
                 'mapped=NativeCastExclusion{prop->record,prop->handle,info};', 'if(seg.exclude){', 'hits[side][slot]=cast(&*exclusion);',
                 '}else++clamp_exclude_unmapped_;', '}elsehits[side][slot]=cast(nullptr);',
                 'if(clamp_slot(slot)){clamp_ticks+=', 'clamp_cast_us_.store(', '++clamp_cast_serial_;',
                 'if(hits[side][slot]->body_handle==exclusion->handle)++clamp_exclude_self_hits_;else++clamp_exclude_hits_;'):
        assert need in stage, need


if __name__ == '__main__':
    root = Path(__file__).resolve().parents[1]
    dxgi = (root / 'src' / 'dxgi_probe.cpp').read_text(encoding='utf-8-sig')
    aim = (root / 'src' / 'native_interaction_aim.hpp').read_text(encoding='utf-8-sig')
    check(dxgi, aim)
    # Negative mutations: each breaks one routing rule and must be rejected.
    dxgi_mutations = (
        ('publish_hand(side,hands.grips[side],raw_grips[side]);', 'publish_hand(side,hands.grips[side],hands.grips[side]);'),
        ('note_grip(side,raw_grips[side],hands.curls[side]);', 'note_grip(side,hands.grips[side],hands.curls[side]);'),
        ('const auto& p=raw_grips[side].position;reach_hands[side]=', 'const auto& p=hands.grips[side].position;reach_hands[side]='),
        ('t.grip[side]=d2vr::fullbody::from_basis(hands.grips[side].position,', 't.grip[side]=d2vr::fullbody::from_basis(raw_grips[side].position,'),
        ('const auto target=clamp_yield || !reach_fresh?raw:', 'const auto target=!reach_fresh?raw:'),
        ('d2vr::clamp_yields(side,authored_hands,carry_bits,', 'd2vr::clamp_yields(side,0u,carry_bits,'),
        ('carry_bits,physical_grab_requested && native_grab.holding(side));', 'carry_bits,false);'),
        ('in.yield=clamp_yield;', 'in.yield=false;'),
        ('in.hand=probes.hand;in.hand_count=probes.count;', 'in.hand_count=0;'),
        ('in.tip=clamp_tip[side];in.wall_tip=clamp_wall_tip[side];', 'in.tip=clamp_tip[side];'),
        ('post(d2vr::NativeInteractionAim::exclude_slot,clamped.exclude_sweep);', ''),
        ('post(d2vr::NativeInteractionAim::wall_tip_slot,clamped.wall_tip_sweep);', ''),
        ('sweep.radius,sweep.exclude?*sweep.exclude:0);', 'sweep.radius);'),
        ('native_grab.hand_contact().fill_clamp_input(side,in,clamp_exclude[side]);', ''),
        ('clamp_exclude[side]=met.exclude;', ''),
        ('hand_clamp.note_drawn(side,clamp_root,g.position);', ''),
        ('const auto event=r.event?r.event:clamped.event;', 'const auto event=r.event;'),
        ('in.post=d2vr::clamp_posts(clamp_alternate,hand_clamp_frame,side);', 'in.post=true;'),
        ('hand_clamp_budget.note(d2vr::NativeInteractionAim::clamp_cast_us_.load(),clamp_now);', ''),
        ('hand_clamp=d2vr::HandClamp{};hand_clamp_budget.reset();', ''),
        ('const auto rel=raw.position+raw.basis[0]*', 'const auto rel=g.position+g.basis[0]*'),
        ('if(clamped.reacquired) if(const auto kept=', 'if(false) if(const auto kept='),
        ('const bool clamp_updated=clamp_live || clamp_yield;', 'const bool clamp_updated=true;'),
        ('const bool clamp_live=game_logic_mask()==2;', 'const bool clamp_live=true;'),
        ('if(clamp_updated) hand_clamp.note_drawn(', 'hand_clamp.note_drawn('),
    )
    aim_mutations = (
        ('weapon_slots=14;', 'weapon_slots=12;'),
        ('if(clamp_slot(slot)) {clamp_ticks+=', 'if(slot==0) {clamp_ticks+='),
        ('hits[side][slot]=cast(&*exclusion);', 'hits[side][slot]=cast(nullptr);'),
        ('weapon_req_[side][slot]={from,to,radius,true,exclude};', 'weapon_req_[side][slot]={from,to,radius,true};'),
    )
    for index, (needle, replacement) in enumerate(dxgi_mutations):
        assert needle in dxgi, f'dxgi mutation {index} missed its production target'
        try:
            check(dxgi.replace(needle, replacement), aim)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f'Unsafe dxgi routing mutation {index} was accepted')
    for index, (needle, replacement) in enumerate(aim_mutations):
        assert needle in aim, f'aim mutation {index} missed its production target'
        try:
            check(dxgi, aim.replace(needle, replacement))
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f'Unsafe cast-slot mutation {index} was accepted')
    print(f'PASS hand clamp C9/C14 source routing and {len(dxgi_mutations)+len(aim_mutations)} negative mutations (not runtime/native execution or headset proof)')
