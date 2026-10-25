from pathlib import Path
import re


def check(probe: str, xr: str) -> None:
    gate = probe.index("if(saved_settings_requested) {", probe.index('config>>saved_settings_flag'))
    load = probe.index("d2vr::load_preferences(", gate)
    migrate = probe.index("remove_legacy_aa_knobs(", load)
    crosshair = probe.index('L"no-hand-crosshair",legacy_ec', migrate)
    pending = probe.rfind("if(first_start_pending)", load, migrate)
    assert pending != -1 and load < pending < migrate < crosshair, "AA migration must be gated on the missing AA marker after the preferences load"
    assert probe.rfind("if(first_start_pending)", load, crosshair) == pending, "the crosshair migration must not be gated on the AA marker"
    cross_block = probe[crosshair:probe.index("if(preferences) {", crosshair)]
    assert "fs::remove" not in cross_block and "crosshair_legacy_file=" in cross_block, "migration may only remember the file, not delete it"
    persist = probe.index("game_xr->persist_preferences()")
    removal = probe.index("fs::remove(crosshair_legacy_file", persist - 200)
    assert persist < removal and probe.count("fs::remove(crosshair_legacy_file") == 1, "the legacy file is deleted only after a successful save"
    assert re.search(r"if\(game_xr->persist_preferences\(\)\) fs::remove\(crosshair_legacy_file", probe), "delete must be conditional on the save succeeding"
    assert "if(log_note) log_note(" in xr and "log_note=&note_recoverable" in probe, "a failed first-start save must be logged"
    assert "first_start_pending=!aa_chosen_preference" in probe, "the pending flag must come from the saved marker"
    end = probe.index("native_menu_mode=0;config>>native_menu_mode;", gate)
    assert crosshair < end, "migration must sit inside the saved-settings block"
    assert probe.count("remove_legacy_aa_knobs(") == 1, "legacy knobs may only be removed by the one-time migration"
    knob_reads = [probe.index(k) for k in ('L"force-taa"', 'L"aa-mode"', 'L"fsr4")')]
    assert all(read > crosshair for read in knob_reads), "the knob reads must follow the migration"
    adopt = probe.index("game_xr->adopt_first_start_aa(")
    vendor = probe.rindex("present_adapter_vendor.load(std::memory_order_relaxed)) {", 0, adopt)
    dlss_block = probe.rindex("game_xr->dlss_available(dlss_offered());", 0, adopt)
    assert vendor > dlss_block, "the first-start decision belongs inside the Present DLSS block"
    guard = probe.rindex("if(first_start_pending && present_adapter_vendor.load(std::memory_order_relaxed))", 0, adopt)
    assert dlss_block < guard < adopt, "adopt must be guarded by the pending flag and a known vendor"
    assert probe.count("adopt_first_start_aa(") == 1, "only one adopt call site"
    assert re.search(r"first_start_pending=false;\s*game_xr->adopt_first_start_aa\(", probe), "the pending flag must clear before adopting"
    assert "decision!=d2vr::FirstStartAa::wait" in probe, "wait must retry next Present"
    assert "settings_.aa_chosen(),settings_.crosshair(),settings_.scripted_recenter(),settings_.cutscene_mode()}" in xr, "every GameXr save must pass both new fields"
    assert xr.count("save_preferences(") == 2 and xr.count("settings_.aa_chosen(),settings_.crosshair(),settings_.scripted_recenter(),settings_.cutscene_mode()}") == 2, "both GameXr saves must carry the new fields"
    assert "aa_chosen_preference,crosshair_preference,scripted_recenter_preference,static_cast<d2vr::CutsceneMode>(cutscene_mode_setting.load()));" in probe, "GameXr must be constructed with the saved marker and crosshair"
    assert '"aa_first_start":{' in probe.replace('\\"', '"'), "status must report aa_first_start"
    # the sim's --vr-setting temporal_aa writes build/aa-mode; the diagnostic reads stay
    assert 'std::ifstream knob(root/L"build"/L"aa-mode")' in probe and 'L"force-taa"' in probe and 'L"build"/L"fsr4")' in probe


root = Path(__file__).resolve().parents[1]
probe = (root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8")
xr = (root / "src" / "game_xr.hpp").read_text(encoding="utf-8")
check(probe, xr)
mutations = (
    (probe.replace("if(first_start_pending) { // v609 migration", "if(true) { // v609 migration", 1), xr),
    (probe.replace("first_start_pending=!aa_chosen_preference", "first_start_pending=true", 1), xr),
    (probe.replace("if(first_start_pending && present_adapter_vendor.load(std::memory_order_relaxed))", "if(present_adapter_vendor.load(std::memory_order_relaxed))", 1), xr),
    (probe.replace("first_start_pending=false;\n                                        game_xr->adopt", "game_xr->adopt", 1), xr),
    (probe, xr.replace("settings_.aa_chosen(),settings_.crosshair(),settings_.scripted_recenter(),settings_.cutscene_mode()});", "false,true});", 1)),
    (probe.replace("{   // v609 crosshair migration, whatever", "if(first_start_pending) {   // v609 crosshair migration, whatever",1), xr),
    (probe.replace("if(game_xr->persist_preferences()) fs::remove(crosshair_legacy_file,legacy_ec);", "{game_xr->persist_preferences();fs::remove(crosshair_legacy_file,legacy_ec);}",1), xr),
    (probe, xr.replace("if(log_note) log_note(", "if(false) log_note(",1)),
    (probe.replace('std::ifstream knob(root/L"build"/L"aa-mode")', 'std::ifstream knob(root/L"build"/L"aa-mode-gone")', 1), xr),
)
for index, (mutated_probe, mutated_xr) in enumerate(mutations):
    assert (mutated_probe, mutated_xr) != (probe, xr), f"Mutation {index} did not reach its production target"
    try:
        check(mutated_probe, mutated_xr)
    except (AssertionError, ValueError):
        continue
    raise AssertionError(f"Unsafe production mutation {index} was accepted")
print("PASS AA first-start routing: migration gate, Present-only decision, v9 save fields, knob reads, negative mutations")
