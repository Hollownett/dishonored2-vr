"""G2 spike (camera plan Task 4): the Cinematic32 capture probe (build/cinematic-probe -> artifacts/cinematic-probe.csv) is off
without the knob, only reads, and its two sim-only experiments reuse existing kinds of writes: "stereo" skips sample_stereo's restore
for exact mask 32 only, "offset" writes the gameplay head-view window (view+0x9c, head_saved, head_wrote) that restore_head_view puts
back after Setup, counted apart from the gameplay head writes."""
from pathlib import Path


def fn(probe: str, signature: str) -> str:
    body = probe[probe.index(signature):]
    return body[:body.index("\n}\n")]


def check(probe: str) -> None:
    # off without the knob: the mode starts 0, is reset at every StartProbe and set only by the knob file
    assert "static std::atomic<unsigned> cinematic_probe_mode{0};" in probe
    assert probe.count("cinematic_probe_mode=") == 2
    start = probe.index("std::lock_guard<std::mutex> trace_lock(cinematic_probe_mutex);cinematic_probe_csv.close();cinematic_probe_rows=0;cinematic_probe_mode=0;")
    knob = probe[start:probe.index("\n        }\n", probe.index('fs::exists(root/L"build"/L"cinematic-probe")', start))]
    assert 'if(fs::exists(root/L"build"/L"cinematic-probe")) {' in knob
    assert 'cinematic_probe_mode=1u|(text.find("stereo")!=std::string::npos?2u:0u)|(text.find("offset")!=std::string::npos?4u:0u);' in knob
    assert 'output/L"cinematic-probe.csv"' in knob
    # every entry point returns at once without the knob (bit 1)
    setup = fn(probe, "void cinematic_probe_setup(void* view) noexcept {")
    offset = fn(probe, "void cinematic_probe_offset(void* view) noexcept {")
    begin = fn(probe, "void cinematic_probe_begin(void* view) noexcept {")
    present = fn(probe, "void cinematic_probe_present(IDXGISwapChain* chain,UINT64 number,const DXGI_SWAP_CHAIN_DESC& desc) noexcept {")
    for body in (setup, begin, present):
        assert body.split("\n")[1].strip().startswith("if(!(cinematic_probe_mode.load()&1) || !capturing.load()"), body.split("\n")[1]
    # the experiments need the knob text AND the simulated backend
    assert "inline bool cinematic_probe_experiment(unsigned bit) noexcept {return simulated_vr_requested && (cinematic_probe_mode.load()&bit)!=0;}" in probe
    assert "inline bool cinematic_probe_keeps_stereo() noexcept {return cinematic_probe_experiment(2) && game_logic_mask()==32;}" in probe
    # read-only: the observers write no engine memory and touch no stereo/mode state
    for body in (setup, begin, present):
        for banned in ("WriteProcessMemory", "Interlocked", "enable_stereo", "restore_stereo", "head_wrote", "head_saved", "->set(", "native_cvars"):
            assert banned not in body, banned
    assert "capture_frame(chain,name.c_str())" in present and "cinematic_probe_experiment(4)" in present[:present.index("capture_frame(")]
    assert "cinematic_probe_rows<cinematic_probe_rows_max" in present and "cinematic_probe_rows>=cinematic_probe_rows_max" in present
    assert "static constexpr UINT cinematic_probe_rows_max=2048;" in probe
    # the offset experiment: exact 32, a matched native view, the present thread, never over a gameplay head write; position only (+5 cm left)
    assert offset.count("WriteProcessMemory(") == 1 and probe.count("cinematic_probe_wrote=true") == 1
    gate = offset[:offset.index("WriteProcessMemory(")]
    for need in ("if(!cinematic_probe_experiment(4) || !(cinematic_probe_mode.load()&1) || !capturing.load() || !view || !matched) return;",
                 "if(game_logic_mask()!=32 || head_wrote || GetCurrentThreadId()!=xr_present_thread || !cinematic_probe_test_setup(frames.load())) return;",
                 "static_assert(sizeof(window)==sizeof(head_saved));",
                 "head_saved=window;head_source_address=source;",
                 "for(int axis=0;axis<3;++axis) position[axis]+=.05f*left[axis];",
                 "std::memcpy(window.data()+(0xa8-0x9c),position.data(),12);",
                 "head_wrote=true;cinematic_probe_wrote=true;"):
        assert need in gate, need
    assert gate.index("head_saved=window;") < gate.index("std::memcpy(window.data()+(0xa8-0x9c)") < gate.index("head_wrote=true;cinematic_probe_wrote=true;")
    assert "reinterpret_cast<void*>(source+0x9c),window.data(),window.size()" in offset
    # restore_head_view puts the window back exactly as for gameplay and counts the probe's restores apart (head_writes==head_restores holds)
    restore = fn(probe, "void restore_head_view() noexcept {")
    assert "head_saved.data(),head_saved.size(),&written) && written==head_saved.size()) {" in restore
    assert "if(cinematic_probe_wrote) ++cinematic_probe_restores;else ++head_restores;}" in restore
    assert "head_wrote=false;cinematic_probe_wrote=false;" in restore
    # hook order: record before any head write, the experiment after prepare_head_view, restore in __finally
    hook = fn(probe, "void __fastcall view_setup_hook(void* view,int* window_rect,int* render_rect) {")
    order = [hook.index(x) for x in ("if(claimed) cinematic_probe_setup(view);", "if(claimed) observe_eye_setup(view);",
                                     "if(claimed) prepare_head_view(view);", "if(claimed) cinematic_probe_offset(view);",
                                     "original(view,window_rect,render_rect);setup_completed=true;", "if(claimed && head_requested) restore_head_view();")]
    assert order == sorted(order), order
    assert "observe_eye_begin(view);cinematic_probe_begin(view);" in probe
    # the stereo experiment only bypasses the restore for exact 32; the probe row is taken before it
    stereo = fn(probe, "void sample_stereo(IDXGISwapChain* chain,UINT64 number,const DXGI_SWAP_CHAIN_DESC& desc) {")
    assert stereo.index("cinematic_probe_present(chain,number,desc);") < stereo.index(
        "if(xr_requested && !render_world_scene() && !cinematic_probe_keeps_stereo()) {")
    assert probe.count("cinematic_probe_keeps_stereo()") == 2
    # the shipping gates are untouched: render_world_scene still admits only logic 2 and world UI
    assert "inline bool render_world_scene() {const auto logic=game_logic_mask();return logic==2 || d2vr::world_ui_logic(logic)!=d2vr::WorldUiKind::none;}" in probe
    assert ',\\"cinematic_probe\\":" << cinematic_probe_status_json()' in probe


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src/dxgi_probe.cpp").read_text(encoding="utf-8"))
    print("cinematic_probe_check ok")
