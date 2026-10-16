from pathlib import Path


root = Path(__file__).resolve().parents[1]
source = (root / 'src/dxgi_probe.cpp').read_text(encoding='utf-8-sig')
weapons = (root / 'src/native_weapon_hands.hpp').read_text(encoding='utf-8-sig')
controls = source[source.index('const auto control_machine='):source.index('const auto choke_offer=', source.index('const auto control_machine='))]
assert controls.index('manual_equip.publish(') < controls.index('manual_equip.hip_item_current(') < controls.index('manual_equip.status().generation!=hip_draw_generation')
assert '!left_gun_model.load() && manual_equip.status().generation' not in controls
assert 'if(left_gun_model.load())hip_draw_generation=0' not in source
compact_controls=''.join(controls.split())
assert 'native_grab.holding(0)||(!gun_owns&&(grip_intent_out[0].fist||(input.left_grip>.55f&&input.left_trigger>.55f)))' in compact_controls
assert 'native_grab.holding(0)||grip_intent_out[0].fist||' not in compact_controls
assert 'r.manual_generation=manual_equip.holster();' in source
assert 'r.manual_generation=manual_equip.status().generation' not in source
assert source.count('manual_equip.reserve_hip(') == 1
assert source.count('manual_equip.begin_wheel(') == 1
assert source.count('manual_equip.commit_wheel(') == 2
assert 'native_wheel_ui=(logic&256u)!=0 && wheel_controller' in controls
assert 'control_machine+0x2f8)==wheel_controller' in controls
assert 'manual_wheel_closing && native_wheel_ui' in controls
assert '!game_xr->controller_input.x && native_wheel_ui' in source
assert 'if(native_wheel_ui)manual_wheel_token=manual_equip.commit_wheel' in source
assert 'manual_equip.allow_holster(' not in weapons
assert 'if(manual_status.installed)manual_equip.service_game_callback(' in weapons
assert 'execute_holster(request.manual_generation,request.manual_owner,' in weapons
assert 'params.bytes.data(),nullptr,0,0x5b' in weapons
assert 'machine,64,params' in weapons
assert 'manual_equip_hooks_disabled' in source
assert 'NativeManualEquipHooks::process_owner().ready_for_install()' in source
assert source.count('NativeManualEquipRuntime::process_owner().disable(') >= 3
print('PASS manual-equip production wiring guard; source routing only, not native or headset execution')
