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
assert set(calls) == {"userdata_rva", "sphere_rva", "cinfo_rva", "create_rva", "destroy_rva", "velocity_rva", "filter_rva"}, calls   # B16: 0x97600
for engine_call in ("create_body(reinterpret_cast<void*>(world),&id,s.cinfo.data(),1,1)", "destroy_bodies(reinterpret_cast<void*>(world),ids,1,0)",
                    "set_velocity(reinterpret_cast<void*>(world),handle,linear.data(),angular.data(),0)", "set_filter_info(system,handle,filter,1,0)"):
    assert native.count(engine_call) == 1
    lam = re.search(r"auto call=\[&\] \{[^}]*" + re.escape(engine_call), native)
    assert lam, engine_call + " must be a guarded lambda"
    rest = native[lam.end():]
    assert "guarded_call(call)" in rest[:rest.index("\n    }\n")], engine_call + " must run inside guarded_call"
for name in ("static bool create(R1BodyStorage& s,", "static DestroyOutcome destroy(", "static bool velocity(", "static bool set_filter("):
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
# final review P3-2: the dispatcher never calls game_logic_mask() (it locks); it reads the logic the render thread published with the release gate
assert "logic_(" not in code_of(tick) and "logic_published_.load(std::memory_order_relaxed)" in tick
assert "now-logic_tick<=fresh_ms" in tick, "a stale publication reads as not gameplay"
gate_src = function(runtime, "static ReleaseOutput release_gate(")
assert gate_src.index("logic_published_.store(signals.logic,std::memory_order_relaxed);") < gate_src.index("release_tick_.store(now_ms,std::memory_order_release);")
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
assert "for(const auto& slot:cast_excluded_bodies)" in function(trace, "inline bool cast_excluded(")
# final review P1-1: a hit on a hand body is never "no hit": the cast continues past it (at most kMaxCastContinuations casts, counted), and
# with no hand body alive the path is the old one exactly: the continuation is reached only behind the hand-body test, which reads the two
# slots (0xffffffff by default never matches), so no extra cast, read or counter runs
assert "cast_excluded" not in function(trace, "inline std::optional<WorldTraceHit> convert_world_hit("), "the conversion never discards a hit"
query = function(trace, "static WorldTraceQueryResult sphere_query_checked(")
cont = function(trace, "static WorldTraceQueryResult continue_past_hand_body(")
assert code_of(trace).count("continue_past_hand_body(") == 2, "its definition and one call"
call_at = query.index("return continue_past_hand_body(")
guard = query[query.rindex("if(cast_excluded(collector.body)) {", 0, call_at):call_at]
assert guard.startswith("if(cast_excluded(collector.body)) {") and guard.count(";") == 1 and "cast_excluded_hits.fetch_add(1,std::memory_order_relaxed);" in guard, guard
assert len(re.findall(r"(?<![\w])cast\(", code_of(query))) == 1, "sphere_query_checked itself casts once"
assert query.index("if(collector.hit_count!=1)return {};") < query.index("if(cast_excluded(collector.body)) {") < query.index("std::uintptr_t entity{};"), \
    "the hand-body test sits where the old discard was; the rest of the path is unchanged"
assert query.count("if(cast_excluded(contact->body)) {cast_excluded_hits.fetch_add(1,std::memory_order_relaxed);continue;}") == 2, "the all-contact collectors skip hand contacts"
cont_code = code_of(cont)
assert len(re.findall(r"(?<![\w])cast\(", cont_code)) == 2 and cont_code.count("cast_continuations.fetch_add(1,std::memory_order_relaxed);") == 2, "each continuation cast counted"
assert "inline constexpr unsigned kMaxCastContinuations=2;" in trace and "if(used>=kMaxCastContinuations) return blocked(last);" in cont
assert cont.index("if(!source && !exclude) if(const auto hand=body_exclusion(read,world,first.body)) {") < cont.index("NativeRegularContactCollector all{};")
assert "hit->entity=0;hit->hand_body=true;hit->body_class=BodyClass::dynamic;" in cont and "cast_continuation_exhausted.fetch_add(1" in cont, "blocked, never no hit"
assert "if(cast_excluded(contact->body)) continue;" in cont
for key in ("cast_continuations", "cast_continuation_exhausted"):
    assert '\\"%s\\":' % key in runtime, key
# P3-1: the hand body's free probe sees WORLD_LOW 0; only its own slot uses that mask
aim_src = (src / "native_interaction_aim.hpp").read_text(encoding="utf-8")
assert "inline constexpr std::uint32_t kHandBodyFreeLayers=kWorldOccluderLayers|1u;" in trace
assert code_of(aim_src).count("kHandBodyFreeLayers") == 1 and "slot==hand_body_free_slot?kHandBodyFreeLayers:kWorldOccluderLayers," in aim_src

