from pathlib import Path


root = Path(__file__).resolve().parents[1]


def read(path):
    return (root / path).read_text(encoding='utf-8-sig')


def section(text, start, end):
    first = text.index(start)
    return text[first:text.index(end, first + len(start))]


def ordered(text, *tokens):
    position = 0
    for token in tokens:
        position = text.index(token, position) + len(token)


source = read('src/dxgi_probe.cpp')
interaction = read('src/native_interaction_aim.hpp')
attachment = read('src/native_attachment_pose.hpp')
mailbox = read('src/native_full_hand_contact.hpp')
launch = read('tools/run_demo.ps1')
upload = section(source, 'void invoke_arm_upload(void* model,const void* source,UINT count,FullHandUploadWork* contact) {',
                 'void observe_arm_upload(')
prepare = section(upload, 'const auto full_hand_producer=', 'const auto consume_native=')
terminal = upload[upload.index('if(contact && contact->receipt) {'):]
consumer = section(upload, 'const auto consume_native=[&] {', '\n    };\n    if(contact && contact->receipt)')

assert upload.count('original_arm_upload(') == 1
assert 'original_arm_upload(model,submitted,count);' in consumer
entry = section(prepare, 'palette=posed->joints;', '// The item reader and skinning must use the same palette.')
ordered(entry, 'bool full_hand_empty{};',
        'if(contact && native_interaction_aim.full_hand_collision() && full_hand_producer)',
        'native_player_camera(read_self,image_base,full_hand_producer->owner.camera)',
        'camera && !camera->authored_view()?d2vr::NativeDoorEmptyGate::capture(',
        'image_base,read_self,*camera,native_grab,GetTickCount64(),1u<<side)',
        'full_hand_empty=empty && empty->hands.arms==snapshot->arms && empty->epoch==t.pose_epoch &&',
        'full_hand_contact().producer_current(*full_hand_producer)',
        'if(contact && native_interaction_aim.full_hand_collision() && !full_hand_empty)++full_hand_ineligible_baseline;',
        'if(contact && native_interaction_aim.full_hand_collision() && full_hand_empty) {',
        'full_body.prepare_contact_arm(', 'contact->raw_selected=true;', 'palette=*posed->raw_joints;')
eligible_entry = entry[entry.index('if(contact && native_interaction_aim.full_hand_collision() && full_hand_empty) {'):]
assert eligible_entry.count('full_body.prepare_contact_arm(') == 1
assert entry[:entry.index('if(contact && native_interaction_aim.full_hand_collision() && full_hand_empty) {')].count('palette=') == 1
assert 'if(owner.valid() && query_source && contact && contact->raw_selected && contact->prepared)' in prepare
assert 'if(owner.valid() && query_source && contact && contact->prepared)' not in prepare
ordered(prepare, 'full_hand_contact().producer(side)', 'full_body.prepare_contact_arm(',
        'contact->raw_selected=true;', 'palette=*posed->raw_joints;contact->fallback=palette;', 'full_hand_contact().candidate(',
        'full_hand_upload_current(*contact,contact->receipt->sample())', 'full_hand_targets_current(*contact->receipt)',
        'FullHandQueryTransaction::rebase(contact->receipt->result(),',
        'contact->receipt->sample().prepared->model_frame(),native->model_frame,', 'if(rebased)', 'palette=*rebased;',
        'origin.admission={&native_interaction_aim.full_hand_contact(),contact->receipt->id(),',
        '&d2vr::NativeFullHandContact::acquire_attachment,&d2vr::NativeFullHandContact::release_attachment',
        'attachment_pose.publish(side,', 'palette,origin)', 'sample.producer=*full_hand_producer;',
        'sample.prepared=contact->prepared;', 'contact->raw_sample=std::move(sample);',
        'd2vr::NativeFullHandContact::ProducerReceipt admitted{};',
        'if(native_interaction_aim.full_hand_contact().publish(*contact->raw_sample,GetTickCount64(),&admitted))',
        'contact->raw_sample->producer=admitted;', 'submitted=palette.data();')
raw_publication = section(prepare, 'contact->raw_sample=std::move(sample);', 'if(raw_admission &&')
assert 'contact->raw_sample.reset()' not in raw_publication
assert 'sample.tracking.raw_grip[side]=d2vr::body::mul(posed->world_from_solve,t.raw_grip[side]);' in prepare
assert 'sample.proxies=*proxies;' in prepare
assert 'geometry->proxies(*posed->raw_joints,native->model_frame)' in prepare
assert 'if(!accepted && contact) {contact->receipt.reset();contact->raw_selected=false;}' in prepare
work = section(source, 'struct FullHandUploadWork {', 'static FullHandUploadWork* full_hand_upload_work(')
assert 'std::optional<d2vr::NativeFullHandContact::Sample> raw_sample;' in work
assert 'bool raw_selected{};' in work
assert 'contact->source=origin;' in prepare
assert prepare.index('contact->source=origin;') < prepare.index('origin.admission=')

