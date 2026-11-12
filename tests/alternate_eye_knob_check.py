"""alternate-eye (plan F task 1): source-routing pins. With the knob off nothing changes (every new branch is gated on the knob / on
GameXr::alternate_enabled, which defaults to off); with it on the one engine view is the parity eye with that eye's own pose and FOV,
only that eye's swapchain is refreshed from it, the other eye is resubmitted from its kept image with its kept pose, and a disagreement
falls back to paired for the session. The engine pipeline is one frame deep (2026-10-22 double vision): a Setup is recorded for the Present
that SHOWS its image (alternate_display_frame), promoted into GameXr at that Present, and the plan is made for that display frame; the
Present-time view count (the next frame's Setup) never gates a frame. The pure logic is tests/alternate_eye_check.cpp; the timing and
images are a sim run, and tools/alt_pose_trace_check.py joins the alt-pose-trace.csv of one."""
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
if features.exists():  # plan F task 4: a launcher row exists (enable kind, file alternate-eye); default OFF since v637 (headset v636: the eyes a frame apart doubled one eye on motion)
    import json
    rows = [f for f in json.loads(features.read_text(encoding='utf-8'))['features'] if f['file'] == 'alternate-eye']
    assert len(rows) == 1 and rows[0]['id'] == 'performance-mode' and rows[0]['kind'] == 'enable' and rows[0]['default'] is False
    assert 'selected anti-aliasing' in rows[0]['help'] and 'double' in rows[0]['label']
    assert any(f['id']=='alternate-native-taa' and f['file']=='no-alternate-native-taa' and f['kind']=='disable' and f['default'] for f in json.loads(features.read_text(encoding='utf-8'))['features'])

# GameXr: everything is off by default and gated
assert 'bool alternate_enabled{};' in xr and 'int alternate_fresh_eye{-1};' in xr
assert 'if(!alternate_enabled) return rendered_mask_==3;' in xr   # the paired rule is untouched with the knob off
assert xr.count('rendered_mask_==3') == 2                           # the helper and its comment: no other direct use remains
assert 'return alternate_enabled?build_layer_views(alt_effective_(),alternate_frame,pose_epoch()).views[eye]:rendered_views_[eye];' in xr
assert 'const bool alt_stale=alternate_enabled && alternate_fresh_eye>=0' in xr
assert 'if(alternate_enabled && !alt_stale && gameplay) {' in xr
assert '&& !alt_stale?antialiasing_->apply(processed)' in xr and 'if(sharpness>0 && !alt_stale)' in xr
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
assert 'const UINT64 alt_display=d2vr::alternate_display_frame(frames.load());' in head   # the Present that shows this Setup's image
assert 'alt_eye=d2vr::AlternateEye::parity_eye(alt_display)' in head and 'parity_eye(target)' not in head
assert 'const bool planned=(plan>>2)==alt_display;' in head
assert 'alternate_fallback("alternate-eye: the engine view count disagreed with the plan for 30 frames")' in head
assert '(*xr_views)[static_cast<std::size_t>(alt_eye)]' in head
# the view is recorded for its display frame, never written into GameXr at Setup (that would name it for the next Present)
assert 'if(alternate_on() && stereo_active.load()) { // alternate-eye: this view' in head
assert 'const int logical_eye=alt_eye>=0?alt_eye:eye;' in head
assert 'game_xr->rendered_view(static_cast<UINT>(logical_eye),{render_fov.left,render_fov.right,render_fov.up,render_fov.down},cant?&rendered_orientation:nullptr)' in head
assert 'alt_pending.record(alt_display,logical_eye,recorded,game_xr->pose_epoch(),game_xr->tracking_prediction_time(),game_xr->locate_cpu_time());' in head
assert 'record_rendered_eye(static_cast<UINT>(alt_eye)' not in head and 'alt_head_stamp' not in probe
assert 'raw_index' not in head
# the Present: this Present shows the Setup recorded FOR it; the view count the engine prepared since is the next frame's and never gates it
present_alt = between(probe, 'if(alternate_on()) {\n                                // alternate-eye: this Present shows', 'mono_path=alt_ready.single;')
assert 'alt_slot=alt_pending.take(number);' in present_alt and 'views.size()' not in present_alt
assert 'd2vr::alternate_present_readiness(alt_slot.has_value(),alt_slot?alt_slot->mask:0u,alt_slot?alt_slot->epoch:0,' in present_alt
assert 'mono_eye_frame.load()==number,paired_left_ready && native_eye_pair.complete(),alt_nonfree);' in present_alt
assert 'mono_path=alt_ready.single;pair_ready=alt_ready.ready;alt_fresh=alt_ready.fresh_eye;' in probe
assert 'if((alternate_on() || layer_pose_sync_on()) && pair_ready && alt_slot && game_xr)' in probe   # layer-pose sync promotes paired slots the same way
assert 'game_xr->record_rendered_view(slot_eye,alt_slot->view[slot_eye],alt_slot->display_time[slot_eye],alt_slot->locate_cpu_time[slot_eye]);' in probe
assert probe.count('alt_pending.record(') == 2 and probe.count('alt_pending.take(') == 3 and 'alt_pending.reset();' in probe   # + the paired layer-pose sync (own / latest slot)
# knob-off: the old readiness line is untouched (the alternate branch is an else-less if before it)
assert 'pair_ready=mono_path?(stereo_active.load() && views.size()==1 && mono_eye_frame.load()==number):' in probe
assert 'game_xr->alternate_enabled=alternate_on();game_xr->alternate_fresh_eye=alt_fresh;game_xr->alternate_frame=number;' in probe
assert probe.count('alternate_enabled') == 1 or 'game_xr->alternate_enabled' in probe

# plan + descriptor + fallback + status
after = between(probe, 'void alternate_after_present(', 'bool capture_texture(')
assert 'alt_plan.rendered(eye,number)' in after
assert 'const UINT64 next_display=d2vr::alternate_display_frame(number);' in after   # the plan is for the frame the next Setup SHOWS
assert 'alt_plan.eye_for_frame(next_display,free_gameplay,hold,epoch_changed)' in after and 'number+1' not in after  # v1219: hold = authored or scope
assert 'alt_plan_stamp=(next_display<<2)|static_cast<UINT64>(next+1);' in after
assert 'if(d2vr::alternate_install_descriptor(gameplay,next)) alternate_select_descriptor(next<0);' in after   # v791: the wheel gets two views
assert 'if(gameplay) alternate_select_descriptor(next<0);' not in after
sel = between(probe, 'void alternate_select_descriptor(bool paired) noexcept {', 'void alternate_after_present(')
assert 'paired?(write_pointer() && write_separation()):(write_separation() && write_pointer())' in sel
fb = between(probe, 'void alternate_fallback(const char* reason) noexcept {', 'void alternate_select_descriptor(')
assert 'compare_exchange_strong(expected,2)' in fb and 'mono_view_fallback(reason)' in fb
assert 'alternate_fallback("alternate-eye: an eye image could not be kept")' in probe
assert '\\"alternate_eye\\":{\\"state\\":' in probe and '\\"alternated\\":" << alt_alternated.load()' in probe
assert '\\"paired\\":" << alt_paired.load()' in probe and '\\"fallbacks\\":" << alt_fallbacks.load()' in probe
assert 'alternate_eye_check.cpp' in build and 'alternate_eye_knob_check.py' in build and 'alt_pose_trace_check.py' in build
# GameXr: the promoted view is exactly the one the Setup wrote (pose, FOV, canted orientation, locate times)
assert 'XrView rendered_view(UINT eye,XrFovf rendered_fov,const XrQuaternionf* rendered_orientation=nullptr) const {' in xr
assert 'void record_rendered_view(UINT eye,const XrView& view,XrTime display_time,std::int64_t locate_cpu) {' in xr
assert 'rendered_views_[eye]=view;rendered_mask_|=1u<<eye;' in xr
# the pose trace is a diagnostic: read once in StartProbe; every eye mode (alternate, paired, perf-mono), never a behaviour change
assert probe.count('L"alt-pose-trace"') == 1 and 'alt_pose_trace_state=fs::exists(root/L"build"/L"alt-pose-trace")?1:0;' in probe
assert 'static bool alt_pose_trace_on() noexcept {return alt_pose_trace_state.load()==1;}' in probe
assert 'if(alt_pose_trace_on() && !layer_pose_sync_on() && !(alternate_on() && stereo_active.load()))' in head   # paired / mono Setup rows (knob-off record), after the record
# fix round 1: authored / settings are read at the Present that submits the frame, not from the lagging plan; every gate is the tested function
assert 'const bool alt_nonfree=alternate_on() && !d2vr::alternate_single_view_allowed(authored_camera,game_xr && game_xr->settings_open());' in probe
assert 'std::optional<d2vr::PendingViews<XrView>::Slot> alt_slot;int alt_fresh=-1;' in probe
assert 'alternate_after_present(number,gameplay,d2vr::alternate_free_gameplay(gameplay,game_xr->settings_open()),authored_camera,alt_fresh);' in probe
assert 'if(alt_mismatch_run.note(mismatch)) alternate_fallback("alternate-eye: the engine view count disagreed with the plan for 30 frames");' in head and 'if(mismatch && mono)' in head
assert 'return alternate_pair_ready(rendered_mask_,alternate_fresh_eye,' in xr
assert 'if(alternate_may_keep(gameplay,authored_camera,settings_.open())) {\n                        keep_alternate_eye_(eye,processed);' in xr   # v802: then the next-frame diagnostic
assert 'alt_kept_ok_(authored_camera || settings_.open())' in xr and 'if(nonfree) return false;' in xr
assert 'if(!alternate_enabled && (alt_kept_[0] || alt_kept_[1])) {alt_kept_[0].Reset();alt_kept_[1].Reset();' in xr   # released after fallback
# Temporal modes require verified ownership; selected upscalers are enabled unless explicitly disabled.
# The effective mode must never replace the saved/requested preference.
assert 'taa_preference=d2vr::alternate_effective_temporal' not in probe
assert 'temporal_live_mode=d2vr::alternate_effective_temporal(alternate_on(),taa_preference,alternate_taa_requested,alternate_upscalers_requested);' in probe
assert 'd2vr::alternate_effective_temporal(alternate_on(),*requested_temporal,alternate_native_taa_allowed(),alternate_upscalers_requested)' in probe
assert 'alternate_upscalers_requested=!fs::exists(root/L"build"/L"no-alternate-upscalers");' in probe
assert 'alternate_smaa_applies(alternate_enabled,settings_.effective_temporal_aa())' in xr
assert 'const std::size_t eye_slot=d2vr::alternate_state_slot(alt_eye,eye);' in head
assert 'fsr_vfov[eye_slot]=' in head and 'jitter_counter[eye_slot]=counter;' in head and 'auto& e=eye_jitter[eye_slot];' in head
assert 'fsr_vfov[static_cast<std::size_t>(eye)]' not in head and 'jitter_counter[static_cast<std::size_t>(eye)]' not in head
print('PASS: alternate-eye knob routing (gating, own-eye pose, display-frame records, kept image, view-count truth, fallback, status)')

