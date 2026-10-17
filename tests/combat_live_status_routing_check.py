from pathlib import Path
import sys


def check(source: str, aim: str) -> None:
    helper_begin = source.index("static std::string combat_policy_status_json()")
    helper_end = source.index("static std::atomic<unsigned> wheel_dbg_w", helper_begin)
    helper = source[helper_begin:helper_end]
    assert "{std::lock_guard<std::mutex> lock(combat_policy_mutex);value=combat_policy_observation;}" in helper
    assert "d2vr::NativeInteractionAim::sword_contact_status_json()" in helper
    assert '\\"policy_rt_edges\\":' in helper
    live_begin = source.index('std::ofstream live(output_directory/L"xr-live-status.json"')
    live_end = source.index('if(!live) xr_status_write_failed=true', live_begin)
    live = source[live_begin:live_end]
    assert "live <<" in live
    assert live.count("combat_policy_status_json()") == 1, "Live status must export the existing coherent combat/contact helper exactly once"
    assert "<< feedback_status_json() << combat_policy_status_json()" in live
    final_begin = source.index('<< ",\\"physical_swings\\":" << physical_swings << combat_policy_status_json()')
    final_end = source.index("feedback_status_json()", final_begin)
    assert source[final_begin:final_end].count("combat_policy_status_json()") == 1, "Final probe output must retain the same helper"
    assert source.count("combat_policy_status_json()") == 3, "Expected definition, live call and final call"
    assert "std::lock_guard<std::mutex> guard(trace_mutex_);return sword_last_contact_;" in aim
    assert "const auto observation=sword_contact_observation();" in aim
    for field in ("capture_rejected", "queue_full", "nonlethal_rejected", "epoch_rejected", "consumer_rejected", "invoked", "native_returned"):
        assert '\\"' + field + '\\":' in aim, f"Missing existing contact field: {field}"


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    if len(sys.argv) > 1:
        root = Path(sys.argv[1]).resolve()
    source = (root / "src/dxgi_probe.cpp").read_text(encoding="utf-8-sig")
    aim = (root / "src/native_interaction_aim.hpp").read_text(encoding="utf-8-sig")
    check(source, aim)
    mutations = (
        source.replace("<< feedback_status_json() << combat_policy_status_json()", "<< feedback_status_json()", 1),
        source.replace('<< ",\\"physical_swings\\":" << physical_swings << combat_policy_status_json()',
                       '<< ",\\"physical_swings\\":" << physical_swings', 1),
        source.replace("value=combat_policy_observation;", "value={};", 1),
        source.replace("d2vr::NativeInteractionAim::sword_contact_status_json()", 'std::string("null")', 1),
    )
    for index, mutation in enumerate(mutations):
        assert mutation != source, f"Mutation {index} missed its production target"
        try:
            check(mutation, aim)
        except (AssertionError, ValueError):
            continue
        raise AssertionError(f"Missing or incoherent live/final diagnostic mutation {index} was accepted")
    print("PASS live/final combat/contact helper source routing; not runtime or damage proof")