targets = section(source, 'static bool full_hand_targets_current(', 'void invoke_arm_upload(')
assert 'for(const auto& target:receipt.result().raw_targets)' in targets
assert 'for(const auto& target:receipt.result().final_targets)' in targets
assert targets.count('FullHandTargetWitness::current(reader,receipt.sample().source,target)') == 2
ordered(terminal, 'if(mailbox.consume(*contact->receipt,GetTickCount64(),',
        'full_hand_upload_current(*contact,sample) && full_hand_targets_current(*contact->receipt)',
        '[&](const auto&){consume_native();++full_hand_clamped;}))return;', '++full_hand_fallback;',
        '\n    }\n    if(contact && contact->raw_selected) {',
        'contact->raw_sample && mailbox.producer_current(contact->raw_sample->producer)',
        'full_hand_upload_current(*contact,*contact->raw_sample,true)',
        'mailbox.producer_current(contact->raw_sample->producer)',
        'contact->slots,contact->fallback,contact->source)', 'palette=contact->fallback;submitted=palette.data();',
        'attachment_pose.clear(contact->side);', 'submitted=source;', 'consume_native();')
raw_terminal = terminal[terminal.index('if(contact && contact->raw_selected) {'):]
assert raw_terminal.count('mailbox.producer_current(contact->raw_sample->producer)') == 2
assert 'contact->receipt' not in raw_terminal
assert 'contact->receipt->sample(),true' not in terminal
assert 'if(!contact->receipt)++full_hand_receipt_missing;' in prepare
assert 'else if(!contact->receipt->result().contact)++full_hand_clear_receipts;' in prepare
assert 'resolve_full_hand(' not in source
current_upload = section(source, 'static bool full_hand_upload_current(', 'static bool full_hand_targets_current(')
assert 'NativeDoorEmptyGate::capture(image_base,read_self,*camera,native_grab,now,1u<<work.side)' in current_upload
assert 'if(!gate || gate->hands.arms!=work.binding->arms || gate->epoch!=sample.tracking.pose_epoch)return false;' in current_upload
assert 'final_now-tracking.tick<=32 && final_now-tracking.source_tick<=32' in current_upload


def routing_model(*, typed_empty, held=False, carry=False, armed=False, prepared=True, terminal_current=True):
    palette = 'baseline-posed'
    attachment = 'baseline-tracked'
    raw_selected = bool(typed_empty and not held and not carry and not armed and prepared)
    raw_published = raw_selected
    if raw_selected:
        if terminal_current:
            palette = 'raw-or-corrected'
            attachment = 'experimental-tracked'
        else:
            palette = 'original-native'
            attachment = 'cleared'
    return palette, attachment, raw_published


for occupied_kind in ('held', 'carry', 'armed'):
    assert routing_model(typed_empty=True, **{occupied_kind: True}) == ('baseline-posed', 'baseline-tracked', False)
assert routing_model(typed_empty=False) == ('baseline-posed', 'baseline-tracked', False)
assert routing_model(typed_empty=True, prepared=False) == ('baseline-posed', 'baseline-tracked', False)
assert routing_model(typed_empty=True) == ('raw-or-corrected', 'experimental-tracked', True)
assert routing_model(typed_empty=True, terminal_current=False) == ('original-native', 'cleared', True)

assert 'NativeAttachmentAdmission admission{};' in attachment
ordered(attachment, 'frame.admitted_source=side->source;', 'frame.unlock();',
        'admission.acquire(admission.context,admission.receipt)', 'frame.admitted=true;mutex_.lock();',
        'same_source(side->source,frame.admitted_source)', 'frame_current(anim,side->source,receipt_frame_exact(side->source))', 'frame.written=copy_slot(')
cleanup = section(attachment, 'const auto cleanup=[](void* raw) noexcept {', 'return invoke_cleanup(')
ordered(cleanup, 'restore_slot(', 'value->unlock();', 'admission.release(admission.context,admission.receipt);')
assert 'frame_current(s.animation,s.source,receipt_frame_exact(s.source))' in attachment
assert 'NativeAttachmentReceiptFramePolicy receipt_frame_policy{NativeAttachmentReceiptFramePolicy::exact}' in attachment
assert 'first.receipt_frame_policy==second.receipt_frame_policy' in attachment
assert '!receipt_policy_valid(source)' in attachment
assert '!frame_current(animation,source,true)' in attachment
assert 'source.admission.receipt && source.receipt_frame_policy==NativeAttachmentReceiptFramePolicy::exact' in attachment
assert 'static bool acquire_attachment(void* context,std::uint64_t receipt) noexcept' in mailbox
assert 'static void release_attachment(void* context,std::uint64_t receipt) noexcept' in mailbox
assert 'bool producer_current(const ProducerReceipt&' in mailbox
assert 'ProducerReceipt* admitted=' in mailbox

assert source.count('native_grab.require_contact_proxies(false);') == 1
assert 'require_contact_proxies(true)' not in source
ordinary = section(source, 'const auto& raw=game_xr->controller_input;', 'static int native_lock_hand=')
doors = section(interaction, 'void service_door_contact(', 'struct StageFrame {')
assert 'use_input' in ordinary and 'grip_intent_out' in ordinary
assert 'full_hand' not in ordinary and 'full_hand' not in doors
assert ('if(synchronized_observer.enabled() || native_interaction_aim.anatomical_shadow_enabled() || '
        'native_interaction_aim.full_hand_collision())for(unsigned side=0;side<2;++side)if((hands.hands>>side)&1u)') in source
