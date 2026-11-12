"""Hands plan Task 4 fix round 1, ruling H16 (user 2026-10-25: loot line of sight is checked from the hand): source routing.

Pins, in src/native_interaction_aim.hpp and src/dxgi_probe.cpp:
- the occlusion hook calls the original 3b7b90 exactly once, with `los_origin` from loot_los_origin(); that helper returns the native
  pointer unless the call is a stage-0 LOS (callers 3b8b05 / 3b87b5, range 0, this scope's sensor, fresh) on a takeable owner, and then
  hands over a thread-local copy of the gate's palm (no game memory written: the same seam as range_origin());
- the gate gets `!scope->hand.reach` (hand mode) and `scope->hand.clamped_palm`, never the raw grip or palm probe;
- dxgi_probe publishes as clamped palm only the hand clamp's own drawn palm (`g.position` after the clamp) when the clamp output is valid,
  not a yield, a re-acquire or a released side, with the clamp enabled; <= 100 ms old at publish_hands; never grip_poses / raw_grips;
- the counters reach the status JSON.
The check runs itself against mutated sources, which must fail.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/native_interaction_aim.hpp"
PROBE = ROOT / "src/dxgi_probe.cpp"
CALL = "const auto result=occlusion_(sensor,entity,los_origin,axis,point,range,output); // original once, raw return preserved"
PICK = "const auto* los_origin=loot_los_origin(observed_scope,sensor,entity,point,range,caller,origin);"
GATE = "loot_los_hand_origin(true,!scope->hand.reach,scope->hand.clamped_palm,"
SWEPT = ("if(clamp_live && clamped.valid && !clamp_yield && !clamped.reacquired && clamped.state!=d2vr::ClampState::released) "
         "clamp_palm_swept[side]=g.position;")


def body(text, start, end):
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

    los = body(header, "std::uintptr_t occlusion_result_body(", "bool idle_character_publication(")
    need(los.count("occlusion_(") == 1 and CALL in los, "the original 3b7b90 runs once, with los_origin")
    need(0 <= los.find(PICK) < los.find(CALL), "los_origin is chosen before the original call")
    helper = body(header, "const void* loot_los_origin(const Context* scope,", "static std::uintptr_t __fastcall occlusion(")
    need(helper != "", "loot_los_origin present")
    for cond in ("scope->stage!=0", "scope->side<0", "range!=0", "(caller!=base_+0x3b8b05 && caller!=base_+0x3b87b5)",
                 "reinterpret_cast<std::uintptr_t>(sensor)!=scope->sensor", "!fresh(*scope,GetTickCount64())",
                 "const bool take=takeable(owner);", "if(!take && !body) return native;",
                 "const bool body=!take && hand_judged_los(false,character_type(owner).value_or(false),"):  # v1228: downed characters too
        need(cond in helper, f"loot_los_origin gate: {cond}")
    need(GATE in helper, "the gate reads hand mode and the clamped palm")
    need("loot_los_origin_.data()" in helper and "return native;" in helper, "a thread-local copy or the native pointer")
    for bad in ("write(", "*reinterpret_cast<float", "memcpy(", "grip", ".palm."):
        need(bad not in helper, f"loot_los_origin never uses {bad!r}")
    need("inline static thread_local std::array<float,4> loot_los_origin_{};" in header, "the origin copy is thread-local")
    need("if(published_[side] && clamped_palms[side] && finite(*clamped_palms[side]))hands_[side].clamped_palm=clamped_palms[side];" in header,
         "publish_hands stores the clamped palm")
    # dxgi_probe: the clamp's own palm, valid output only, published under the mutex, fresh at publish_hands
    need(SWEPT in probe, "only a valid, swept, non-yield, non-released clamp output is a clamped palm")
    need("hand_clamped_palm[side]=hand_clamp_enabled && clamp_palm_swept[side]" in probe, "no clamped palm with build/no-hand-clamp")
    need("character_tick-hand_clamped_tick[side]<=100) clamped_palms[side]=hand_clamped_palm[side];" in probe, "<= 100 ms old")
    need("grip_poses,character_carry,palms,clamped_palms,preview);" in probe, "publish_hands gets the clamped palms")
    published = body(probe, "std::array<std::optional<d2vr::Vec3>,2> clamped_palms{};", "native_interaction_aim.publish_hands(")
    need(published != "" and "grip_poses" not in published and "raw_grips" not in published and "hand_raw_world" not in published, "the clamped palms never come from the controller")
    need('",\\"loot_hand_los\\":" << native_interaction_aim.loot_hand_los()' in probe, "counters in the status JSON")
    return out


def main():
    header, probe = HEADER.read_text(encoding="utf-8"), PROBE.read_text(encoding="utf-8")
    real = problems(header, probe)
    assert not real, real
    mutations = {
        "eye always": (header.replace(CALL, CALL.replace("los_origin,axis", "origin,axis"), 1), probe),
        "non-loot swapped": (header.replace("if(!take && !body) return native;", "", 1), probe),
        "gaze swapped": (header.replace(GATE, "loot_los_hand_origin(true,true,scope->hand.clamped_palm,", 1), probe),
        "raw controller published": (header, probe.replace(SWEPT, SWEPT.replace("g.position;", "raw_grips[side].position;"), 1)),
        "released side published": (header, probe.replace(SWEPT, SWEPT.replace(" && clamped.state!=d2vr::ClampState::released", ""), 1)),
        "stale palm": (header, probe.replace("character_tick-hand_clamped_tick[side]<=100)", "character_tick-hand_clamped_tick[side]<=100000)", 1)),
    }
    for name, (h, p) in mutations.items():
        assert (h, p) != (header, probe), name
        assert problems(h, p), f"mutation not caught: {name}"
    print("loot_hand_los_routing_check ok")


if __name__ == "__main__":
    sys.exit(main())