# 9. status key and registration
assert ',\\"hand_body\\":" << d2vr::HandBodyRuntime::status_json(hand_body_requested.load())' in probe
assert "python tests\\hand_body_routing_check.py" in build
print("hand_body_routing_check ok")

# 11. B14: the drive. Knob off stays no behaviour change: the render side only computes two local bit sets inside the clamp loop (written,
# never read outside the knob-gated publication) and the drive runs only from the runtime's G1 tick.
free_uses = [m.start() for m in re.finditer(r"hand_body_free_bits|hand_body_yield_bits", probe)]
assert len(free_uses) == 7, len(free_uses)   # the declaration, one |= each in the clamp loop, the two reads in the publication, B15's yield read
publish_at = probe.index("d2vr::HandBodyRuntime::publish_hands(")
publish_block, publish_start = enclosing_block(probe, publish_at)
reads = [u for u in free_uses if publish_start <= u < publish_start + len(publish_block)]
assert len(reads) == 1, "the yield bits are read in the knob-gated publication (P3-1: the free bits through the hand body's probe)"
# final review P3-1: the free bits reach the hand body only through its own probe block, knob-gated; the probe is posted only there
probe_at = probe.index("constexpr unsigned probe_slot=d2vr::NativeInteractionAim::hand_body_free_slot;")
probe_block, probe_start = enclosing_block(probe, probe_at)
assert probe[probe_start - 120:probe_start].rstrip().endswith("if(hand_body_requested.load(std::memory_order_relaxed) && d2vr::HandBodyRuntime::active())")
assert [u for u in free_uses if probe[u:u + 19] == "hand_body_free_bits" and probe_start <= u < probe_start + len(probe_block)], "the clamp's free bit feeds the probe gate"
for m in re.finditer(r"hand_body_probe_free_bits", probe):
    assert (probe_start <= m.start() < probe_start + len(probe_block)) or (publish_start <= m.start() < publish_start + len(publish_block)) or \
        probe[m.start():m.start() + 30].startswith("hand_body_probe_free_bits{}"), "set only in the probe block, read only in the publication"
assert code_of(probe).count("hand_body_free_slot") == 1 and probe.count("post_weapon_segment(side,probe_slot,") == 1
for need in ("probe_streak[side]>=3 && (((hand_body_free_bits>>side)&1u) || native_hold_here)", "if(clamp_updated && wanted && !motor_hold_here && d2vr::finite(palm.position)) {",
             "native_hold_here?held_clamp_live.entity:0", "else probe_streak[side]=0;   // unanswered: never free on an old answer", "d2vr::R1Recipe::radius"):
    assert need in probe_block, need
assert "unsigned hand_body_free_bits{},hand_body_yield_bits{},hand_visual_contact_bits{};" in probe
free_gate = probe[probe.index("const bool answered_free="):probe.index("if(hand_body_free_streak[side]>=3) hand_body_free_bits|=1u<<side;")]
for need in ("clamp_updated && clamped.valid && !clamp_yield", "clamped.state==d2vr::ClampState::free", "!clamped.reacquired", "!clamped.held", "!clamped.contact", "!reach_clamped",
             "!palm_unanswered", "clamped.drawn.position-palm.position"):
    assert need in free_gate, need + ": the free bit needs an answered free palm cast to the palm itself (DeepSeek P1/P2)"
# DeepSeek P1: the tracked-arm frame's source tick and epoch stay real code (an edit once swallowed them into a comment)
assert "hands.tick=GetTickCount64();hands.source_tick=game_xr->grip_pose_tick();hands.epoch=game_xr->pose_epoch();" in code_of(probe)
assert "if(clamp_yield) hand_body_yield_bits|=1u<<side;" in probe
for line in ("hand_body_in.target=palm_grips;", "hand_body_in.free=hand_body_probe_free_bits&hands.hands;", "hand_body_in.locomotion=",
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
# final review P3-4: the snap destroys on this tick; the recreate is a later tick's ordinary create (counted as the snap); storage is round-robin
assert snap.index("if(busy)") < snap.index("do_destroy(") < snap.index("s.snap_recreate=true;") and "do_create(" not in snap, snap
assert "if(s.snap_recreate && counters_[side].creates.load(std::memory_order_relaxed)>before) {s.snap_recreate=false;count(side,&Counters::snaps);}" in service
assert "if(!li.gameplay || !li.tracked || (action!=BodyAction::create && action!=BodyAction::none)) s.snap_recreate=false;" in service
assert "const unsigned i=(last_pick_[side]+k)%storage_per_side;" in create_lambda and "current_[side]=pick;last_pick_[side]=pick;" in create_lambda
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
            '\\"tick_cost_max_us\\":', '\\"pressed_ticks\\":', '\\"steady_tick_cost_max_us\\":', '\\"faults\\":', '\\"time_factor_verified\\":false'):
    assert key in runtime, key
