"""Plan B task B13 wiring: the production hand body (src/hand_body.hpp, src/hand_body_runtime.hpp over the shared src/hand_body_native.hpp).

* Knob off is no behaviour change: build/hand-body (and no build/no-hand-body) is read in the knob block; without it nothing is installed
  (no hook, no thread, no engine call), the render-side release gate and the hands publication are not reached, the input path is
  untouched, and the 0x89fb0 hook pays one relaxed load of a null pointer.
* The creation, destroy and velocity calls exist only in hand_body_native.hpp, each inside guarded_call with 0x8a7820 re-read right
  before it; createConstraint 0xc90de0 appears nowhere; production code never calls into the B11 spike.
* The bodies are destroyed before NativeGrab's hook goes in every shutdown path, the dispatcher never waits on a render-thread lock, the
  cast exclusion covers both hand handles, and the status has a "hand_body" object.
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = root / "src"
probe = (src / "dxgi_probe.cpp").read_text(encoding="utf-8")
grab = (src / "native_grab.hpp").read_text(encoding="utf-8")
body = (src / "hand_body.hpp").read_text(encoding="utf-8")
runtime = (src / "hand_body_runtime.hpp").read_text(encoding="utf-8")
native = (src / "hand_body_native.hpp").read_text(encoding="utf-8")
spike_engine = (src / "hand_body_spike_engine.hpp").read_text(encoding="utf-8")
trace = (src / "native_world_trace.hpp").read_text(encoding="utf-8")
build = (root / "tools/build-msvc.cmd").read_text(encoding="utf-8")


def function(source, signature):
    """The definition starting at `signature`; the body brace is the first one after the closing parenthesis of the parameter list
    (a default argument such as `={}` is not the body)."""
    start = source.index(signature)
    open_at = source.index("{", start)
    if "(" in signature:
        m = re.compile(r"\)\s*(?:const\s*)?(?:noexcept\s*)?\{").search(source, start)
        open_at = m.end() - 1
    depth = 0
    for i in range(open_at, len(source)):
        depth += source[i] == "{"
        depth -= source[i] == "}"
        if depth == 0:
            return source[start:i + 1]
    raise AssertionError(signature)


def code_of(text):
    """Comments and string literals removed."""
    return re.sub(r'"(?:[^"\\\n]|\\.)*"', '""', re.sub(r"//[^\n]*", "", text))


def enclosing_block(source, at):
    """The text of the innermost {...} block containing position `at`."""
    depth = 0
    for i in range(at, -1, -1):
        if source[i] == "}":
            depth += 1
        elif source[i] == "{":
            if depth == 0:
                start = i
                break
            depth -= 1
    depth = 0
    for j in range(start, len(source)):
        depth += source[j] == "{"
        depth -= source[j] == "}"
        if depth == 0:
            return source[start:j + 1], start
    raise AssertionError("unbalanced")


# 1. the knob block: build/hand-body and build/no-hand-body read together, once; the install only behind it
knob = 'hand_body_requested=d2vr::hand_body_knob_enabled(fs::exists(root/L"build"/L"hand-body"),fs::exists(root/L"build"/L"no-hand-body"));'
assert probe.count(knob) == 1, "build/hand-body and build/no-hand-body are read in the knob block"
assert probe.count('L"no-hand-body"') == 1
assert "if(hand_body_requested) start_hand_body(root,output);" in probe and probe.count("start_hand_body(root,output)") == 1
assert probe.index(knob) < probe.index("if(hand_body_requested) start_hand_body(root,output);")
start = function(probe, "static void start_hand_body(")
assert probe.count("HandBodyRuntime::install(") == 1 and "HandBodyRuntime::install(" in start
assert start.index('L"hand-body-spike"') < start.index("HandBodyRuntime::install("), "never with the spike (the shared hooks)"
assert start.index("native_grab.enabled()") < start.index("HandBodyRuntime::install("), "needs NativeGrab's 0x89fb0 hook"
assert "if(!ok) hand_body_requested=false;" in start, "a refused install leaves the input path untouched"
assert "hand-body.txt" in start
assert "static std::atomic<bool> hand_body_requested{false};" in probe, "default off"
assert "inline bool hand_body_knob_enabled(bool hand_body_file,bool no_hand_body_file) {return hand_body_file && !no_hand_body_file;}" in body

# 2. knob off: no hook, no dispatch entry, no thread, no engine call outside install(); the render-side calls are behind the knob
install = function(runtime, "static bool install(")
stop_fn = function(runtime, "static bool stop()")
assert runtime.count("hand_body_dispatch.store(&dispatch_entry") == 2 and "hand_body_dispatch.store(&dispatch_entry" in install
assert "hand_body_dispatch.store(&dispatch_entry" in stop_fn, "the only other store: stop() restoring the entry to destroy a late body"
assert stop_fn.index("if(!active_.load(std::memory_order_acquire)) {stop_writer();return true;}") < stop_fn.index("hand_body_dispatch.store(&dispatch_entry")
assert runtime.count("HandBodyNative::install_hooks(") == 1 and "HandBodyNative::install_hooks(" in install
assert install.index("HandBodyNative::verify(") < install.index("HandBodyNative::install_hooks("), "pins before hooks"
assert install.index("HandBodyNative::install_hooks(") < install.index("hand_body_dispatch.store(&dispatch_entry")
assert "start_writer();" in install and runtime.count("start_writer();") == 1, "the log thread only from install()"
assert "MH_CreateHook" not in runtime and "MH_EnableHook" not in runtime
assert "inline std::atomic<void (*)(void* manager)> hand_body_dispatch{nullptr};" in body
hook_line = "if(const auto hand_body=hand_body_dispatch.load(std::memory_order_relaxed)) {try {hand_body(manager);} catch(...) {}}"
assert grab.count(hook_line) == 1, "the 0x89fb0 hook's only addition: one relaxed load of a null pointer"
invoke = function(grab, "void invoke(void* manager,void* command)")
assert invoke.index("original_(manager,command);") < invoke.index("hand_body_dispatch.load"), "after the original dispatcher"
for call in ("HandBodyRuntime::release_gate(", "HandBodyRuntime::publish_hands("):
    assert probe.count(call) == 1, call
    block, _ = enclosing_block(probe, probe.index(call))
    head = probe[:probe.index(block)][-200:]
    assert head.rstrip().endswith("if(hand_body_requested.load(std::memory_order_relaxed))"), call + " must sit behind the knob"
gate_block, _ = enclosing_block(probe, probe.index("HandBodyRuntime::release_gate("))
assert "input.menu=gate.menu;input.pause_request=gate.pause_request;" in gate_block, "only the knob-on path rewrites the input"
assert probe.index("HandBodyRuntime::release_gate(") < probe.index("controller_bridge.publish(input,logic==2);"), "before the composer sees the input"
assert probe.count("input.menu=gate.menu") == 1

# 3. engine calls: only in hand_body_native.hpp, each inside guarded_call with 0x8a7820 re-read right before it; never 0xc90de0
for name, text in (("hand_body.hpp", body), ("hand_body_runtime.hpp", runtime), ("dxgi_probe.cpp", probe), ("native_grab.hpp", grab),
                   ("hand_body_spike_engine.hpp", spike_engine)):
    assert not re.search(r"reinterpret_cast<[\w:]+>\(base_\+(create|destroy|velocity|cinfo|userdata|sphere)_rva\)", text), name + " calls the engine directly"
    assert "0xc8e5b0" not in code_of(text) and "0xc8f6b0" not in code_of(text), name
for name in ("hand_body.hpp", "hand_body_runtime.hpp", "hand_body_native.hpp", "hand_body_spike.hpp", "hand_body_spike_engine.hpp",
             "hand_body_commands.hpp", "hand_body_lifecycle.hpp", "dxgi_probe.cpp", "native_grab.hpp"):
    assert "0xc90de0" not in code_of((src / name).read_text(encoding="utf-8")), name + ": createConstraint stays out"
calls = re.findall(r"reinterpret_cast<[\w:]+>\(base_\+(\w+_rva)\)", native)
assert set(calls) == {"userdata_rva", "sphere_rva", "cinfo_rva", "create_rva", "destroy_rva", "velocity_rva"}, calls
for engine_call in ("create_body(reinterpret_cast<void*>(world),&id,s.cinfo.data(),1,1)", "destroy_bodies(reinterpret_cast<void*>(world),ids,1,0)",
                    "set_velocity(reinterpret_cast<void*>(world),handle,linear.data(),angular.data(),0)"):
    assert native.count(engine_call) == 1
    lam = re.search(r"auto call=\[&\] \{[^}]*" + re.escape(engine_call), native)
    assert lam, engine_call + " must be a guarded lambda"
    rest = native[lam.end():]
    assert "guarded_call(call)" in rest[:rest.index("\n    }\n")], engine_call + " must run inside guarded_call"
for name in ("static bool create(R1BodyStorage& s,", "static DestroyOutcome destroy(", "static bool velocity("):
    fn = function(native, name)
    at = fn.index("guarded_call(call)")
    checks = list(re.finditer(r"if\(busy_now\(\)\) return [\w:]+;", fn[:at]))
    assert checks and ";" not in fn[checks[-1].end():at], name + ": 0x8a7820 re-read right before the call"
build_fn = function(native, "static bool create(R1BodyStorage& s,")
assert "if(!guarded_call(build) || !built)" in build_fn, "the ctors run under the guard too"
# the runtime reaches the engine only through HandBodyNative's guarded functions
for fn in ("HandBodyNative::create(", "HandBodyNative::destroy(", "HandBodyNative::velocity("):
    assert fn in runtime, fn

# 4. production never calls into the spike
for name, text in (("hand_body.hpp", body), ("hand_body_runtime.hpp", runtime), ("hand_body_native.hpp", native)):
    assert "hand_body_spike" not in code_of(text).replace('""', ""), name + " must not include or call the spike"
    assert "Spike" not in code_of(text), name + " must not use spike types"

# 5. the recipe and the order: create, then the one wake write; destroy before any world change (release), abandon only as the last resort
create = function(body, "void do_create(")
wake = create.index("engine_.velocity(side,handle,Vec3{0,0,-R1Recipe::wake_speed})")
assert create.index("count(side,&Counters::creates);") < wake, "the wake write comes right after the create"
assert re.findall(r"engine_\.(\w+)\(", create) == ["create", "velocity"], "a create and one wake write, nothing else"
service = function(body, "void service(")
ab = service.index("case BodyAction::abandon:")
ab_block = service[ab:service.index("break;", ab)]
assert "engine_." not in ab_block.replace("engine_.forget", ""), "abandon makes no engine call"
assert "count(side,&Counters::abandons);" in ab_block
assert "if(!busy && !latched_" in service[service.index("case BodyAction::create:"):], "never a create while busy"
destroy = function(body, "void do_destroy(")
assert destroy.index("if(busy)") < destroy.index("engine_.destroy"), "never a direct destroy while busy"
gate = function(body, "ReleaseOutput update(const ReleaseSignals& s,bool body_alive,std::uint64_t now_ms)")
for reason in ("s.logic!=2", "s.menu", "s.pause_request", "s.settings_open", "s.authored"):
    assert reason in gate[gate.index("const bool reason="):], reason + " releases the bodies"
tick = function(runtime, "static void tick()")
assert "const bool gameplay=gameplay_logic && fresh && !release && !last_hands_.authored;" in tick
assert "li.gameplay=gameplay;" in tick

# 6. concurrency: the dispatcher never waits on a lock the render thread holds; the render reads a copy
assert "std::unique_lock<std::mutex> hands_lock(hands_mutex_,std::try_to_lock);" in tick
assert "lock_guard" not in tick and "lock_guard" not in function(runtime, "static void dispatch_entry(")
note = function(runtime, "static void note_event(")
assert "std::try_to_lock" in note and "lock_guard" not in note
slot = function(body, "bool publish(const std::optional<WorldTrackedPose>& pose)")
assert "std::try_to_lock" in slot
# review P3-2: service() and write() share an atomic re-entry guard (never a std::mutex try_lock from the owning thread), and write() is
# refused outside the dispatcher's G1 tick
write_fn = function(body, "bool write(unsigned side,const hand_body::CommandRecord& record)")
assert "const TickGuard guard(ticking_);" in service and "const TickGuard guard(ticking_);" in write_fn
assert "tick_mutex_" not in body
assert write_fn.index("engine_.at_g1_point()") < write_fn.index("const TickGuard guard(ticking_);") < write_fn.index("engine_.apply(")
assert "e.at_g1_point=[] {return in_g1_tick_;};" in runtime
entry_fn = function(runtime, "static void dispatch_entry(")
assert entry_fn.index("in_g1_tick_=true;") < entry_fn.index("try {tick();}") < entry_fn.index("in_g1_tick_=false;")
assert "inline static thread_local bool in_g1_tick_{};" in runtime

# review P2-1 / round 2: the load generation is bumped on loading EXIT (after loading start's same-world destroy had its chance); ANY
# generation change with a body alive is an abandon: HandBody passes the generation to the B10 lifecycle untouched (no destroy path for it)
gate_fn = function(runtime, "static ReleaseOutput release_gate(")
assert "if(loading) loading_.store(true);" in gate_fn
assert "else if(loading_.exchange(false)) generation_.fetch_add(1,std::memory_order_acq_rel);" in gate_fn
assert "if(loading && !loading_.exchange(true)) generation_" not in runtime
loop = service[service.index("LifecycleInput li=per_side[side];"):service.index("const BodyAction action=life_.update(side,li);")]
assert "li.world_generation" not in loop.replace("last_gen_=li.world_generation;", ""), "the generation reaches the lifecycle untouched"
assert "gen_hold" not in body

# review P3-1: the create runs in the tick's world snapshot (no re-read in the runtime's create)
create_lambda = runtime[runtime.index("e.create=[](unsigned side,std::uintptr_t world,Vec3 at,std::uint32_t& handle)"):runtime.index("e.velocity=[]")]
assert "world_now()" not in create_lambda
assert "engine_.create(side,world,at,handle)" in function(body, "void do_create(")

# review P3-3 / P3-4: the G1 violation reason and the install failure reason reach the status
assert '",\\"g1_violation\\":"' in runtime and 'note_event(0,"g1_violated",0);' in runtime
assert '",\\"install_why\\":"' in runtime
assert start.count("HandBodyRuntime::note_install_failure(") == 2

# 7. the bodies are destroyed before NativeGrab's hook goes, in every shutdown path
for disable in ("const bool grab_disabled=native_grab.disable();", "const bool grab_disabled=interaction_disabled && native_grab.disable();",
                "if(native_interaction_aim.disable())native_grab.disable();"):
    at = probe.index(disable)
    before = probe.rfind("HandBodyRuntime::stop();", 0, at)
    assert before != -1 and at - before < 6000, "hand body stop must precede " + disable
assert probe.count("d2vr::HandBodySpikeEngine::stop();d2vr::HandBodyRuntime::stop();") == probe.count("d2vr::HandBodySpikeEngine::stop();")
stop = stop_fn
assert stop.index("body->shutdown()") < stop.index("HandBodyNative::disable_hooks()"), "the destroy request comes before the unhook"
# DeepSeek P1: the in-flight tick is drained before the final check, so a body created by a tick that began before the stop is seen
assert stop.index("hand_body_dispatch.store(nullptr") < stop.index("while(dispatch_inflight_.load()") < stop.index("alive=body_alive() || dispatch_inflight_.load();")
assert "const bool keep=alive || stranded_[side].load();" in stop, "an exclusion stays while its body may exist"
assert "if(shutdown_.load(std::memory_order_acquire)) return;" in function(body, "void do_create("), "no create once the stop began"
# DeepSeek P1: gone only when the slot is positively not ours; unreadable after the call is `unknown` (storage retired)
assert "return after==Own::absent?DestroyOutcome::gone:after==Own::present?DestroyOutcome::still_present:DestroyOutcome::unknown;" in native
assert "else if(r==DestroyOutcome::unknown) release_slot(side,true,true);" in runtime
# DeepSeek P3: the dispatcher's fault path never waits on the status reader
assert "std::unique_lock<std::mutex> lock(why_mutex_,std::try_to_lock);" in function(body, "void latch(")
assert "g1_mutex_" not in runtime

# 8. cast exclusion: both production hand handles
assert "cast_excluded_bodies[side].store(handle,std::memory_order_relaxed);" in function(runtime, "static void exclude(")
convert = function(trace, "inline std::optional<WorldTraceHit> convert_world_hit(")
assert "if(cast_excluded(c.body))" in convert
assert "for(const auto& slot:cast_excluded_bodies)" in function(trace, "inline bool cast_excluded(")

# 9. status key and registration
assert ',\\"hand_body\\":" << d2vr::HandBodyRuntime::status_json(hand_body_requested.load())' in probe
assert "python tests\\hand_body_routing_check.py" in build
print("hand_body_routing_check ok")

# 11. B14: the drive. Knob off stays no behaviour change: the render side only computes two local bit sets inside the clamp loop (written,
# never read outside the knob-gated publication) and the drive runs only from the runtime's G1 tick.
free_uses = [m.start() for m in re.finditer(r"hand_body_free_bits|hand_body_yield_bits", probe)]
assert len(free_uses) == 6, len(free_uses)   # the declaration, one |= each in the clamp loop, the two reads in the publication
publish_at = probe.index("d2vr::HandBodyRuntime::publish_hands(")
publish_block, publish_start = enclosing_block(probe, publish_at)
reads = [u for u in free_uses if publish_start <= u < publish_start + len(publish_block)]
assert len(reads) == 2, "the bits are read only inside the knob-gated publication"
assert "unsigned hand_body_free_bits{},hand_body_yield_bits{};" in probe
free_gate = probe[probe.index("const bool answered_free="):probe.index("if(hand_body_free_streak[side]>=3) hand_body_free_bits|=1u<<side;")]
for need in ("clamp_updated && clamped.valid && !clamp_yield", "clamped.state==d2vr::ClampState::free", "!clamped.reacquired", "!clamped.held", "!clamped.contact", "!reach_clamped",
             "!palm_unanswered", "clamped.drawn.position-palm.position"):
    assert need in free_gate, need + ": the free bit needs an answered free palm cast to the palm itself (DeepSeek P1/P2)"
# DeepSeek P1: the tracked-arm frame's source tick and epoch stay real code (an edit once swallowed them into a comment)
assert "hands.tick=GetTickCount64();hands.source_tick=game_xr->grip_pose_tick();hands.epoch=game_xr->pose_epoch();" in code_of(probe)
assert "if(clamp_yield) hand_body_yield_bits|=1u<<side;" in probe
for line in ("hand_body_in.target=palm_grips;", "hand_body_in.free=hand_body_free_bits&hands.hands;", "hand_body_in.locomotion=",
             "hand_body_in.player_root=r;"):
    assert line in publish_block, line + " belongs to the knob-gated publication"
assert "hand_body_in" not in probe[:publish_start] and "hand_body_in" not in probe[publish_start + len(publish_block):]
# the drive: called only from the runtime's tick, after service(), once per side; spawn (create and snap) only where the clamp found it free
assert runtime.count("body->drive(") == 1 and "body->drive(" in tick and tick.index("body->service(in,spawn,gate);") < tick.index("body->drive(")
assert "->drive(" not in code_of(probe) and ".drive(" not in code_of(grab), "the drive runs only from the runtime tick"
assert "HandBodyRuntime::drive" not in probe
assert "const bool free=fresh && now-last_hands_tick_<=free_fresh_ms && ((last_hands_.free>>side)&1u)!=0;" in tick
assert "if(li.tracked && free && finite(last_hands_.palm[side])) spawn[side]=last_hands_.palm[side];" in tick
assert "d.target_free=fresh && now-last_hands_tick_<=free_fresh_ms && ((last_hands_.free>>side)&1u)!=0;" in tick
assert "d.yield=!fresh || !tracked || ((last_hands_.yield>>side)&1u)!=0;" in tick
drive_fn = function(body, "void drive(const DriveInput& in)")
assert drive_fn.index("engine_.at_g1_point()") < drive_fn.index("const TickGuard guard(ticking_);") < drive_fn.index("engine_.motion(") < drive_fn.index("engine_.apply(")
assert re.findall(r"engine_\.(\w+)\(", drive_fn) == ["at_g1_point", "world", "busy", "motion", "apply"], re.findall(r"engine_\.(\w+)\(", drive_fn)
assert "hand_body::encode_set_velocity(s.handle,written,angular)" in drive_fn and "encode_set_transform" not in code_of(body + runtime + native)
assert drive_fn.index("if(s.written_tick==tick_index_)") < drive_fn.index("engine_.apply("), "one velocity write per side per tick"
assert drive_fn.index("s.written_tick=tick_index_;") < drive_fn.index("engine_.apply("), "the write is marked before it is attempted"
assert drive_fn.index('latch(side,"read-back garbage') < drive_fn.index("servo_step("), "garbage never reaches the servo"
assert "lock_guard" not in drive_fn and "unique_lock" not in drive_fn, "the drive never takes a lock"
# the snap: destroy + recreate (no setBodyTransform 0xc85860 anywhere in production), only with a free spawn and never while busy
snap = function(body, "void do_snap(")
assert snap.index("if(busy)") < snap.index("do_destroy(") < snap.index("do_create("), snap
assert snap.index("if(!spawn") < snap.index("do_destroy("), "only when the clamp shows the target free"
for name, text in (("hand_body.hpp", body), ("hand_body_runtime.hpp", runtime), ("hand_body_native.hpp", native)):
    assert "0xc85860" not in code_of(text), name + ": no setBodyTransform"
# the read-back goes through the shared native_motion layout, no engine call; the velocity guard is the drive's own cap
assert "native_motion_raw(read_,world,record)" in function(native, "static std::optional<BodyMotion> motion(")
assert "length(v)>kHandWriteMaxSpeed || length(w)>kHandWriteMaxSpin" in function(native, "static bool velocity(")
assert "e.motion=[](unsigned side,std::uint32_t handle)" in runtime
# proof (d): the cost of every tick is measured where it lands (the dispatcher runs on the render thread)
assert entry_fn.index("const auto t0=qpc_us();") < entry_fn.index("try {tick();}") < entry_fn.index("const auto t1=qpc_us();")
for key in ('\\"cost_max_us\\":', '\\"player_drift_um\\":', '\\"contact_frames\\":', '\\"snaps\\":', '\\"drive\\":{',
            '\\"tick_cost_max_us\\":', '\\"steady_tick_cost_max_us\\":', '\\"faults\\":', '\\"time_factor_verified\\":false'):
    assert key in runtime, key
print("hand_body_routing_check B14 ok")

# 10. the B13 sim scenario compiles and fits a 120 s session with a 60 s lead
import sys
sys.path.insert(0, str(root / "linux"))
try:
    from d2vr_linux import sim
except Exception:   # the Linux rig is not importable everywhere (Windows cl runs): the wiring above is the gate
    sim = None
if sim is not None:
    segments = sim.scenario_segments("hand_body_pause", 60000)
    sim.compile_script(segments, 120)
    assert max(s["end_ms"] for s in segments) <= 120000
    print("hand_body_pause scenario compiles")
    soak = sim.scenario_segments("hand_body_soak", 45000)
    sim.compile_script(soak, 120)
    assert max(s["end_ms"] for s in soak) <= 120000 and len(soak) <= 128, (max(s["end_ms"] for s in soak), len(soak))
    print("hand_body_soak scenario compiles")