# the 180-frame miss counter counts only frames planned as one view; paired frames reset it (review double-vision I1)
assert 'mono_view_misses=d2vr::alternate_miss_count(static_cast<unsigned>(mono_view_misses.load()),alt_planned_single[number&1].load()!=0,pair_ready);' in probe
assert 'alt_planned_single[next_display&1]=next>=0?1:0;' in probe

# v792 stale-eye warp: knob, the companion view recorded at the Setup and handed over at its Present, the warp before the copy, the layer
# view substituted only for a warped eye, and the reticle moved with the image
assert 'stale_eye_warp_knob=!fs::exists(root/L"build"/L"no-stale-eye-warp");' in probe
assert 'alt_pending.record_companion(alt_display,static_cast<int>(other),*companion);' in probe
assert 'if(alt_slot->companion_mask&(1u<<other)) game_xr->stale_eye_target(other,alt_slot->companion[other],number);' in probe
assert 'game_xr->stale_eye_warp_enabled=stale_eye_warp_knob;' in probe
assert 'stale_warp_->apply(processed,*h);' in xr
assert xr.index('stale_warp_->apply(processed,*h);') < xr.index('} else context_->CopyResource(images_[eye].at(index).texture,eye_image);}')
# v808: a canted eye is resampled into its own located view between the warp and the copy, and submitted with that view
assert xr.index('stale_warp_->apply(processed,*h);') < xr.index('eye_image=uncant_->apply(processed,*h);') < xr.index('} else context_->CopyResource(images_[eye].at(index).texture,eye_image);}')
assert 'layers[eye].pose=uncanted_[eye]?straight_pose:' in xr and 'warp_point(uncant_back_[eye],' in xr
assert 'else if(stale_warped_(static_cast<UINT>(eye))) out[static_cast<std::size_t>(eye)].view=stale_target_[static_cast<std::size_t>(eye)];' in xr
assert 'warp_point(stale_back_[eye],ru,rv);' in xr
print('PASS: v792 stale-eye warp routing (knob, companion record and hand-over, warp before the copy, layer view, reticle)')

# v794: a Setup that planned its eye's temporal state and returned early only cancels the plan (no session-long fallback); real
# ownership failures name their site; views and hands are located for the display of their image (two periods ahead)
assert 'alternate_taa_runtime.cancel_plan();++alternate_taa_plan_cancels;' in probe
assert 'alternate_taa_runtime.cancel_plan();alternate_taa_runtime.fail(' not in probe
assert '"Alternate temporal ownership failed (site "' in probe
assert 'legacy_locate_horizon=fs::exists(root/L"build"/L"legacy-locate-horizon");' in probe
assert 'game_xr->locate_periods=legacy_locate_horizon?1:2;' in probe
assert 'locate_display_time_=frame_.predictedDisplayTime+frame_.predictedDisplayPeriod*locate_periods;' in xr
print('PASS: v794 routing (cancelled plans keep temporal AA, failure sites, two-period locate horizon)')
# v796 parallax: knob, the game cameras recorded with the views (never an authored camera) and handed over at their Present with the
# stale eye's FSR/DLSS depth, the kept camera follows the kept image, the warp gets the parallax, the reticle moves at its own distance
assert 'stale_eye_parallax_knob=!fs::exists(root/L"build"/L"no-stale-eye-parallax");' in probe
assert 'if(!authored_camera) alt_cameras.record(alt_display,logical_eye,{world,(*optical)[10],(*optical)[11],warp_grips,warp_grip_bits,warp_palms},' in probe   # v802: and the drawn hands (v811: their palms)
assert probe.count(',(*optical)[10],(*optical)[11],warp_grips,warp_grip_bits,warp_palms}') == 2 and 'warp_grips=hands.grips;warp_grip_bits=hands.hands;' in probe
assert 'warp_palms[side]=hands.hands>>side&1u?drawn_palm_frame(side,hands.grips[side]).position' in probe   # v811: the drawn palms own their pixels
assert 'alt_cameras.record_companion(alt_display,static_cast<int>(other),' in probe
assert 'game_xr->record_rendered_camera(static_cast<UINT>(alt_fresh),cameras->view[static_cast<std::size_t>(alt_fresh)],number);' in probe
assert 'game_xr->stale_eye_target_camera(other,cameras->companion[other],number);' in probe
assert 'fsr_taa.eye_depth(static_cast<int>(other),depth_w,depth_h)' in probe
assert 'game_xr->stale_eye_parallax_enabled=stale_eye_parallax_knob;' in probe
assert 'stale_parallax_warps' in probe and 'stale_parallax_misses' in probe
assert 'kept_cameras_[eye]=rendered_cameras_[eye];kept_camera_valid_[eye]=rendered_camera_frame_[eye]==alternate_frame;' in xr
assert 'stale_warp_->apply(processed,*h,&*stale_layers_[eye],' in xr
assert 'parallax_target(stale_layers_[eye]->world,ru,rv,reticle->depth)' in xr
print('PASS: v796 parallax routing (knob, cameras recorded and handed over, kept depth, kept camera, warp input, reticle distance)')

# v798 fresh layer: the fresh eye goes first, its depth and kept image reach the stale eye's warp with its camera, knob and status
assert 'const UINT eye=alternate_enabled && alternate_fresh_eye==1?1u-pass:pass;' in xr
assert 'fresh_now?&rendered_cameras_[fresh_eye]:nullptr,f(alt_records_[fresh_eye].view.fov),stale_near_range);' in xr
assert 'stale_warp_->apply(processed,*h,&*stale_layers_[eye],kept_depth,fresh_image,fresh_depth,kept_motion,fresh_motion,kept_rig,fresh_rig,stale_rig_target(eye))' in xr
assert 'fsr_taa.eye_depth(alt_fresh,depth_w,depth_h)' in probe
assert '{std::ifstream knob(root/L"build"/L"stale-near-range");' in probe
assert 'game_xr->stale_near_range=stale_near_range;' in probe
assert 'stale_fresh_warps' in probe and 'warp_hands' not in probe
print('PASS: v798 fresh-layer routing (fresh eye first, its camera, depth and image to the stale warp, knob, status)')