# fix round 1: no assertion or statement may hide in a // comment (two line joins swallowed CHECKs once: review P2-3)
checks_src = (root / "tests/hand_body_check.cpp").read_text(encoding="utf-8")
assert not re.search(r"//[^\n]*\bCHECK\(", checks_src), "a CHECK( inside a // comment in tests/hand_body_check.cpp"
for name, text in (("hand_body.hpp", body), ("hand_body_runtime.hpp", runtime), ("hand_body_native.hpp", native)):
    assert not re.search(r"//[^\n]*[a-z)\]][a-z_]+(\.|->)[a-z_]+(=|\()[^\n]*;", text), name + ": code swallowed into a comment?"
assert not re.search(r"//[^\n]*[a-z]hands\.(source_tick|epoch)=", probe)
# fix round 1: proof (d) instrumentation: CPU time from QueryThreadCycleTime (calibrated at install, availability reported), wall histogram,
# over-budget counts and the slowest tick's phases in the status
assert code_of(runtime).count("QueryThreadCycleTime(") == 3 and "QueryThreadCycleTime" not in code_of(body)
assert install.index("cycles_per_us_=calibrate_cycles_per_us();") < install.index("new HandBody(engine(),true)")
for key in ('\\"timing\\":', '\\"cpu_available\\":', '\\"over_wall\\":', '\\"over_cpu\\":', '\\"hist\\":[', '\\"slow\\":{', '\\"root_moved_ticks\\":'):
    assert key in runtime, key
assert drive_fn.index("const auto c0=cpu_now();") < drive_fn.index("engine_.motion(") and "cost.read=clock_us();" in drive_fn and "cost.write=clock_us();" in drive_fn
print("hand_body_routing_check B14 ok")

# 12. B15: which pose the visible hand is drawn at. Knob off (no build/hand-body, or no body) leaves hands.grips exactly as the clamp drew it;
# the raw controller poses are not read on this path (they keep driving intent), and the intent paths do not read the drawn pose.
visual_hdr = (src / "hand_visual_source.hpp").read_text(encoding="utf-8")
assert "raw_grips" not in visual_hdr and "hand_body_native" not in visual_hdr and "HandBodyRuntime" not in visual_hdr, "the source selector is pure"
assert "hand_visual_source_check.cpp" in build and build.index("hand_body_lifecycle_check.exe") < build.index("hand_visual_source_check.cpp")
anchor_at = probe.index("anchors[side]=drawn;")                       # the Task 12 block computes the anchor from the clamp pose first
vstart = probe.index("// B15: which pose the visible hand is drawn at")
final_stmt = "for(unsigned side=0;side<2;++side) if(hands.hands&(1u<<side)) hands.grips[side]=d2vr::resolve_drawn_grip(visual[side],anchors[side]).pose;"
final_at = probe.index(final_stmt)
vblock = probe[vstart:final_at + len(final_stmt)]
assert probe.index("hand_clamp.note_drawn(") < anchor_at < vstart < final_at < probe.index("held_clamp_live.applied[side]={}") < probe.index("publish_hand(side,hands.grips[side],raw_grips[side])")
assert "raw_grips" not in code_of(vblock) and "raw_grips" not in code_of(probe[anchor_at - 900:anchor_at + 700]), "B15 and the anchor read the palm pose, never the raw controller"
assert "palm_grips[side]" in vblock and "in.clamp=hands.grips[side];" in vblock and "in.anchor=anchors[side].position;" in vblock
assert "if(hand_body_requested.load(std::memory_order_relaxed) && d2vr::HandBodyRuntime::active()) {" in vblock, "knob off: nothing below runs"
assert vblock.index("visual[side].drawn=hands.grips[side];") < vblock.index("if(hand_body_requested"), "knob off: the passthrough is the clamp's pose"
assert "hands.grips[side]=visual[side]" not in code_of(vblock), "the gated block never writes the drawn pose itself: the one function below does"
assert vblock.index("hand_visual_source.update(") < vblock.index(final_stmt)
for need in ("d2vr::HandBodyRuntime::contact_now(side)", "d2vr::HandBodyRuntime::pose_stamped(side)", "held_planes(side,", "world_hit_near_body(", "behind_held_planes("):
    assert need in vblock, need
