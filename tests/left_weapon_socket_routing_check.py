from pathlib import Path


root = Path(__file__).resolve().parents[1]


def section(text, start, end):
    first = text.index(start)
    return text[first:text.index(end, first + len(start))]


def ordered(text, *tokens):
    position = 0
    for token in tokens:
        position = text.index(token, position) + len(token)


def check(source):
    producer = section(source, 'const d2vr::NativeLeftWeaponSocketOwner::Epochs socket_epochs',
                       'full_body.publish(t,native_ownership);')
    ordered(producer, 'socket_epochs{t.pose_epoch,t.reference_epoch,t.mapping_revision}',
            'availability.epoch=t.pose_epoch;availability.tick=hands.tick;',
            'availability.valid=attachment_pose_installed && full_body_tracking_ready.load()',
            'availability.focused=game_logic_mask()==2',
            'availability.physical_held=', 'native_grab.holding(0)',
            'availability.physical_reserved=', 'native_grab.waiting_native_query(0)',
            'availability.placement_serial=held_placement_serial.load(std::memory_order_acquire);',
            'availability.native_object=', 'hand_holding_object && hand_hold_side==0',
            'socket_owner.refresh(socket_binding,socket_epochs,availability)',
            'if(!t.native_socket_token[0] && socket_binding)',
            'NativeLeftWeaponSocket::capture(image_base,reader,*socket_binding,availability,GetTickCount64())',
            'socket_owner.publish(', 'socket_binding,socket_epochs,availability)')
    assert 'game_xr->last_action_sync==XR_SUCCESS && !authored_camera' in producer
    assert '(hands.hands&1u)!=0 && t.grip_valid[0]' in producer
    upload = section(source, 'void invoke_arm_upload(void* model,const void* source,UINT count,FullHandUploadWork* contact) {',
                     'void observe_arm_upload(')
    admission = section(upload, 'const auto socket_token=t.native_socket_token[side];',
                        'if(accepted && (diagnostic || raw_admission')
    ordered(admission, 'attachment_pose_installed && side==0 && socket_owner.current(socket_token,snapshot,',
            '{t.pose_epoch,t.reference_epoch,t.mapping_revision}',
            'if(socket_token && socket_current) {', 'origin.admission=',
            '&d2vr::NativeLeftWeaponSocketOwner::acquire,&d2vr::NativeLeftWeaponSocketOwner::release',
            'origin.receipt_frame_policy=d2vr::NativeAttachmentReceiptFramePolicy::current_parent;',
            'if(!socket_current && attachment_pose_installed)attachment_pose.clear(side);',
            'const bool accepted=socket_current &&', 'attachment_pose.publish(side,', 'palette,origin)')
    ordered(admission, 'if(accepted && socket_token) {submitted_socket_token=socket_token;',
            'submitted_socket_epochs={t.pose_epoch,t.reference_epoch,t.mapping_revision};')
    ordered(upload, 'palette=posed->joints;', 'const auto socket_token=t.native_socket_token[side];',
            'const bool accepted=socket_current &&', 'if(accepted) {for(',
            'submitted=palette.data();', 'else {++arm_pose_fallbacks[side];')
    assert upload.count('original_arm_upload(') == 1
    terminal = section(upload, 'if(submitted_socket_token &&', 'original_arm_upload(model,submitted,count);')
    ordered(terminal, '!capturing.load(std::memory_order_acquire)', 'full_body_stopping.load()',
            'arm_binding_cache.load()!=snapshot', 'NativeLeftWeaponSocketOwner::process_owner().current(',
            'submitted_socket_token,snapshot,submitted_socket_epochs)', 'attachment_pose.clear(side);submitted=source;',
            'diagnostic->palette_valid=false;', 'diagnostic->landmarks_valid=false;')
    assert source.count('capturing=false;\n        d2vr::NativeLeftWeaponSocketOwner::process_owner().clear();') == 3
    configure = section(source, 'NativeLeftWeaponSocketOwner::process_owner().configure(', 'body_upload_seen=')
    ordered(configure, 'return capturing.load(std::memory_order_acquire) &&',
            '!full_body_stopping.load() && full_body_tracking_ready.load() && read_self(address,output,size);')
    assert 'return GetTickCount64();' in configure


source = (root / 'src/dxgi_probe.cpp').read_text(encoding='utf-8-sig')
check(source)
for before, after in (
    ('side==0 && socket_owner.current(socket_token,snapshot,', 'side==0 && true && ignored(socket_token,snapshot,'),
    ('{t.pose_epoch,t.reference_epoch,t.mapping_revision}', '{t.pose_epoch,0,0}'),
    ('native_grab.holding(0)', 'false'),
    ('native_grab.waiting_native_query(0)', 'false'),
    ('hand_holding_object && hand_hold_side==0', 'false'),
    ('availability.placement_serial=held_placement_serial.load(std::memory_order_acquire);', 'availability.placement_serial=0;'),
    ('&d2vr::NativeLeftWeaponSocketOwner::acquire', 'nullptr'),
    ('availability.valid=attachment_pose_installed &&', 'availability.valid='),
    ('attachment_pose_installed && side==0 && socket_owner.current', 'side==0 && socket_owner.current'),
    ('submitted_socket_token,snapshot,submitted_socket_epochs)', 'submitted_socket_token,snapshot,{})'),
    ('attachment_pose.clear(side);submitted=source;', 'attachment_pose.clear(side);'),
    ('NativeLeftWeaponSocketOwner::process_owner().clear();', 'NativeLeftWeaponSocketOwner::process_owner();'),
    ('!full_body_stopping.load() && full_body_tracking_ready.load() && read_self(address,output,size);',
     'read_self(address,output,size);'),
):
    assert before in source
    try:
        check(source.replace(before, after))
    except (AssertionError, ValueError):
        pass
    else:
        raise AssertionError('Routing guard accepted mutation: ' + before)

print('PASS LEFT socket routing source guard and negative mutations; not DXGI execution or headset acceptance')
