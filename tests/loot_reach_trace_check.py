"""G1 loot measurement trace (hands plan Task 1): source routing of the read-only native Use trace in src/native_interaction_aim.hpp.

The trace (knob build/loot-reach-trace -> artifacts/native-loot-reach.csv) logs, per routed stage-0 cycle, every owner in the hand-volume
result set with its rank after order_native_use_hits, the 3b7b90 LOS calls / blocked results on it, the native-selected owner and the
prompt owner. It must be strictly read-only: each value is read AFTER the original call it describes, and no trace code writes a hit
record, a return value or the LOS out byte. This check pins that by source order and runs itself against mutated sources (a row write
moved before `self->sort_(...)`, a LOS read moved before the original call, a trace that writes the out byte) which must fail.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/native_interaction_aim.hpp"
PROBE = ROOT / "src/dxgi_probe.cpp"
COLUMNS = "t,side,palm_x,palm_y,palm_z,dir_x,dir_y,dir_z,owner,spawn,takeable,set_count,rank,los_called,los_blocked,selected_owner,prompt_owner,prompt_t"


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

    # knob: the trace opens only when build/loot-reach-trace exists, after the hooks install; off by default
    need('const bool loot_reach_trace_requested=fs::exists(root/L"build"/L"loot-reach-trace");' in probe, "knob read next to the other build/ knobs")
    opens = re.findall(r"native_interaction_aim\.loot_reach_trace\(", probe)
    need(len(opens) == 1, "exactly one open call")
    need('if(native_interaction_requested && loot_reach_trace_requested && !native_interaction_aim.loot_reach_trace(output/L"native-loot-reach.csv"))' in probe,
         "the open is guarded by the knob")
    need(probe.find("native_interaction_aim.install(image_base") < probe.find("native_interaction_aim.loot_reach_trace(") , "opened after install()")
    need(header.count("loot_reach_on_.store(true") == 1 and "loot_reach_on_.store(true" in body(header, "bool loot_reach_trace(const std::filesystem::path& file)", "void attach_physical_grab("),
         "only loot_reach_trace() turns the trace on")
    need("std::atomic<bool> loot_reach_on_{};" in header, "the trace starts off")
    need("loot_reach_on_=false;std::lock_guard<std::mutex> loot(loot_reach_mutex_);loot_reach_csv_.close();" in body(header, "bool disable() noexcept {", "return restored;"),
         "disable() closes the trace")
    need(f'"{COLUMNS}\\n"' in header, "the column header")
    # caps
    need("static constexpr unsigned loot_reach_row_cap=8192,loot_reach_flush_rows=64" in header, "row cap 8192, flush every 64")
    cycle = body(header, "void loot_trace_cycle(const Context& scope) noexcept {", "    template<class Invoke> static auto counted_callback(")
    need("loot_reach_rows_<loot_reach_row_cap" in cycle and "++loot_reach_rows_%loot_reach_flush_rows==0" in cycle, "rows written under the cap")
    # read-only: trace bodies write nothing native
    trace_code = body(header, "LootReachCandidate* loot_candidate(std::uintptr_t owner) noexcept {", "    template<class Invoke> static auto counted_callback(")
    need(trace_code != "", "trace helpers present")
    for bad in ("memcpy", "*output", "output[", "write(", "retrieved_=", "target_owner_", "target_object_", "seen_["):
        need(bad not in trace_code, f"trace helpers never touch {bad!r}")
    # sort: the ranks are read after the native sort AND after the mod's re-order write
    sort = body(header, "static void sort_body(void* data,unsigned first,int last,unsigned char mode,std::uintptr_t caller) {", "std::optional<bool> character_type(")
    i_native = sort.find("self->sort_(data,first,last,mode);")
    i_write = sort.find("std::memcpy(data,hits.data(),scope.count*sizeof(NativeUseHit));")
    i_trace = sort.find("self->loot_trace_ranks(")
    need(i_native >= 0 and i_write >= 0 and i_trace > i_native and i_trace > i_write, "sort: trace after self->sort_ and after the re-order write")
    # fetch: the set is read after the original retrieval
    fetch = body(header, "std::uintptr_t fetch_result_body(void* manager,void* id,void* output,std::uintptr_t caller) {", "static void __fastcall sort(")
    i_fetch = fetch.find("const auto result=self->fetch_(manager,id,output);")
    i_set = fetch.find("self->loot_trace_set(")
    need(i_fetch >= 0 and i_set > i_fetch, "fetch: trace after the original")
    need(fetch.rstrip().endswith("return result;\n    }") and fetch.count("return result;") == 1, "fetch: the original result returned unchanged")
    # LOS: counted after the original 3b7b90 call; the return value and *output are untouched by the trace
    los = body(header, "std::uintptr_t occlusion_result_body(", "bool idle_character_publication(")
    i_orig = los.find("const auto result=occlusion_(sensor,entity,los_origin,axis,point,range,output);")
    i_los = los.find("++candidate->los_called;")
    need(i_orig >= 0 and i_los > i_orig, "LOS: counted after the original call")
    los_trace = body(los, "try { // G1 trace: this candidate's stage-0 LOS call", "} catch(...) {}")
    need(los_trace != "" and "*output" not in los_trace and "result=" not in los_trace.replace("(result&0xffu)", ""), "LOS trace reads only the raw result")
    need(los.count("return qualified_result;") == 1 and los.rstrip().endswith("return qualified_result;\n    }"), "qualified return separate from raw LOS trace")
    need(los.count("if(output)*output=inside?0:1;") == 1 and los.count("if(output)*output=0;") == 1 and
         los.count("*output=1;") == 1, "out-byte writes are pickup-range rejection, inside-sphere clearance and existing scan continuation")
    # target(): the native-selected owner is recorded after the selection is known; the rows after the whole stage
    tgt = body(header, "void target(const Context& scope) noexcept {", "const InteractionRay* ray(")
    need(tgt.find("if(loot_cycle_.open) loot_cycle_.selected=object?owner:0;") > tgt.find("if(!candidate && (!direct_owner || *direct_owner!=owner)) object=0;") >= 0,
         "target(): selected owner after the candidate check")
    stage = body(header, "template<unsigned Index> static void stage_body(void* sensor,StageFrame& frame) {", "template<unsigned Index> static void* __fastcall queue(")
    need(stage.find("self->loot_trace_cycle(*scope);") > stage.find("self->target(*scope);") > stage.find("self->stages_[Index](sensor);") >= 0, "rows after the native stage and target()")
    # prompt_target(): logs the owner it returns, the selection itself unchanged
    prompt = body(header, "std::optional<QualifiedTarget> prompt_target(std::uint64_t now) {", "std::optional<QualifiedTarget> prompt_target_select(")
    need("auto out=prompt_target_select(now);" in prompt and "return out;" in prompt and "loot_prompt_owner_.store(out?out->owner:0" in prompt, "prompt_target logs its owner")
    select = body(header, "std::optional<QualifiedTarget> prompt_target_select(std::uint64_t now) {", "// The other hand's remembered target")
    need("if(const auto live=qualified_target(now))return live;" in select and "return native_selected_locked(now);" in select, "prompt selection unchanged")
    need("inline thread_local NativeInteractionAim::LootReachCycle NativeInteractionAim::loot_cycle_{};" in header, "cycle state is thread-local")
    # Task 4 LOS trace (H15): armed before the original 3b7b90 call, written after it; the hit copy reads 3b7b90's collector after its sort
    arm = los.find("try { // G1 LOS trace (Task 4): arm the hit copy")
    need(0 <= arm < i_orig, "LOS trace: armed before the original call")
    need(los.find("if(loot_los_armed) loot_los_row(") > i_orig >= 0, "LOS trace: the row is written after the original call")
    detour = body(header, "static void __fastcall loot_los_sort(void* collector) {", "    void loot_los_copy(")
    need(detour.count("self->loot_los_sort_(collector);") == 1 and detour.find("self->loot_los_sort_(collector);") < detour.find("self->loot_los_copy("),
         "LOS trace: the collector sort runs once, before the copy")
    need("caller>self->base_+loot_los_owner_rva && caller<self->base_+loot_los_owner_rva+loot_los_owner_size" in detour, "LOS trace: only 3b7b90's sort is copied")
    los_code = body(header, "    void loot_los_copy(std::uintptr_t collector) noexcept {", "    LootReachCandidate* loot_candidate(")
    need(los_code != "", "LOS trace helpers present")
    for bad in ("*output", "output[", "write(", "*reinterpret_cast", "retrieved_=", "seen_["):
        need(bad not in los_code, f"LOS trace helpers never touch {bad!r}")
    opened = body(header, "bool loot_reach_trace(const std::filesystem::path& file) {", "void attach_physical_grab(")
    need(header.count("MH_EnableHook(at)==MH_OK) loot_los_sort_enabled_=true;") == 1 and "MH_EnableHook(at)==MH_OK) loot_los_sort_enabled_=true;" in opened,
         "LOS trace: the sort hook is enabled only when the trace opens")
    need("code==loot_los_sort_code" in opened, "LOS trace: the sort's code bytes are verified before hooking")
    need("MH_DisableHook(reinterpret_cast<void*>(base_+loot_los_sort_rva))" in body(header, "bool disable() noexcept {", "return restored;"), "disable() removes the sort hook")
    need("inline thread_local NativeInteractionAim::LootLosCapture NativeInteractionAim::loot_los_capture_{};" in header, "LOS capture is thread-local")
    return out


def main():
    header, probe = HEADER.read_text(encoding="utf-8"), PROBE.read_text(encoding="utf-8")
    real = problems(header, probe)
    assert not real, real
    # mutations that must fail
    sort_call = "        self->sort_(data,first,last,mode); // Native sort runs once, including recursion.\n"
    assert sort_call in header
    i = header.index("        try { // G1 trace: the final order")
    j = header.index("} catch(...) {}\n", i) + len("} catch(...) {}\n")
    moved = header[:i] + header[j:]
    moved = moved.replace(sort_call, header[i:j] + sort_call)
    assert problems(moved, probe), "a row write moved before self->sort_(...) must fail"
    los_call = "        const auto result=occlusion_(sensor,entity,los_origin,axis,point,range,output); // original once, raw return preserved\n"
    k = header.index("        try { // G1 trace: this candidate's stage-0 LOS call")
    m = header.index("} catch(...) {}\n", k) + len("} catch(...) {}\n")
    early = header[:k] + header[m:]
    early = early.replace(los_call, header[k:m].replace("(result&0xffu)", "(0u)") + los_call)
    assert problems(early, probe), "a LOS count before the original call must fail"
    writes = header.replace("++candidate->los_called;", "++candidate->los_called;*output=1;", 1)
    assert problems(writes, probe), "a trace that writes the out byte must fail"
    hits = header.replace("if(!read_(at,hits.data(),count*sizeof(NativeUseHit))) return;", "if(!read_(at,hits.data(),count*sizeof(NativeUseHit))) return;std::memcpy(reinterpret_cast<void*>(at),hits.data(),64);", 1)
    assert problems(hits, probe), "a trace that writes the hit records must fail"
    ungated = probe.replace("if(native_interaction_requested && loot_reach_trace_requested && !native_interaction_aim.loot_reach_trace(",
                            "if(native_interaction_requested && !native_interaction_aim.loot_reach_trace(", 1)
    assert problems(header, ungated), "an open without the knob must fail"
    uncapped = header.replace("loot_reach_row_cap=8192", "loot_reach_row_cap=65536", 1)
    assert problems(uncapped, probe), "a different row cap must fail"
    k2 = header.index("        try { // G1 LOS trace (Task 4): arm the hit copy")
    m2 = header.index("} catch(...) {}\n", k2) + len("} catch(...) {}\n")
    late_arm = (header[:k2] + header[m2:]).replace(los_call, los_call + header[k2:m2], 1)
    assert problems(late_arm, probe), "arming after the original call must fail"
    writes_out = header.replace("const auto out=rig::value<unsigned char>(read_,output);", "const auto out=rig::value<unsigned char>(read_,output);*reinterpret_cast<unsigned char*>(output)=1;", 1)
    assert problems(writes_out, probe), "a LOS trace that writes the out byte must fail"
    unhooked = header.replace("MH_DisableHook(reinterpret_cast<void*>(base_+loot_los_sort_rva))", "true", 1)
    assert problems(unhooked, probe), "a sort hook left installed must fail"
    print("loot_reach_trace_check ok")


if __name__ == "__main__":
    sys.exit(main())
