"""G5 (plan Task 12): the free-finger probes, the thin-prop collider and the cast budget in src/dxgi_probe.cpp.

- a thin prop (under 1 m, smallest local axis < 2 cm) becomes a held_slab from its oriented bounds in finger_collider, not a classify_held fit;
- a free hand posts its palm-normal cast every frame (no alternate-frame gate) and fingertip casts along the current pose;
- the casts per free hand per frame are at most the constant finger_probe_casts (2, or 3 with build/finger-probe-3);
- the weapon and native-hand skip is unchanged: a weapon hand or a hand the game animates is never a free hand and gets no touch curl;
- build/no-finger-surface keeps the v609 probes, budget and TTL.
Runs itself against mutated sources, each of which must fail.
"""
import re
import sys
from pathlib import Path


def between(text: str, start: str, end: str) -> str:
    a = text.index(start)
    return text[a:text.index(end, a)]


def check(probe: str) -> list:
    problems = []
    def need(cond, what):
        if not cond:
            problems.append(what)
    collider = between(probe, "static std::optional<d2vr::rig::HeldShape> finger_collider(", "\n}\n")
    need("held_local_extents(" in collider and "held_slab(" in collider, "thin props: held_slab from the local box")
    need(re.search(r"finger_surface\s*&&[^;]*thin_prop_axis", collider) is not None, "thin slab gated by the knob and the 2 cm axis")
    need("inline constexpr double thin_prop_axis=.02" in probe or "constexpr double thin_prop_axis=.02" in probe, "thin axis 2 cm")
    need(collider.index("held_slab(") < collider.index("classify_held(") if "held_slab(" in collider and "classify_held(" in collider else False,
         "the thin path is taken before classify_held")
    block = between(probe, "// G5 free-hand probes", "// G5 end free-hand probes")
    need("post_weapon_segment(side,3," in block, "palm cast on slot 3")
    palm_lines = [line.strip() for line in block.splitlines() if "post_weapon_segment(side,3," in line]
    need("probe_frame+side" not in block and "&1u)==0" not in block and len(palm_lines) == 1 and palm_lines[0].startswith("{const auto sweep="),
         "palm cast every frame for a free hand (an unconditional statement)")
    need("d2vr::rig::palm_sweep(*geometry,palm,d2vr::rig::free_palm_reach)" in block, "free palm reach 8 cm")
    need("d2vr::rig::finger_sweep(*geometry,palm,f,finger_drawn_angles[side][f])" in block, "fingertip along the current pose")
    posts = block.count("post_weapon_segment(")
    need(posts == 2, f"two post sites in the free branch (palm + the fingertip loop), found {posts}")
    need(re.search(r"for\(unsigned k=0;k\+1<finger_probe_casts;\+\+k\)", block) is not None, "fingertip casts bounded by finger_probe_casts-1")
    need(re.search(r"static constexpr unsigned finger_probe_casts_default=2;static unsigned finger_probe_casts=finger_probe_casts_default;", probe) is not None,
         "finger_probe_casts default is 2 (ruling H18); 3 only via build/finger-probe-3")
    need('finger_probe_casts=fs::exists(root/L"build"/L"finger-probe-3")?3u:finger_probe_casts_default;' in probe, "build/finger-probe-3 knob")
    need("finger_free_budget(finger_ik[side].speed,finger_free_solve_budget)" in probe, "fast-hand solve cap routed")
    need("const bool ease_opening=(held_ease || free_hand) && !legacy_finger_ease;" in probe, "legacy-finger-ease turns the free-hand eased opening off")
    need("free_hand=finger_surface && !legacy_finger_ease &&" in probe, "legacy-finger-ease turns G5 free-hand probes off (v611)")
    need("start(finger_drawn_valid[side]?finger_drawn_angles[side]:d2vr::rig::FingerAngles{},fk_t)" in probe, "hold edge blends from the open pose when nothing was drawn")
    need("std::array<std::uint64_t,2> held_shape_sig" in probe, "ease signature is a hash, not a string")
    need("weapon_hit(side,slot,fk_now,d2vr::NativeInteractionAim::producer_clamp,150)" in block, "the finger slots read with 150 ms")
    need("probe_cache[side].probed(" in block and "probe_cache[side].shapes(" in block, "the sticky cache feeds the solver")
    # the free hand is never a weapon hand, a native-animated hand or a hold, and the knob turns it off
    free = re.search(r"\bfree_hand=(finger_surface[^;]*);", probe)
    need(free is not None, "free_hand defined")
    if free:
        expr = free.group(1)
        for token in ("finger_surface", "!holding", "!native_hold", "!((fk_weapon_bits>>side)&1u)", "!(hands.native_hands>>side&1u)"):
            need(token in expr, f"free_hand requires {token}")
    need("const bool touch_side=!legacy_finger_curl && game_xr->touch_status().bound && !(hands.native_hands>>side&1u) && !((fk_weapon_bits>>side)&1u);" in probe,
         "the weapon / native-hand touch skip is unchanged")
    # v609 path kept for the knob / non-free hands
    need("if(((probe_frame+side)&1u)==0) {const auto sweep=d2vr::rig::palm_sweep(*geometry,palm);" in probe, "v609 alternate palm sweep kept")
    need("d2vr::NativeInteractionAim::weapon_hit(side,slot,fk_now)" in probe, "v609 100 ms read kept")
    need('finger_surface=!fs::exists(root/L"build"/L"no-finger-surface");' in probe, "build/no-finger-surface knob")
    need("const unsigned budget=free_hand?d2vr::rig::finger_free_budget(finger_ik[side].speed,finger_free_solve_budget):finger_solve_budget;" in probe, "free-hand solve budget")
    need(re.search(r"static unsigned finger_free_solve_budget=[25];", probe) is not None, "free solve budget constant")
    need("stop_extended" not in probe or "free_hand" in between(probe, "contact=d2vr::rig::finger_contact(", ";}"), "stop_extended for free hands only")
    for key in ("probe_hits_world", "probe_hits_thin", "stuck_frames"):
        need(f'\\"{key}\\":' in probe, f"status finger_ik.{key}")
    for key in ("blocked", "hyperextended"):
        need(f'bits("{key}",' in probe, f"status finger_ik.{key}")
    return problems


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    probe = (root / "src/dxgi_probe.cpp").read_text(encoding="utf-8")
    build = (root / "tools/build-msvc.cmd").read_text(encoding="utf-8")
    problems = check(probe)
    if "python tests\\finger_collider_routing_check.py" not in build:
        problems.append("registered in build-msvc.cmd")
    if problems:
        for p in problems:
            print("FAIL:", p)
        return 1
    mutations = {
        "palm cast on alternate frames": ("{const auto sweep=d2vr::rig::palm_sweep(*geometry,palm,d2vr::rig::free_palm_reach);", "if(probe_frame&1u) {const auto sweep=d2vr::rig::palm_sweep(*geometry,palm,d2vr::rig::free_palm_reach);"),
        "a third fingertip cast unbounded": ("for(unsigned k=0;k+1<finger_probe_casts;++k)", "for(unsigned k=0;k<finger_probe_casts;++k)"),
        "weapon hand counted free": ("free_hand=finger_surface && !legacy_finger_ease && !holding && !native_hold && !((fk_weapon_bits>>side)&1u) && ", "free_hand=finger_surface && !legacy_finger_ease && !holding && !native_hold && "),
        "thin props through classify_held": ("held_slab(", "held_slab_unused("),
        "fixed curl-0.5 probe": ("d2vr::rig::finger_sweep(*geometry,palm,f,finger_drawn_angles[side][f])", "d2vr::rig::finger_sweep(*geometry,palm,f)"),
    }
    for name, (old, new) in mutations.items():
        if old not in probe:
            print("FAIL: mutation anchor missing:", name)
            return 1
        if not check(probe.replace(old, new, 1)):
            print("FAIL: mutation not caught:", name)
            return 1
    print("finger_collider_routing_check ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
