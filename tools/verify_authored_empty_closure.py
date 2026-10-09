"""Independent saved-pose FK/skin/transition validation for private authored candidates."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from author_empty_contact_closure import Closure,locals_of,interpolate_local
from refine_empty_contact_closure import RefinedClosure
from refine_empty_web_closure import WebClosure
from compare_empty_arc_helpers import ArcWebClosure
from fit_empty_thumb import ThumbClosure,LegacyThumbClosure,AnchoredThumbClosure,NativePlaneThumbClosure
from refine_empty_thumb_offsets import OffsetThumbClosure
from fit_empty_thumb_wrap import WrapThumbClosure
from refine_empty_wrap_helpers import WrapHelperClosure,NativePlaneWrapHelperClosure,MiddlePhalanxWrapHelperClosure
from read_empty_source_evidence import source_evidence
from author_native_hand_candidate import fk,skin
from check_authored_hand_contacts import evaluate
from export_native_hand_poses import verified_arm
from render_body_rig import skin_positions

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--candidate',type=Path,required=True);parser.add_argument('--output',type=Path);args=parser.parse_args()
    candidate=json.loads(args.candidate.read_text());source_path=Path(candidate['source_readonly_report']);baseline_path=Path(candidate['baseline']);report=json.loads(source_path.read_text());baseline=json.loads(baseline_path.read_text());
    if hashlib.sha256(source_path.read_bytes()).hexdigest()!=candidate.get('source_sha256')or hashlib.sha256(baseline_path.read_bytes()).hexdigest()!=candidate.get('baseline_sha256'):raise ValueError('Source or baseline hash changed')
    qualification=source_evidence(source_path,candidate['source_qualification']['companion'])if candidate.get('source_qualification')else None
    if qualification and qualification['companion_sha256']!=candidate['source_qualification'].get('companion_sha256'):raise ValueError('Source companion hash changed')
    root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';output=[]
    for item,old in zip(candidate['arms'],baseline['arms']):
        arm=verified_arm(report,item['side']);model=(WrapThumbClosure if candidate.get("authoring_model")=="WrapThumbClosure45" else NativePlaneThumbClosure if candidate.get("authoring_model")=="ThumbClosure45_native_planes" else OffsetThumbClosure if candidate.get("authoring_model")=="OffsetThumbClosure45" else AnchoredThumbClosure if candidate.get("authoring_model")=="ThumbClosure45_palm_anchor" else ThumbClosure if candidate.get("authoring_model")=="ThumbClosure45_palm_partial" else LegacyThumbClosure if candidate.get("authoring_model")=="ThumbClosure45" else ArcWebClosure if candidate.get("authoring_model")=="ArcWebClosure39" else WebClosure if len(item["parameters"])==39 else RefinedClosure)(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'))
        pose=[np.asarray(x) for x in item['authored_transform_matrices']];released=[np.asarray(x)for x in item['index_released_transform_matrices']];local=locals_of(pose,model.parents)
        if candidate.get('authoring_model')=='WrapHelperClosure48':model=WrapHelperClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'))
        if candidate.get('authoring_model')=='NativePlaneWrapHelperClosure48':model=NativePlaneWrapHelperClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'))
        if candidate.get('authoring_model')=='MiddlePhalanxWrapHelperClosure48':model=MiddlePhalanxWrapHelperClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'))
        regenerated=model.pose(np.asarray(item['parameters']));repro=max(float(np.max(np.abs(a-b)))for a,b in zip(pose,regenerated))
        main=sum(model.main,[]);allowed=set(main+model.finger_helpers+model.thumb)
        rotation=max(float(np.max(np.abs(x[:3,:3].T@x[:3,:3]-np.eye(3)))) for x in pose)
        length=max(float(np.linalg.norm(local[j][:3,3]-model.native[j][:3,3])) for j in main)
        neutral=max(float(np.max(np.abs(local[j]-model.native[j])))for j in range(69)if j not in allowed)
        states=[]
        for amount in (0.,.25,.5,.75,1.):
            blended=fk([interpolate_local(a,b,amount)for a,b in zip(model.native,local)],model.parents);palette=skin(arm,blended)
            after=np.concatenate([skin_positions(v,palette)[tri]for v,tri in model.surfaces]);metric=evaluate(model.before,after,model.ids)
            states.append(dict(grip=amount,**metric))
        palette=skin(arm,released);after=np.concatenate([skin_positions(v,palette)[tri]for v,tri in model.surfaces]);release_metric=evaluate(model.before,after,model.ids)
        output.append(dict(side=item['side'],asset_sha256=hashlib.sha256((root/(item['side']+'-arm.bmd6model')).read_bytes()).hexdigest(),maximum_reproduction_error=repro,maximum_rotation_orthonormal_error=rotation,maximum_main_local_length_error=length,maximum_neutral_local_error=neutral,structural_validation_passed=max(repro,rotation,length,neutral)<1e-5,grip_sweep=states,index_released=release_metric,geometry_acceptance_passed=all(x['introduced_triangle_crossings']==0 for x in states)and release_metric['introduced_triangle_crossings']==0,accessory_contact_certified=False))
    result=dict(candidate_sha256=hashlib.sha256(args.candidate.read_bytes()).hexdigest(),source_sustained_empty_bits=report.get('sustained_empty_bits'),source_tracked_writes_absence_certified=report.get('tracked_arm_writes_absent_verified') is True,source_authored_endpoint_certified=report.get('authored_pose_confirmed') is True,source_qualification=qualification,captured_fist=False,game_memory_access=False,production_activation=False,headset_acceptance=False,arms=output)
    target=args.output or args.candidate.parent/'independent-validation.json'
    if target.exists():raise ValueError('Refuse overwrite validation')
    target.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(output=str(target),summary=[dict(side=a['side'],structural_pass=a['structural_validation_passed'],crossings=[s['introduced_triangle_crossings']for s in a['grip_sweep']],released=a['index_released']['introduced_triangle_crossings'],geometry_pass=a['geometry_acceptance_passed'])for a in output])))
if __name__=='__main__':main()
