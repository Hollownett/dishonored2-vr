"""Flat diagnostic stereo modes (paired 3 / single 2) feed the frame split from the Present hook, and every mode's
dxgi-probe-result.json carries the frame_split block (nested {adapter, split}, like xr-live-status.json)."""
from pathlib import Path

probe = (Path(__file__).resolve().parents[1] / 'src' / 'dxgi_probe.cpp').read_text(encoding='utf-8')

# the flat feed sits before the desktop Present and reuses the XR path's stamp, previous-enter and gpu handling
feed_at = probe.index('if(!xr_requested && stereo_requested && capturing.load() && !(flags&DXGI_PRESENT_TEST)) {')
present_at = probe.index('const auto result=original_present(chain,desktop_interval,flags);')
assert feed_at < present_at
block = probe[feed_at:probe.index('}', probe.index('split_pending=true;', feed_at))]
assert 'split_prev_enter_ns=frame_split_prev_enter_ns.exchange(present_start_ns);' in block
assert 'native_gpu_requested && native_gpu_prepared' in block
# the consuming code after the Present is shared: one feed site, one adapter query
assert probe.count('frame_split.feed(') == 1 and probe.count('frame_split_adapter_json="null"') == 1
# a Present that did not feed (including XR modes outside gameplay) still resets the interval
assert 'else if(!(flags&DXGI_PRESENT_TEST)) {' in probe[present_at:present_at + 1500]
# the result file every mode writes
result_at = probe.index('std::ofstream result(output/L"dxgi-probe-result.json"')
assert '<< ",\\"frame_split\\":" << frame_split_status_json()' in probe[result_at:result_at + 600]
# eye identity: the begin hook names the eye by view address (eye_views), never by the raw index
hook_at = probe.index('void __fastcall eye_begin_hook(')
hook = probe[hook_at:probe.index('original_eye_begin(backend,world,view,index);', hook_at)]
assert 'frame_split_eyes.begin(reinterpret_cast<std::uintptr_t>(view),left_view,right_view,index,stamp_ns)' in hook
assert 'PostTonemapPass::eye_views[0]' in hook and 'PostTonemapPass::eye_views[1]' in hook
assert 'frame_split_eye_ns[index]' not in probe and 'if(index==0 || index==1) {const auto stamp_ns' not in probe
# gpu_busy is the list sum, gpu_span the elapsed span, at both feed sites
assert probe.count('last_list_sum_ms)*1e6):0;') == 2 and probe.count('split_span_ns=native_gpu_requested') == 2
assert 'split_gpu_ns,split_span_ns,&split_phase,&split_mod);' in probe
print('PASS: frame split flat-mode feed and result-file block')
