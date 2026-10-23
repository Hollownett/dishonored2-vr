"""Plan B task B11 wiring: the hand-body spike exists only behind build/hand-body-spike (absent knob: no hook, no counter, no thread, no
engine call), its tick runs from the existing 0x89fb0 hook, it stops (destroying its body on the game thread) before any shutdown path
disables NativeGrab, it calls only the engine functions the B11 list allows (each pinned with the RE doc's bytes), and the mod's own casts
discard a hit on the spike body."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
probe = (root / "src/dxgi_probe.cpp").read_text(encoding="utf-8")
grab = (root / "src/native_grab.hpp").read_text(encoding="utf-8")
spike = (root / "src/hand_body_spike.hpp").read_text(encoding="utf-8")
engine = (root / "src/hand_body_spike_engine.hpp").read_text(encoding="utf-8")
trace = (root / "src/native_world_trace.hpp").read_text(encoding="utf-8")
doc = (root / "docs/native-hand-body-re.md").read_text(encoding="utf-8")


def function(source, signature):
    start = source.index(signature)
    depth = 0
    for i in range(source.index("{", start), len(source)):
        depth += source[i] == "{"
        depth -= source[i] == "}"
        if depth == 0:
            return source[start:i + 1]
    raise AssertionError(signature)


# 1. knob gate: the only install is inside start_hand_body_spike, which runs only when the knob file exists
assert probe.count("start_hand_body_spike(root,output)") == 1
assert 'if(fs::exists(root/L"build"/L"hand-body-spike")) start_hand_body_spike(root,output);' in probe, "spike install must sit behind the knob"
assert probe.count("HandBodySpikeEngine::install(") == 1 and "HandBodySpikeEngine::install(" in function(probe, "static void start_hand_body_spike(")
start = function(probe, "static void start_hand_body_spike(")
assert "parse_spike_route(text)" in start and "native_grab.enabled()" in start, "route parsed once, NativeGrab's hook required"
assert 'hand-body-spike.txt' in start, "the knob value is logged"

# 2. hooks, the dispatch entry and the cast exclusion are armed only inside install()
install = function(engine, "static bool install(")
assert engine.count("MH_CreateHook(") == 1 and "MH_CreateHook(" in install
assert engine.count("MH_EnableHook(") == 1 and "MH_EnableHook(" in install
assert engine.count("hand_body_spike_dispatch.store(&dispatch_entry") == 1 and "hand_body_spike_dispatch.store(&dispatch_entry" in install
assert install.index("verify(base,read,why)") < install.index("MH_CreateHook("), "pins are checked before any hook"
assert install.index("spike_route_refusal(route)") < install.index("MH_CreateHook("), "R2/R3 install nothing"
assert "inline std::atomic<void (*)(void* manager)> hand_body_spike_dispatch{nullptr};" in spike
assert "if(const auto spike=hand_body_spike_dispatch.load(std::memory_order_relaxed)) {try {spike(manager);} catch(...) {}}" in grab
invoke = function(grab, "void invoke(void* manager,void* command)")
assert invoke.index("original_(manager,command);") < invoke.index("hand_body_spike_dispatch.load"), "the spike ticks after the original dispatcher"
assert "inline std::atomic<std::uint32_t> cast_excluded_body{0xffffffffu};" in trace
for name, text in (("dxgi_probe.cpp", probe), ("native_grab.hpp", grab), ("hand_body_spike.hpp", spike)):
    assert "cast_excluded_body.store" not in text, name + " must not arm the cast exclusion"
convert = function(trace, "inline std::optional<WorldTraceHit> convert_world_hit(")
assert "excluded=cast_excluded_body.load(std::memory_order_relaxed); excluded!=0xffffffffu && c.body==excluded" in convert and "cast_excluded_hits.fetch_add" in convert

# 3. the spike stops (destroying on the game thread) before NativeGrab is disabled, in every shutdown path
for disable in ("const bool grab_disabled=native_grab.disable();", "const bool grab_disabled=interaction_disabled && native_grab.disable();",
                "if(native_interaction_aim.disable())native_grab.disable();"):
    at = probe.index(disable)
    before = probe.rfind("HandBodySpikeEngine::stop();", 0, at)
    assert before != -1 and at - before < 6000, "spike stop must precede " + disable
stop = function(engine, "static bool stop()")
assert stop.index("stop_requested_.store(true") < stop.index("disable_hooks()"), "the destroy request comes before the unhook"

# 4. engine calls: only the B11 list (plus the doc's pure-store ctors), each through guarded_call, never the forbidden ones
called = set(re.findall(r"reinterpret_cast<[\w:]+>\(base_\+(\w+_rva)\)", engine))
assert called == {"userdata_rva", "sphere_rva", "cinfo_rva", "create_rva", "destroy_rva", "velocity_rva"}, called
code = re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', re.sub(r"//[^\n]*", "", engine))   # comments and string literals removed
for rva in ("0x91b50", "0x91e20", "0xc85860", "0xc90de0", "0x8ae9b0", "0x883f60", "0x97600", "0xc8e7c0", "0x808ea0", "0x809d80"):
    assert rva not in code, rva
for call in ("create(reinterpret_cast<void*>(world),&id,cinfo_.data(),1,1)", "destroy(reinterpret_cast<void*>(world),ids,1,0)",
             "velocity(reinterpret_cast<void*>(world),handle,linear.data(),angular.data(),0)"):
    assert re.search(r"auto call=\[&\] \{[^}]*" + re.escape(call), engine), call
    assert engine.count(call) == 1
assert engine.count("guarded_call(") >= 4
create = function(engine, "static bool create_body(")
assert "hand_body::hand_filter_word(false)" in create and "flags=0" in create and "motion_type=2" in create and "mass=2.f" in create
assert "busy_now()" in create and "busy_now()" in function(engine, "static bool destroy_body(") and "busy_now()" in function(engine, "static bool push_body(")
assert "world!=world_at_create_" in function(engine, "static bool destroy_body(") and "world!=world_at_create_" in function(engine, "static bool push_body(")
assert "used_=true" in create, "single shot: the mod storage is never reused"
for name in ("static bool create_body(", "static bool destroy_body(", "static bool push_body("):
    body = function(engine, name)
    at = body.index("guarded_call(call)")
    check = body.rfind("if(busy_now()) return false;", 0, at)
    assert check != -1 and ";" not in body[check + len("if(busy_now()) return false;"):at], name + ": 0x8a7820 must be re-read right before the engine call"
assert "own_state(world,handle)" in function(engine, "static bool destroy_body("), "destroy only a slot that still holds our body"
entry = function(engine, "static void dispatch_entry(")
assert "if(!outside || !frame_thread || id!=frame_thread) return;" in entry, "acts only at the G1 point"

# 5. every hooked or called RVA with a doc pin matches the doc's bytes
doc_pins = {int(m.group(1), 16): m.group(2).split() for m in re.finditer(r"^bytes: 0x([0-9a-f]+) ((?:[0-9a-f]{2} ){15}[0-9a-f]{2})", doc, re.M)}
checked = 0
for m in re.finditer(r"\{0x([0-9a-f]+),\{((?:0x[0-9a-f]{2},){15}0x[0-9a-f]{2})\}\}", engine):
    rva = int(m.group(1), 16)
    mine = [b[2:] for b in m.group(2).split(",")]
    if rva in doc_pins:
        assert mine == doc_pins[rva], hex(rva)
        checked += 1
assert checked >= 8, checked

# 6. the pure header stays pure (the check compiles it without windows.h or MinHook)
assert "windows.h" not in spike and "MinHook" not in spike
print("hand_body_spike_routing_check ok")

# 7. the two sim scenarios compile and fit a 90 s session with a 60 s lead
import sys
sys.path.insert(0, str(root / "linux"))
try:
    from d2vr_linux import sim
except Exception:   # the Linux rig is not importable everywhere (Windows cl runs): the wiring above is the gate
    sim = None
if sim is not None:
    for name in ("hand_body_spike", "hand_body_spike_reload"):
        sim.compile_script(sim.scenario_segments(name, 60000), 90)
    print("hand_body_spike scenarios compile")
