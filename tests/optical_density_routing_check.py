from pathlib import Path

# v1030: canting used to force the optical density (a 1.069x game render that the XR eye, sized to the runtime's recommended rect,
# could not hold, so an EyeResolve pass ran on both eyes every frame). The v1029 session of 2026-11-05 measured that chain at
# 15.94 ms per frame and 72448 resolves; the v1020 session it is compared against ran the same canted frusta at the eye size with no
# resolve. The density is now opt-in (build/optical-density) and canting alone must not request it.
def check(probe: str) -> None:
    cant = probe.index('cant_requested=symmetric_frustum_requested && headset_size_requested')
    line = probe[cant:cant + 400]
    assert '!fs::exists(root/L"build"/L"no-cant")' in line, "build/no-cant must still turn canting off"
    # the request line right after the cant decision
    after = probe[cant:cant + 4000]
    assert 'if(cant_requested && fs::exists(root/L"build"/L"optical-density")) {optical_density_requested=true;optical_density_applied=false;}' in after, \
        "canting may only request the optical density when build/optical-density is present"
    assert 'if(cant_requested) {optical_density_requested=true' not in probe, "canting must not force the optical density"
    # the cfg flag still requests it on its own (the launcher's output-size option)
    assert 'optical_density_requested=optical_density_flag==1;' in probe
    # the density path itself is unchanged: applied once in gameplay, and the resolution-change path still honours it
    assert 'if(optical_density_requested && !optical_density_applied && gameplay && game_xr->tracking_views()' in probe
    assert 'game_xr->scaled_output_size(*request,optical_density_requested && optical_density_applied)' in probe


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"))
    print("optical_density_routing_check ok")
