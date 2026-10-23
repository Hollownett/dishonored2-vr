"""Guard HUD profiler callback draining and publication across restart."""
from pathlib import Path

package = Path(__file__).resolve().parents[1]
draw = (package / "src/native_hud_child_draw.hpp").read_text(encoding="utf-8")
probe = (package / "src/dxgi_probe.cpp").read_text(encoding="utf-8")

def ordered(text, *tokens):
    position = 0
    for token in tokens:
        position = text.index(token, position) + len(token)

disable = draw[draw.index("    static bool disable() noexcept {"):draw.index("    static void fixture(")]
ordered(disable, "cpu_profile=nullptr;", "MH_DisableHook(target_)",
        "while(depth_ && GetTickCount64()<until)", "if(depth_)return false;", "clear();return true;")
hook = draw[draw.index("    static std::uint32_t __fastcall hook("):draw.index("    static bool publish(")]
ordered(hook, "++depth_;++calls;++nesting_;", "const auto profile=cpu_profile.load(",
        "if(profile)profile(0,nesting_);", "if(profile)profile(1,nesting_);",
        # the live-crosshair fast path pairs its own callbacks and unwinds the depth in its __finally
        "__try {result=original_(iggy,scaled", "__finally {if(profile){profile(2,nesting_);profile(3,nesting_);}--nesting_;--depth_;}",
        "if(profile)profile(1,nesting_);", "__try {if(!suppress)result=original_",
        "__finally {eye_layout_scale_=saved_eye_scale;", "if(profile)profile(2,nesting_);",
        "if(profile)profile(3,nesting_);--nesting_;--depth_;}")
start = probe[probe.index('DWORD WINAPI StartProbe(void*) {'):]
ordered(start,
        'if(!d2vr::NativeHudChildDraw::disable()) throw std::runtime_error("Native HUD profile did not drain before activation");',
        'render_perf_requested=false;',
        'd2vr::NativeHudChildDraw::install(image_base,&read_self)',
        'bool render_perf_setup=fs::exists(root/L"build"/L"render-perf");',
        'if(render_perf_setup) {',
        'render_perf_cpu=std::make_unique<d2vr::RenderPerfCpu>();',
        'render_perf_cpu_log.open(',
        'render_perf_requested=render_perf_setup;',
        'd2vr::NativeHudChildDraw::cpu_profile=render_perf_requested.load()?&render_perf_hud_profile:nullptr;')
setup = start[start.index('bool render_perf_setup='):start.index('render_perf_requested=render_perf_setup;')]
assert setup.count('render_perf_setup=false;render_perf_write_failed=true;') == 2
assert 'render_perf_requested=' not in setup
assert 'render_perf_requested.load()' not in setup
assert start.count('Native HUD profile did not drain before activation') == 1
print('PASS: captured callbacks drain before replacement; null profile on stop/start; failed setup cannot publish profiling')