assert code_of(probe).count("resolve_drawn_grip(") == 1, "X-4: one function decides"
# the drawn pose and the visual source are read only inside the block (intent code earlier in the frame never sees them); every use is listed
assert [m.group(0) for m in re.finditer(r"hand_visual_source\.\w+|hand_visual_stats\.\w+", probe)] == ["hand_visual_source.hpp", "hand_visual_source.reset", "hand_visual_source.update", "hand_visual_stats.note", "hand_visual_stats.json"]
assert all(vstart <= m.start() <= final_at + len(final_stmt) for m in re.finditer(r"visual\[side\]", probe)), "visual[] lives only in the B15 block"
res = function(visual_hdr, "inline DrawnGrip resolve_drawn_grip(")
assert "VisualSource::clamp" in res and "anchor.weight>0" in res
pick = function(visual_hdr, "static VisualSource pick(")
assert pick.index("yield") < pick.index("body_elected") < pick.index("anchor_active"), "yield, then body, then anchor, then clamp"
upd = function(visual_hdr, "VisualOutput update(unsigned side")
assert "VisualSource::anchor" in upd and "contact_gap" in upd and "world_hit_near" in upd and "stale_floor_s" in upd
for intent in ("intent.grips=raw_grips;", "note_grip(side,raw_grips[side],hands.curls[side]);", "const auto& p=raw_grips[side].position;reach_hands[side]="):
    assert intent in probe, intent
assert ',\\"hand_visual\\":" << hand_visual_stats.json()' in probe
for key in ("body_frames", "clamp_frames", "controller_frames", "anchor_frames", "blends", "snaps"):
    assert f'\\"{key}\\":' in visual_hdr, key
# the contact signal: shortfall on an uncapped command, fresh within 50 ms; pose serial; no statement may hide in a comment on those lines
assert 'contact_now' in body and 'short_streak>=dc.contact_ticks' in body and "pose_stamped" in runtime and "!c.capped.load(" in body and "contact_fresh_ms" in body
for line in body.splitlines():
    if "contact_now" in line or "contact_frames" in line:
        assert not re.search(r"//.*\b(d|c)\.\w+=", line), "a statement swallowed by a comment: " + line.strip()[:80]
print("hand_body_routing_check B15 ok")

# 13. B16: held things follow the hand body (src/hold_coupling.hpp). Knob off (or no ready body) is today's behaviour by construction; the
# hand's filter word is written only by the hand body (allow-listed in write(), 0x97600 only in hand_body_native.hpp) and restored only through
# HandFilterKeeper's SafeRelease or a new body; no pair API; nothing swallowed into a comment.
coupling = (src / "hold_coupling.hpp").read_text(encoding="utf-8")
held_obj = (src / "native_held_object.hpp").read_text(encoding="utf-8")
assert "hold_coupling_check.cpp" in build and build.index("hand_visual_source_check.exe") < build.index("hold_coupling_check.cpp")
# layer_restore_only_via_safe_release: no filter writer outside the held-object swap (native_held_object.hpp) and the hand body's own files
writers = re.compile(r"\bset_filter\(|encode_set_filter\(|0x97600|filter_rva|setBodyCollisionFilterInfo")
allowed = {"native_held_object.hpp", "hand_body.hpp", "hand_body_native.hpp", "hand_body_runtime.hpp", "hand_body_commands.hpp"}
for f in sorted(src.iterdir()):
    if f.suffix not in (".hpp", ".cpp", ".h"):
        continue
    if writers.search(code_of(f.read_text(encoding="utf-8", errors="replace"))):
        assert f.name in allowed, f.name + " writes a filter word (only the held-object swap and the hand body may)"
assert not writers.search(code_of(probe)) and not writers.search(code_of(coupling)) and not writers.search(code_of(grab))
assert "0x91b50" not in code_of(native + runtime + body + coupling) and "0x91e20" not in code_of(native + runtime + body + coupling), "no pair API"
assert "0x3a" not in code_of((src / "hand_body_commands.hpp").read_text(encoding="utf-8")), "no record 0x3a"
# write(): the allow-list (9 / 0x30, 0x1a / 0x20 with an allowed word), before any engine read; a filter write has its own one-per-tick slot
assert "const bool velocity_record=type==9 && record.size==0x30,filter_record=type==0x1a && record.size==0x20;" in write_fn
assert write_fn.index("if(!velocity_record && !filter_record)") < write_fn.index("engine_.world") and write_fn.index("hand_filter_word_allowed(word)") < write_fn.index("engine_.world")
assert "std::uint64_t& slot=filter_record?s.filter_tick:s.written_tick;" in write_fn
for need in ("engine_.at_g1_point()", "const TickGuard guard(ticking_);", "!life_.can_write(side,world,handle)", "busy", "s.pending_destroy"):
    assert need in write_fn, need + ": the G1 point and the lifecycle guards hold for the filter record"
