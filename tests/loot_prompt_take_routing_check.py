"""Hands plan Task 4, ruling H14 (the prompt equals the take): source routing in src/native_interaction_aim.hpp and src/dxgi_probe.cpp.

A squeeze takes only the hand's live native selection. The prompt path falls back to each hand's remembered target for 500 ms
(held_grace_ms), which bridges a routing flip (the other hand's cycle) but also showed a coin for half a second after the hand's own
native scan stopped selecting it (Task 3, floor coin: prompt 7.6 % against selected 0.5 %). Pins:
- target(), in the block that commits this hand's routed cycle (fresh, current revision, queued), right after target_owner_ is written,
  drops seen_[side] when held_prompt_survives_cycle(held owner, takeable(held owner), the cycle's owner) is false, and counts it;
- the count reaches the status JSON (loot_prompt_lingers_dropped);
- the prompt fallback itself (held, then the game's own selection) is unchanged.
The check runs itself against mutated sources, which must fail.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/native_interaction_aim.hpp"
PROBE = ROOT / "src/dxgi_probe.cpp"
DROP = ("if(auto& held=seen_[scope.side];held.tick && !held_prompt_survives_cycle(held.target.owner,takeable(held.target.owner),"
        "target_owner_[scope.side])) {\n                    held={};++loot_prompt_lingers_dropped_;")
COMMIT = "if(scope.side>=0 && fresh(scope,now) && scope.revision==routing_.revision() &&\n               queued_revision_[scope.side]==scope.revision) {"


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

    tgt = body(header, "void target(const Context& scope) noexcept {", "const InteractionRay* ray(")
    commit = body(tgt, COMMIT, "else if(scope.side<0)")
    need(commit != "", "target() commits the routed cycle in one block")
    i_owner = commit.find("target_owner_[scope.side]=object?owner:0;")
    i_drop = commit.find(DROP)
    need(i_owner >= 0 and i_drop > i_owner, "the loot linger is dropped after this cycle's owner is written, inside the commit block")
    need(tgt.count("held_prompt_survives_cycle(") == 1, "one drop site")
    need("std::atomic<UINT64> loot_prompt_lingers_dropped_{}" in header and "loot_prompt_lingers_dropped_=0;" in header, "counter declared and reset at install")
    need('",\\"loot_prompt_lingers_dropped\\":" << native_interaction_aim.loot_prompt_lingers_dropped()' in probe, "counter in the status JSON")
    select = body(header, "std::optional<QualifiedTarget> prompt_target_select(std::uint64_t now) {", "// The other hand's remembered target")
    need("if(const auto live=qualified_target(now))return live;" in select and "if(const auto held=newest_held_locked(now))return held;" in select
         and "return native_selected_locked(now);" in select, "prompt fallback order unchanged")
    need("static constexpr std::uint64_t held_grace_ms=500;" in header, "the grace itself is unchanged (non-loot keeps it)")
    return out


def main():
    header, probe = HEADER.read_text(encoding="utf-8"), PROBE.read_text(encoding="utf-8")
    real = problems(header, probe)
    assert not real, real
    assert DROP in header and COMMIT in header
    mutations = {
        "drop removed": (header.replace(DROP, "if(false) {\n                    ", 1), probe),
        "loot test bypassed": (header.replace("takeable(held.target.owner),target_owner_", "false,target_owner_", 1), probe),
        "drop before the owner write": (header.replace("target_generation_[scope.side]=generation.value_or(-1);target_owner_[scope.side]=object?owner:0;",
                                                       "target_generation_[scope.side]=generation.value_or(-1);", 1)
                                        .replace("held={};++loot_prompt_lingers_dropped_;", "held={};++loot_prompt_lingers_dropped_;target_owner_[scope.side]=object?owner:0;", 1), probe),
        "status missing": (header, probe.replace('",\\"loot_prompt_lingers_dropped\\":"', '",\\"x\\":"', 1)),
    }
    for name, (h, p) in mutations.items():
        assert h != header or p != probe, name
        assert problems(h, p), f"mutation not caught: {name}"
    print("loot_prompt_take_routing_check ok")


if __name__ == "__main__":
    sys.exit(main())
