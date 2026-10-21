from pathlib import Path


def check(probe: str, game_xr: str, settings: str, features: str) -> None:
    assert 'L"no-hold-y-menu"' in probe and "hold_y_menu_enabled=!fs::exists(" in probe
    assert '\\"hold_y\\":{\\"enabled\\":' in probe
    for key in ("fires", "blocked_presses", "last_hold_ms"):
        assert f'\\"{key}\\":' in probe, key
    assert '#include "y_hold.hpp"' in game_xr
    assert "y_hold_.update(raw_y,blocked," in game_xr
    # the feed is gameplay-only and stops while the panel is open (Y then closes it by a fresh press)
    feed = game_xr[game_xr.index("if(gameplay && settings_enabled_ && y_hold_enabled"):]
    assert "!settings_.open()" in feed[:feed.index("{")]
    assert "settings_.open_from_gameplay()" in game_xr
    assert "controller_input.suppress_y=true" in game_xr
    # a Talk offer or the wheel owns the press
    assert "game_xr->y_hold_blocked=talk_target.has_value() || talk_route.owned || x_wheel.wheel()" in probe
    assert "game_xr->y_hold_enabled=hold_y_menu_enabled;" in probe
    # fire frame keeps the held Y (the opening update seeds it); the panel is a world-locked quad, not part of the eye image
    assert "if(hold.suppress_y && !hold.fire)" in game_xr
    assert "menu_panel_.reset();" in game_xr and "y_hold_.reset();y_hold_wait_release_=true;" in game_xr
    assert "if(gameplay && !settings_.open()) menu_panel_.reset();" in game_xr
    assert "menu_panel_.hold(head_pose_now())" in game_xr and "settings_layer()" in game_xr and "ensure_settings_chain();" in game_xr
    assert "if(settings_.open() && !gameplay) {" in game_xr
    assert "game_xr->y_hold_blocked=false;" in probe
    assert "close_latch_" in settings
    assert "gameplay_open_" in settings and "void open_from_gameplay()" in settings
    assert '"file": "no-hold-y-menu"' in features and '"kind": "disable"' in features


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"),
          (root / "src/game_xr.hpp").read_text(encoding="utf-8"),
          (root / "src/vr_settings.hpp").read_text(encoding="utf-8"),
          (root / "tools/default_features.json").read_text(encoding="utf-8"))
    print("hold_y_routing_check ok")