assert "inline constexpr bool hand_filter_word_allowed(std::uint32_t word)" in body
# a new body starts with the default word: create, finish (destroy / lost / gone) and abandon reset it
assert "reset_filter(side);" in function(body, "void do_create(") and "reset_filter(side);" in function(body, "void finish(")
assert "reset_filter(side);" in service[service.index("case BodyAction::abandon:"):service.index("case BodyAction::create:")]
# the native filter call: our own slot, the body's world, an allowed word, busy re-read right before guarded_call (checked in section 3 too)
setf = function(native, "static bool set_filter(")
assert "hand_filter_word_allowed(filter)" in setf and "own_state(world,handle,s)!=Own::present" in setf and "world!=world_at_create" in setf
assert "{0x97600,{0x48,0x83,0xec,0x48,0x80,0x7c,0x24,0x70,0x00,0x41,0x0f,0xb6,0xc1,0x74,0x3a,0x84}}" in native, "0x97600 byte-pinned (verify())"
# the runtime: the type-0x1a apply goes to HandBodyNative::set_filter; the wanted word comes only from a fresh publication, after service()
assert "return HandBodyNative::set_filter(s->storage,HandBodyNative::world_now(),s->world,handle,filter);" in runtime
assert tick.index("body->service(in,spawn,gate);") < tick.index("body->write_filter(side,want);") < tick.index("body->drive(")
assert "if(fresh) for(unsigned side=0;side<2;++side) {" in tick and "const std::uint32_t want=last_hands_.filter[side];" in tick
assert runtime.count("write_filter(") == 1 and "write_filter(" not in code_of(probe)
# the keeper: a layer returns only when the object that took it out ended its own SafeRelease check, or the body went (a new body is born with the
# default word); the hold's end only starts that object's check (DeepSeek P2: one check per released object)
keeper = function(coupling, "template<class Overlap> FilterKeeperOutput update(unsigned side,const FilterKeeperInput& in,Overlap overlap)")
ended = keeper[keeper.index("if(s.holding && (!hold || in.held_entity!=s.held.entity)) {"):keeper.index("if(hold && !s.holding) {")]
assert "s.rel[s.count]=s.held;s.rel[s.count].safe.reset();++s.count;s.holding=false;" in ended and "default" not in ended.replace("max_releases", "")
assert "if(o.restore_layer) {++out.restored;if(o.unresolved) ++out.unresolved;drop(s,i);} else ++i;" in keeper
assert keeper.count("drop(s,") == 3, "drop: the restore, a regrab of the same object, and a full table (counted unresolved)"
assert "for(std::size_t i=0;i<s.count;++i) mask&=hand_body::word_mask(holding_word(s.rel[i].layer));" in keeper, "every running check keeps its layer out"
assert "default_word" not in keeper[keeper.index("if(hold) s.held.layer"):keeper.index("std::uint32_t mask=default_mask;")]
assert "const auto o=e.safe.update(r);" in keeper and "SafeRelease safe{}" in coupling
assert "HandOverlap out;out.answered=count>0;" in coupling, "nothing read is no answer"
# dxgi: knob off is no behaviour change. The B16 block is knob-gated; everything it sets has today's value when it does not run.
b16_at = probe.index("// B16: held things follow the hand body (Bonelab")
b16_block, _ = enclosing_block(probe, probe.index("const double couple_now="))
assert probe[b16_at:probe.index(b16_block)].rstrip().endswith("if(hand_body_requested.load(std::memory_order_relaxed) && d2vr::HandBodyRuntime::active())")
assert "auto& body_view=hold_body_view[side];body_view=HoldBodyView{};" in b16_block, "the views are rebuilt every frame inside the gate"
assert "unsigned hold_coupled_bits{},hold_holding_bits{};std::array<std::uint32_t,2> hand_filter_words{{d2vr::R1Recipe::filter,d2vr::R1Recipe::filter}};" in probe
assert b16_at < vstart, "B16 decides before B15 draws"
for name, pattern in (("hold_coupled_bits", r"hold_coupled_bits\|="), ("hand_filter_words", r"hand_filter_words\[side\]="), ("hold_holding_bits", r"hold_holding_bits\|=")):
    for m in re.finditer(pattern, probe):
        assert probe.index(b16_block) <= m.start() < probe.index(b16_block) + len(b16_block), name + " is set only inside the knob-gated block"
# fix round 1: part 2, after B15's single pick, couples motor holds to the DRAWN hand (B15's body_weight); knob-gated; the only couple() calls
p2_at = probe.index("// B16 part 2 (fix round 1 ruling)")
p2_block, _ = enclosing_block(probe, probe.index("static std::array<std::uint64_t,2> coupled_serial{};"))
assert final_at < p2_at < probe.index(p2_block) and probe[p2_at:probe.index(p2_block)].rstrip().endswith("if(hand_body_requested.load(std::memory_order_relaxed) && d2vr::HandBodyRuntime::active())")
for m in re.finditer(r"native_grab\.couple\(", probe):
    assert probe.index(p2_block) <= m.start() < probe.index(p2_block) + len(p2_block), "couple() only in part 2"
