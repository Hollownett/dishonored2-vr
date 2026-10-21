"""alternate-eye (plan F task 1): source-routing pins. With the knob off nothing changes (every new branch is gated on the knob / on
GameXr::alternate_enabled, which defaults to off); with it on the one engine view is the parity eye with that eye's own pose and FOV,
only that eye's swapchain is refreshed from it, the other eye is resubmitted from its kept image with its kept pose, and a disagreement
falls back to paired for the session. The pure logic is tests/alternate_eye_check.cpp; the timing and images are a sim run."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
probe = (root / 'src' / 'dxgi_probe.cpp').read_text(encoding='utf-8')
xr = (root / 'src' / 'game_xr.hpp').read_text(encoding='utf-8')
build = (root / 'tools' / 'build-msvc.cmd').read_text(encoding='utf-8')
features = root / 'tools' / 'default_features.json'


def between(text, start, end):
    a = text.index(start)
    return text[a:text.index(end, a)]


# knob: read once in StartProbe before prepare_stereo, never a default feature, needs the mono-view prerequisites, turns mono on
assert probe.count('L"alternate-eye"') == 1
at = probe.index('L"alternate-eye"')
assert at < probe.index('            prepare_stereo();', at)
block = probe[at:at + 700]
assert 'alt_eye_state=1;mono_view_state=1;' in block and 'alt_eye_state=2;' in block and 'native_eye_capture_requested' in block
if features.exists():
    assert 'alternate-eye' not in features.read_text(encoding='utf-8')

# GameXr: everything is off by default and gated
assert 'bool alternate_enabled{};' in xr and 'int alternate_fresh_eye{-1};' in xr
assert 'if(!alternate_enabled) return rendered_mask_==3;' in xr   # the paired rule is untouched with the knob off
assert xr.count('rendered_mask_==3') == 2                           # the helper and its comment: no other direct use remains
assert 'return alternate_enabled?build_layer_views(alt_effective_(),alternate_frame,pose_epoch()).views[eye]:rendered_views_[eye];' in xr
assert 'const bool alt_stale=alternate_enabled && alternate_fresh_eye>=0' in xr
assert 'if(alternate_enabled && !alt_stale && gameplay) keep_alternate_eye_(eye,processed);' in xr
assert '&& !alt_stale?antialiasing_->apply(sources[eye])' in xr and 'if(sharpness>0 && !alt_stale)' in xr
assert 'if(!alternate_enabled || alternate_fresh_eye<0) return true;' in xr
# a stale eye is drawn from the kept copy of ITS OWN image and its layer view is its own record
assert 'if(alt_stale) sources[eye]=alt_kept_[eye].Get();' in xr
eff = between(xr, 'std::array<EyeRecord<XrView>,2> alt_effective_() const {', 'bool tracked_pair_() const')
assert 'out[e]={rendered_views_[e],alternate_frame' in eff and 'alternate_fresh_eye<0 || eye==alternate_fresh_eye' in eff
keep = between(xr, 'void keep_alternate_eye_(', '// perf-mono-view diagnostic')
assert 'record={};' in keep and 'record={rendered_views_[eye],alternate_frame,pose_epoch(),true' in keep
assert '!(rendered_mask_&(1u<<eye))' in keep   # no recorded view for this image: nothing kept
assert 'build_layer_views(alt_effective_(),alternate_frame,pose_epoch()).ok' in xr   # age and epoch gate

# probe: the plan is only a request; the view count decides; the logical eye is the frame parity, never the raw begin index
head = between(probe, 'void prepare_head_view(void* view) noexcept {', 'void restore_head_view() noexcept')
assert 'alt_eye=d2vr::AlternateEye::parity_eye(target)' in head
assert 'alternate_fallback("alternate-eye: the engine view count disagreed with the plan for 30 frames")' in head
assert '(*xr_views)[static_cast<std::size_t>(alt_eye)]' in head
assert 'if(alt_eye>=0) {' in head and 'record_rendered_eye(static_cast<UINT>(alt_eye)' in head
assert 'alt_head_stamp=((frames.load()+1)<<1)|static_cast<UINT64>(alt_eye);' in head
assert 'raw_index' not in head
# the Present: a one-view frame is ready only when the recorded view was made for THIS displayed frame
assert '(!alternate_on() || (alt_head_stamp.load()>>1)==number)' in probe
assert 'if(alternate_on() && views.size()==2) mono_path=false;' in probe
assert 'game_xr->alternate_enabled=alternate_on();game_xr->alternate_fresh_eye=alt_fresh;game_xr->alternate_frame=number;' in probe
assert probe.count('alternate_enabled') == 1 or 'game_xr->alternate_enabled' in probe

# plan + descriptor + fallback + status
after = between(probe, 'void alternate_after_present(', 'bool capture_texture(')
assert 'alt_plan.rendered(eye,number)' in after and 'alt_plan.eye_for_frame(number+1,free_gameplay,authored,epoch_changed)' in after
assert 'if(gameplay) alternate_select_descriptor(next<0);' in after
sel = between(probe, 'void alternate_select_descriptor(bool paired) noexcept {', 'void alternate_after_present(')
assert 'paired?(write_pointer() && write_separation()):(write_separation() && write_pointer())' in sel
fb = between(probe, 'void alternate_fallback(const char* reason) noexcept {', 'void alternate_select_descriptor(')
assert 'compare_exchange_strong(expected,2)' in fb and 'mono_view_fallback(reason)' in fb
assert 'alternate_fallback("alternate-eye: an eye image could not be kept")' in probe
assert '\\"alternate_eye\\":{\\"state\\":' in probe and '\\"frames\\":" << alt_frames.load()' in probe
assert 'alternate_eye_check.cpp' in build and 'alternate_eye_knob_check.py' in build
print('PASS: alternate-eye knob routing (gating, own-eye pose, kept image, view-count truth, fallback, status)')
