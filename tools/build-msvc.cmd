@echo off
call "%D2VR_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if not "%errorlevel%"=="0" exit /b 1
if not exist build mkdir build
if not exist artifacts\research\glyphs mkdir artifacts\research\glyphs
if not exist build\md mkdir build\md
cl /nologo /c /TC /W3 /O2 /MD /Ithird_party\minhook\include third_party\minhook\src\buffer.c third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c third_party\minhook\src\hde\hde64.c /Fo:build\md\
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MD tests\audio_listener_pose_check.cpp /Fo:build\audio_listener_pose_check.obj /Fe:build\audio_listener_pose_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\audio_listener_pose_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MD tests\native_headphone_mode_check.cpp /Fo:build\native_headphone_mode_check.obj /Fe:build\native_headphone_mode_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_headphone_mode_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MD tests\native_audio_listener_check.cpp build\md\buffer.obj build\md\hook.obj build\md\trampoline.obj build\md\hde64.obj /Fo:build\native_audio_listener_check.obj /Fe:build\native_audio_listener_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_audio_listener_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_kill_range_scope_check.cpp /Fo:build\native_kill_range_scope_check.obj /Fe:build\native_kill_range_scope_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_kill_range_scope_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_choke_distance_scope_check.cpp /Fo:build\native_choke_distance_scope_check.obj /Fe:build\native_choke_distance_scope_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_choke_distance_scope_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_world_query_source_check.cpp /Fo:build\native_world_query_source_check.obj /Fe:build\native_world_query_source_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_world_query_source_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_initial_contact_check.cpp /Fo:build\native_initial_contact_check.obj /Fe:build\native_initial_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_initial_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_regular_contact_check.cpp /Fo:build\native_regular_contact_check.obj /Fe:build\native_regular_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_regular_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_door_owner_check.cpp /Fo:build\native_door_owner_check.obj /Fe:build\native_door_owner_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_door_owner_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_door_request_check.cpp /Fo:build\native_door_request_check.obj /Fe:build\native_door_request_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_door_request_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_door_progress_check.cpp /Fo:build\native_door_progress_check.obj /Fe:build\native_door_progress_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_door_progress_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_door_empty_gate_check.cpp /Fo:build\native_door_empty_gate_check.obj /Fe:build\native_door_empty_gate_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_door_empty_gate_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_door_ingress_check.cpp /Fo:build\native_door_ingress_check.obj /Fe:build\native_door_ingress_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_door_ingress_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_door_interaction_check.cpp /Fo:build\native_door_interaction_check.obj /Fe:build\native_door_interaction_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_door_interaction_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_door_clip_content_check.cpp /Fo:build\native_door_clip_content_check.obj /Fe:build\native_door_clip_content_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_door_clip_content_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_door_skeleton_content_check.cpp /Fo:build\native_door_skeleton_content_check.obj /Fe:build\native_door_skeleton_content_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_door_skeleton_content_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\physical_door_control_check.cpp /Fo:build\physical_door_control_check.obj /Fe:build\physical_door_control_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\physical_door_control_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_door_contact_check.cpp /Fo:build\native_door_contact_check.obj /Fe:build\native_door_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_door_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\physical_door_geometry_check.cpp /Fo:build\physical_door_geometry_check.obj /Fe:build\physical_door_geometry_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\physical_door_geometry_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_door_animation_check.cpp /Fo:build\native_door_animation_check.obj /Fe:build\native_door_animation_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_door_animation_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_door_event_admission_check.cpp /Fo:build\native_door_event_admission_check.obj /Fe:build\native_door_event_admission_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_door_event_admission_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\anatomical_local_contact_check.cpp /Fo:build\anatomical_local_contact_check.obj /Fe:build\anatomical_local_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\anatomical_local_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\full_hand_contact_check.cpp /Fo:build\full_hand_contact_check.obj /Fe:build\full_hand_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\full_hand_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\full_hand_target_witness_check.cpp /Fo:build\full_hand_target_witness_check.obj /Fe:build\full_hand_target_witness_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\full_hand_target_witness_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\full_hand_query_transaction_check.cpp /Fo:build\full_hand_query_transaction_check.obj /Fe:build\full_hand_query_transaction_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\full_hand_query_transaction_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\full_body_contact_arm_check.cpp /Fo:build\full_body_contact_arm_check.obj /Fe:build\full_body_contact_arm_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\full_body_contact_arm_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_full_hand_contact_check.cpp /Fo:build\native_full_hand_contact_check.obj /Fe:build\native_full_hand_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_full_hand_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\anatomical_raw_pair_check.cpp /Fo:build\anatomical_raw_pair_check.obj /Fe:build\anatomical_raw_pair_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\anatomical_raw_pair_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_anatomical_shadow_check.cpp /Fo:build\native_anatomical_shadow_check.obj /Fe:build\native_anatomical_shadow_check.exe /link user32.lib Xinput9_1_0.lib
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_anatomical_shadow_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_world_contact_surface_check.cpp /Fo:build\native_world_contact_surface_check.obj /Fe:build\native_world_contact_surface_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\native_world_contact_surface_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\hand_palette_contact_check.cpp /Fo:build\hand_palette_contact_check.obj /Fe:build\hand_palette_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\hand_palette_contact_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\full_body_frame_latch_check.cpp /Fo:build\full_body_frame_latch_check.obj /Fe:build\full_body_frame_latch_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\full_body_frame_latch_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\raw_hand_candidate_check.cpp /Fo:build\raw_hand_candidate_check.obj /Fe:build\raw_hand_candidate_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\raw_hand_candidate_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
for %%T in (finger_ik_check finger_snapshot_check prop_watch_check hand_contact_check anatomical_world_contact_check sword_flight_check vr_settings_native_check game_art_check held_release_check native_takedown_range_check native_slash_speed_check native_use_los_anchor_check calibrated_body_anchor_check body_ik_check native_use_fetch_completion_check native_interaction_install_order_check native_interaction_stop_check native_block_eligibility_check motion_sword_check game_xr_motion_check native_grab_lifetime_check frame_timing_queue_check native_grab_sequence_check native_grab_obstruction_check glyph_swap_async_check glyph_swap_gpu_check avatar_fit_reuse_check xr_floor_datum_check native_torso_fit_check avatar_geometry_fit_check native_pose_pipeline_check native_readable_range_check native_guarded_call_check native_crosshair_check native_art_movie_patch_check synchronized_observer_check wheel_avatar_check native_action_camera_check native_use_order_check native_hand_volume_check body_renderer_check native_hud_art_probe_check native_hud_child_draw_check native_hud_child_capture_check native_wrist_art_publisher_check native_marker_child_binding_check native_art_bounds_check native_ui_layout_check full_body_driver_check native_interaction_range_check native_attachment_pose_check native_fire_seam_trace_check native_hud_fallback_check game_xr_wrist_recovery_check render_perf_cpu_check render_perf_gpu_check left_weapon_render_receipt_check native_hip_visibility_check native_character_gaze_check sword_contact_policy_check render_perf_gpu_loss_check) do (
    cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include /Ithird_party\openxr\include tests\%%T.cpp /Fo:build\%%T.obj /Fe:build\%%T.exe /link user32.lib gdi32.lib Xinput9_1_0.lib d3d11.lib dxgi.lib d3dcompiler.lib
    if errorlevel 1 goto build_failed
    if not errorlevel 0 goto build_failed
    build\%%T.exe
    if errorlevel 1 goto build_failed
    if not errorlevel 0 goto build_failed
)
python tests\native_interaction_range_abi_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\combat_live_status_routing_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\render_perf_gpu_source_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\render_perf_callback_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\render_perf_maintenance_check.py --source src\dxgi_probe.cpp --gpu-header src\render_perf_gpu.hpp
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\render_perf_readiness_check.py --header src\render_perf_gpu.hpp
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
for %%T in (drawn_palm_residual_check palm_old_new_driver_repro) do (
    cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include /Ithird_party\openxr\include tests\%%T.cpp /Fo:build\%%T.obj /Fe:build\%%T.exe /link user32.lib gdi32.lib Xinput9_1_0.lib d3d11.lib dxgi.lib d3dcompiler.lib
    if errorlevel 1 goto build_failed
    if not errorlevel 0 goto build_failed
    build\%%T.exe
    if errorlevel 1 goto build_failed
    if not errorlevel 0 goto build_failed
)
python tests\fsr_diagnostic_drop_source_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\render_attribution_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\hud_profile_lifecycle_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\left_weapon_render_receipt_source_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\test_playground.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\repair_private_menu_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\test_playground_capture.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\live_session_tools_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\native_menu_patch_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\frame_timing_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\live_performance_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
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
python tests\live_performance_check.py
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\physical_grab_check.cpp /Fo:build\physical_grab_check.obj /Fe:build\physical_grab_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\physical_grab_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\held_placement_session_check.cpp /Fo:build\held_placement_session_check.obj /Fe:build\held_placement_session_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\held_placement_session_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MD /Ithird_party\minhook\include tests\physical_carry_followup_check.cpp build\md\buffer.obj build\md\hook.obj build\md\trampoline.obj build\md\hde64.obj /Fo:build\physical_carry_followup_check.obj /Fe:build\physical_carry_followup_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
for %%M in (geometry fresh stale source throw) do (
    build\physical_carry_followup_check.exe %%M
    if errorlevel 1 goto build_failed
    if not errorlevel 0 goto build_failed
)
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
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\grip_latency_replay_check.cpp /Fo:build\grip_latency_replay_check.obj /Fe:build\grip_latency_replay_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\play_controls_check.cpp /Fo:build\play_controls_check.obj /Fe:build\play_controls_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\arm_body_check.cpp /Fo:build\arm_body_check.obj /Fe:build\arm_body_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\arm_body_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_takedown_check.cpp /Fo:build\native_takedown_check.obj /Fe:build\native_takedown_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_takedown_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_offered_choke_check.cpp /Fo:build\native_offered_choke_check.obj /Fe:build\native_offered_choke_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_offered_choke_check.exe
if not "%errorlevel%"=="0" exit /b 1
for %%T in (native_manual_equip_check native_manual_equip_hooks_check native_manual_equip_runtime_check weapon_pose_stabilizer_check native_assassination_receipt_check) do (
    cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\%%T.cpp /Fo:build\%%T.obj /Fe:build\%%T.exe
    if errorlevel 1 goto build_failed
    if not errorlevel 0 goto build_failed
    build\%%T.exe
    if errorlevel 1 goto build_failed
    if not errorlevel 0 goto build_failed
)
python tests\native_assassination_receipt_routing_check.py
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
python tests\manual_equip_routing_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\physical_grip_routing_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\native_turning_lock_routing_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\diagnostic_isolation_routing_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\combat_scheduler_routing_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\sword_combat_routing_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\native_sword_contact_routing_check.py
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc /Ithird_party\minhook\include tests\native_sword_contact_check.cpp /Fo:build\native_sword_contact_check.obj /Fe:build\native_sword_contact_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_sword_contact_check.exe
if not "%errorlevel%"=="0" exit /b 1
python tests\left_weapon_socket_routing_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\left_socket_terminal_diagnostics_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\attachment_socket_audit_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\held_object_routing_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\hand_clamp_routing_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\full_hand_query_routing_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\full_hand_launch_check.py
if not "%errorlevel%"=="0" exit /b 1
python tests\build_exit_status_check.py
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_talk_offer_check.cpp /Fo:build\native_talk_offer_check.obj /Fe:build\native_talk_offer_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_talk_offer_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_weapon_visibility_check.cpp /Fo:build\native_weapon_visibility_check.obj /Fe:build\native_weapon_visibility_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_weapon_visibility_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_left_weapon_socket_check.cpp /Fo:build\native_left_weapon_socket_check.obj /Fe:build\native_left_weapon_socket_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_left_weapon_socket_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc tests\carry_diagnostics_check.cpp /Fo:build\carry_diagnostics_check.obj /Fe:build\carry_diagnostics_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
build\carry_diagnostics_check.exe
if errorlevel 1 goto build_failed
if not errorlevel 0 goto build_failed
for %%T in (combat_target_memory_check native_left_weapon_socket_owner_check native_held_placement_check native_held_object_check) do (
    cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\%%T.cpp /Fo:build\%%T.obj /Fe:build\%%T.exe
    if errorlevel 1 goto build_failed
    if not errorlevel 0 goto build_failed
    build\%%T.exe
    if errorlevel 1 goto build_failed
    if not errorlevel 0 goto build_failed
)
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\vr_gestures_check.cpp /Fo:build\vr_gestures_check.obj /Fe:build\vr_gestures_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\vr_gestures_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\grab_beam_check.cpp /Fo:build\grab_beam_check.obj /Fe:build\grab_beam_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_hud_census_check.cpp /Fo:build\native_hud_census_check.obj /Fe:build\native_hud_census_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_hud_census_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_marker_probe_check.cpp /Fo:build\native_marker_probe_check.obj /Fe:build\native_marker_probe_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_marker_probe_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\grab_beam_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\controller_glyph_atlas_check.cpp /Fo:build\controller_glyph_atlas_check.obj /Fe:build\controller_glyph_atlas_check.exe /link gdi32.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\controller_glyph_atlas_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\bc7_encode_check.cpp /Fo:build\bc7_encode_check.obj /Fe:build\bc7_encode_check.exe /link d3d11.lib dxgi.lib d3dcompiler.lib
if not "%errorlevel%"=="0" exit /b 1
build\bc7_encode_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_interaction_two_hand_check.cpp /Fo:build\native_interaction_two_hand_check.obj /Fe:build\native_interaction_two_hand_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_interaction_two_hand_check.exe
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
build\grip_latency_replay_check.exe
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
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\native_world_ui_check.cpp /Fo:build\native_world_ui_check.obj /Fe:build\native_world_ui_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_world_ui_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\world_ui_capture_check.cpp /Fo:build\world_ui_capture_check.obj /Fe:build\world_ui_capture_check.exe /link d3d11.lib d3dcompiler.lib dxgi.lib
if not "%errorlevel%"=="0" exit /b 1
build\world_ui_capture_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\world_ui_completion_check.cpp /Fo:build\world_ui_completion_check.obj /Fe:build\world_ui_completion_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\world_ui_completion_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_menu_entry_lifecycle_check.cpp /Fo:build\native_menu_entry_lifecycle_check.obj /Fe:build\native_menu_entry_lifecycle_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_menu_entry_lifecycle_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_grab_compile.cpp /Fo:build\native_grab_compile.obj /Fe:build\native_grab_compile.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_grab_compile.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\native_shot_aim_check.cpp /Fo:build\native_shot_aim_check.obj /Fe:build\native_shot_aim_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_shot_aim_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MD /Ithird_party\fidelityfx tests\fsr3_deferred_check.cpp /Fo:build\fsr3_deferred_check.obj /Fe:build\fsr3_deferred_check.exe /link d3d11.lib dxgi.lib third_party\fidelityfx\libs\ffx_backend_dx11_x64.lib third_party\fidelityfx\libs\ffx_fsr3_x64.lib third_party\fidelityfx\libs\ffx_fsr3upscaler_x64.lib third_party\fidelityfx\libs\ffx_frameinterpolation_x64.lib third_party\fidelityfx\libs\ffx_opticalflow_x64.lib
if not "%errorlevel%"=="0" exit /b 1
build\fsr3_deferred_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT tests\settings_menu_layout_check.cpp /Fo:build\settings_menu_layout_check.obj /Fe:build\settings_menu_layout_check.exe /link Xinput9_1_0.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\settings_menu_layout_check.exe
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
python -m unittest discover -s tests -p native_left_aim_profile_check.py -k archive
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
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\world_ui_pose_check.cpp /Fo:build\world_ui_pose_check.obj /Fe:build\world_ui_pose_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\world_ui_pose_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\taa_mv_dejitter_check.cpp /Fo:build\taa_mv_dejitter_check.obj /Fe:build\taa_mv_dejitter_check.exe /link d3d11.lib d3dcompiler.lib
if not "%errorlevel%"=="0" exit /b 1
build\taa_mv_dejitter_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\canted_fov_check.cpp /Fo:build\canted_fov_check.obj /Fe:build\canted_fov_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\canted_fov_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Ithird_party\minhook\include tests\sampler_lod_bias_check.cpp build\buffer.obj build\hook.obj build\trampoline.obj build\hde64.obj /Fo:build\sampler_lod_bias_check.obj /Fe:build\sampler_lod_bias_check.exe /link d3d11.lib
if not "%errorlevel%"=="0" exit /b 1
build\sampler_lod_bias_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc tests\vr_combat_check.cpp /Fo:build\vr_combat_check.obj /Fe:build\vr_combat_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\vr_combat_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc tests\vr_perf_governor_check.cpp /Fo:build\vr_perf_governor_check.obj /Fe:build\vr_perf_governor_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\vr_perf_governor_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc /Ithird_party\minhook\include tests\weapon_collision_check.cpp /Fo:build\weapon_collision_check.obj /Fe:build\weapon_collision_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc /Ithird_party\minhook\include tools\push_lab.cpp /Fo:build\push_lab.obj /Fe:build\push_lab.exe
if not "%errorlevel%"=="0" exit /b 1
build\push_lab.exe build\push-lab
if not "%errorlevel%"=="0" exit /b 1
build\weapon_collision_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc tests\hand_clamp_check.cpp /Fo:build\hand_clamp_check.obj /Fe:build\hand_clamp_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\hand_clamp_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc tests\hand_reach_check.cpp /Fo:build\hand_reach_check.obj /Fe:build\hand_reach_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\hand_reach_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc /Ithird_party\minhook\include tests\grip_ik_pipeline_check.cpp /Fo:build\grip_ik_pipeline_check.obj /Fe:build\grip_ik_pipeline_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\grip_ik_pipeline_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc /Ithird_party\minhook\include tests\hand_clamp_feed_check.cpp /Fo:build\hand_clamp_feed_check.obj /Fe:build\hand_clamp_feed_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc tests\palm_offset_check.cpp /Fo:build\palm_offset_check.obj /Fe:build\palm_offset_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\palm_offset_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\hand_clamp_feed_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc /Ithird_party\minhook\include tests\native_world_trace_check.cpp /Fo:build\native_world_trace_check.obj /Fe:build\native_world_trace_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_world_trace_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT /Isrc /Ithird_party\minhook\include tests\native_console_check.cpp /Fo:build\native_console_check.obj /Fe:build\native_console_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_console_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\grab_indicator_check.cpp /Fo:build\grab_indicator_check.obj /Fe:build\grab_indicator_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\grab_indicator_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\wrist_hud_image_check.cpp /Fo:build\wrist_hud_image_check.obj /Fe:build\wrist_hud_image_check.exe /link gdi32.lib user32.lib
if not "%errorlevel%"=="0" exit /b 1
build\wrist_hud_image_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_wrist_hud_check.cpp /Fo:build\native_wrist_hud_check.obj /Fe:build\native_wrist_hud_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_wrist_hud_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_hud_rect_check.cpp /Fo:build\native_hud_rect_check.obj /Fe:build\native_hud_rect_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_hud_rect_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\render_scale_transaction_check.cpp /Fo:build\render_scale_transaction_check.obj /Fe:build\render_scale_transaction_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\render_scale_transaction_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\xr_present_pacing_check.cpp /Fo:build\xr_present_pacing_check.obj /Fe:build\xr_present_pacing_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\xr_present_pacing_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\gpu_pass_timer_check.cpp /Fo:build\gpu_pass_timer_check.obj /Fe:build\gpu_pass_timer_check.exe /link d3d11.lib dxgi.lib
if not "%errorlevel%"=="0" exit /b 1
build\gpu_pass_timer_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MD /Ithird_party\minhook\include /Ithird_party\fidelityfx tests\fsr_taa_gpu_check.cpp /Fo:build\fsr_taa_gpu_check.obj /Fe:build\fsr_taa_gpu_check.exe /link d3d11.lib dxgi.lib d3dcompiler.lib third_party\fidelityfx\libs\ffx_backend_dx11_x64.lib third_party\fidelityfx\libs\ffx_fsr3_x64.lib third_party\fidelityfx\libs\ffx_fsr3upscaler_x64.lib third_party\fidelityfx\libs\ffx_frameinterpolation_x64.lib third_party\fidelityfx\libs\ffx_opticalflow_x64.lib
if not "%errorlevel%"=="0" exit /b 1
build\fsr_taa_gpu_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\fsr_taa_gpu_check.exe --fsr4
if not "%errorlevel%"=="0" exit /b 1
build\fsr_taa_gpu_check.exe --fsr4-missing
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MD /Ithird_party\fidelityfx tests\fsr4_eye_upscaler_check.cpp /Fo:build\fsr4_eye_upscaler_check.obj /Fe:build\fsr4_eye_upscaler_check.exe /link d3d11.lib dxgi.lib
if not "%errorlevel%"=="0" exit /b 1
build\fsr4_eye_upscaler_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_interaction_prompt_check.cpp /Fo:build\native_interaction_prompt_check.obj /Fe:build\native_interaction_prompt_check.exe
if not "%errorlevel%"=="0" exit /b 1
build\native_interaction_prompt_check.exe
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /MT tests\native_list_gpu_timer_check.cpp /Fo:build\native_list_gpu_timer_check.obj /Fe:build\native_list_gpu_timer_check.exe /link d3d11.lib dxgi.lib
if not "%errorlevel%"=="0" exit /b 1
build\native_list_gpu_timer_check.exe
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
cl /nologo /std:c++17 /utf-8 /EHsc /W4 /WX /O2 /bigobj /Zi /Fd:build\dxgi_probe.pdb /MD /LD /Ithird_party\minhook\include /Ithird_party\openxr\include /Ithird_party\fidelityfx src\dxgi_probe.cpp build\md\buffer.obj build\md\hook.obj build\md\trampoline.obj build\md\hde64.obj /Fo:build\dxgi_probe.obj /Fe:%D2VR_DIAGNOSTIC_OUTPUT% /link /DEBUG /OPT:REF /OPT:ICF d3d11.lib dxgi.lib bcrypt.lib user32.lib d3dcompiler.lib gdi32.lib third_party\fidelityfx\libs\ffx_backend_dx11_x64.lib third_party\fidelityfx\libs\ffx_fsr3_x64.lib third_party\fidelityfx\libs\ffx_fsr3upscaler_x64.lib third_party\fidelityfx\libs\ffx_frameinterpolation_x64.lib third_party\fidelityfx\libs\ffx_opticalflow_x64.lib
exit /b %errorlevel%
:build_failed
exit /b 1
