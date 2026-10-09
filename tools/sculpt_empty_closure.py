"""Offline silhouette trials: main curls scored against native palm skin clearance."""
import argparse,json
from pathlib import Path
import numpy as np
from refine_empty_web_closure import WebClosure,assess
from export_native_hand_poses import verified_arm
from render_authored_hand_close import render_close
from author_native_hand_candidate import skin
from render_body_rig import skin_positions

def clearance(model,pose):
    bases=np.array([model.source[c[0]][:3,3]for c in model.main]);forward=bases.mean(axis=0)-model.source[model.wrist][:3,3];forward/=np.linalg.norm(forward)
    original=skin(model.arm,model.source);palette=skin(model.arm,pose);points=[]
    for vertices,_ in model.surfaces:
        before=skin_positions(vertices,original);long=(before-model.source[model.wrist][:3,3])@forward
        for i,v in enumerate(vertices):
            nonfinger=sum(w for w,j in zip(v[1],v[2])if not any(x in model.names[j]for x in ('_03_','_04_','_end_','thumb','helper_index_1','helper_index_2','helper_middle_1','helper_middle_2','helper_ring_1','helper_ring_2','helper_pinky_1','helper_pinky_2')))
            if nonfinger>=128 and .005<long[i]<float(np.min((bases-model.source[model.wrist][:3,3])@forward))-.004:points.append(skin_positions([v],palette)[0])
    palm=np.asarray(points)
    if len(palm)<20:raise ValueError('Insufficient native palm skin vertices')
    distances=[float(np.min(np.linalg.norm(palm-pose[model.names.index(model.arm['side']+'_'+d+'_end_jnt')][:3,3],axis=1)))for d in ('index','medium','ring','pinky')]
    return dict(fingertip_bone_to_native_palm_vertex_distances=distances,palm_vertices=len(palm))

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--candidate',type=Path,required=True);parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    if args.output.exists():raise ValueError('Use new output')
    source=json.loads(args.candidate.read_text());baseline=json.loads(Path(source['baseline']).read_text());report=json.loads(Path(source['source_readonly_report']).read_text());args.output.mkdir(parents=True);root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';result=[]
    for item,old in zip(source['arms'],baseline['arms']):
        arm=verified_arm(report,item['side']);model=WebClosure(arm,old['authored_transform_matrices'],root/(item['side']+'-arm.bmd6model'));variants=[];best=None
        for title,pattern in [('prior',None),('rounded',(1.,1.05,1.05)),('pip-tight',(1.1,1.,1.15)),('deep',(1.15,1.05,1.2))]:
            p=np.array(item['parameters']+[0.]*max(0,39-len(item['parameters'])))
            if pattern:p[:12]=np.tile(pattern,4)
            score,closed,released,pose,partial=assess(model,p);dist=clearance(model,pose);objective=score+1000*sum(dist['fingertip_bone_to_native_palm_vertex_distances'])
            entry=dict(title=title,parameters=p.tolist(),score=score,silhouette_contact_score=objective,closed=closed,index_released=released,partial_crossings=partial,**dist);variants.append(entry)
            render_close(arm,pose,root/(item['side']+'-arm.bmd6model'),args.output/(item['side']+'-'+title+'.png'),title)
            if best is None or objective<best[0]:best=(objective,p,entry,pose)
            print(json.dumps(dict(side=item['side'],title=title,closed=closed['introduced_triangle_crossings'],partial=partial,**dist)),flush=True)
        _,p,entry,pose=best;entry=dict(entry);released=model.pose(p,True);entry.update(side=item['side'],variants=variants,authored_transform_matrices=[m.tolist()for m in pose],index_released_transform_matrices=[m.tolist()for m in released]);result.append(entry)
    source.update(authoring_model='WebClosure39',precursor=str(args.candidate),arms=result,production_activation=False)
    (args.output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()

