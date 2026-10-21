"""perf-mono-view (plan E task 5a): the diagnostic that makes XR render ONE centre view per frame must be read once in StartProbe,
write the single-eye descriptor only under the knob, fill BOTH eye swapchains from the same source, name its one view by address
as eye 0, and fall back to the paired path (never present garbage). Source-routing pins; the headset-free timing is a sim run."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
probe = (root / 'src' / 'dxgi_probe.cpp').read_text(encoding='utf-8')
xr = (root / 'src' / 'game_xr.hpp').read_text(encoding='utf-8')
features = (root / 'tools' / 'default_features.json')


def between(text, start, end):
    a = text.index(start)
    return text[a:text.index(end, a)]


# the knob string is read once, in StartProbe, before prepare_stereo, and never becomes a default feature
assert probe.count('L"perf-mono-view"') == 1
read_at = probe.index('L"perf-mono-view"')
assert read_at < probe.index('            prepare_stereo();', read_at)
assert 'mono_view_state=1;' in probe[read_at:read_at + 600] and 'mono_view_state=2;' in probe[read_at:read_at + 700]
if features.exists():
    assert 'perf-mono-view' not in features.read_text(encoding='utf-8')

# the descriptor: written only when the knob state is on; the separation is 0 so the one view is the centre
prep = between(probe, 'void prepare_stereo() {', '// The engine renders each eye at fraction f')
mono_block = between(prep, 'if(mono_view_state.load()==1) {', 'if(!read_self(image_base+d2vr::selected_mode_rva')
assert 'left_metadata' in mono_block and 'diagnostic_mode_pointer=left_metadata.data();' in mono_block
assert 'if(mono_view_state.load()==1) diagnostic_separation=0;' in prep
assert probe.count('diagnostic_mode_pointer=left_metadata.data();') == 2  # flat stereo_flag==2 and the mono branch

# routing: one prepared view is eye 0, still matched by address, and eye 1 is never published
setup = between(probe, 'void observe_eye_setup(void* view) noexcept {', 'void observe_eye_begin(void* view) noexcept {')
assert 'native.size()!=(mono?1u:2u)' in setup and 'eye_views[1]=0' in setup and 'difference<.001f' in setup
head = between(probe, 'void prepare_head_view(void* view) noexcept {', 'void restore_head_view() noexcept')
assert 'const bool mono=mono_view_state.load()==1 && stereo_active.load();' in head
assert 'native.size()!=(mono?1u:2u)' in head and 'difference<.001f' in head
assert 'GameXr::mono_centre_view(*xr_views)' in head
# eye 0 guarded per-frame sites still run once: the single view is eye 0
assert 'if(eye==0) { // comfort telemetry' in head and 'if(eye==0 && native_crosshair_installed)' in head
# both eyes record the same data so rendered_mask_ reaches 3
rec = between(head, 'if(mono) { // the one image serves both', '} else game_xr->record_rendered_eye(')
assert 'record_rendered_eye(0,fov,&rendered_orientation,&xr_eye.pose.position)' in rec
assert 'record_rendered_eye(1,fov,&rendered_orientation,&xr_eye.pose.position)' in rec
assert 'rendered_position' in xr and 'static XrView mono_centre_view' in xr
assert 'std::min(v[0].fov.angleLeft,v[1].fov.angleLeft)' in xr and 'std::max(v[0].fov.angleRight,v[1].fov.angleRight)' in xr

# both eye swapchains are filled from the same source in mono (GameXr::present copies sources[eye] to chain eye)
assert 'completed_left=mono_path?paired_backbuffer:paired_left;completed_right=paired_backbuffer;' in probe
assert 'mono_path=mono_view_state.load()==1;' in probe
assert 'mono_eye_frame.load()==number' in probe and 'mono_eye_frame=frames.load()+1;' in probe

# the fallback exists, runs once, restores the paired descriptor and half separation, and the status names the state
fb = between(probe, 'void mono_view_fallback(const char* reason) noexcept {', 'bool capture_texture(')
assert 'compare_exchange_strong(expected,2)' in fb and 'paired_metadata.data()' in fb and 'diagnostic_half_separation' in fb
assert 'note_recoverable(reason)' in fb
assert 'mono_view_fallback("perf-mono-view: the one engine view was not matched for 180 gameplay frames")' in probe
assert 'mono_view_fallback("perf-mono-view: engine still prepares two views")' in probe
assert '\\"mono_view\\":\\"" << (mono_view_state.load()==1?"on":mono_view_state.load()==2?"fallback":"off")' in probe
print('PASS: mono-view routing (knob, descriptor, eye-0 identity, shared source, fallback, status)')
