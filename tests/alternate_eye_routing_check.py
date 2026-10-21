"""Plan F task 2: per-frame mod work is keyed to the FIRST VIEW OF THE DISPLAYED FRAME, not to eye==0. In alternate-eye mode the engine prepares
one view per frame (always native slot 0, whichever eye it renders), so a bare `eye==0` would only be right by accident. In paired mode the
first view is eye 0, so the guard is unchanged there. Audit (every site reads centre-of-both-eyes / controller / body data, never the rendered
eye's own pose): comfort meter + authored heading, physical crouch inputs, HUD world frame, turning publish, physical grab, controller trace,
controller aim, native camera trace / left aim, weapon attachment + arm upload, crosshair, audio listener. The anchor smoother, the authored
heading reuse and head_body_anchor already compare the frame counter. The view write (pupil, orientation, projection, eye_jitter) is
per view by design and uses the logical eye (xr_eye). Source pins; the counts are a sim run."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
probe = (root / 'src' / 'dxgi_probe.cpp').read_text(encoding='utf-8')
split = (root / 'src' / 'frame_split.hpp').read_text(encoding='utf-8')
build = (root / 'tools' / 'build-msvc.cmd').read_text(encoding='utf-8')


def between(text, start, end):
    a = text.index(start)
    return text[a:text.index(end, a)]


head = between(probe, 'void prepare_head_view(void* view) noexcept {', 'void restore_head_view() noexcept')

# the guard: frame-counter compare in alternate single-view frames, eye 0 (first view) otherwise
assert 'const UINT64 per_frame_target=frames.load()+1;' in head
assert 'const bool per_frame=alt_eye>=0?alt_per_frame_done!=per_frame_target:eye==0;' in head
assert 'if(alt_eye>=0) alt_per_frame_done=per_frame_target;' in head
assert probe.count('alt_per_frame_done=0;') == 1  # reset in StartProbe
# the guard is computed before any site that uses it and after the eye match
guard_at = head.index('const bool per_frame=')
assert head.index('++head_matched;') < guard_at

# no bare eye==0 per-frame guard is left in the head-view function (the one ternary above is the paired-mode fallback)
bare = [m.start() for m in re.finditer(r'eye==0', head)]
assert len(bare) == 1 and bare[0] > guard_at and head[bare[0]-1:bare[0]] == ':', bare

# every audited site is keyed to per_frame
sites = [
    'if(per_frame) { // comfort telemetry',
    'if(per_frame && game_logic_mask()==2) {physical_drop=',
    'if(per_frame && game_logic_mask()==2 && !authored_camera) {',          # HUD world frame
    'if(native_turning_requested && per_frame && game_logic_mask()==2)',    # turning publish
    'physical_grab_requested && per_frame &&',                                # physical grab
    'if(per_frame && controller_trace_csv.is_open())',
    'controller_aim_requested && per_frame)',
    '(native_camera_trace_requested || native_left_aim_requested) && per_frame)',
    '(tracked_arms_requested || native_weapon_attachment_requested) && per_frame)',
    'if(per_frame && native_crosshair_installed)',
]
for site in sites:
    assert head.count(site) == 1, site
listener = between(head, 'head_wrote=true;SIZE_T written{};', 'constexpr auto tracked_head=')
assert 'if(per_frame) {' in listener  # audio listener

# the per-frame sites read both-eye / controller data: the centre pose and hand centre, never xr_eye (the rendered eye's own pose)
for name in ('comfort telemetry', ):
    pass
telemetry = between(head, 'if(per_frame) { // comfort telemetry', '// 6DoF: physical head motion')
assert 'xr_eye' not in telemetry and 'centre_pose' in telemetry
listener_body = between(head, 'if(per_frame) {\n            constexpr auto tracked_head', 'native_audio_listener.publish(')
assert 'xr_eye' not in listener_body and 'centre_pose' in listener_body

# frame counters that already guard once-per-frame state
assert 'if(anchor_smooth_frame!=frames.load())' in head
assert 'authored_heading_frame=frames.load()' in head

# frame-split stamps: a one-view frame is accepted on eye 0 when alternate-eye is on, and only then
assert 'alternate_on());' in between(probe, 'frame_split.feed(', 'frame_split_trace_on')
assert 'bool single_view=false' in split and 'const bool one_eye=single_view && s.eye1_begin<=0;' in split

# the begin-view hook names the view by address (slot 0 for the single view): taa maintain and the frame stamp stay slot-keyed
begin = between(probe, 'void observe_eye_begin(void* view) noexcept {', 'bool eye_begin_hook')  if 'bool eye_begin_hook' in probe else probe
assert 'mono_eye_frame=frames.load()+1;' in probe

assert 'alternate_eye_routing_check.py' in build
print('PASS: alternate-eye routing (per-frame guard, audited sites, frame split single view)')
