"""Plan B task B11 wiring: the hand-body spike exists only behind build/hand-body-spike (absent knob: no hook, no counter, no thread, no
engine call), its tick runs from the existing 0x89fb0 hook, it stops (destroying its body on the game thread) before any shutdown path
disables NativeGrab, it calls only the engine functions the B11 list allows (each pinned with the RE doc's bytes), and the mod's own casts
discard a hit on the spike body. B13: the pins, hooks and R1 engine calls moved to src/hand_body_native.hpp (shared with the production
hand body); the spike calls them, and the checks below follow them there."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
probe = (root / "src/dxgi_probe.cpp").read_text(encoding="utf-8")
grab = (root / "src/native_grab.hpp").read_text(encoding="utf-8")
spike = (root / "src/hand_body_spike.hpp").read_text(encoding="utf-8")
engine = (root / "src/hand_body_spike_engine.hpp").read_text(encoding="utf-8")
native = (root / "src/hand_body_native.hpp").read_text(encoding="utf-8")   # B13: the shared R1 recipe and count-only hooks
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
hooks = function(native, "static bool install_hooks(")
assert "MH_CreateHook(" not in engine and "MH_EnableHook(" not in engine, "the spike installs hooks only through the shared native"
assert native.count("MH_CreateHook(") == 1 and "MH_CreateHook(" in hooks
assert native.count("MH_EnableHook(") == 1 and "MH_EnableHook(" in hooks
assert engine.count("HandBodyNative::install_hooks(") == 1 and "HandBodyNative::install_hooks(" in install
assert engine.count("hand_body_spike_dispatch.store(&dispatch_entry") == 1 and "hand_body_spike_dispatch.store(&dispatch_entry" in install
assert install.index("verify(base,read,why)") < install.index("HandBodyNative::install_hooks("), "pins are checked before any hook"
assert install.index("spike_route_refusal(route)") < install.index("HandBodyNative::install_hooks("), "R2/R3 install nothing"
assert "inline std::atomic<void (*)(void* manager)> hand_body_spike_dispatch{nullptr};" in spike
assert "if(const auto spike=hand_body_spike_dispatch.load(std::memory_order_relaxed)) {try {spike(manager);} catch(...) {}}" in grab
invoke = function(grab, "void invoke(void* manager,void* command)")
assert invoke.index("original_(manager,command);") < invoke.index("hand_body_spike_dispatch.load"), "the spike ticks after the original dispatcher"
assert "inline std::atomic<std::uint32_t> cast_excluded_bodies[2]{0xffffffffu,0xffffffffu};" in trace
for name, text in (("dxgi_probe.cpp", probe), ("native_grab.hpp", grab), ("hand_body_spike.hpp", spike), ("hand_body_native.hpp", native)):
    assert "cast_excluded_bodies[" not in text.replace("cast_excluded_bodies[2]{", ""), name + " must not arm the cast exclusion"
assert "cast_excluded_bodies[0].store(handle" in engine, "the spike excludes its body (slot 0)"
convert = function(trace, "inline std::optional<WorldTraceHit> convert_world_hit(")
excluded = function(trace, "inline bool cast_excluded(")
assert "excluded!=0xffffffffu && body==excluded" in excluded
# final review P1-1: the conversion never discards; the closest path counts a hand-body hit and continues past it (never "no hit")
assert "cast_excluded" not in convert
assert "if(cast_excluded(collector.body)) {   // P1-1" in trace and "return continue_past_hand_body(" in trace

# 3. the spike stops (destroying on the game thread) before NativeGrab is disabled, in every shutdown path
for disable in ("const bool grab_disabled=native_grab.disable();", "const bool grab_disabled=interaction_disabled && native_grab.disable();",
                "if(native_interaction_aim.disable())native_grab.disable();"):
    at = probe.index(disable)
    before = probe.rfind("HandBodySpikeEngine::stop();", 0, at)
    assert before != -1 and at - before < 6000, "spike stop must precede " + disable
stop = function(engine, "static bool stop()")
assert stop.index("stop_requested_.store(true") < stop.index("HandBodyNative::disable_hooks()"), "the destroy request comes before the unhook"

# 4. engine calls: only the B11 list (plus the doc's pure-store ctors), each through guarded_call, never the forbidden ones. Since B13 they
#    live in the shared native header; the spike makes none of its own.
assert not re.findall(r"reinterpret_cast<[\w:]+>\(base_\+(\w+_rva)\)", engine), "the spike calls the engine only through HandBodyNative"
called = set(re.findall(r"reinterpret_cast<[\w:]+>\(base_\+(\w+_rva)\)", native))
# B16 adds 0x97600 setBodyCollisionFilterInfo (filter_rva) to the shared header for the production hand body only (the global call list); the
# spike never reaches it
assert called == {"userdata_rva", "sphere_rva", "cinfo_rva", "create_rva", "destroy_rva", "velocity_rva", "filter_rva"}, called
assert "set_filter" not in engine, "the spike never writes a filter word"
for text in (engine, native):
    code = re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', re.sub(r"//[^\n]*", "", text))   # comments and string literals removed
    for rva in ("0x91b50", "0x91e20", "0xc85860", "0xc90de0", "0x8ae9b0", "0x883f60", "0x97600", "0xc8e7c0", "0x808ea0", "0x809d80"):
        if rva == "0x97600" and text is native:
            continue
        assert rva not in code, rva
for call in ("create_body(reinterpret_cast<void*>(world),&id,s.cinfo.data(),1,1)", "destroy_bodies(reinterpret_cast<void*>(world),ids,1,0)",
             "set_velocity(reinterpret_cast<void*>(world),handle,linear.data(),angular.data(),0)"):
    assert re.search(r"auto call=\[&\] \{[^}]*" + re.escape(call), native), call
    assert native.count(call) == 1
assert native.count("guarded_call(") >= 4
create = function(native, "static bool create(R1BodyStorage& s,")
assert "R1Recipe::filter" in create and "R1Recipe::flags" in create and "R1Recipe::motion_type" in create and "R1Recipe::mass_kg" in create
recipe = function(spike_base := (root / "src/hand_body.hpp").read_text(encoding="utf-8"), "struct R1Recipe {")
assert "filter=hand_body::hand_filter_word(false)" in recipe and "flags=0" in recipe and "motion_type=2" in recipe and "mass_kg=2.f" in recipe
destroy = function(native, "static DestroyOutcome destroy(")
velocity = function(native, "static bool velocity(")
assert "world!=world_at_create" in destroy and "world!=world_at_create" in velocity
assert "used_=true" in function(engine, "static bool create_body("), "single shot: the spike's storage is never reused"
for name, body in (("create", create), ("destroy", destroy), ("velocity", velocity)):
    at = body.index("guarded_call(call)")
    check = list(re.finditer(r"if\(busy_now\(\)\) return [\w:]+;", body[:at]))
    assert check, name + ": 0x8a7820 must be re-read right before the engine call"
    assert ";" not in body[check[-1].end():at], name + ": nothing between the 0x8a7820 re-read and the call"
assert "own_state(world,handle,s)" in destroy, "destroy only a slot that still holds our body"
entry = function(engine, "static void dispatch_entry(")
assert "if(!point.g1_point) return;" in entry, "acts only at the G1 point"
note = function(native, "static Dispatch note_dispatch()")
assert "return {outside && frame_thread && id==frame_thread," in note

# 5. every hooked or called RVA with a doc pin matches the doc's bytes
doc_pins = {int(m.group(1), 16): m.group(2).split() for m in re.finditer(r"^bytes: 0x([0-9a-f]+) ((?:[0-9a-f]{2} ){15}[0-9a-f]{2})", doc, re.M)}
checked = 0
for m in re.finditer(r"\{0x([0-9a-f]+),\{((?:0x[0-9a-f]{2},){15}0x[0-9a-f]{2})\}\}", native):
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
