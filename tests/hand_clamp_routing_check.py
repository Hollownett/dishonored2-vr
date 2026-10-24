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

    # Task 8 (G5/C6, fix round 1): the controller's poses are copied BEFORE the palm offset (C9: aim, press, selection, reach, throw and door intent stay
    # on the controller); the offset then makes palm_grips/hands.grips, which only the clamp, reach clamp, IK and drawn weapon read. The tilt applies to
    # empty hands only (weapon, carrying or holding), the blend advances for every hand tracked or not, and a throw never reaches the XR path.
    palm = between(s, 'unsignedweapon_bits{}', 'palm_grips=hands.grips;')
    assert s.index('raw_grips=hands.grips;') < s.index('unsignedweapon_bits{}') < s.index('palm_grips=hands.grips;')
    assert 'palm_occupied_bits(weapon_bits,carrying_bits,holding,hold_side,state_fresh)' in palm
    assert 'for(unsignedside=0;side<2;++side){constdoubletilt=palm_tilt[side].update(((occupied>>side)&1u)!=0,palm_dt);if(!(hands.hands&(1u<<side)))continue;try{' in palm
    assert 'hands.grips[side]=d2vr::apply_palm_offset(hands.grips[side],game_xr->palm_offset(side),tilt);' in palm
    assert palm.index('apply_palm_offset(') < palm.index('}catch(...){}')
    assert 'raw_grips=hands.grips' not in palm and 'raw_grips[side]=' not in palm
    # The raw poses are taken before anything moves the drawn hands.
    assert pub.index('raw_grips=hands.grips;') < pub.index('reach_clamp(') < pub.index('hand_clamp.update(')
    # Yields (C9): authored arms, a native carry on that side (fresh hand state), a physical hold. The reach clamp never applies on a yield.
    assert 'if(hand_holding_object&&hand_state_tick&&GetTickCount64()-hand_state_tick<=300)carry_bits=1u<<(hand_hold_side&1u);' in clamp
    for need in ('constboolclamp_yield=d2vr::clamp_yields(side,authored_hands,carry_bits,physical_grab_requested&&native_grab.holding(side))||(held_clamp_live.releasing&&held_clamp_live.side==(side&1u));',
                 'constautotarget=clamp_yield||!reach_fresh?palm:d2vr::reach_clamp(palm,arm_reach.reach[side],&reach_clamped);',
                 'in.yield=clamp_yield;', 'in.target=target;', 'in.root=clamp_root;', 'in.epoch=clamp_epoch;', 'in.chest=chest;',
                 'in.post=d2vr::clamp_posts(clamp_alternate,hand_clamp_frame,side);', 'in.tip_enabled=clamp_tip_on;'):
        assert need in loop, need
    assert 'reach_fresh=full_body_requested&&tracked_arms_requested&&arm_reach.body_valid&&arm_reach.output_age_seconds<.25;' in clamp
    assert 'constautoclamp_root=d2vr::level_frame({anchor.position[0],anchor.position[1],anchor.position[2]},{anchor.basis[0],anchor.basis[1],anchor.basis[2]});' in clamp
    assert 'constautoclamp_epoch=hands.epoch^(hands.reference_epoch<<32)^static_cast<std::uint64_t>(body->player);' in clamp
    assert 'constautoclamp_alternate=hand_clamp_budget.alternate();constautoclamp_tip_on=hand_clamp_budget.tip_enabled();' in clamp
    # The open-hand probe set feeds the leading probes and ClampInput::hand alike (Task 3 carry: else the wall-slide fix is off).
    for need in ('refresh_clamp_probes(side,palm);', 'clamp_tip[side]=d2vr::leading_probe(probes.hand,target,velocity,clamp_tip[side]);',
                 'clamp_wall_tip[side]=d2vr::wall_leading_probe(probes.hand,target,velocity,clamp_wall_tip[side]);',
                 'in.hand=probes.hand;in.hand_count=probes.count;', 'in.tip=clamp_tip[side];in.wall_tip=clamp_wall_tip[side];'):
        assert need in loop, need
    # Last frame's answers, routed by the clamp's last state; then the meet's loose list and exclusion, then the update.
    route = 'd2vr::route_clamp_hits(in,clamp_state[side],slot0_answer,answer(d2vr::NativeInteractionAim::tip_slot),answer(d2vr::NativeInteractionAim::wall_tip_slot),exclude_answer);'
    assert 'constautoslot0_answer=answer(0),exclude_answer=answer(d2vr::NativeInteractionAim::exclude_slot);' in loop
    assert route in loop
    assert loop.index(route) < loop.index('native_grab.hand_contact().fill_clamp_input(side,in,clamp_exclude[side]);') < loop.index('clamped=hand_clamp.update(side,in);clamp_state[side]=clamped.state;')
    # build/no-hand-clamp (M2): the pre-Task-5 drawn hand, exactly: chest palm sweep + blade, the meet() exemption. Only there.
    legacy = between(loop, 'if(!hand_clamp_enabled){', '}else{')
    for need in ('constautor=weapon_collision.update_with_palm(side,dt,chest,g.position,blade_axis,blade,query,blade_offset,blade_radius);',
                 'if(!r.contact)cast_held[side]=0;elseif(palm_hit)cast_held[side]=palm_hit->entity;',
                 'constautomeeting=native_grab.hand_contact().meet(side,cast_held[side]);',
                 'cast_entities[side]=meeting.entity?meeting.entity:cast_held[side];', 'if(r.contact&&!meeting.prop)g.position=r.grip;',
                 'if(r.contact)++weapon_contact_frames;', 'g.position=g.position+meeting.shift;', 'event=r.event;haptic=r.haptic;'):
        assert need in legacy, need
    assert 'hand_clamp.' not in legacy and 'clamp_hold' not in legacy and 'clamp_shot' not in legacy
    assert 'hand_clamp_enabled=!fs::exists(root/L"build"/L"no-hand-clamp");' in s
    assert 'staticboolhand_clamp_enabled=true;' in s
    # The query: palm (slot 0) then blade (slot 1) on the pre-clamp path, the blade alone (slot 1) with the clamp.
    for need in ('unsignedslot=hand_clamp_enabled?1u:0u;', 'constunsignedk=slot++;',
                 'd2vr::NativeInteractionAim::post_weapon_segment(side,k,a,b,k?std::max(.02,blade_radius):.05);',
                 'if(k==1)blade_hit=c;elseif(k==0)palm_hit=c;'):
        assert need in loop, need
    clamped_part = loop[loop.index(legacy) + len(legacy):]
    assert '.meet(side,' not in s.replace(legacy, '') and 'cast_held' not in clamp.replace(legacy, '')
    assert 'update_with_palm' not in s.replace(legacy, '')
    # Posts: palm/chest to slot 0, the tip, the wall tip and the exclusion sweep (with its excluded entity) to their slots; only while drawn.
    posts = between(clamped_part, 'if(clamped.valid&&!clamp_yield){constautopost=', 'constautomet=')
    for need in ('d2vr::NativeInteractionAim::post_weapon_segment(side,post_slot,sweep.from,sweep.to,sweep.radius,sweep.exclude?*sweep.exclude:0,met_body?met_body->bone:-1,met_body?met_body->handle:0u);',
                 'if(!sweep.valid||(sweep.exclude&&!*sweep.exclude))return;', 'post(0,clamped.palm_sweep);',
                 'post(d2vr::NativeInteractionAim::tip_slot,clamped.tip_sweep);', 'post(d2vr::NativeInteractionAim::wall_tip_slot,clamped.wall_tip_sweep);',
                 'post(d2vr::NativeInteractionAim::exclude_slot,clamped.exclude_sweep);'):
        assert need in posts, need
    # The meet composes with the clamp (Task 3: meet_clamp); the re-acquire frame holds the hand.
    meet = between(clamped_part, 'constautomet=native_grab.hand_contact().meet_clamp(side,hand_clamp,clamped,target);', 'constautor=')
    for need in ('g.position=met.drawn;', 'held_by=met.held_by;', 'clamp_exclude[side]=met.exclude;', 'met_prop=met.prop;',
                 'if(clamped.reacquired)if(constautokept=clamp_hold.hold(side,clamp_epoch,clamp_root,target.position,hand_clamp.config().snap_gap)){g.position=*kept;',
                 '}else{clamp_exclude[side].reset();clamp_hold.clear(side);}', 'cast_entities[side]=held_by;'):
        assert need in meet, need
    # The blade sweeps from the drawn grip; compose_blade (M1): the hand drawn kept out of the planes, the clamp told its own hand.
    blade = 'constautor=weapon_collision.update(side,dt,g.position,blade_axis,blade,query,blade_offset,blade_radius);'
    compose = 'constautocomposed=d2vr::compose_blade(hand_clamp,side,g.basis,g.position,r,met_prop);g.position=composed.drawn;'
    note = 'if(clamp_updated)hand_clamp.note_drawn(side,clamp_root,composed.noted);'
    assert blade in clamped_part and compose in clamped_part and note in clamped_part
    assert clamped_part.index('clamped=hand_clamp.update(side,in);clamp_state[side]=clamped.state;') < clamped_part.index('constautomet=') \
        < clamped_part.index(blade) < clamped_part.index(compose) < clamped_part.index(note)
    assert clamped_part.index('d2vr::align_weapon_blade(g,*r.blade_dir,side==1&&reverse_sword);') < clamped_part.index(note)
    assert 'note_drawn(side,clamp_root,g.position)' not in s and 'g.position=r.grip;' not in clamped_part
    assert 'constboolclamp_drew=clamp_updated&&clamped.valid&&!clamp_yield;if(clamp_drew||!clamp_updated)clamp_hold.note(side,clamp_epoch,clamp_root,g.position);' in clamped_part
    # Casts are answered only in gameplay (Stage0, logic 2). In a world UI the hand follows the controller kept out of the held planes
    # (I1); the hold is for the resume (re-acquire) frame only.
    assert 'constboolclamp_live=game_logic_mask()==2;' in clamp
    gate = 'd2vr::ClampOutputclamped{};constboolclamp_updated=clamp_live||clamp_yield;boolclamp_post=false;if(clamp_updated){d2vr::ClampInputin{};'
    assert gate in clamped_part and clamped_part.index(gate) < clamped_part.index(route)
    paused = between(clamped_part, 'if(!clamp_updated){', '}elseif(clamped.valid&&!clamp_yield){constautopost=')
    assert 'g.position=d2vr::paused_hand(hand_clamp,side,palm,clamp_epoch,hand_clamp.config().snap_gap);' in paused  # N1: guarded keep-out
    assert 'keep_out(' not in paused
    assert 'post_weapon_segment' not in paused and 'clamp_hold' not in paused and 'update(' not in paused
    assert clamped_part.count('clamp_hold.hold(') == 1
    # I2: while clamped, shots and the crosshair leave from the drawn barrel (direction untouched: the drawn barrel, v361).
    assert 'constautoshot=d2vr::clamped_shot_offset(g.position,palm.position,clamp_drew,now,game_xr->pose_epoch(),&hand_clamp,side);' in clamped_part  # N5: held planes
    assert 'clamp_shot[side]=shot;' in clamped_part
    # N5: the hand-aimed left aim is published once, by publish_left_aim (guarded origin, then the publishes), after the hands when they
    # are published this frame (this frame's planes), else at once; a scope guard flushes it at the latest.
    publish = between(s, 'constautopublish_left_aim=[&](d2vr::WorldTrackedPoseleft){', '};')
    origin = 'left.position=d2vr::guarded_shot_origin(released_shot,clamp_shot[0],0,left.position,GetTickCount64(),game_xr->pose_epoch());'
    assert publish.index(origin) < publish.index('native_left_aim.publish(*body,left,') < publish.index('native_shot_aim.publish(left,') \
        < publish.index('native_gameplay_aim_trace.publish(*body,left,')
    assert 'left.basis' not in publish
    # The pose (barrel, offset, smoothing, jitter filter) is built once; shots/powers get it only through the strict gate, the crosshair
    # through its own (valid pose, game-owned view) gate, so it stays visible while attacking or gripping.
    branch = between(s, 'constautogate=d2vr::ranged_aim_gate(gate_in);', 'if(swing_tip){')
    assert 'barrel_aim(left);aim_offset(left);' in branch and branch.index('left=*steady;') < branch.index('if(gate.shots)ranged_left=left;') < branch.index('if(gate.reticle)reticle_left=left;')
    gate_in = between(s, 'd2vr::RangedAimInputsgate_in{};', 'constautogate=')
    assert 'gate_in.right_trigger=game_xr->controller_input.right_trigger;' in gate_in and 'gate_in.left_grip=game_xr->controller_input.left_grip;' in gate_in
    ranged_tail = between(s, 'if(ranged_left||reticle_left){', '}if(render_world_scene()&&(tracked_arms_requested||native_weapon_attachment_requested)&&per_frame)')
    assert ranged_tail.index('aim_for_shots=ranged_left.has_value();') < ranged_tail.index('pending_left_aim=aim_pose;') < ranged_tail.index('elsepublish_left_aim(aim_pose);')
    assert publish.index('d2vr::finalize_aim_ray(left,') > publish.index(origin) and publish.index('if(!aim_for_shots){native_shot_aim.clear();return;}') < publish.index('native_left_aim.publish(*body,left,')
    assert 'native_shot_aim.publish(' not in branch and 'native_left_aim.publish(' not in branch and 'native_shot_aim.publish(' not in ranged_tail
    assert s.count('native_shot_aim.publish(') == 1
    flush = 'publish_hand(side,hands.grips[side],raw_grips[side]);}pending_publish.flush();'
    assert flush in s and s.index(flush) > s.index('hand_clamp_budget.note(d2vr::NativeInteractionAim::clamp_cast_us_.load(),clamp_now);')
    assert '~PendingLeftAim(){flush();}' in s and 'try{publish(left);}catch(...){}' in s
    # The reticle projects the exact finalized float ray (guarded origin included), flushed after the hands and before the projection.
    cross = between(s, 'pending_publish.flush();//completeguardedfloatraybeforereticleprojection', 'native_crosshair.publish(')
    assert 'shared_aim_ray->current(' in cross and 'constautotarget=shared_aim_ray->target(30.f);' in cross
    assert publish.index(origin) < publish.index('shared_aim_ray=receipt;') and 'hand.basis=' not in cross
    # Ruling 1 (snap-back): the released guard reads last frame's slot 0 and tip-slot answers before the update, and after it posts its
    # eye -> hand sweep on the tip slot (unused by a released side); the shot origin uses it while the side is released.
    guard_read = '{std::lock_guard<std::mutex>hand_lock(hand_state_mutex);released_shot.read(side,slot0_answer,exclude_answer,now);}'
    assert guard_read in loop and loop.index(guard_read) < loop.index(route) < loop.index('clamped=hand_clamp.update(side,in);')
    guard_post = 'eye_sweep=released_shot.posted(side,clamp_drew,clamped,clamp_post,{anchor.position[0],anchor.position[1],anchor.position[2]},target.position,now,game_xr->pose_epoch());'
    assert guard_post in clamped_part and clamped_part.index('clamped=hand_clamp.update(side,in);') < clamped_part.index(guard_post)
    assert 'if(eye_sweep){d2vr::NativeInteractionAim::post_weapon_segment(side,d2vr::NativeInteractionAim::exclude_slot,eye_sweep->from,eye_sweep->to,eye_sweep->radius);' in clamped_part  # N7
    # N4: an epoch change or a world-UI pause restarts the C14 window. N6: a load forgets the clamp (non-world frames of prepare_head_view).
    assert '{staticstd::uint64_tbudget_epoch{};if(clamp_epoch!=budget_epoch||!clamp_live){hand_clamp_budget.reset_window();budget_epoch=clamp_epoch;}}' in clamp
    nonworld = between(s, 'if(!render_world_scene()){native_audio_listener.clear();', 'return;}')
    assert 'if(d2vr::clamp_forgets_on(game_logic_mask()))forget_hand_clamp();' in nonworld
    forget = between(s, 'staticvoidforget_hand_clamp()noexcept{', 'staticvoidrefresh_clamp_probes(')
    assert 'reset_clamp_state();' in forget and 'hand_clamp_budget.reset_window();' in forget
    # Final review P3: one reset of everything the clamp keeps (planes, held hand, exclusion, state, shot offsets under hand_state_mutex),
    # used by a load, an authored camera and StartProbe alike.
    reset = between(s, 'staticvoidreset_clamp_state()noexcept{', 'staticvoidforget_hand_clamp()noexcept{')
    for need in ('hand_clamp.reset(0);hand_clamp.reset(1);', 'clamp_hold.clear(0);clamp_hold.clear(1);', 'clamp_exclude={};', 'clamp_state={};',
                 'std::lock_guard<std::mutex>hand_lock(hand_state_mutex);', 'released_shot.clear(0);released_shot.clear(1);clamp_shot={};'):
        assert need in reset, need
    assert reset.index('std::lock_guard<std::mutex>hand_lock(hand_state_mutex);') < reset.index('clamp_shot={};')
    authored = between(s, 'nothingstoredisreused(C9)', 'for(unsignedside=0;side<2;++side)if(hands.hands&(1u<<side))publish_hand(')
    assert authored.startswith('nothingstoredisreused(C9)reset_clamp_state();')
    assert 'hand_clamp=d2vr::HandClamp{};hand_clamp_budget.reset();reset_clamp_state();' in s
    assert 'clamped=hand_clamp.update(side,in);clamp_state[side]=clamped.state;clamp_post=in.post;' in clamped_part
    # M2 scope: with build/no-hand-clamp the dxgi path bypasses the clamp entirely (legacy branch first, the clamp only in the else);
    # the shot origin is then raw (neither clamp_shot nor released_shot is written), and the C9 raw routing is outside both branches.
    assert s.count('hand_clamp.update(') == 1 and 'hand_clamp.update(' in clamped_part
    assert loop.index('if(!hand_clamp_enabled){') < loop.index('}else{') < loop.index('hand_clamp.update(')
    for name in ('hand_clamp.', 'released_shot', 'clamp_shot', 'clamp_hold', 'compose_blade', 'paused_hand'):
        assert name not in legacy, name
    assert s.count('clamp_shot[side]=') == 1 and 'clamp_shot[side]=' in clamped_part and s.count('released_shot.posted(') == 1
    for raw_route in ('publish_hand(side,hands.grips[side],raw_grips[side]);', 'note_grip(side,raw_grips[side],', 'constauto&p=raw_grips[side].position;reach_hands'):
        assert raw_route in s and raw_route not in loop, raw_route   # after the per-side branches: the same in both modes
    assert loop.index('constautorel=raw.position+raw.basis[0]*') < loop.index('if(!hand_clamp_enabled){')   # swing speed raw in both modes
    # M4 (C9 selection): sword pickup reach and the wake/yank press read the controller; the sword drop position stays drawn.
    for need in ('in.copy_distance=d2vr::length(at-hand_raw.position);', 'in.pickup_dot=d2vr::dot(hand_raw.basis[0],', 'hand_raw=hand_raw_world[1];',
                 'w=hand_raw_world[h];v=hand_velocity[h];', 'r.pos={fl(hand_w.position.x),fl(hand_w.position.y),fl(hand_w.position.z)};'):
        assert need in s, need
    assert 'w=hand_world[h];' not in s and 'length(at-hand_w.position)' not in s
    # M5: a default wall tip without hand geometry.
    assert 'clamp_wall_tip{d2vr::default_clamp_wall_tip(),d2vr::default_clamp_wall_tip()};' in s
    assert 'clamp_wall_tip[s]=d2vr::default_clamp_wall_tip();' in s
    # Events and haptics from either; contact frames from either.
    for need in ('event=r.event?r.event:clamped.event;haptic=r.haptic?r.haptic:clamped.haptic;',
                 'if(clamped.state==d2vr::ClampState::contact||r.contact)++weapon_contact_frames;', 'if(event){++weapon_contact_events;',
                 'if(event&&event->entity&&physical_grab_requested&&', 'if(haptic)game_xr->pulse(side,haptic->amplitude,haptic->seconds);'):
        assert need in clamp, need
    for gone in ('r.event->', 'if(r.event)', 'if(r.haptic)'):
        assert gone not in clamp, gone
    # Swing speed (C9: attack detection) from the raw controller.
    assert 'constautorel=raw.position+raw.basis[0]*(side==1&&reverse_sword?-blade:blade)-' in loop
    # C14: the clamp's cast cost is noted once per game-thread scope; the hands alternate / drop the tip by CastBudget.
    after = clamp[clamp.index('hand_clamp_budget.note('):]
    assert 'clamp_cast_serial_.load();' in clamp and 'hand_clamp_budget.note(d2vr::NativeInteractionAim::clamp_cast_us_.load(),clamp_now);' in clamp
    # Publication after the clamp (C9): drawn hand for the drop position, raw controller for throw velocity, press grips and reach.
    assert 'for(unsignedside=0;side<2;++side)if(hands.hands&(1u<<side))publish_hand(side,hands.grips[side],raw_grips[side]);' in pub
    assert 'staticvoidpublish_hand(unsignedside,constd2vr::WorldTrackedPose&drawn,constd2vr::WorldTrackedPose&raw){' in s
    producer = between(s, 'staticvoidpublish_hand(', 'staticboolnpc_entity(')
    assert 'window.emplace_back(qpc.QuadPart,raw.position);' in producer and '(raw.position-window.front().second)*(1/dt)' in producer
    assert 'hand_world[side&1]=drawn;hand_raw_world[side&1]=raw;' in producer and 'g.position' not in producer
    assert 'if((hands.hands>>side)&1u)note_grip(side,raw_grips[side],hands.curls[side]);' in s
    assert 'constauto&p=raw_grips[side].position;reach_hands[side]=' in s
    assert 'note_grip(side,hands.grips' not in s and 'constauto&p=hands.grips[side].position;reach_hands' not in s
    # The drawn consumers keep the drawn hand: the arm IK target and the solid hands' drawn pose (raw for their push velocity).
    assert 't.grip[side]=d2vr::fullbody::from_basis(hands.grips[side].position,' in s
    assert 'solid_hands.publish(side,can_push,hands.grips[side],palm_grips[side],' in s
    assert 'intent.grips=raw_grips;' in s
    # Reset with the weapon collision; the exclusion's +0x14 knob.
    assert 'weapon_collision=d2vr::WeaponCollision{};weapon_collision_qpc=0;hand_clamp=d2vr::HandClamp{};hand_clamp_budget.reset();reset_clamp_state();' in s
    assert 'd2vr::NativeInteractionAim::clamp_exclude_info_zero_=fs::exists(root/L"build"/L"hand-clamp-exclude-info-zero");' in s
    # Status.
    status = between(s, '",\\"hand_clamp\\":{\\"active\\":', '",\\"hud_relayouts\\":')
    for field in ('contact_frames', 'snapbacks', 'reach_clamped', 'yields', 'missed_notes', 'repeated_updates', 'reacquire_blocked',
                  'reacquire_holds', 'unanswered', 'cast_us', 'cast_us_max', 'tip_probe', 'alternate', 'enabled', 'restore_factor', 'recover_wait', 'released_shots', 'exclusion', 'self_hits', 'unmapped'):
        assert '\\"' + field + '\\":' in status, field
    assert s.index('",\\"weapon_collision\\":{') < s.index('",\\"hand_clamp\\":{\\"active\\":')

    # native_interaction_aim.hpp: slots, exclusion mapping, timing.
    a = compact(aim)
    for need in ('inlinestaticconstexprunsignedweapon_slots=15;', 'staticconstexprunsignedtip_slot=11,wall_tip_slot=12,exclude_slot=13;', 'staticconstexprunsignedhand_body_free_slot=14;',
                 'returnslot==0||slot==tip_slot||slot==wall_tip_slot||slot==exclude_slot;',
                 'weapon_req_[side][slot]={from,to,radius,true,exclude,exclude_bone,exclude_handle,producer};'):
        assert need in a, need
    stage = between(a, 'std::array<std::array<std::optional<WorldTraceHit>,weapon_slots>,2>hits{};', '++weapon_casts_;')
    # I1: a guard's limb maps too (native_cast_exclusion); an unmapped exclusion casts the sweep without it, never drops it.
    for need in ('automapped=native_cast_exclusion(self->read_,self->base_,*world,entity,seg.exclude_bone,seg.exclude_handle,', 'NativeHeldObject::exclusion_body(self->base_,self->read_,entity)', 'exclusion_of(seg.exclude,side,seg)', 'excluded.get(entity,side,seg.exclude_bone,seg.exclude_handle,',
                 'kWorldOccluderLayers,kWorldOccluderRequired,exclusion,clamp_slot(slot));', 'if(seg.exclude){', 'hits[side][slot]=cast(&*exclusion);',
                 '}else{++clamp_exclude_unmapped_;hits[side][slot]=cast(nullptr);}', '}elsehits[side][slot]=cast(nullptr);',
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
        ('const auto target=clamp_yield || !reach_fresh?palm:', 'const auto target=!reach_fresh?palm:'),
        ('d2vr::clamp_yields(side,authored_hands,carry_bits,', 'd2vr::clamp_yields(side,0u,carry_bits,'),
        ('carry_bits,physical_grab_requested && native_grab.holding(side))', 'carry_bits,false)'),
        ('in.yield=clamp_yield;', 'in.yield=false;'),
        ('in.hand=probes.hand;in.hand_count=probes.count;', 'in.hand_count=0;'),
        ('in.tip=clamp_tip[side];in.wall_tip=clamp_wall_tip[side];', 'in.tip=clamp_tip[side];'),
        ('post(d2vr::NativeInteractionAim::exclude_slot,clamped.exclude_sweep);', ''),
        ('post(d2vr::NativeInteractionAim::wall_tip_slot,clamped.wall_tip_sweep);', ''),
        ('sweep.radius,sweep.exclude?*sweep.exclude:0,met_body?met_body->bone:-1,', 'sweep.radius,0,-1,'),
        ('native_grab.hand_contact().fill_clamp_input(side,in,clamp_exclude[side]);', ''),
        ('clamp_exclude[side]=met.exclude;', ''),
        ('if(clamp_updated) hand_clamp.note_drawn(side,clamp_root,composed.noted);', ''),
        ('hand_clamp.note_drawn(side,clamp_root,composed.noted);', 'hand_clamp.note_drawn(side,clamp_root,composed.drawn);'),
        ('g.position=composed.drawn;', 'g.position=r.grip;'),
        ('event=r.event?r.event:clamped.event;', 'event=r.event;'),
        ('in.post=d2vr::clamp_posts(clamp_alternate,hand_clamp_frame,side);', 'in.post=true;'),
        ('hand_clamp_budget.note(d2vr::NativeInteractionAim::clamp_cast_us_.load(),clamp_now);', ''),
        ('hand_clamp=d2vr::HandClamp{};hand_clamp_budget.reset();', ''),
        ('const auto rel=raw.position+raw.basis[0]*', 'const auto rel=g.position+g.basis[0]*'),
        ('if(clamped.reacquired) if(const auto kept=', 'if(false) if(const auto kept='),
        ('const bool clamp_updated=clamp_live || clamp_yield;', 'const bool clamp_updated=true;'),
        ('const bool clamp_live=game_logic_mask()==2;', 'const bool clamp_live=true;'),
        ('g.position=d2vr::paused_hand(hand_clamp,side,palm,clamp_epoch,hand_clamp.config().snap_gap);',
         'if(const auto kept=clamp_hold.hold(side,clamp_epoch,clamp_root,target.position,hand_clamp.config().snap_gap)) g.position=*kept;'),
        ('if(clamp_drew || !clamp_updated) clamp_hold.note(', 'if(clamp_drew) clamp_hold.note('),
        ('left.position=d2vr::guarded_shot_origin(released_shot,clamp_shot[0],0,left.position,GetTickCount64(),game_xr->pose_epoch());', ''),
        ('pending_left_aim=aim_pose;', 'publish_left_aim(aim_pose);'),
        ('            pending_publish.flush(); // the left aim, with this frame\'s clamp state\n', ''),
        (',game_xr->pose_epoch(),&hand_clamp,side);', ',game_xr->pose_epoch());'),
        ('post_weapon_segment(side,d2vr::NativeInteractionAim::exclude_slot,eye_sweep->from', 'post_weapon_segment(side,d2vr::NativeInteractionAim::tip_slot,eye_sweep->from'),
        ('if(clamp_epoch!=budget_epoch || !clamp_live) {hand_clamp_budget.reset_window();', 'if(clamp_epoch!=budget_epoch) {'),
        ('if(d2vr::clamp_forgets_on(game_logic_mask())) forget_hand_clamp();', ''),
        ('    hand_clamp.reset(0);hand_clamp.reset(1);clamp_hold.clear(0);clamp_hold.clear(1);clamp_exclude={};', '    clamp_hold.clear(0);clamp_hold.clear(1);clamp_exclude={};'),
        ('const auto target=shared_aim_ray->target(30.f);', 'const auto target=world.position;'),
        ('if(!aim_for_shots) {native_shot_aim.clear();return;}', ''),
        ('if(gate.reticle) reticle_left=left;', ''),
        ('if(gate.shots) ranged_left=left;', 'ranged_left=left;'),
        ('{std::lock_guard<std::mutex> hand_lock(hand_state_mutex);released_shot.read(side,slot0_answer,exclude_answer,now);}', ''),
        ('if(eye_sweep) {d2vr::NativeInteractionAim::post_weapon_segment(', 'if(false) {d2vr::NativeInteractionAim::post_weapon_segment('),
        ('clamp_state[side]=clamped.state;clamp_post=in.post;', 'clamp_state[side]=clamped.state;'),
        ('g.position=d2vr::paused_hand(hand_clamp,side,palm,clamp_epoch,hand_clamp.config().snap_gap);', 'g.position=hand_clamp.keep_out(side,palm.basis,palm.position,std::nullopt);'),
        ('d2vr::clamped_shot_offset(g.position,palm.position,clamp_drew,', 'd2vr::clamped_shot_offset(g.position,palm.position,false,'),
        ('in.copy_distance=d2vr::length(at-hand_raw.position);', 'in.copy_distance=d2vr::length(at-hand_w.position);'),
        ('w=hand_raw_world[h];v=hand_velocity[h];', 'w=hand_world[h];v=hand_velocity[h];'),
        ('hand_clamp_enabled=!fs::exists(root/L"build"/L"no-hand-clamp");', 'hand_clamp_enabled=true;'),
        ('const auto r=weapon_collision.update_with_palm(side,dt,chest,', 'const auto r=weapon_collision.update(side,dt,'),
        ('unsigned slot=hand_clamp_enabled?1u:0u;', 'unsigned slot=1u;'),
        ('clamp_wall_tip{d2vr::default_clamp_wall_tip(),d2vr::default_clamp_wall_tip()};', 'clamp_wall_tip{};'),
    )
    aim_mutations = (
        ('weapon_slots=15;', 'weapon_slots=12;'),
        ('if(clamp_slot(slot)) {clamp_ticks+=', 'if(slot==0) {clamp_ticks+='),
        ('hits[side][slot]=cast(&*exclusion);', 'hits[side][slot]=cast(nullptr);'),
        ('kWorldOccluderRequired,exclusion,clamp_slot(slot));', 'kWorldOccluderRequired,exclusion);'),
        ('else {++clamp_exclude_unmapped_;hits[side][slot]=cast(nullptr);}', 'else {++clamp_exclude_unmapped_;}'),
        ('native_cast_exclusion(self->read_,self->base_,*world,entity,seg.exclude_bone,', 'native_rigid_body_only(self->read_,self->base_,*world,entity,seg.exclude_bone,'),
        ('weapon_req_[side][slot]={from,to,radius,true,exclude,exclude_bone,exclude_handle,producer};', 'weapon_req_[side][slot]={from,to,radius,true,exclude,exclude_bone,exclude_handle};'),
    )
    # Swapping the raw copy behind the palm offset (raw would carry the calibration) must be rejected.
    raw_line = 'raw_grips=hands.grips; // the tracked poses, before the palm offset and before world collision moves the drawn hands\n'
    assert raw_line in dxgi
    swapped = dxgi.replace(raw_line, '', 1).replace('palm_grips=hands.grips;', raw_line + 'palm_grips=hands.grips;', 1)
    try:
        check(swapped, aim)
    except (AssertionError, ValueError):
        pass
    else:
        raise AssertionError('raw copy after the palm offset was accepted')
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