# v1151 movers: knob, both eyes' motion textures handed over per Present (each its last render's), parts and rules into the layers,
# the alternate object-motion fix only with the knob, status
fsr = (root / 'src' / 'fsr_taa.hpp').read_text(encoding='utf-8')
assert 'stale_eye_motion_knob=!fs::exists(root/L"build"/L"no-stale-eye-motion");' in probe
assert 'fsr_taa.eye_motion(static_cast<int>(motion_eye),motion_w,motion_h)' in probe and 'game_xr->stale_motion_rules=stale_eye_motion_knob;' in probe
assert 'd2vr::ObjectMotion::note_alternate_view(alternate_on() && mono && stale_eye_motion_knob?address:0);' in probe
assert 'stale_eye_motion_knob?receipt:std::nullopt' in probe
assert '\\"stale_motion\\":' in probe and '\\"motion_passes\\":' in probe and '\\"motion_refusals\\":' in probe
assert 'stale_layers_[eye]->motion_rules=stale_motion_rules;' in xr and 'const StaleMotion kept_motion=stale_motion(eye),fresh_motion=fresh_now?stale_motion(fresh_eye):StaleMotion{};' in xr
assert 'if(job.receipt && !job.receipt->reset && job.receipt->phase>=motion_warmup_phase && e.motion_uav.p && e.depth_srv.p && job.motion.p) {' in fsr and 'motion_warmup_phase=4;' in fsr  # v1186 warm-up
print('PASS: v1151 mover routing (knob, motion textures per eye, parts and rules, receipt-gated pass, status)')

# v1162/v1163 rig mask: knob, rig draws of the alternate one-view replayed into the rig depth (right after the engine's draw), the
# mask resolved at the copy, both-or-none masks, the mask as the motion pass's arm rule, rig draws on the engine's rows, status
rig = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
om = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
mp = (root / 'src' / 'eye_motion_pass.hpp').read_text(encoding='utf-8')
assert 'stale_eye_rig_knob=!fs::exists(root/L"build"/L"no-stale-eye-rig");' in probe
assert 'const std::uintptr_t rig_view=alternate_on() && mono && stale_eye_rig_knob?address:0;' in probe and 'd2vr::RigMask::begin_epoch();' in probe
assert 'stale_eye_rig_motion_knob=!fs::exists(root/L"build"/L"no-stale-eye-rig-motion");' in probe
assert 'const bool rig_on=alternate_on() && stale_eye_rig_knob;' in probe and 'd2vr::RigMask::enabled=rig_on;if(game_xr) {game_xr->stale_rig_masks=rig_on;game_xr->stale_rig_motion_rules=stale_eye_rig_motion_knob;}' in probe
assert probe.count('d2vr::RigMask::Scope rig_mask(context,d2vr::RenderPassMap::current_view(),d2vr::RenderPassMap::current_pass());') == 4
assert probe.count('rig_mask.after({') == 4 and probe.count('&rig_mask_draw<Binding>,&rig_mask_targets<Binding>);') == 4
assert 'game_xr->stale_eye_rig(motion_eye,rig.Get(),rig_w,rig_h,number,rig_live && stale_eye_rig_knob,fsr_taa.eye_rig_motion(static_cast<int>(motion_eye)));' in probe
assert '\\"rig_mask\\":' in probe and '\\"rig_mask_pixels\\":' in probe
assert 'if(!stale_rig_masks || eye>1 || !stale_rig_live_[eye] || stale_rig_frame_[eye]!=alternate_frame || !stale_rig_[eye]) return {};' in xr
assert 'if(!kept_rig.texture || !fresh_rig.texture) {if(kept_rig.texture || fresh_rig.texture) ++stale_rig_one_sided;kept_rig={};fresh_rig={};}' in xr
assert 'const bool view_render=RigMask::take_view_draws()>0;' in fsr and 'c->ClearRenderTargetView(rig_target.Get(),far_value);' in fsr
assert 'if(e.rig_live && e.rig_srv.p) {' in fsr and '++motion_rig_masks;' in fsr
assert '(mvScale.z>0?(rig==0 || (mvScale.w>0 && rig==255)):proj.y/(d+proj.x)>=proj.z)' in mp  # v1179: 255 = own motion, 254 = rig without
assert 'const bool own=RigMask::previous_palette(c,have_render?static_cast<int>(view.eye):-1,rows_own?view.prev_frame:~0u,rows_own,' in om and 'const bool rows_own=kind==PrevMvpTable::other_last;' in om  # v1207 (was v1204/v1206): no view needed and 'if(!own) std::memcpy(prev,cur,64);' in om  # v1166: own palette + own rows, or both current
assert 'restore_t0(); // v1166' in rig and 'k.mv_scale[2]=1;if(e.rig_motion) {k.mv_scale[3]=1;++motion_rig_moving;}' in fsr
assert 'd2vr::RigMask::skin_copied(d3d,i,reinterpret_cast<ID3D11Buffer*>(buffer),frame,' in probe and 'o[i.xy]=held?253:own?255:rig?254:0;' in rig  # v1179 ring + per-pixel validity; v1183 held class 253
assert 'if(!c || !pass_view || !enabled.load(std::memory_order_relaxed) || pass_view!=view.load(std::memory_order_relaxed)) return;' in rig
assert 'ds.DepthFunc=D3D11_COMPARISON_EQUAL;ds.StencilEnable=FALSE;' in rig and 'targets(c,rtv_count,rtvs.data(),dsv);' in rig
print('PASS: v1163 rig mask routing (knob, one-view rig draws replayed, mask at the copy, both-or-none masks, motion arm rule, raw rig rows, status)')

# v1171: rig motion vectors (default on in alternate mode since v1198, see below); off = the v1165 rig draws, no palette work, rig pixels zeroed
assert "if(mode_.load(std::memory_order_relaxed)<1 || !RigMask::motion.load(std::memory_order_relaxed)) return false;" in om
assert 'const bool rig_motion_now=RigMask::motion.load(std::memory_order_relaxed) && job.receipt' in fsr
assert 'inline static std::atomic<bool> motion{false};' in rig
assert 'pad_render_edge' not in fsr and 'edge_pad' not in fsr  # v1171: the v1168 FSR input edge replication is reverted
print('PASS: v1171 rig motion (off = v1165 rig draws), FSR input edge replication reverted')

# v1184: AFW v2 opt-in (build/afw2 -> StaleLayers::afw2 after stale_eye_layers), status and dump meta; play spaces recorded with the cameras
assert 'game_xr->stale_afw2=stale_afw2_knob;' in probe  # v1216: the knob is default-on now (pinned below)
assert 'stale_layers_[eye]->afw2=stale_afw2; // v1184: knob build/afw2' in xr
assert '\\"afw2\\":{\\"on\\":' in probe and 'meta<<",\\"afw2\\":"<<(layers.afw2?"true":"false");' in probe
assert 'alt_play.record(alt_display,logical_eye,*play,game_xr->pose_epoch(),0,0);' in probe and 'game_xr->stale_eye_target_play(other,plays->companion[other]);' in probe
print('PASS: v1184 AFW v2 knob (layers, status, meta) and play spaces with the cameras')

# v1189: one-view frames never share shadows (each eye render draws its own); knob build/alt-shadow-share restores the paired order
assert 'const int share_order=alternate_on() && !alt_shadow_share_knob?0:begin_order;' in probe and 'shadow_share.on_eye_begin(share_order,address,gate,' in probe
assert 'alt_shadow_share_knob=fs::exists(root/L"build"/L"alt-shadow-share");' in probe
print('PASS: v1189 no shadow sharing on one-view frames (knob alt-shadow-share)')

# v1190: rig target (knob stale-eye-rig-target): retargeted rig/held replay, published per render for the stale eye, t15 of AFW v2
assert 'd2vr::RigMask::target_on=rig_target_knob;' in probe  # v1198: default on in alternate mode (pin below)
assert 'game_xr->stale_eye_rig_target(other,target.Get(),tw,th,number,tv);' in probe and 'stale_layers_[eye]->rig_target=static_cast<bool>(stale_rig_target(eye).texture);' in xr
assert 'd2vr::rig_target::book().publish(vp,e,has_e,prev_vp,frame,pending_eye);' in probe  # v1199: + the eye
print('PASS: v1190 rig target routing (knob, E at Setup, replay, per-render hand-off to the stale eye)')

# v1191: GPU spans (stale warp, rig target copy, motion pass), rectangle copy of the rig target, one-view weapon placement (knob alt-weapon-place)
assert 'ModGpuScope warp_scope(context_.Get(),ModSpan::stale_warp);' in xr and 'ModSpan::rig_target' in fsr and 'ModSpan::eye_motion' in fsr
assert 'RigMask::take_target(c,depth_resource_.p,se.rig_target.p,draws,depth_w,depth_h,static_cast<unsigned>(job.eye))' in fsr
assert 'd2vr::ObjectMotion::alt_weapon_place=alt_weapon_place_knob;' in probe  # v1198: default on in alternate mode (pin below)
assert 'weapon_draw::Place::place_one_view(vp,prev_vp,cur,placed,placed_prev,own_prev,still,draw_key(s,*sh))' in om
print('PASS: v1191 GPU spans, rig target rectangle copy, one-view weapon placement (knob alt-weapon-place)')

