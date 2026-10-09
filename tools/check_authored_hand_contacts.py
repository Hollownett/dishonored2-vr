"""Offline triangle-crossing diagnostic for flesh/glove authored candidates.

Reports intersections, not a blanket collision/no-clipping certification.
Ignores seams sharing native positions; separates existing from introduced crossings.
"""
import argparse
import json
from collections import Counter
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree
from analyse_arm_mesh import decode_mesh
from author_native_hand_candidate import skin,authored
from export_native_hand_poses import verified_arm
from render_body_rig import palettes,skin_positions


def segment_triangle(start,end,tri):
    direction=end-start;e1=tri[:,1]-tri[:,0];e2=tri[:,2]-tri[:,0]
    h=np.cross(direction,e2);det=np.einsum('ij,ij->i',e1,h);good=np.abs(det)>1e-12
    inverse=np.where(good,1/np.where(good,det,1),0);s=start-tri[:,0]
    u=inverse*np.einsum('ij,ij->i',s,h);q=np.cross(s,e1)
    v=inverse*np.einsum('ij,ij->i',direction,q);t=inverse*np.einsum('ij,ij->i',e2,q)
    return good&(u>1e-6)&(v>1e-6)&(u+v<1-1e-6)&(t>1e-6)&(t<1-1e-6)


def crossing(a,b):
    hit=np.zeros(len(a),dtype=bool)
    for first,second in ((a,b),(b,a)):
        for edge in ((0,1),(1,2),(2,0)):hit|=segment_triangle(first[:,edge[0]],first[:,edge[1]],second)
    return hit


def evaluate(before,after,ids):
    centre=after.mean(axis=1);radius=np.linalg.norm(after-centre[:,None,:],axis=2).max(axis=1)
    tree=cKDTree(centre);pairs=tree.query_pairs(2*radius.max(),output_type='ndarray');new=[];existing=[];checked=0
    for start in range(0,len(pairs),20000):
        chunk=pairs[start:start+20000];a,b=chunk[:,0],chunk[:,1]
        valid=np.linalg.norm(centre[a]-centre[b],axis=1)<=radius[a]+radius[b]
        # Common topological/duplicated seam positions are expected neighbours.
        same=np.any(np.linalg.norm(before[a,:,None,:]-before[b,None,:,:],axis=3)<2e-5,axis=(1,2));valid&=~same
        a,b=a[valid],b[valid]
        valid=np.all(after[a].max(axis=1)>=after[b].min(axis=1),axis=1)&np.all(after[b].max(axis=1)>=after[a].min(axis=1),axis=1)
        a,b=a[valid],b[valid];checked+=len(a)
        now=crossing(after[a],after[b]);old=crossing(before[a],before[b])
        new.extend((int(x),int(y))for x,y in zip(a[now&~old],b[now&~old]));existing.extend((int(x),int(y))for x,y in zip(a[now&old],b[now&old]))
    def labelled(values):return [dict(first=ids[a],second=ids[b])for a,b in values[:40]]
    extents={}
    for first,second in new:
        a,b=after[first],after[second];overlap=[]
        for tri,plane in ((a,b),(b,a)):
            normal=np.cross(plane[1]-plane[0],plane[2]-plane[0]);length=np.linalg.norm(normal)
            if length<1e-12:continue
            signed=(tri-plane[0])@(normal/length)
            overlap.append(max(0.,min(float(signed.max()),float(-signed.min()))))
        category='/'.join(sorted((ids[first]['region'],ids[second]['region'])))
        extents.setdefault(category,[]).append(max(overlap,default=0.))
    return dict(exact_candidate_pairs_checked=checked,introduced_triangle_crossings=len(new),existing_crossings_retained=len(existing),
                introduced_region_pairs=dict(Counter('/'.join(sorted((ids[a]['region'],ids[b]['region'])))for a,b in new)),
                triangle_plane_overlap_extent_proxy={k:dict(maximum_native_units=max(v),p95_native_units=float(np.percentile(v,95)))for k,v in extents.items()},
                first_introduced_pairs=labelled(new),coplanar_contacts_not_evaluated=True,no_clipping_certified=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--candidate',type=Path,required=True);args=parser.parse_args()
    candidate=json.loads(args.candidate.read_text());source=json.loads(Path(candidate['source_readonly_report']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';result=[]
    for item in candidate['arms']:
        side=item['side'];arm=verified_arm(source,side);original,_=palettes(arm);full=skin(arm,[np.asarray(m)for m in item['authored_transform_matrices']])
        parameters=item['evidence']['thumb_opposition_rotation_vector_radians']+item['evidence']['thumb_flex_radians']
        released,_=authored(arm,0.,item['evidence']['helper_drivers_enabled'],item['evidence'].get('relaxed_knuckles',False),item['evidence'].get('thumb_clearance_native_units',.012),item['evidence'].get('mesh_thumb_fit',False),parameters,item['evidence'].get('fan_fingers',False));release=skin(arm,released)
        originals=[];closed=[];opened=[];ids=[];wrist=arm['joint_names'].index(side+'_wrist_jnt');origin=np.asarray(arm['sampled_joints'][wrist]['position'])
        for number,surface in enumerate(decode_mesh((root/(side+'-arm.bmd6model')).read_bytes())['surfaces']):
            if not any(n in surface['material']for n in ('player_emily_hands.material','player_emily_gloves.material')):continue
            v=surface['vertices'];before=skin_positions(v,original);after=skin_positions(v,full);index=skin_positions(v,release)
            triangles=np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3);visible=np.all(np.linalg.norm(before[triangles]-origin,axis=2)<.25,axis=1)
            for triid,triangle in enumerate(triangles):
                if visible[triid]:
                    regions=Counter()
                    for vertex in triangle:
                        for joint,weight in zip(v[vertex][2],v[vertex][1]):
                            if not weight:continue
                            name=arm['joint_names'][joint];region=next((d for d in ('thumb','index','medium','ring','pinky')if d in name),None)
                            if region is None:region='medium'if 'middle'in name else 'palm'
                            regions[region]+=weight
                    originals.append(before[triangle]);closed.append(after[triangle]);opened.append(index[triangle]);ids.append(dict(surface=number,triangle=triid,region=regions.most_common(1)[0][0]))
        baseline=np.asarray(originals)
        result.append(dict(side=side,triangles=len(ids),closed=evaluate(baseline,np.asarray(closed),ids),index_released=evaluate(baseline,np.asarray(opened),ids)))
    output=args.candidate.parent/'hand-contact-diagnostic.json';output.write_text(json.dumps(dict(candidate=str(args.candidate),game_memory_access=False,arms=result),indent=2)+'\n')
    print(json.dumps(dict(output=str(output),summary=[dict(side=a['side'],closed_crossings=a['closed']['introduced_triangle_crossings'],released_crossings=a['index_released']['introduced_triangle_crossings'])for a in result])))


if __name__=='__main__':main()