ordered(source[source.index('t.epoch=hands.epoch^(hands.reference_epoch<<32);'):],
        'const auto& raw=raw_grips[side];', 't.raw_grip[side]=', 't.raw_grip_hands|=1u<<side;', 'full_body.publish(t,native_ownership);')

service = section(interaction, 'void service_full_hand_contact(', 'void service_anatomical_contact(')
ordered(service, 'full_hand_contact_.service(*owner,GetTickCount64(),', 'NativeWorldTrace::sphere_query_checked(',
        '&sample.source,true)', '},valid,light,[&](const NativeFullHandContact::Sample& sample,std::uint32_t body,std::uint32_t key)',
        'FullHandTargetWitness::capture(read_,sample.source,body,key)')
assert 'if constexpr(Index==0) {if(scope)' in interaction
assert 'try {self->service_full_hand_contact(*scope);}' in interaction
for reason in ('scope_unqualified', 'owner_or_grab_unavailable', 'verification_failed', 'inactive', 'stage_exception'):
    assert 'full_hand_contact_.clear(NativeFullHandContact::ContactReason::' + reason + ')' in interaction
diagnostics = section(source, 'const auto diagnostics=contact.diagnostics();', 'return value.str();}()')
assert 'index<diagnostics.count' in diagnostics
for field in ('published', 'overwritten', 'count', 'next'):
    assert 'diagnostics.' + field in diagnostics
for field in ('receipt', 'epoch', 'side_epoch', 'side', 'frame', 'tracking_revision', 'tracking_tick', 'source_tick',
              'query_tick', 'publish_tick', 'selected_tick', 'lookup', 'destruction'):
    assert 'record.' + field in diagnostics
for field in ('tick', 'epoch', 'side_epoch', 'latest_frame', 'latest_tracking_tick', 'latest_source_tick', 'reason',
              'mismatch_bits', 'work_mask', 'transaction_state', 'budget_hit'):
    assert 'event.' + field in diagnostics

prerequisites = section(source, 'full_hand_query_requested=fs::exists(', 'native_grab.require_contact_proxies(false);')
for prerequisite in ('!full_body_requested', '!tracked_arms_requested', '!physical_grab_requested',
                     '!controller_aim_requested', '!attachment_pose_installed'):
    assert prerequisite in prerequisites
ordered(prerequisites, 'if(full_hand_query_requested &&', 'throw std::runtime_error(',
        'native_interaction_aim.full_hand_collision(full_hand_query_requested);')
assert '$taskManifest.full_hand_response_enabled -and ($Simulated -or $NoArms -or -not (Test-Path' in launch
assert "'build\\full-body'" in launch
ordered(launch, "'build\\full-hand-query'", '$taskHandExisted=Test-Path', '[IO.File]::ReadAllBytes($taskHandFlag)',
        'try {', "if($taskManifest.full_hand_response_enabled) {[IO.File]::WriteAllText($taskHandFlag,'on')}",
        'inject_probe.py', '} finally {', 'if($taskManifest.full_hand_response_enabled)',
        '[IO.File]::WriteAllBytes($taskHandFlag,$taskHandBefore)', 'Remove-Item -LiteralPath $taskHandFlag')
assert "'--physical-grab'" in launch
assert "if (-not $Simulated)" in launch and "'--controller-aim'" in launch and "if (-not $NoArms)" in launch and "'--tracked-arms'" in launch

print('PASS full-hand production routing guard; source-only, not native execution, enabled launch, or headset acceptance')

hip_claim = section(source, 'auto query_input=use_input;', 'std::array<bool,2> claimants')
assert 'if(hip_draw_reserved)query_input[0]=1.f;' in hip_claim
assert '(query_input[h]>.55f || gazing(h))' in hip_claim
assert '!(physical_grab_requested && native_grab.holding(h))' in hip_claim
assert 'controller_aim_requested' in hip_claim
assert 'gun_owns || hip_draw_reserved || talk_targets[0]' in ordinary
hip_authority = section(source, 'in.draw_authorized=grip_intent_out[0].use', 'in.now=hip_now;')
assert 'native_interaction_aim.query_ready(0,hip_now)' in hip_authority
assert '!grip_intent_out[0].fist' in hip_authority
assert 'physical_ready' in hip_authority
hip_yield = section(source, 'const bool preempt=d2vr::released_gaze_yields(', 'bool native_use_idle{};')
assert 'query_input,{grip_intent_out[0].use_started' in hip_yield
assert 'in.gameplay=logic==2 && input.active && game_xr->state==XR_SESSION_STATE_FOCUSED && !chord.menu_modifier && !x_wheel.wheel() && hands[0];' in source
assert 'in.release_owner=d2vr::NativeLeftWeaponSocketOwner::process_owner().current_token();' in source
assert 'in.busy=busy || !hip_current || hip_current->carrying() || !physical_ready || authored_camera;' in source
