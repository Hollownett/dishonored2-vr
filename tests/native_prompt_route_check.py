from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "src/dxgi_probe.cpp").read_text()
start = source.index("const auto anchor_kind=d2vr::native_prompt_anchor(")
end = source.index("const auto& frame=native_hud_world_frame;", start)
route = source[start:end]
assert "native_prompt_anchor(compact.child.target," in route
assert "prompt_target_anchor?" not in route
assert "if(anchor_kind!=d2vr::NativePromptAnchor::interaction) target.reset();" in route
assert "if(anchor_kind!=d2vr::NativePromptAnchor::takedown) take.reset();" in route

retained_start = source.index("if(compact.child.retained_entry) {")
legacy_start = source.index("auto target=native_interaction_aim.prompt_target(now);", retained_start)
retained_route = source[retained_start:legacy_start]
assert "native_retained_prompt_anchor(read_self,image_base,*compact.child.retained_entry,retained_npc)" in retained_route
assert "retained_npc=npc_entity(compact.child.retained_entry->owner)" in retained_route
assert "if(!retained_npc)point.y+=.05" in retained_route
assert "native_marker_world_point(*anchor,frame,metadata.epoch" in retained_route
assert "prompt_target(now)" not in retained_route
assert "has_retained_prompt(game_xr->pose_epoch(),now)" in source

callback_start = source.index("if(retained_entry) d2vr::NativeHudChildDraw::publish(")
callback_end = source.index("static std::uint64_t next_geometry_log", callback_start)
callback = source[callback_start:callback_end]
assert "false,0,retained_entry)" in callback
assert "else if(event.sequence)" in callback
assert "else if(retained_identity)" not in callback

hold_start = source.index("const auto fail=[&]{", source.index("void publish_native_hud_art("))
hold_end = source.index("const auto& compact=native_hud_compact_art[slot]", hold_start)
hold = source[hold_start:hold_end]
assert "native_hud_child_same_identity(live_scope.child,c.child)" in hold
assert "scope_current(c.child,game_xr->pose_epoch(),now)" in hold
assert "native_prompt_entry::refresh(read_self,image_base,*c.child.retained_entry)" in hold
assert "clear_prompt_entry(owner->element,*key)" in source
print("PASS: retained native sources own world placement, sparse/carry routing, exact Remove and last-good artwork")
