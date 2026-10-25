"""G1 palm-ordered native Use (hands plan Task 3): source routing of the palm probe into order_native_use_hits.

Pins, in src/native_interaction_aim.hpp and src/dxgi_probe.cpp:
- sort_body orders the hand-volume hits with the published palm probe (`scope.scope->hand.palm`), the other hand's pick, the owner's
  model bounds and the loot test, and passes the legacy flag as the last argument;
- the legacy flag is build/legacy-palm-select OR the gaze/reach aim mode (ruling H7: gaze/reach keeps the v610 beam), and the legacy
  path gets the old inputs (the hand volume_centre and the aim ray's forward axis);
- publish_hands receives per-side probes built from `grip_poses` (the raw controller, C9/C10) plus the palm fit, the calibrated arm reach
  and the controller velocity, never from the drawn `hands.grips`;
- the occupancy gate in `hand_target` is unchanged (an occupied hand offers nothing; asserted, not changed);
- the arm reach crosses threads through atomics (avatar_fit_reuse lives on the present thread, the probes are built on the render thread).
The check also runs itself against mutated sources, which must fail.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/native_interaction_aim.hpp"
PROBE = ROOT / "src/dxgi_probe.cpp"

OCCUPANCY_GATE = (
    "const std::array<bool,2> hand_target{controller_aim_requested && !talk_targets[0] && native_interaction_aim.has_target(0,controls_now) && "
    "!(physical_grab_requested && native_grab.holding(0)) && !holding_object,\n"
    "                                    controller_aim_requested && !talk_targets[1] && native_interaction_aim.has_target(1,controls_now) && "
    "!(physical_grab_requested && native_grab.holding(1)) && !holding_object};")


def body(text, start, end):
    """The source between the first `start` and the next `end` after it ('' when either is missing)."""
    a = text.find(start)
    if a < 0:
        return ""
    b = text.find(end, a + len(start))
    return text[a:b] if b > a else ""


def problems(header, probe):
    out = []

    def need(ok, what):
        if not ok:
            out.append(what)

    sort = body(header, "static void sort_body(", "std::optional<bool> character_type(")
    need(bool(sort), "sort_body found")
    need("PalmProbe probe=scope.scope->hand.palm;" in sort, "sort_body starts from the published palm probe (scope.scope->hand.palm)")
    need("const bool legacy=select.legacy || scope.scope->hand.reach;" in sort, "legacy = build/legacy-palm-select OR gaze/reach mode (H7)")
    need("if(legacy) probe=legacy_beam_probe(scope.scope->hand.volume_centre,scope.scope->hand.hand.basis[0]);" in sort,
         "the legacy path gets the v610 inputs (volume_centre, aim forward axis)")
    call = re.search(r"order_native_use_hits\(hits\.data\(\),scope\.count,probe,scope\.preferred,scope\.other_hand,(.*?)\)\) throw", sort, re.S)
    need(call is not None, "order_native_use_hits gets the probe, the preferred owner and the other hand's owner")
    if call:
        args = call.group(1)
        need("self->hit_owner(hit,scope.world)" in args, "owner: hit_owner")
        need("self->takeable(e)" in args, "loot: takeable")
        need("self->owner_bounds(e)" in args, "bounds: owner_bounds (model+0x1840)")
        need(args.rstrip().endswith("select.tuning,legacy"), "tuning, then the legacy flag as the last argument")
    need(len(re.findall(r"order_native_use_hits\(", header)) == 1, "one ordering call in the header")

    # the knob: build/legacy-palm-select -> palm_select(..., legacy) -> select.legacy
    need('native_interaction_aim.palm_select(d2vr::load_grab_tuning(root/L"build"/L"grab-tuning.cfg").value,fs::exists(root/L"build"/L"legacy-palm-select"));' in probe,
         "dxgi_probe passes build/legacy-palm-select (and grab-tuning.cfg) to palm_select")
    sel = body(header, "void palm_select(", "}")
    need("palm_select_.legacy=legacy" in sel and "palm_select_.tuning=tuning" in sel, "palm_select stores the knob and the tuning")

    # the other hand's pick: its remembered (held) target owner, recorded with the retrieved set
    fetch = body(header, "std::uintptr_t fetch_result_body(", "static void __fastcall sort(")
    need("other_hand=self->other_hand_owner_locked(scope->side,GetTickCount64());" in fetch, "fetch_result records the other hand's owner")
    need("retrieved_={*data,world,static_cast<unsigned>(*count),preferred,scope,false,other_hand};" in fetch, "the retrieved set carries it")

    # the probes: raw controller grip (C9), never the drawn hands
    pub = body(probe, "std::array<d2vr::PalmProbe,2> palms{};", "native_interaction_aim.publish_hands(")
    need(bool(pub), "the probes are built right before publish_hands")
    need("d2vr::palm_probe(side,*grip_poses[side],game_xr->palm_offset(side)," in pub, "probes from grip_poses + the palm fit")
    need("avatar_fit_arm_reach[side].load(" in pub, "the calibrated arm reach")
    need("hand_velocity[side]" in pub, "the controller velocity")
    need("hands.grips" not in pub and "palm_grips" not in pub and "hand_world" not in pub, "never the drawn hands")
    need("native_interaction_aim.publish_hands(*body,hand_rays,game_xr->pose_epoch(),character_tick,grip_centres,reach,grip_poses,character_carry,palms);" in probe,
         "publish_hands receives the probes")
    need(len(re.findall(r"native_interaction_aim\.publish_hands\(", probe)) == 1, "one publish_hands call")
    need("std::array<PalmProbe,2> palms={}) {" in body(header, "void publish_hands(", ") {") + ") {", "publish_hands takes the per-side probes")
    need("if(published_[side])hands_[side].palm=palms[side];" in header, "each published hand keeps its probe")
    need("PalmProbe palm{};};" in body(header, "struct Publication {", "struct Contact {"), "Publication carries the probe")

    # arm reach: present thread -> render thread through atomics, set from the active fit, reset with it
    need("static std::array<std::atomic<double>,2> avatar_fit_arm_reach{};" in probe, "arm reach atomics declared")
    need("arm[side]=active->arm_reach_metres[side].value_or(0);" in probe, "set from avatar_fit_reuse.active_fit()->arm_reach_metres")
    need(len(re.findall(r"avatar_fit_arm_reach\[side\]\.store\(", probe)) >= 2, "stored per frame and reset on session start")

    # occupancy gate unchanged
    need(OCCUPANCY_GATE in probe, "hand_target occupancy gate unchanged")
    return out


def main():
    header, probe = HEADER.read_text(encoding="utf-8"), PROBE.read_text(encoding="utf-8")
    found = problems(header, probe)
    for p in found:
        print("FAIL:", p)
    if found:
        return 1
    mutations = {
        "drawn hands feed the probes": (header, probe.replace("d2vr::palm_probe(side,*grip_poses[side],", "d2vr::palm_probe(side,hands.grips[side],", 1)),
        "knob ignored": (header, probe.replace('fs::exists(root/L"build"/L"legacy-palm-select")', "false", 1)),
        "reach mode loses the old beam": (header.replace("const bool legacy=select.legacy || scope.scope->hand.reach;", "const bool legacy=select.legacy;", 1), probe),
        "volume centre instead of the palm": (header.replace("PalmProbe probe=scope.scope->hand.palm;", "PalmProbe probe=legacy_beam_probe(scope.scope->hand.volume_centre,{});", 1), probe),
        "occupancy gate changed": (header, probe.replace("&& !(physical_grab_requested && native_grab.holding(1)) && !holding_object}", "&& !holding_object}", 1)),
        "other hand dropped": (header.replace("scope.preferred,scope.other_hand,", "scope.preferred,0,", 1), probe),
    }
    for name, (h, p) in mutations.items():
        if (h, p) == (header, probe) or not problems(h, p):
            print(f"FAIL: mutation not caught: {name}")
            return 1
    print("PASS: palm probe routing (raw grip + fit + arm reach + velocity), legacy-palm-select / reach keep the old beam, other hand last, occupancy gate unchanged")
    return 0


if __name__ == "__main__":
    sys.exit(main())
