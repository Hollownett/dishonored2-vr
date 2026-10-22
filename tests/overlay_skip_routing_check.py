"""Source wiring of plan E task 4 (game_xr_simulation_check cannot run on Linux): every overlay kind decides through its gate,
marks itself rendered only after the release, a new swapchain bumps its generation, the producers pass the capture's draw tick,
and both knobs are read and listed."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
xr = (root / "src/game_xr.hpp").read_text(encoding="utf-8")
probe = (root / "src/dxgi_probe.cpp").read_text(encoding="utf-8")

for kind in ("world_ui", "hud_window", "settings", "hint", "dot", "vignette"):
    need = f"overlay_needs_image(OverlayKind::{kind},"
    done = f"overlay_rendered(OverlayKind::{kind},overlay_key)"
    assert xr.count(need) == 1, f"{kind}: exactly one gate call expected"
    assert xr.count(done) == 1, f"{kind}: exactly one overlay_rendered expected"
    assert xr.index(need) < xr.index(done), f"{kind}: marked rendered before it is drawn"
assert "overlay_needs_image(wrist_kind," in xr and "overlay_rendered(wrist_kind,overlay_key)" in xr, "wrist gate missing"
# the release comes before the mark, in every overlay body
for release, kind in (("Release world UI image", "world_ui"), ("Release HUD window image", "hud_window"), ("Release settings panel", "settings"),
                      ("Release controls card", "hint"), ("Release wheel dot", "dot"), ("Release comfort vignette", "vignette"), ("Release wrist HUD", "wrist_kind")):
    assert xr.index(release) < xr.index(f"overlay_rendered(OverlayKind::{kind},overlay_key)" if kind != "wrist_kind" else "overlay_rendered(wrist_kind,overlay_key)"), release
# a gate is invalid from the moment a render starts until it is released
assert re.search(r"gate\.invalidate\(\);return true;", xr), "a started render must leave its gate invalid"
# generations: every chain created through create_overlay_chain gets a new one; destroying or closing drops them
assert "chain_generations_[chain]=++chain_generation_counter_" in xr
assert "chain_generations_.erase(chain)" in xr
assert "invalidate_overlays();chain_generations_.clear();" in xr
# stale re-shows count as skips; the status carries the counters
assert xr.count("overlay_stats.note(OverlayKind::world_ui,false)") == 1 and xr.count("overlay_stats.note(OverlayKind::hud_window,false)") == 1
assert "overlay_stats.end_frame();" in xr
assert probe.count("overlay_status_json()") >= 3 and "overlay_releases_per_frame" in probe and "overlay_skipped" in probe
# the producers pass the engine's draw tick
assert "ui.recorded_tick" in probe and "panel.recorded_tick" in probe
# knobs
assert 'L"build"/L"no-overlay-skip"' in probe and 'L"build"/L"no-discovery-throttle"' in probe
features = {f["file"] for f in json.loads((root / "tools/default_features.json").read_text())["features"]}
assert {"no-overlay-skip", "no-discovery-throttle"} <= features
# discovery: the per-frame cache is the only path; validation stays a live lambda on every call; hooks share game_logic_mask
assert "logic_mask_cache.get(discovery_throttle_enabled,frames.load(" in probe and "logic_mask_valid(game)" in probe
assert "discovery_throttle." not in probe and "DiscoveryCadence" not in probe
print("overlay_skip_routing_check ok")