assert "drawn_body_weight[side]=visual[side].body_weight;" in vblock and "std::array<double,2> drawn_body_weight{};" in probe
assert code_of(probe).count("drawn_body_weight[side]") == 2, "written by B15, read by part 2 only"
assert "in.body_weight=body_view.weight;" in p2_block and "body_view.weight=drawn_body_weight[side];" in p2_block
# fix round 2 (review P3-2): the anchor view is fetched once per side per frame (part 1), part 2 reuses it
assert code_of(probe).count("native_grab.hold_anchor(") == 2, "the Task 12 block and B16 part 1 only"
assert "native_grab.hold_anchor(" in b16_block and "native_grab.hold_anchor(" not in p2_block
cn = function(coupling, "inline CouplingOutput couple_native(")
assert "WorldTrackedPose carried=request;" in cn and "body->" not in cn, "a native target never reads the body: it follows the drawn grip"
assert "in.clamped_native=clamped_controller;" in cn and cn.index("in.clamped_native=clamped_controller;") < cn.index("if(drawn && HoldCoupling::usable(body_ready,body))"), "not drawn: B4's output"
hc_upd = function(coupling, "CouplingOutput update(unsigned side,const CouplingInput& in)")
assert "if(!use) {s.native.reset();s.has_push=false;s.push_closing=0;return out;}" in hc_upd, "no usable body: today's pose, no release"
# final review P2-1 (amends fix rounds 2-3): a coupled hold releases on the HELD OBJECT stuck (a native hold: the clamp's push of the object's
# request over 0.35 m for 0.25 s, not closing faster than 0.5 m/s), never on the hand body's gap; a motor hold: PhysicalGrab's own rule
assert code_of(hc_upd).count("out.release=") == 1 and "out.release=s.native.update(out.object_blocked && slow,dt);" in hc_upd
assert "out.object_blocked=push>native_release_m;" in hc_upd and "const bool slow=!(s.push_closing>=stuck_closing_mps);" in hc_upd
assert "stuck_closing_mps=HandServoConfig{}.snap_closing_mps" in coupling
assert "out.lag_m=length(in.controller.position-in.body->position);" in hc_upd and not re.search(r"lag_m\s*>", code_of(hc_upd)), "the body gap is status only"
assert not re.search(r"\b(lag_release_m|prop_release_m)\b", code_of(hc_upd)), "no body-gap rule for motor holds"
assert "if(w>0) {" in hc_upd, "the drawn weight only places the target"
assert "out.body_weight=s.body_w;" in visual_hdr
assert "native_grab.couple(" not in code_of(probe[:b16_at]) and code_of(grab).count("void couple(") == 1
assert "in.yield=(((hand_body_yield_bits&~hold_coupled_bits)|authored_hands)>>side)&1u;" in vblock
for line in ("hand_body_in.yield=(hand_body_yield_bits&~hold_coupled_bits)|authored_hands;", "hand_body_in.filter=hand_filter_words;",
             "hand_body_in.target[side].position=native_body_target[side].position;", "hand_body_in.holding=hold_holding_bits;"):
    assert line in publish_block, line
