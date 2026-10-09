@echo off
call "%D2VR_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b 1
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\native_lean_snapshot_check.cpp /Fo:build\native_lean_snapshot_check.obj /Fe:build\native_lean_snapshot_check.exe /link Xinput9_1_0.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\native_lean_snapshot_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\lean_control_check.cpp /Fo:build\lean_control_check.obj /Fe:build\lean_control_check.exe /link Xinput9_1_0.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\lean_control_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\tracking_offset_check.cpp /Fo:build\tracking_offset_check.obj /Fe:build\tracking_offset_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\tracking_offset_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\native_head_query_lifetime_check.cpp /Fo:build\native_head_query_lifetime_check.obj /Fe:build\native_head_query_lifetime_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_head_query_lifetime_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\native_carry_snapshot_check.cpp /Fo:build\native_carry_snapshot_check.obj /Fe:build\native_carry_snapshot_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_carry_snapshot_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_lean_lifecycle_check.cpp /Fo:build\native_lean_lifecycle_check.obj /Fe:build\native_lean_lifecycle_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_lean_lifecycle_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_weapon_dry_lease_check.cpp /Fo:build\native_weapon_dry_lease_check.obj /Fe:build\native_weapon_dry_lease_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_weapon_dry_lease_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\native_equipment_ownership_check.cpp /Fo:build\native_equipment_ownership_check.obj /Fe:build\native_equipment_ownership_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_equipment_ownership_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_lean_query_trace_check.cpp /Fo:build\native_lean_query_trace_check.obj /Fe:build\native_lean_query_trace_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_lean_query_trace_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\menu_chord_check.cpp /Fo:build\menu_chord_check.obj /Fe:build\menu_chord_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\menu_chord_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\simulated_load_confirm_check.cpp /Fo:build\simulated_load_confirm_check.obj /Fe:build\simulated_load_confirm_check.exe /link Xinput9_1_0.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\simulated_load_confirm_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\simulated_grab_fixture_check.cpp /Fo:build\simulated_grab_fixture_check.obj /Fe:build\simulated_grab_fixture_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\simulated_grab_fixture_check.exe
if not "%errorlevel%"=="0" exit /b 1
python tests\simulated_gameplay_script_profile_check.py
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\simulated_gameplay_script_check.cpp /Fo:build\simulated_gameplay_script_check.obj /Fe:build\simulated_gameplay_script_check.exe /link user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\simulated_gameplay_script_check.exe
if not "%errorlevel%"=="0" exit /b 1
python tests\simulated_cleanup_exception_profile_check.py
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\simulated_cleanup_exception_check.cpp /Fo:build\simulated_cleanup_exception_check.obj /Fe:build\simulated_cleanup_exception_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\simulated_cleanup_exception_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\avatar_calibration_check.cpp /Fo:build\avatar_calibration_check.obj /Fe:build\avatar_calibration_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\avatar_calibration_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\avatar_fit_reuse_check.cpp /Fo:build\avatar_fit_reuse_check.obj /Fe:build\avatar_fit_reuse_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\avatar_fit_reuse_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\avatar_fit_events_check.cpp /Fo:build\avatar_fit_events_check.obj /Fe:build\avatar_fit_events_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\avatar_fit_events_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\gameplay_aim_check.cpp /Fo:build\gameplay_aim_check.obj /Fe:build\gameplay_aim_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\gameplay_aim_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\native_arm_binding_snapshot_check.cpp /Fo:build\native_arm_binding_snapshot_check.obj /Fe:build\native_arm_binding_snapshot_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_arm_binding_snapshot_check.exe
if not "%errorlevel%"=="0" exit /b 1
python tests\native_hand_export_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\frame_timing_check.py
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\physical_grab_check.cpp /Fo:build\physical_grab_check.obj /Fe:build\physical_grab_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\physical_grab_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_taa_history_check.cpp /Fo:build\native_taa_history_check.obj /Fe:build\native_taa_history_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_taa_history_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\native_rigid_body_check.cpp /Fo:build\native_rigid_body_check.obj /Fe:build\native_rigid_body_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_rigid_body_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\openxr\include tests\game_xr_simulation_check.cpp /Fo:build\game_xr_simulation_check.obj /Fe:build\game_xr_simulation_check.exe /link d3d11.lib dxgi.lib d3dcompiler.lib gdi32.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\game_xr_simulation_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\openxr\include tests\avatar_death_reload_wizard_check.cpp /Fo:build\avatar_death_reload_wizard_check.obj /Fe:build\avatar_death_reload_wizard_check.exe /link d3d11.lib dxgi.lib d3dcompiler.lib gdi32.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\avatar_death_reload_wizard_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\openxr\include tests\avatar_identity_prompt_check.cpp /Fo:build\avatar_identity_prompt_check.obj /Fe:build\avatar_identity_prompt_check.exe /link d3d11.lib dxgi.lib d3dcompiler.lib gdi32.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\avatar_identity_prompt_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\finger_curl_check.cpp /Fo:build\finger_curl_check.obj /Fe:build\finger_curl_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\finger_curl_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\interaction_ray_check.cpp /Fo:build\interaction_ray_check.obj /Fe:build\interaction_ray_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\interaction_ray_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tools\arm_mesh_simulation.cpp /Fo:build\arm_mesh_simulation.obj /Fe:build\arm_mesh_simulation.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\menu_input_check.cpp /Fo:build\menu_input_check.obj /Fe:build\menu_input_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\menu_input_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\menu_pointer_check.cpp /Fo:build\menu_pointer_check.obj /Fe:build\menu_pointer_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\menu_pointer_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_menu_state_check.cpp /Fo:build\native_menu_state_check.obj /Fe:build\native_menu_state_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_menu_state_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_player_camera_check.cpp /Fo:build\native_player_camera_check.obj /Fe:build\native_player_camera_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_player_camera_check.exe
if not "%errorlevel%"=="0" exit /b 1
python tests\interaction_fields_check.py
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\body_anchor_check.cpp /Fo:build\body_anchor_check.obj /Fe:build\body_anchor_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\body_anchor_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\head_locomotion_check.cpp /Fo:build\head_locomotion_check.obj /Fe:build\head_locomotion_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\head_locomotion_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\head_calibration_check.cpp /Fo:build\head_calibration_check.obj /Fe:build\head_calibration_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\head_calibration_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\turn_control_check.cpp /Fo:build\turn_control_check.obj /Fe:build\turn_control_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\turn_control_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\vr_reuse_check.cpp /Fo:build\vr_reuse_check.obj /Fe:build\vr_reuse_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\vr_reuse_check.exe
if not "%errorlevel%"=="0" exit /b 1
python tools\embed_smaa.py
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\eye_antialiasing_check.cpp /Fo:build\eye_antialiasing_check.obj /Fe:build\eye_antialiasing_check.exe /link d3d11.lib dxgi.lib d3dcompiler.lib
if not "%errorlevel%"=="0" exit /b 1
build\eye_antialiasing_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\eye_sharpen_check.cpp /Fo:build\eye_sharpen_check.obj /Fe:build\eye_sharpen_check.exe /link d3d11.lib dxgi.lib d3dcompiler.lib
if not "%errorlevel%"=="0" exit /b 1
build\eye_sharpen_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\desktop_mirror_check.cpp /Fo:build\desktop_mirror_check.obj /Fe:build\desktop_mirror_check.exe /link d3d11.lib dxgi.lib d3dcompiler.lib
if not "%errorlevel%"=="0" exit /b 1
build\desktop_mirror_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_cvars_check.cpp /Fo:build\native_cvars_check.obj /Fe:build\native_cvars_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_cvars_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\vr_settings_check.cpp /Fo:build\vr_settings_check.obj /Fe:build\vr_settings_check.exe /link gdi32.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\vr_settings_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\arm_ik_check.cpp /Fo:build\arm_ik_check.obj /Fe:build\arm_ik_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\arm_ik_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\game_camera_check.cpp /Fo:build\game_camera_check.obj /Fe:build\game_camera_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\game_camera_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\game_stereo_check.cpp /Fo:build\game_stereo_check.obj /Fe:build\game_stereo_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\game_stereo_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\tracked_pose_check.cpp /Fo:build\tracked_pose_check.obj /Fe:build\tracked_pose_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\tracked_pose_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\context_state_check.cpp /Fo:build\context_state_check.obj /Fe:build\context_state_check.exe /link d3d11.lib dxgi.lib
if not "%errorlevel%"=="0" exit /b 1
build\context_state_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\controller_input_check.cpp /Fo:build\controller_input_check.obj /Fe:build\controller_input_check.exe /link Xinput9_1_0.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\vr_controls_check.cpp /Fo:build\vr_controls_check.obj /Fe:build\vr_controls_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\play_controls_check.cpp /Fo:build\play_controls_check.obj /Fe:build\play_controls_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\play_controls_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\controls_hint_image_check.cpp /Fo:build\controls_hint_image_check.obj /Fe:build\controls_hint_image_check.exe /link gdi32.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\controls_hint_image_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_finger_grip_check.cpp /Fo:build\native_finger_grip_check.obj /Fe:build\native_finger_grip_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_finger_grip_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\taa_constants_capture_check.cpp /Fo:build\taa_constants_capture_check.obj /Fe:build\taa_constants_capture_check.exe /link d3d11.lib dxgi.lib
if not "%errorlevel%"=="0" exit /b 1
build\taa_constants_capture_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\vr_controls_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\controller_input_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /DNOMINMAX /EHsc /W4 /WX /O2 /MT tests\right_grip_owner_check.cpp /Fo:build\right_grip_owner_check.obj /Fe:build\right_grip_owner_check.exe /link Xinput9_1_0.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\right_grip_owner_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\pad_focus_check.cpp /Fo:build\pad_focus_check.obj /Fe:build\pad_focus_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\pad_focus_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\render_size_check.cpp /Fo:build\render_size_check.obj /Fe:build\render_size_check.exe /link d3d11.lib dxgi.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\render_size_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\menu_panel_check.cpp /Fo:build\menu_panel_check.obj /Fe:build\menu_panel_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\menu_panel_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\xr_continuity_check.cpp /Fo:build\xr_continuity_check.obj /Fe:build\xr_continuity_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\xr_continuity_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\probe_session_check.cpp /Fo:build\probe_session_check.obj /Fe:build\probe_session_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\probe_session_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\game_rig_check.cpp /Fo:build\game_rig_check.obj /Fe:build\game_rig_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\game_rig_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\rig_transform_check.cpp /Fo:build\rig_transform_check.obj /Fe:build\rig_transform_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\rig_transform_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\arm_palette_check.cpp /Fo:build\arm_palette_check.obj /Fe:build\arm_palette_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\arm_palette_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\forearm_roll_check.cpp /Fo:build\forearm_roll_check.obj /Fe:build\forearm_roll_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\forearm_roll_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\arm_tracking_check.cpp /Fo:build\arm_tracking_check.obj /Fe:build\arm_tracking_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\arm_tracking_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\hand_bind_check.cpp /Fo:build\hand_bind_check.obj /Fe:build\hand_bind_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\hand_bind_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tools\arm_binding_probe.cpp /Fo:build\arm_binding_probe.obj /Fe:build\arm_binding_probe.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\crash_observer_check.cpp /Fo:build\crash_observer_check.obj /Fe:build\crash_observer_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\crash_observer_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\camera_comfort_check.cpp /Fo:build\camera_comfort_check.obj /Fe:build\camera_comfort_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\camera_comfort_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\openxr\include src\xr_probe.cpp /Fo:build\xr_probe.obj /Fe:build\xr_probe.exe /link d3d11.lib dxgi.lib
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /c /TC /W3 /O2 /MT /Ithird_party\minhook\include third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c /Fo:build\
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\hook_check.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fo:build\hook_check.obj /Fe:build\hook_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\hook_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_weapon_attachment_trace_check.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fo:build\native_weapon_attachment_trace_check.obj /Fe:build\native_weapon_attachment_trace_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_weapon_attachment_trace_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_weapon_source_receipts_check.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fo:build\native_weapon_source_receipts_check.obj /Fe:build\native_weapon_source_receipts_check.exe /link user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\native_weapon_source_receipts_check.exe
if not "%errorlevel%"=="0" exit /b 1
python tests\native_weapon_source_frame_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\native_weapon_source_joins_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\native_weapon_attachment_abi_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\native_weapon_attachment_profile_check.py
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\gpu_dispatch_bindings_check.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fo:build\gpu_dispatch_bindings_check.obj /Fe:build\gpu_dispatch_bindings_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\gpu_dispatch_bindings_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_pad_focus_check.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fo:build\native_pad_focus_check.obj /Fe:build\native_pad_focus_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_pad_focus_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /DNOMINMAX /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_gameplay_aim_lifecycle_check.cpp /Fo:build\native_gameplay_aim_lifecycle_check.obj /Fe:build\native_gameplay_aim_lifecycle_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_gameplay_aim_lifecycle_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\gameplay_aim_transaction_check.cpp /Fo:build\gameplay_aim_transaction_check.obj /Fe:build\gameplay_aim_transaction_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\gameplay_aim_transaction_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /DNOMINMAX /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_gameplay_aim_install_check.cpp /Fo:build\native_gameplay_aim_install_check.obj /Fe:build\native_gameplay_aim_install_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_gameplay_aim_install_check.exe
if not "%errorlevel%"=="0" exit /b 1
set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe.dll
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\native_graphics_owner_check.cpp /Fo:build\native_graphics_owner_check.obj /Fe:build\native_graphics_owner_check.exe /link d3d11.lib dxgi.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\native_graphics_owner_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\native_eye_sources_check.cpp /Fo:build\native_eye_sources_check.obj /Fe:build\native_eye_sources_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_eye_sources_check.exe
if not "%errorlevel%"=="0" exit /b 1
if "%~1"=="candidate" set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe_candidate.dll
if "%~1"=="pipeline" set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe_pipeline.dll
if "%~1"=="paired" set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe_paired.dll
if "%~1"=="view_setup" set D2VR_DIAGNOSTIC_OUTPUT=build\d2vr_render_probe_view_setup.dll
if not "%~2"=="" set D2VR_DIAGNOSTIC_OUTPUT=build\%~2
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /LD /Ithird_party\minhook\include /Ithird_party\openxr\include src\dxgi_probe.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fo:build\dxgi_probe.obj /Fe:%D2VR_DIAGNOSTIC_OUTPUT% /link d3d11.lib dxgi.lib bcrypt.lib user32.lib d3dcompiler.lib gdi32.lib
exit /b %errorlevel%