# v1192: sim-pose-hold (simulated XR + gameplay script only): the script clock is sampled on eye-0-fresh frames, dumps only there, truth_next meta
assert 'd2vr::sim_pose_hold::on=static_cast<bool>(knob.is_open() || fs::exists(root/L"build"/L"sim-pose-hold")) && simulated_vr_requested && simulated_gameplay_script_requested;' in probe
assert 'return sim_pose_clock.sample(elapsed,change);}();' in probe and 'simulated_gameplay_script.poses(script_elapsed)' in probe and 'simulated_gameplay_script.sample(script_elapsed,' in probe
assert 'const UINT64 display=d2vr::alternate_display_frame(number);' in probe
assert 'if(d2vr::sim_pose_hold::on.load() && !d2vr::sim_pose_hold::changes(game_xr->alternate_frame,' in probe and 'if(stale_next.truth) stale_truth_next();' in probe
assert 'd2vr::sim_pose_hold::on.load()?0.f:finger_touch_params.pose_smooth' in probe
print('PASS: v1192 sim-pose-hold (sim script only, clock phase, dumps on change frames, truth_next, no finger spring)')

# v1193: one-view draws without their context's newest view take the view written into their bound b1 (buffer_views.hpp), not the newest
om2=(root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'tl_view_src_=view_source(own,alt,s.view_valid,s.view_buffer,s.cb[0],b1_known);' in om2 and 'if(valid) buffer_views_.store(r,view);' in om2
assert 'Predicate' not in (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
print('PASS: v1193 one-view draws tagged by their bound b1 view (rig target E, held mask, placement); v1191 predicate removed')

# v1194: the recording context's own snapshot of its bound b1 wins on one-view frames; view source and per-consumer use counters
assert 's->view_buffer=valid?r:nullptr;' in om2 and 'if(tl_view_src_==1 || tl_view_src_==5) {out=s.view;ok=true;}' in om2 and '\\"view_use\\":[' in om2
print('PASS: v1194 view source (own record-time b1 snapshot, b1 table, newest) and per-consumer counters')

# v1195: the drawn hands follow a head step on the same frame (both stereo modes): clamp carried with the play mapping, reach shoulders
# carried with the camera root, a body elected by agreement drawn at the clamp pose; legacy knobs restore each
clamp_src=(root / 'src' / 'hand_clamp.hpp').read_text(encoding='utf-8');vis_src=(root / 'src' / 'hand_visual_source.hpp').read_text(encoding='utf-8')
assert 's.drawn_world=carry_play(s.play,*in.play,s.drawn_world);' in clamp_src and 'carry_translation && !play_carry?' in clamp_src
assert 'in.play=d2vr::to_game_pose(d2vr::camera_relative_controller(d2vr::TrackedPose{{0,0,0},{0,0,0,1}},hand_centre,head_origin.position),head_origin,anchor,1.0);' in probe
assert 'r.shoulder=d2vr::HandClamp::carry_play(*reach_root,clamp_root,r.shoulder);' in probe
assert 'by_contact || !agreement_draws_clamp?in.body->position:in.clamp.position' in vis_src
for k in ('legacy-clamp-play-lag','reach-root-carry','legacy-body-agreement'): assert k in probe
print('PASS: v1195 drawn hands without the one-frame head lag (clamp play carry, reach shoulder carry, agreement draws the clamp pose)')

# v1196: hand-chain trace (sim-pose-hold only), hashed b1 view table, newest_why counters
assert 'if(d2vr::sim_pose_hold::on.load() && fs::exists(root/L"build"/L"hand-chain-trace"))' in probe and 'hand_trace.csv<<alt_display' in probe
assert 'template<class V,std::size_t N=1024> class BufferViews' in (root / 'src' / 'buffer_views.hpp').read_text(encoding='utf-8')
assert 'alt_newest_why_' in (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
print('PASS: v1196 hand-chain trace knob, hashed b1 view table, newest_why')

# v1197: a one-view draw whose b1 no classified write filled takes its context's own view (source 5), not the newest; the v1195 reach carry is opt-in
bv=(root / 'src' / 'buffer_views.hpp').read_text(encoding='utf-8')
assert 'if(alternate && own_valid && own_buffer) return 5;' in bv and 'tl_view_src_==1 || tl_view_src_==5' in (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'static bool reach_shoulder_carry=false;' in probe
print('PASS: v1197 own view for unclassified b1 draws; reach root carry opt-in')
# v1197: the warp's failure reason and the AFW v2 per-stage GPU times in the status
assert 'try {stale_warp_error=std::string(e.what()).substr(0,300);} catch(...) {}' in xr and '\\"stale_warp_error\\":\\""' in probe
assert '\\"afw2_stages\\":{' in probe and 'd2vr::Afw2Pass::stage_names[k]' in probe
print('PASS: v1197 stale_warp_error and perf_gpu_ms.afw2_stages')

# v1198: the stale-eye hand/held fixes default on in performance mode only (alt_eye_state==1), each with a no-<name> opt-out;
# the old opt-in file names are no-ops; afw2 stays opt-in; the effective state is in alternate_eye.features
assert 'const bool alt_defaults=alt_eye_state.load()==1;' in probe
assert probe.index('{alt_eye_state=1;mono_view_state=1;}') < probe.index('const bool alt_defaults=alt_eye_state.load()==1;')
assert 'd2vr::RigMask::motion=alt_defaults && stale_eye_rig_motion_knob;' in probe
print('PASS: v1198 rig motion default on in alternate mode (no-stale-eye-rig-motion: off, with the warp rig rules)')
assert 'd2vr::SkinMotion::enabled=alt_defaults && !fs::exists(root/L"build"/L"no-stale-eye-skin-motion");' in probe
print('PASS: v1198 skin motion default on in alternate mode (no-stale-eye-skin-motion)')
assert 'd2vr::ObjectMotion::held_mask=alt_defaults && !fs::exists(root/L"build"/L"no-stale-eye-held-mask");' in probe
print('PASS: v1198 held mask default on in alternate mode (no-stale-eye-held-mask)')
assert 'rig_target_knob=alt_defaults && !fs::exists(root/L"build"/L"no-stale-eye-rig-target");d2vr::RigMask::target_on=rig_target_knob;' in probe
print('PASS: v1198 rig target default on in alternate mode (no-stale-eye-rig-target)')
assert 'alt_weapon_place_knob=alt_defaults && !fs::exists(root/L"build"/L"no-alt-weapon-place");d2vr::ObjectMotion::alt_weapon_place=alt_weapon_place_knob;' in probe
print('PASS: v1198 one-view weapon placement default on in alternate mode (no-alt-weapon-place)')
for k in ('L"stale-eye-rig-motion"','L"stale-eye-skin-motion"','L"stale-eye-held-mask"','L"stale-eye-rig-target"','L"alt-weapon-place"'): assert k not in probe, k
assert 'stale_afw2_knob=alt_defaults && !fs::exists(root/L"build"/L"no-afw2");' in probe  # v1216: was opt-in
assert '\\"features\\":{\\"rig_motion\\":' in probe and '\\"alt_weapon_place\\":' in probe
print('PASS: v1198 old opt-in names are no-ops, afw2 opt-in, alternate_eye.features status')

# v1199: one-view draws take a view only when certain (the b1 they bind was written in their own pass job: buffer_views.hpp view_certain),
# else the draw-level consumers fall back; the rig target is two hypothesis slices taken by the render's eye; build/alt-view-heuristic = v1197
om = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
rpm = (root / 'src' / 'render_pass_map.hpp').read_text(encoding='utf-8')
rm = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
rt = (root / 'src' / 'rig_target.hpp').read_text(encoding='utf-8')
assert 'const bool job_certain=view_certain(s.view_valid,s.view_buffer,s.cb[0],s.view_job,job,s.type==1);' in om  # v1200: now the cross-check
assert 'if(alt && !alt_view_heuristic.load(std::memory_order_relaxed)) {' in om and 'L"alt-view-heuristic"' in om
assert 's->view_job=0;if(const auto source=pass_source_.load(std::memory_order_relaxed)) s->view_job=source().job;' in om
assert 'current_job_=job_serial_.fetch_add(1,std::memory_order_relaxed)+1;' in rpm and rpm.count('current_job_=previous_job;') == 3
assert 'd2vr::RenderPassMap::current_job()}' in probe
assert 'ok[h]=rig_target::book().latest_of(h,e,vp);' in om and 'bool latest_of(unsigned eye,M4& e,float* vp=nullptr)' in rt
assert 'd2vr::rig_target::book().publish(vp,e,has_e,prev_vp,frame,pending_eye);' in probe
assert 'c->CopySubresourceRegion(out,0,0,0,0,trg.Get(),slice,&box);' in rm and 'a.ArraySize=2;' in rm
assert '\\"certainty\\":{' in om and '\\"hypothesis_check\\":[' in om
assert '\\"rig_target_state\\":{\\"none\\":' in probe and '\\"rig_world_share\\":{\\"n\\":' in probe
print('PASS: v1199 certain one-view draw views, two-slice rig target, afw2 rig_target_state/rig_world_share status')

# v1200: the one-view draw's view by its rows' content (buffer_views.hpp rows_shear/pick_by_shear against every recorded eye view), the
# Setup-end final snapshot feeds the candidates; content status under alternate_eye.object_motion.content; build/alt-view-content-tol
assert 'const bool by_content=rows && content_view(rows,out,prev_rows);' in om and 'const bool certain=by_content;' in om  # v1201: content only
assert 'early_ok=gather_current(s.layout,sh->data,sh->size,zn,early);' in om and 'const bool have_view=current_view(s,view,content_wanted?early:nullptr,early_prev_ok?early_prev:nullptr);' in om
assert 'd2vr::ObjectMotion::note_alternate_render(*alternate_receipt);' in probe  # v1206 (was v1200 note_setup_final)
assert 'L"alt-view-content-tol"' in om and '\\"content\\":"+content_json()' in om
bv = (root / 'src' / 'buffer_views.hpp').read_text(encoding='utf-8')
assert 'inline double rows_shear(const ProjectionXY& p,const float* rows) noexcept {' in bv and 'inline int pick_by_shear(' in bv
print('PASS: v1200 one-view draw views by content (rows vs each recorded render projection)')

# v1201: same-eye ties name the newest render (pick_by_shear_eyes), cross-eye ties ambiguous; certainty [certain, fell back, n/a] + why
assert 'const int pick=pick_by_shear_eyes(shear,eyes,frames,tl_cand_n_,' in om and 'inline int pick_by_shear_eyes(' in bv
assert 'enum CertOutcome : unsigned {cert_certain,cert_fallback,cert_na,cert_outcomes};' in om and '_fallback_why' in om
assert 'certainty(cert_placement,held_any()?cert_fallback:cert_na);' in om
print('PASS: v1201 same-eye ties, content-only certainty, applicable-draw certainty counts')

# v1202: one-view eye 1 jitter starts half a sequence later (neighbouring renders never share a projection); none-fit diagnostics
jit = (root / 'src' / 'dlss_jitter.hpp').read_text(encoding='utf-8')
assert 'inline std::uint32_t alternate_jitter_index(std::uint32_t phase,int eye,std::uint32_t phases) noexcept {' in jit
assert 'if(alternate_taa_phase) counter=d2vr::alternate_jitter_index(*alternate_taa_phase,alt_eye,dlss_owned?jitter_phases.load(std::memory_order_relaxed):8u);' in probe
assert '\\"none_fit_by_kind\\":{' in om
print('PASS: v1202 one-view jitter offset per eye, none-fit diagnostics')

# v1203: candidates = newest two renders per eye, shear fit 1e-5, Gram test (previous rows) for skewed models
assert 'if(newer>=2) continue;' in om and 'content_fit_{1e-4}' in om  # v1205: back to 1e-4
assert 'inline bool rows_gram(const ProjectionXY& p,const float* rows,double g[6]) noexcept {' in bv
assert 'const bool by_content=rows && content_view(rows,out,prev_rows);' in om and '\\"certain_gram\\":' in om
print('PASS: v1203 newest-two candidates, fit 1e-5, Gram test for skewed models')

# v1204: every rig draw is chained and recorded (fix_any) whether or not its view is certain; rig_zero_why status
assert 'kind=table_.fix_render(view.eye,view.frame,view.prev_frame,cur,prev,false);' in om  # v1206 (was v1204 fix_any)
assert '\\"rig_zero_why\\":[' in om
print('PASS: v1204 rig motion independent of the per-draw view, rig_zero_why')

# v1205: uncertain draws keep the v1197 heuristic view as the chain label (flagged); render-identity consumers need have_content; ring
# palette only for the draw's own render; raw-rows per-render status
assert 'tl_view_heuristic_=!certain;' in om and 'const bool have_content=have_view && !(alternate() && tl_view_heuristic_);' in om
assert 'if(near_draw && one_view_place && have_content) {' in om and 'if(alt && !apply && kind!=PrevMvpTable::same && kind!=PrevMvpTable::own_last) note_raw();' in om
rm2 = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
assert 'if(frame==~0u || r.tag[k].load(std::memory_order_acquire)!=frame)' in rm2  # v1206 (was v1205 r.last)
print('PASS: v1205 heuristic chain labels for uncertain draws, content-only render consumers, ring per own render, raw-row status')

# v1206: alternate records only from the receipt finish() publishes (final VP + previous render); previous rows and the rig palette from
# exactly the receipt's previous render (fix_render, tagged palette ring); uncertain draws zero; per-render status ring + probe rows
ant = (root / 'src' / 'alternate_native_taa.hpp').read_text(encoding='utf-8')
ats = (root / 'src' / 'alternate_taa_state.hpp').read_text(encoding='utf-8')
assert 'std::optional<Receipt> finish(std::uintptr_t view) {' in ant and 'p.previous_counter = eye.last->key.native_counter;' in ats
assert 'const auto alternate_receipt=alternate_taa_runtime.finish(reinterpret_cast<std::uintptr_t>(view));' in probe
assert 'static void note_alternate_render(const alternate_taa::Receipt& r) noexcept {' in om and 'if(old.receipt && !v.receipt) return;' in om
assert 'snapshot(static_cast<unsigned>(eye),alt_view)' not in om  # no mid-frame alternate snapshots
assert 'else if(view.receipt && view.has_prev) kind=table_.fix_render(view.eye,view.frame,view.prev_frame,cur,prev,true);' in om
assert 'if(alt && (!have_content || !view.receipt) && !alt_view_heuristic.load(std::memory_order_relaxed) && have_prev) {' in om
assert 'mul(rec.has_prev?rec.prev_vp:rec.m[2],inverse_own,out.camera);' in om and 'if(!alt && !apply && mode_.load(std::memory_order_relaxed)>=1' in om
assert 'static constexpr unsigned gens=8,' in om and '\\"renders\\":"+renders_json()' in om
assert 'const unsigned k=frame%rig_ring_slots;' in rm2 and 'inline constexpr unsigned rig_ring_slots=8;' in rm2
assert '\\"object_motion\\":"<<d2vr::ObjectMotion::render_json(receipt->key.display);' in probe
print('PASS: v1206 receipt records, exact previous render for rows and rig palette, uncertain zero, per-render status')

# v1207: the rig without a content view takes the render its chain proves (prove_render); probed rows pinned; GPU census of wild rig MVs
assert 'if((!have_content || !view.receipt) && std::memcmp(cur,prev,64)) chained=rig_render_by_chain(prev,view);' in om
assert 'int prove_render(const RenderKey* r,int n,const float* prev) const noexcept {' in om and 'd2vr::ObjectMotion::pin(receipt->key.display);' in probe
rm3 = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
fsr3 = (root / 'src' / 'fsr_taa.hpp').read_text(encoding='utf-8')
assert 'if(wild) {o[i.xy]=252;InterlockedAdd(g[3],1);}' in rm3  # v1209 (was v1207 256 px) and 'RigMask::note_mask(d,n[0],n[2],n[3]);' in fsr3
print('PASS: v1207 rig render by chain, pinned probe rows, wild rig MV census')

# v1208: rig slots from the uploaded CPU palette (never copied from a dynamic wrapper), wrapper fingerprint, MV-target applicability
rm4 = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
assert 'static void fingerprint(ID3D11DeviceContext* c,unsigned model,ID3D11Buffer* current,ID3D11Buffer* other)' in rm4  # v1211: slots now come from the drawn palette (pin below)
assert 'd2vr::ObjectMotion::note_mv_texture(b.res[1]);' in probe and 'cpu_ok?bytes.data():nullptr,cpu_ok?bd.ByteWidth:0u,copied!=0,' in probe
print('PASS: v1208 rig palette slots from the uploaded CPU palette, fingerprint, MV-target applicability')

# v1209: same-render guard of wild rig vectors (mask 252 -> zero MV before the motion pass and the upscaler), rig draw forensics, draw views
rm5 = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
fsr5 = (root / 'src' / 'fsr_taa.hpp').read_text(encoding='utf-8')
assert 'static bool guard(ID3D11DeviceContext* c,ID3D11Resource* mv,ID3D11ShaderResourceView* mask,UINT w,UINT h) noexcept {' in rm5
assert 'if(e.mv_key==job.motion.p) RigMask::guard(c,job.motion.p,e.rig_srv.p,depth_w,depth_h);' in fsr5
assert fsr5.index('RigMask::guard(c,job.motion.p') < fsr5.index('dispatched=motion_pass_.dispatch(')
print('PASS: v1209 rig guard, forensics, draw views')

# v1210: nested draw hooks are one draw (inner RigMask/ObjectMotion scopes inert), draw-time probe of rig slots and rows, other-view MV writers n/a
rm6 = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
om6 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'nested_=tl_scope_depth_++>0;' in rm6 and '~Scope() {--tl_scope_depth_;if(nested_) return;' in rm6 and 'void after(const DrawArgs& args,DrawFn draw,TargetsFn targets) noexcept {\n            if(nested_) return;' in rm6
assert 'nested_(RigMask::nested_draw()),s_(installed_ && !nested_?before(c):nullptr)' in om6 and 'if(nested_) return;\n            SkinMotion::restore(c_);' in om6
assert 'probe_on=rig_on && d2vr::RigMask::motion.load() && rig_forensics_knob;' in probe and 'RigMask::probe_arm(view.display,want,sh->size,' in om6  # v1216: was diag::on()
assert 'v && v!=alt_view_.load(std::memory_order_relaxed)) {' in om6
print('PASS: v1210 nested draw hooks, draw-time rig probe, other-view MV writers')

# v1211: a rig ring slot holds the current palette its render's rig draw binds (copied at the draw), never the skin-copy hook's upload
rm7 = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
om7 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'c->CopyResource(r.slot[k].Get(),cur.Get());' in rm7 and 'UpdateSubresource(r.slot' not in rm7
assert 'if(have_render) RigMask::capture_drawn(c,view.frame,' in om7
print('PASS: v1211 rig ring slots from the drawn current palette')
# v1212: a rig slot is taken only with the rows its capturing draw drew (one draw of the previous render), wild-render dumps + skin log,
# draws recorded while no eye render is published are not applicable
rm8 = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
om8 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'if(rows && r.rows[k]!=rows) {pair_rows_mismatch_.fetch_add(1,std::memory_order_relaxed);return nullptr;}' in rm8 and 'r.rows[k]=rows;' in rm8
assert 'rows_own,rows_own?hash64(prev)|1u:0u,' in om8 and 'RigMask::capture_drawn(c,view.frame,hash64(cur)|1u);' in om8
assert 'if(o==cert_fallback && tl_content_why_==5) o=cert_na;' in om8 and 'now-newest>PrevMvpTable::gens;' in om8
assert '!image_skin_enabled || wrote,race,skin_why,' in probe and 'dump(p,"wild");' in rm8
print('PASS: v1212 rig pair from one draw, wild-render dumps, no-render draws n/a')
# v1213: VR arms never show the engine pose after a tick fallback (kind 3: image or held image), VR-pose switch = zero motion, wild bound = render width
rm9 = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
om9 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'd2vr::image_skin_plan(kind,full_body.image_fresh(now),true);' in probe
assert 'r.vr[n]!=r.vr[k]) {skin_switch_.fetch_add(1,std::memory_order_relaxed);return nullptr;}' in rm9 and 'const float b=k.z;' in rm9
assert om9.index('RigMask::capture_drawn(c,view.frame,hash64(cur)|1u);') < om9.index('const bool own=RigMask::previous_palette(')
print('PASS: v1213 held VR arms, skin switch zero motion, wild bound')
# v1214: no hold over an arm the game owns on purpose (authored clip / player state) or outside arm-tracking logic; guard renders filed by cause
rm10 = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
assert '!((authored_arm_bits.load(std::memory_order_relaxed)>>image_slot)&1u) && d2vr::rig::wheel_tracks_arms(upload_logic,dual_wield_on());' in probe
assert 'const int kind=submitted!=source?image_kind:vr_gap?3:0;' in probe and probe.index('static std::atomic<unsigned> authored_arm_bits{};') < probe.index('const bool vr_gap=')
assert 'guard_cause_[b&1u?0:b&6u?1:2].fetch_add(1,std::memory_order_relaxed);' in rm10
print('PASS: v1214 hold only over unintended refusals, guard causes')
# v1216: AFW v2 default on in performance mode (build/no-afw2: off; build/afw2 = no-op), effective state in alternate_eye.features;
# the rig forensics (draw-time probe, rig-dump-*.bin, image-skin log) only with build/rig-forensics, never with diagnostics alone
assert 'stale_afw2_knob=alt_defaults && !fs::exists(root/L"build"/L"no-afw2");' in probe and 'L"afw2"' not in probe
assert probe.index('const bool alt_defaults=alt_eye_state.load()==1;') < probe.index('stale_afw2_knob=alt_defaults')
assert '\\"alt_weapon_place\\":' in probe and '<< ",\\"afw2\\":" << (stale_afw2_knob?"true":"false") << \'}\'' in probe
print('PASS: v1216 AFW v2 default on in performance mode (no-afw2), features.afw2')
rm11 = (root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
assert 'rig_forensics_knob=fs::exists(root/L"build"/L"rig-forensics");' in probe and 'diag::on(); // v1210' not in probe
assert 'if(probe_on.load(std::memory_order_relaxed)) { // v1212 skin log' in rm11
assert 'if(d2vr::RigMask::probe_on && d2vr::RigMask::dump_dir.empty())' in probe and 'if(!probe_on.load(std::memory_order_relaxed) || display==~0ull' in rm11
print('PASS: v1216 rig forensics only with build/rig-forensics')
# v1219: the spyglass scope holds paired frames in performance mode (the warp cannot rebuild the narrowed right view; the mask needs a fresh
# right image); the plan's authored flag carries it, so the first free frame after the scope is paired too; status alternate_eye.scope_held
ae = (root / 'src' / 'alternate_eye.hpp').read_text(encoding='utf-8')
assert 'inline bool alternate_plan_paired_hold(bool authored, bool scope_enabled, float scope_m) noexcept {return authored || (scope_enabled && scope_m > 1.f);}' in ae
ap = between(probe, 'void alternate_after_present(', '\nbool capture_texture(')
assert 'const bool hold=d2vr::alternate_plan_paired_hold(authored,spyglass_scope_enabled,spyglass_scope_m.load(std::memory_order_relaxed));' in ap
assert 'const int next=alt_plan.eye_for_frame(next_display,free_gameplay,hold,epoch_changed);' in ap
assert '<< ",\\"scope_held\\":" << alt_scope_held.load()' in probe
print('PASS: v1219 spyglass scope holds paired frames in performance mode')
# v1218: a placed (held) draw's previous rows are the rows this eye's previous render submitted for it (both modes), else camera-only; build/no-placed-history: off
om18 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'placed_history.store(!std::filesystem::exists(knobs_/L"no-placed-history",error),std::memory_order_relaxed); // v1218' in om18
assert 'inline static std::atomic<bool> placed_history{true};' in om18
assert om18.count('table_.placed_previous(placed_,') == 2
print('PASS: v1218 placed draws keep their submitted previous rows (no-placed-history)')
# v1223: a held thing's draws are placed at B * its settled rigid local transform (RigidLocal: settle/outlier/follow, folded per publish); build/no-rigid-local: off
wd23 = (root / 'src' / 'weapon_draw_place.hpp').read_text(encoding='utf-8')
om23 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'weapon_draw::RigidLocal::on.store(!std::filesystem::exists(knobs_/L"no-rigid-local",error),std::memory_order_relaxed); // v1223' in om23
assert 'RigidLocal::fold(s,raw[s] && side_reach[s]>0?ids[s]:0);' in wd23 and 'out.rigid=side_reach[s]>0;' in wd23
assert wd23.count('rigid_part(w,static_cast<unsigned>(candidate),matched,draw_key,frame,') == 2  # v1237: the matched frame (F, previous read, child)
assert om23.count('draw_key(s,*sh)') == 3 and '<<",\\"rigid_local\\":"<<weapon_draw::RigidLocal::json()' in om23
print('PASS: v1223 held things placed at B * rigid L (no-rigid-local)')
# v1226: an unproven held draw keeps the predicted previous (previous binding * rigid L), never camera-only; status per pass/eye prev_by_eye, prev_fail
om26 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'std::memcpy(placed_prev,proven?own:still,64);' not in om26 and 'if(proven) {note_resid(0,own,placed_prev);std::memcpy(placed_prev,own,64);}' in om26
assert 'if(own_prev) std::memcpy(placed_prev,own,64);else if(!predicted_own) std::memcpy(placed_prev,still,64);' in om26
assert '",\\"prev_by_eye\\":["' in om26 and '",\\"prev_fail\\":["' in om26
print('PASS: v1226 held previous falls back to the rigid prediction (prev_by_eye, prev_fail)')
# v1226: a paired right-eye draw whose b1 names eye 1 takes that eye's own record of the frame (latest_ is never published for eye 1 by classify)
om26b = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert '(frame==newest && record_view(static_cast<unsigned>(key),frame,out));' in om26b and 'static bool record_view(unsigned eye,std::uint32_t frame,View& out) noexcept {' in om26b
assert '<<",\\"paired_eyes\\":{\\"left\\":["' in om26b
print('PASS: v1226 paired right-eye draws tagged from the eye record (paired_eyes, record_views)')
# v1231: RigidLocal settles by consensus (a value recurring 3 times in a window, most hits), follows only when the settled value stopped recurring;
# status in_step per publisher path (physical / use / bind)
wd31 = (root / 'src' / 'weapon_draw_place.hpp').read_text(encoding='utf-8')
assert 'static constexpr unsigned settle_n=3,early_n=2,cands=6,window=45,' in wd31 and 'if(c.renders-c.settled_last>window)' in wd31
assert probe.count('held_placed(hand,d2vr::weapon_draw::RigidLocal::path_') == 3  # v1237: through held_placed (census kind + children)
print('PASS: v1231 rigid local by consensus, in_step per publisher path')
# v1232/v1235: OfferedChokeGrip - a held `other` squeeze takes a later offer only for the NPC its press was on (press_npc, never refused at the press);
# a ready re-press takes the running game choke only when this owner chose that victim (click -> release -> press again)
vc32 = (root / 'src' / 'vr_controls.hpp').read_text(encoding='utf-8')
assert 'role_==Role::other && offered_victim && offered_victim!=refused_ && offered_victim==pressed_npc_?offered_victim:0u;' in vc32
assert '(offered_victim?offered_victim:active_victim && active_victim==choked_?active_victim:0u)' in vc32 and '{role_=Role::choke;victim_=choked_=entry;}' in vc32
assert 'else if(role_==Role::ready) {role_=Role::other;refused_=offered_victim;pressed_npc_=press_npc;}' in vc32
assert 'control_active_choke,choke_occupied,(cs?raw.right_trigger:raw.left_trigger)>.35f,choke_press_npc);' in probe
print('PASS: v1235 combat choke: later offer only for the pressed NPC, re-press takes back its own choke')
# v1234: a render with no composition names its holder (Binder::observe 5-arg): a different holder drops G, never the old weapon's grip on the new one
# (dual-wield toss/swap); sanity is per hold (Binder::idle when the hand holds nothing)
wd34 = (root / 'src' / 'weapon_draw_place.hpp').read_text(encoding='utf-8')
assert 'if(!candidate.identity) {candidate.identity=held;candidate.source=held_source;candidate.space=held_space;}' in wd34 and 'if(s.sanity_idle) {s.sanity_idle=false;' in wd34
assert 'd2vr::weapon_draw::Binder::observe(hand,candidate,identity,held_source,held_space);' in probe and '{d2vr::weapon_draw::Binder::idle(hand);continue;}' in probe
print('PASS: v1234 binder drops G for a different holder; sanity per hold')
# v1236 (v1233's 1300 ms live-offer hold removed): a downed body under the hand (live Use target or grab focus) shows its own last kept prompt art
# with no time limit; status body_prompt first_ms_max / cached_frames / fresh_frames / hides_by
hd36 = (root / 'src' / 'native_hud_child_draw.hpp').read_text(encoding='utf-8')
assert 'live_offer' not in hd36 and 'return takedown_slot?1300:std::max<UINT64>(kept,250);' in hd36
assert 'inline BodyPromptMiss body_prompt_cached(std::uintptr_t hand_body,bool carrying,bool kind_art,bool other_prompt_drawn,bool anchor) noexcept {' in hd36
assert 'if(slot==0 && body_prompt_from_cache(now,destination,live_scope)) return;' in probe and 'body_prompt_keep(context,compact,art_owner,point,art_width,depth,now);' in probe
assert 'native_body_prompt_stats.update(now,hand_body(now),shown,' in probe and 'prompt_live_holds' not in probe
ia36 = (root / 'src' / 'native_interaction_aim.hpp').read_text(encoding='utf-8')
sp36 = (root / 'src' / 'selection_policy.hpp').read_text(encoding='utf-8')
assert 'return part?hand_pickup_in_range(probe,part->first,part->second):hand_pickup_in_range(probe,part_point,part_point);' in sp36
assert 'body_los_verdict_.store(native_ok?1u:on?2u:focused?3u:4u,std::memory_order_relaxed);' in ia36 and 'if(on && !native_ok) {qualified_result=result&~std::uintptr_t{0xff};if(output)*output=0;++body_hand_on_passes_;}' in ia36
assert ',\\"use_refused\\":{\\"other_owner\\":' in probe
print('PASS: v1236 body prompt from its kept art while the body is under the hand; a downed body passes the hand-on-it LOS rule at its grabbed part')
# v1237: held things - children of the held entity (+0xb0/+0xb8) are matched with the thing's delta; RigidLocal recycles the stalest cluster
# instead of refusing new draws; census per kind (mixed / no_mv / near-unplaced) and per pass near_unplaced
wd37 = (root / 'src' / 'weapon_draw_place.hpp').read_text(encoding='utf-8')
om37 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'for(auto& c:cs) if(c.active && c.age>=2 && (!free_slot || c.age>free_slot->age)) free_slot=&c;' in wd37
assert 'static const Mat& frame_of(const Side& w,int h) noexcept {return h<=0?w.model:h==1?w.alt_model:h<hist0?w.extra[static_cast<std::size_t>(h-2)]:w.hist[static_cast<std::size_t>(h-hist0)];}' in wd37  # v1239: + earlier reads
assert 'if(!has_frame(w,h) || (h && !(w.bound && pass_eye_ok))) continue;' in wd37 and 'enum Path:unsigned {path_unknown,path_physical,path_use,path_bind,path_severed};' in wd37
assert om37.count('weapon_draw::Place::census(r,r.side>=0 && have_prev)') == 2 and '<<",\\"held_census\\":"<<weapon_draw::Place::census_json()' in om37
assert 'if(read_self(thing[hand]+0xb0,&list,sizeof list) && list && read_self(thing[hand]+0xb8,&count,sizeof count) && count<=64) {' in probe
assert 'd2vr::weapon_draw::Place::stage(hand,d2vr::weapon_draw::Place::kind_weapon);' in probe and 'd2vr::weapon_draw::Place::stage(hand,kind,kids.data(),n);' in probe
print('PASS: v1237 held children placed with the thing, cluster recycling, held census')
# v1238: the sticking hold feeds the goal's own velocity (hand * relative; build/legacy-hand-feed: the hand's); held things' shadow-map draws are
# placed with them (view+0x7510 of the pass view; build/no-held-shadow-place); status hold_sync (motor lag / drawn-hand gap by player speed)
pg38 = (root / 'src' / 'physical_grab.hpp').read_text(encoding='utf-8')
ng38 = (root / 'src' / 'native_grab.hpp').read_text(encoding='utf-8')
om38 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'const auto feed=!now?hand_feed:carry+(hand_feed-carry)*fade;' in pg38 and 'Vec3 feed_velocity() const {return goal_feed && goal_ok_?goal_velocity_:velocity_;}' in pg38
assert 'PhysicalGrab::goal_feed=!std::filesystem::exists(root/L"build"/L"legacy-hand-feed",ec);' in ng38 and 'hold_sync().motor(' in ng38
assert 'if(read_(pass_view+0x7510,lvp,64)) {' in om38 and 'held_shadow_place.store(!std::filesystem::exists(knobs_/L"no-held-shadow-place",error),std::memory_order_relaxed); // v1238' in om38
assert '<< ",\\"hold_sync\\":" << d2vr::hold_sync().json()' in probe and 'd2vr::hold_sync().drawn(kind,player_speed,' in probe
print('PASS: v1238 goal-velocity feed, held shadow placement, hold_sync status')
# v1239: a held thing's draw matches any of the publisher's last 8 distinct reads (exact, sampled) or, with a settled L, is predicted
# (raw_draw = m * inverse(L) near the latest read; resting twins and taken clusters refused; never a sample); status held_census.<kind>.match
wd39 = (root / 'src' / 'weapon_draw_place.hpp').read_text(encoding='utf-8')
assert 'static Prediction predict(unsigned side,std::uint64_t key,std::uint32_t frame,const Mat& m,double gate,double gate_rad,RefOf&& ref_of) noexcept {' in wd39
assert 'out.gate_m=std::min(.05+mps*.05,out.reach);' in wd39 and wd39.count('predict_place(*slot,full(') == 2
assert 'const Mat x=predicted?predicted_x:rigid?mul(matched,l):m;' in wd39 and '\\"match\\":"+match_json(k)' in wd39
print('PASS: v1239 held things matched through the read ring and the predicted rigid match')
# v1240: a finite oversized hand-body speed/spin read-back retires and re-creates the body (wild_motion; max_wild_motion then the latch);
# a held severed part's placed draws per key / pass with the placed L's rigidity and the per-render L spread between keys (severed_draws)
hb40 = (root / 'src' / 'hand_body.hpp').read_text(encoding='utf-8')
assert 'count(side,&Counters::wild_motion);' in hb40 and 'static constexpr std::uint64_t max_wild_motion=8;' in hb40
assert hb40.index('count(side,&Counters::wild_motion);') < hb40.index('servo_step(s.servo,')
wd40 = (root / 'src' / 'weapon_draw_place.hpp').read_text(encoding='utf-8')
om40 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert wd40.count('note_l(matched,') == 2 and om40.count('weapon_draw::Place::severed_draw(r,draw_key(s,*sh),s.layout,sh->size,') == 2
assert '<<",\\"severed_draws\\":"<<weapon_draw::Place::severed_json()' in om40
print('PASS: v1240 hand-body wild-motion recovery and the severed_draws diagnostic')
# v1241: sim fixture "grab left|right [radius] [name]" (simulated XR only) binds the nearest native_rigid_body prop through the ordinary hold
# (seat_of, PhysicalGrab::bind, take_hold); held parts place every pass with one canonical L through one product VP*(B*L) (pass_spread_renders)
ng41 = (root / 'src' / 'native_grab.hpp').read_text(encoding='utf-8')
wd41 = (root / 'src' / 'weapon_draw_place.hpp').read_text(encoding='utf-8')
assert 'if(verb=="grab") {' in probe and 'if(!simulated_vr_requested) return "grab requires simulated XR";' in probe
assert 'native_grab.fixture_active(h)' in probe and 'native_grab.take_fixture_said()' in probe
assert ng41.count('take_hold(lane,side,*body,now);') == 2 and ng41.count('seat_of(*body,p,natural)') == 2 and 'fixture_reel(lane,side,dt);' in ng41
assert 'static Mat canonical(unsigned side,std::uint32_t frame,unsigned ref,const Mat& l) noexcept {' in wd41
assert wd41.count('rigid_now(w,vp,lref,l)') == 2 and wd41.count('RigidLocal::note_placed(') == 2 and '\\"pass_spread_renders\\":' in wd41
print('PASS: v1241 grab fixture and one bitwise L across passes')
# v1242: downed-body prompt art kept per kind (Carry / with Loot) and shown on any downed body under the hand; the kind cache file survives sessions
assert 'auto& k=body_kind_art[body_prompt_kind(owner,now)];' in probe and 'output_directory.always()/L"cache"/L"prompt-kind-art.bin"' in probe
assert 'inline std::optional<std::vector<PromptKindArt>> decode_prompt_kind_art(const std::vector<std::byte>& in) {' in hd36
assert ',\\"kind_frames\\":' in probe and ',\\"kind_cache_loaded\\":' in probe and ',\\"kind_cache_saved\\":' in probe
print('PASS: v1242 body prompt kind art, kind cache file, either hand focus in the hand-on pass')
# v1243: hand_lock = a held thing's pose in the DRAWN hand's joint-59 frame per render (arm draw composition vs the GPU-copy frame its image
# palette was written in), by player/hand speed, unplaced weapons as the reference; placed_view_frame split [same, +1, -1, other]
wd43 = (root / 'src' / 'weapon_draw_place.hpp').read_text(encoding='utf-8')
om43 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'class HandLock {' in wd43 and 'const Mat drawn=mul(mul(m,inv_live),h.j59);' in wd43
assert 'd2vr::weapon_draw::HandLock::note_copy(slot,d2vr::weapon_draw::Place::latest_frame(),' in probe and 'wd::HandLock::note_publish(hand,arm_side,engine_frame,kind,id,weapon_mat(*j),thing,' in probe
assert 'weapon_draw::HandLock::on_draw(lf,weapon_draw::mul(hv->inv[pass_eye],weapon_draw::widen(cur)));' in om43
assert '<<",\\"hand_lock\\":"<<weapon_draw::HandLock::json()' in om43 and 'view.frame==frame+1?1:view.frame+1==frame?2:3' in om43
print('PASS: v1243 hand_lock measurement and placed_view_frame split')
# v1244: Timepiece black screen - an unpaired gameplay run goes flat (theater path), never black; a same-eye repeated Begin (lens pass hypothesis)
# no longer breaks the pair (build/strict-eye-pair: v1243 rule); pair-failure census. Body grab: an aged hover press re-asks as a hover request.
gs44 = (root / 'src' / 'game_stereo.hpp').read_text(encoding='utf-8')
fh44 = (root / 'src' / 'xr_frame_hardening.hpp').read_text(encoding='utf-8')
ng44 = (root / 'src' / 'native_grab.hpp').read_text(encoding='utf-8')
assert 'if(tolerate_repeat && !invalid_ && eye>=0 && eye<=1 && phase_==eye+1) {++repeats;return false;}' in gs44
assert 'native_eye_pair.tolerate_repeat=!fs::exists(root/L"build"/L"strict-eye-pair");' in probe
assert 'inline bool unpaired_gameplay_flat(unsigned run,bool last_projection_usable) noexcept {' in fh44
assert 'else if(game_xr && d2vr::unpaired_gameplay_flat(unpaired_gameplay_run,game_xr->last_projection_usable())) {' in probe
assert 'gameplay && !unpaired_flat,authored_camera || cutscene_immersive,' in probe and ',\\"unpaired_gameplay\\":{\\"flat_frames\\":' in probe
assert 'request_obstruction(side,*body,true);++press_rehovers_;' in ng44 and '<<",\\"press_rehovers\\":"<<los.press_rehovers' in probe
print('PASS: v1244 unpaired gameplay flat fallback, eye pair repeat tolerance, aged hover press re-ask')
# v1245 (diagnostic): native-view-trace.csv - every Setup before/after and every Begin while stereo (Timepiece view census)
assert 'view_trace("begin",view,index,begin_order,_ReturnAddress());' in probe and 'view_trace("setup_out",view,claimed?1:0,head_wrote?1:0,trace_caller);' in probe
assert 'view_trace_csv.open(output/L"native-view-trace.csv",std::ios::trunc);' in probe
print('PASS: v1245 view trace')
# v1246: secondary-view guard (Timepiece lens borrows the eye-1 view object): lens Begin/Setup are no eye, eye view re-created after it
svg46 = (root / 'src' / 'secondary_view_guard.hpp').read_text(encoding='utf-8')
th46 = (root / 'src' / 'native_taa_history.hpp').read_text(encoding='utf-8')
assert 'inline constexpr std::uintptr_t render_rva=0x366590,set_rva=0x366970,def_copy_rva=0x444c0,setup_wrapper_rva=0x374b60,eye_fraction_rva=0x22fd9f0;' in svg46
assert 'if(kind==d2vr::secondary_view::Kind::secondary) {' in probe and 'if(lens_active.load(std::memory_order_relaxed) && view && reinterpret_cast<std::uintptr_t>(view)==lens_view.load()) {lens_setup(view,window_rect,render_rect);return;}' in probe
assert 'if(claimed && head_wrote && lens_guard_enabled) lens_record(view,window_rect);' in probe and 'reinterpret_cast<Wrapper>(image_base+sv::setup_wrapper_rva)(' in probe
assert 'if(lens_view_skipped(view)) {++(lens_active.load()?lens_stats.post_in:lens_stats.post_after);return -1;}' in probe and 'if(read_(reinterpret_cast<std::uintptr_t>(ctx)+taa_history::ctx_view,&view,8) && skip(view)) {++skipped_views;return false;}' in th46
assert 'const bool hold=scope_hold || lens_hold;' in probe and '<< ",\\"timepiece\\":" << lens_status_json()' in probe
assert 'lens_guard_enabled=head_requested && !fs::exists(root/L"build"/L"no-lens-guard");' in probe and 'view_trace_enabled=fs::exists(root/L"build"/L"view-trace");' in probe
print('PASS: v1246 secondary-view guard')
# v1247: arm slip - the GPU skin copy expresses the image palette in the rig frame of the Setup the render draws (eye Setups only, latched at the
# first eye Begin), not the live frame a later tick moved; bitwise-equal frames untouched, > 0.3 m skipped; status arm_slip; build/no-arm-slip-fix
wd47 = (root / 'src' / 'weapon_draw_place.hpp').read_text(encoding='utf-8')
om47 = (root / 'src' / 'object_motion.hpp').read_text(encoding='utf-8')
assert 'static Slip slip(const float* snap,const float* live,double player_mps) noexcept {' in wd47 and 'if(!std::memcmp(snap,live,15*sizeof(float)))' in wd47
assert 'if(claimed && arm_slip_fix) arm_snap_note_setup();' in probe and 'if(eye>=0 && arm_slip_fix) arm_snap_latch(frames.load());' in probe
assert '==d2vr::weapon_draw::HandLock::slip_applied) live=snap.frame;' in probe and 'arm_slip_fix=!fs::exists(root/L"build"/L"no-arm-slip-fix");' in probe
assert '<<",\\"arm_slip\\":"<<weapon_draw::HandLock::slip_json()' in om47
print('PASS: v1247 arm slip fix')