# review P3-1: the keeper's hold is judged on the body existing, not on a fresh pose; a stale pose freezes the checks
assert "if(motor && body_exists) {" in b16_block and "fk.pose_fresh=body_view.ready;" in b16_block
kp = function(coupling, "template<class Overlap> FilterKeeperOutput update(unsigned side,const FilterKeeperInput& in,Overlap overlap)")
assert "for(std::size_t i=0;i<s.count && !s.frozen;)" in kp and "r.now=in.now-s.frozen_total;" in kp
# publish_held_object: the body only with the knob on and a ready view; drawn (weight > 0) or B4's request, origin and shift exactly
pho = function(probe, "static void publish_held_object(")
assert "const bool body_knob=hand_body_requested.load(std::memory_order_relaxed);" in pho
assert "const bool body_on=body_knob && d2vr::HandBodyRuntime::active() && body_view.ready;" in pho and "const bool coupled=body_on && body_view.weight>0;" in pho
assert "in.requested=coupled?d2vr::HoldCoupling::controller_request(request,grip.position,palm_pose.position):request;" in pho
assert "if(out.clamped) {origin=out.target.position;++held_clamp_status.clamped_frames;}" in pho and "d2vr::Vec3 shift=out.clamped?out.shift:d2vr::Vec3{};" in pho
cpl = pho[pho.index("if(body_on) {   // B16"):pho.index("} else if(body_knob) native_body_target[side&1u].valid=false;")]
assert "d2vr::couple_native(" in cpl and "if(coupled) {origin=co.target.position;shift=origin-request.position;}" in cpl and "coupled_release=co.release;" in cpl
assert "if((body_on?coupled_release:L.clamp.should_release()) && !L.drop_sent)" in pho, "P2-1: the stuck-object rule with the body in play, else B4's exactly"
assert "native_body_target[side&1u]={co.body_target?*co.body_target:palm_pose.position,GetTickCount64(),true,!co.object_blocked};" in cpl
# final review P1-1: a held or release sweep that could not see past a hand body (hand_body: blocked) is never a surface for the carried object,
# and a release probe that met one is unanswered for that frame (never a SafeRelease push along the hand's normal)
rel_step = function(probe, "static void held_clamp_release_step(")
assert "for(std::size_t i=0;i<proxies.count && i<hits.size();++i) if(hits[i] && hits[i]->hand_body) in.sweep_answered=false;" in rel_step
assert rel_step.index("in.sweep_answered=false;") < rel_step.index("if(in.sweep_answered) for(") < rel_step.index("release_probe_depth(")
assert "if(raw_hits[i]->hand_body) continue;" in pho and pho.index("if(raw_hits[i]->hand_body) continue;") < pho.index("in.hits[i]=d2vr::to_clamp_hit(*raw_hits[i]);")
# review P3-2: knob off adds no work: the held-clamp begin/end additions are knob-gated
assert "if(hand_body_requested.load(std::memory_order_relaxed)) native_body_target[L.side&1u].valid=false;" in function(probe, "static void held_clamp_end(")
assert "if(hand_body_requested.load(std::memory_order_relaxed)) {hold_coupling.reset(side&1u);" in function(probe, "static void held_clamp_begin(")
# NativeGrab (review P3-2): every B16 addition behind coupling_on_, read once at install from the knob files
assert grab.count("coupling_on_=") == 1 and 'coupling_on_=hand_body_knob_enabled(std::filesystem::exists(root/L"build"/L"hand-body",ec),std::filesystem::exists(root/L"build"/L"no-hand-body",ec));' in grab
avo = function(grab, "HoldAnchorView anchor_view_of(")
assert avo.index("if(coupling_on_) {") < avo.index("body.record+0x6c"), "the layer read only with the knob"
ul = function(grab, "void update_lane(unsigned side,std::uintptr_t world,UINT64 now)")
cg = ul[ul.index("if(coupling_on_) {"):ul.index("const bool sample=")]
for need in ("coupling_current(lane,now)", "lane.sample_hand=p.hand;", "lane.grab.rebase(lane.sample_hand);", "coupled_release=current && lane.coupling.release;"):
    assert need in cg, need + " runs only with the knob"
assert "lane.grab.sample(coupling_on_?lane.sample_hand:p.hand," in ul and "release_sample(coupling_on_?lane.sample_hand:p.hand," in grab
assert "lane.coupling.serial==lane.hold_serial" in function(grab, "static bool coupling_current("), "a coupling reaches only the hold it names"
assert "if(coupling_on_) {lane.hold_serial=++hold_serials_;" in function(grab, "void begin_hold(") and "v.serial=lane.hold_serial;" in avo
cpl_fn = function(grab, "void couple(unsigned side,std::uint64_t serial,")
assert "std::lock_guard<std::mutex> lock(coupling_mutex_);" in cpl_fn and "mutex_)" not in cpl_fn.replace("coupling_mutex_)", ""), "never NativeGrab's pass mutex"
assert "c.release=release;" in cpl_fn, "fix round 2: the raw-gap release travels even when B15 does not draw the body (no offset)"
assert "native_grab.couple(side,held.serial,out.uses_body?std::optional<d2vr::Vec3>(out.target.position-palm_grips[side].position):std::nullopt,out.release,couple_ms);" in p2_block
upd = function(grab, "void update(std::uintptr_t dispatcher)")
assert "if(coupling_on_) {std::unique_lock<std::mutex> pub(coupling_mutex_,std::try_to_lock);" in upd and upd.index("coupling_mutex_") < upd.index("update_lane(side,*world,now)")
rel = ul[ul.index("if(lane.capped && coupled_release && !lane.pending_release)"):ul.index("if(lane.capped) {")]
assert "end_capped_hold(lane,side,ReleaseReason::lag,true);" in rel and "lane.coupling.release=false;" in rel
# fix round 2: the snap waits while holding; the filter word is the engine's read-back, retried, then latched
assert "d.holding=fresh && ((last_hands_.holding>>side)&1u)!=0;" in tick
# final review P2-1: a native hold whose object is free lets B14's stuck snap run (a snag on the hand alone); a motor hold never does
assert "d.hold_snap_ok=fresh && ((last_hands_.hold_snap_ok>>side)&1u)!=0;" in tick
assert "const bool hold_waits=in.holding && !in.hold_snap_ok;" in drive_fn and "!in.target_free || hold_waits" in drive_fn and "if(hold_waits) s.snap_pending=false;" in drive_fn
assert "if(native_body_target[side].object_free) hand_body_in.hold_snap_ok|=1u<<side;" in publish_block and code_of(probe).count("hold_snap_ok|=") == 1
assert "if(++s.filter_misses>=max_filter_misses) latch(side," in write_fn and "static constexpr unsigned max_filter_misses=5;" in body
assert "if(got) {s.filter=*got;" in write_fn and "e.filter=[](unsigned side,std::uint32_t handle)" in runtime
# the comment-swallow guard (three line joins on this plan swallowed statements): no code-shaped text inside a // comment
check_src = (root / "tests/hold_coupling_check.cpp").read_text(encoding="utf-8")
assert not re.search(r"//[^\n]*\bCHECK\(", check_src), "a CHECK( inside a // comment in tests/hold_coupling_check.cpp"
for name, text in (("hold_coupling.hpp", coupling), ("hand_body.hpp", body), ("hand_body_runtime.hpp", runtime), ("hand_body_native.hpp", native),
                   ("native_world_trace.hpp", trace), ("hand_visual_source.hpp", visual_hdr)):
    assert not re.search(r"//[^\n]*[a-z)\]][a-z_]+(\.|->)[a-z_]+(=|\()[^\n]*;", text), name + ": code swallowed into a comment?"
