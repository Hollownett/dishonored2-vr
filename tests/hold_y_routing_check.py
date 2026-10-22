from pathlib import Path


def check(probe: str, game_xr: str, settings: str, features: str) -> None:
    assert 'L"no-hold-y-menu"' in probe and "hold_y_menu_enabled=!fs::exists(" in probe
    assert '\\"hold_y\\":{\\"enabled\\":' in probe
    for key in ("fires", "blocked_presses", "last_hold_ms"):
        assert f'\\"{key}\\":' in probe, key
    assert '#include "y_hold.hpp"' in game_xr
    assert "y_hold_menu_.update(controller_input.y,y_hold_blocked.load(),hold_screen" in game_xr
    assert "!gameplay && settings_available && settings_enabled_?YHoldMenu::Screen::pause" in game_xr
    assert "controller_input.pause_request=hold.pause;" in game_xr
    assert "if(hold.settings && !settings_.open())" in game_xr
    assert "settings_.open_from_gameplay();else settings_.open_from_menu();" in game_xr
    assert "if(!legacy_hold_y_settings && !settings_.open()) settings_input.y=false;" in game_xr
    assert "settings_.update(settings_input,gameplay," in game_xr
    assert 'L"legacy-hold-y-settings"' in probe
    assert "game_xr->legacy_hold_y_settings=legacy_hold_y_settings;" in probe
    # a Talk offer or the wheel owns the press
    assert "game_xr->y_hold_blocked=talk_target.has_value() || talk_route.owned || x_wheel.wheel()" in probe
    assert "game_xr->y_hold_enabled=hold_y_menu_enabled;" in probe
    assert "if(game_xr->y_hold_blocked.load()) input.pause_request=false;" in probe
    # Only an opening settings update sees held Y; Pause consumes Y on its fire frame.
    assert "if(hold.suppress_y && !hold.settings)" in game_xr
    assert "menu_panel_.reset();" in game_xr
    assert "if(gameplay && !settings_.open()) menu_panel_.reset();" in game_xr
    assert "menu_panel_.hold(head_pose_now())" in game_xr and "settings_layer()" in game_xr and "ensure_settings_chain();" in game_xr
    assert "if(settings_.open() && !gameplay) {" in game_xr
    assert "game_xr->y_hold_blocked=false;" in probe
    assert "close_latch_" in settings
    assert "SettingsMenuLayout::gameplay_drawable(gameplay,authored_camera," in game_xr
    assert "hold_ready=y_hold_enabled && state==XR_SESSION_STATE_FOCUSED && controller_input.active" in game_xr
    assert "(!legacy_hold_y_settings || (settings_enabled_ && panel_drawable))" in game_xr
    assert "if(settings_.gameplay_panel_open() && !panel_drawable) settings_.suspend();" in game_xr
    assert "if(settings_chain_ && xrDestroySwapchain) {xrDestroySwapchain(settings_chain_);settings_chain_=XR_NULL_HANDLE;}" in game_xr
    assert "latch_timeout_s=1.5" in settings
    assert "gameplay_open_" in settings and "void open_from_gameplay()" in settings
    assert '"file": "no-hold-y-menu"' in features and '"kind": "disable"' in features
    assert '"file": "legacy-hold-y-settings"' in features


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"),
          (root / "src/game_xr.hpp").read_text(encoding="utf-8"),
          (root / "src/vr_settings.hpp").read_text(encoding="utf-8"),
          (root / "tools/default_features.json").read_text(encoding="utf-8"))
    print("hold_y_routing_check ok")
