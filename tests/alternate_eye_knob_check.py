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
assert 'alt_plan.eye_for_frame(next_display,free_gameplay,authored,epoch_changed)' in after and 'number+1' not in after
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
assert 'const bool own=have_view && RigMask::previous_palette(c,static_cast<int>(view.eye),view.frame,rows_own);' in om and 'rows_own=kind==PrevMvpTable::other_last;' in om and 'if(!own) std::memcpy(prev,cur,64);' in om  # v1166: own palette + own rows, or both current
assert 'restore_t0(); // v1166' in rig and 'k.mv_scale[2]=1;if(e.rig_motion) {k.mv_scale[3]=1;++motion_rig_moving;}' in fsr
assert 'd2vr::RigMask::skin_copied(d3d,i,reinterpret_cast<ID3D11Buffer*>(buffer),frame);' in probe and 'o[i.xy]=held?253:own?255:rig?254:0;' in rig  # v1179 ring + per-pixel validity; v1183 held class 253
assert 'if(!c || !pass_view || !enabled.load(std::memory_order_relaxed) || pass_view!=view.load(std::memory_order_relaxed)) return;' in rig
assert 'ds.DepthFunc=D3D11_COMPARISON_EQUAL;ds.StencilEnable=FALSE;' in rig and 'targets(c,rtv_count,rtvs.data(),dsv);' in rig
print('PASS: v1163 rig mask routing (knob, one-view rig draws replayed, mask at the copy, both-or-none masks, motion arm rule, raw rig rows, status)')

# v1171: rig motion vectors opt-in (build/stale-eye-rig-motion); off = the v1165 rig draws, no palette work, rig pixels zeroed
assert 'd2vr::RigMask::motion=fs::exists(root/L"build"/L"stale-eye-rig-motion");' in probe
assert "if(mode_.load(std::memory_order_relaxed)<1 || !RigMask::motion.load(std::memory_order_relaxed)) return false;" in om
assert 'const bool rig_motion_now=RigMask::motion.load(std::memory_order_relaxed) && job.receipt' in fsr
assert 'inline static std::atomic<bool> motion{false};' in rig
assert 'pad_render_edge' not in fsr and 'edge_pad' not in fsr  # v1171: the v1168 FSR input edge replication is reverted
print('PASS: v1171 rig motion opt-in (knob, off = v1165 rig draws), FSR input edge replication reverted')

# v1184: AFW v2 opt-in (build/afw2 -> StaleLayers::afw2 after stale_eye_layers), status and dump meta; play spaces recorded with the cameras
assert 'stale_afw2_knob=fs::exists(root/L"build"/L"afw2");' in probe and 'game_xr->stale_afw2=stale_afw2_knob;' in probe
assert 'stale_layers_[eye]->afw2=stale_afw2; // v1184: knob build/afw2' in xr
assert '\\"afw2\\":{\\"on\\":' in probe and 'meta<<",\\"afw2\\":"<<(layers.afw2?"true":"false");' in probe
assert 'alt_play.record(alt_display,logical_eye,*play,game_xr->pose_epoch(),0,0);' in probe and 'game_xr->stale_eye_target_play(other,plays->companion[other]);' in probe
print('PASS: v1184 AFW v2 knob (layers, status, meta) and play spaces with the cameras')

# v1189: one-view frames never share shadows (each eye render draws its own); knob build/alt-shadow-share restores the paired order
assert 'const int share_order=alternate_on() && !alt_shadow_share_knob?0:begin_order;' in probe and 'shadow_share.on_eye_begin(share_order,address,gate,' in probe
assert 'alt_shadow_share_knob=fs::exists(root/L"build"/L"alt-shadow-share");' in probe
print('PASS: v1189 no shadow sharing on one-view frames (knob alt-shadow-share)')

# v1190: rig target (knob stale-eye-rig-target): retargeted rig/held replay, published per render for the stale eye, t15 of AFW v2
assert 'rig_target_knob=fs::exists(root/L"build"/L"stale-eye-rig-target");d2vr::RigMask::target_on=rig_target_knob;' in probe
assert 'game_xr->stale_eye_rig_target(other,target.Get(),tw,th,number,tv);' in probe and 'stale_layers_[eye]->rig_target=static_cast<bool>(stale_rig_target(eye).texture);' in xr
assert 'd2vr::rig_target::book().publish(vp,e,has_e,prev_vp,frame);' in probe
print('PASS: v1190 rig target routing (knob, E at Setup, replay, per-render hand-off to the stale eye)')

# v1191: GPU spans (stale warp, rig target copy, motion pass), rectangle copy of the rig target, one-view weapon placement (knob alt-weapon-place)
assert 'ModGpuScope warp_scope(context_.Get(),ModSpan::stale_warp);' in xr and 'ModSpan::rig_target' in fsr and 'ModSpan::eye_motion' in fsr
assert 'RigMask::take_target(c,depth_resource_.p,se.rig_target.p,draws,depth_w,depth_h)' in fsr
assert 'alt_weapon_place_knob=fs::exists(root/L"build"/L"alt-weapon-place");d2vr::ObjectMotion::alt_weapon_place=alt_weapon_place_knob;' in probe
assert 'weapon_draw::Place::place_one_view(vp,prev_vp,cur,placed,placed_prev,own_prev)' in om
print('PASS: v1191 GPU spans, rig target rectangle copy, one-view weapon placement (knob alt-weapon-place)')
rm_src=(root / 'src' / 'rig_mask.hpp').read_text(encoding='utf-8')
assert 'if(pred) c->Begin(pred);' in rm_src and 'c->SetPredication(pred,FALSE);' in rm_src and 'c->SetPredication(engine_pred,engine_value);' in rm_src
print('PASS: v1191 rig target replays only draws the mask replay saw (occlusion predicate)')

# v1192: sim-pose-hold (simulated XR + gameplay script only): the script clock is sampled on eye-0-fresh frames, dumps only there, truth_next meta
assert 'd2vr::sim_pose_hold::on=static_cast<bool>(knob.is_open() || fs::exists(root/L"build"/L"sim-pose-hold")) && simulated_vr_requested && simulated_gameplay_script_requested;' in probe
assert 'return sim_pose_clock.sample(elapsed,change);}();' in probe and 'simulated_gameplay_script.poses(script_elapsed)' in probe and 'simulated_gameplay_script.sample(script_elapsed,' in probe
assert 'const UINT64 display=d2vr::alternate_display_frame(number);' in probe
assert 'if(d2vr::sim_pose_hold::on.load() && !d2vr::sim_pose_hold::changes(game_xr->alternate_frame,' in probe and 'if(stale_next.truth) stale_truth_next();' in probe
assert 'd2vr::sim_pose_hold::on.load()?0.f:finger_touch_params.pose_smooth' in probe
print('PASS: v1192 sim-pose-hold (sim script only, clock phase, dumps on change frames, truth_next, no finger spring)')