for name in ("native_world_trace_check.cpp", "hand_visual_source_check.cpp", "native_world_query_source_check.cpp"):
    assert not re.search(r"//[^\n]*\b(CHECK|assert)\(", (root / "tests" / name).read_text(encoding="utf-8")), "a CHECK inside a // comment in " + name
for line in probe_block.splitlines():
    if "//" in line:
        assert not re.search(r"//.*\b[a-z_]+(\.|->|\[)[\w\]\[]*=[^=]", line), "a statement swallowed by a comment: " + line.strip()[:90]
# P2-2 (X-4 amended): with a grasp anchor active the body is elected only on contact; P3-3: a body that leaves while drawn is blended away
vis_upd = function(visual_hdr, "VisualOutput update(unsigned side")
assert "const bool agrees=!anchor_active && out.gap_m<=(was_body?agree_leave:agree_gap);" in vis_upd and "|| agrees);" in vis_upd
assert "const bool body_left=!present && s.has_source && s.source==VisualSource::body;" in vis_upd and "(!present && !body_left && !leaving)" in vis_upd
for line in b16_block.splitlines() + cpl.splitlines():
    if "//" in line:
        assert not re.search(r"//.*\b[a-z_]+(\.|->|\[)[\w\]\[]*=[^=]", line), "a statement swallowed by a comment: " + line.strip()[:90]
print("hand_body_routing_check B16 ok")

# 10. the B13 sim scenario compiles and fits a 120 s session with a 60 s lead
# B17 + user 2026-10-24 ("turn on all features by default"): the launcher row defaults ON (it writes build/hand-body on a fresh
# home), the DLL still needs build/hand-body, no-hand-body is the hard off switch, and the spike is dev-only (not a launcher row)
import json
assert "hand_body_knob_enabled(bool hand_body_file,bool no_hand_body_file) {return hand_body_file && !no_hand_body_file;}" in body, \
    "default off: needs build/hand-body; no-hand-body beats hand-body"
launcher_rows = json.loads((root / "tools/default_features.json").read_text(encoding="utf-8"))["features"]
hb_rows = [r for r in launcher_rows if r["id"] == "hand-body"]
assert len(hb_rows) == 1 and hb_rows[0]["file"] == "hand-body" and hb_rows[0]["kind"] == "enable" and hb_rows[0]["default"] is True, \
    "launcher row: enable, default on (user 2026-10-24), writes build/hand-body"
assert "Physical hand bodies (experimental, headset test)" == hb_rows[0]["label"]
assert not any("spike" in r["id"] or "spike" in r["file"] or "no-hand-body" in (r["id"], r["file"]) for r in launcher_rows), \
    "hand-body-spike and no-hand-body are not exposed in the launcher"
print("hand body launcher row default on, DLL needs build/hand-body, spike dev-only")


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
    tank = sim.scenario_segments("tank_hand_body", 45000)
    sim.compile_script(tank, 120)
    assert max(s["end_ms"] for s in tank) <= 120000 and len(tank) <= 128
    print("tank_hand_body scenario compiles")
