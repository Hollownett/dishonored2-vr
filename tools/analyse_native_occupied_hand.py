"""Offline occupied native grip/local transfer review; never Empty capture or activation."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import math
import numpy as np
from analyse_arm_mesh import decode_mesh
from author_native_hand_candidate import matrices,fk,skin,render
from check_authored_hand_contacts import evaluate
from compare_native_hand_sources import changes
from export_native_hand_poses import verified_arm
from render_body_rig import skin_positions

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--open',type=Path,required=True);p.add_argument('--occupied',type=Path,required=True);p.add_argument('--authored',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    opened=json.loads(args.open.read_text());occupied=json.loads(args.occupied.read_text());authored=json.loads(args.authored.read_text());args.output.mkdir(exist_ok=True)
    root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';summaries=[]
    for side in ('left','right'):
        arm=verified_arm(opened,side);native=verified_arm(occupied,side);names=arm['joint_names'];parents=arm['skeleton_layout']['parent_indices']
        if names!=native['joint_names']or parents!=native['skeleton_layout']['parent_indices']or arm['skinning_layout']['inverse_bind_rows']!=native['skinning_layout']['inverse_bind_rows']:raise ValueError('Native grip rig differs')
        source=np.asarray(matrices(arm));closed=np.asarray(matrices(native))
        a=[source[j]if parent<0 else np.linalg.inv(source[parent])@source[j]for j,parent in enumerate(parents)]
        b=[closed[j]if parent<0 else np.linalg.inv(closed[parent])@closed[j]for j,parent in enumerate(parents)]
        roots=[names.index(side+'_thumb_01_jnt')]+[parents[names.index(side+'_'+digit+'_02_jnt')]for digit in ('index','medium','ring','pinky')]
        roots+=[names.index(side+suffix)for suffix in ('_helper_thumb_0_1stdorsalmuscle_l_jnt','_helper_thumb_tendon_l_jnt')]
        def mapped(j):
            for _ in range(69):
                if j in roots:return True
                j=parents[j]
                if j<0:return False
            raise ValueError('Cyclic hierarchy')
        mask=[mapped(j)for j in range(69)];transferred=fk([b[j]if mask[j]else a[j]for j in range(69)],parents)
        helpers=[]
        for j,name in enumerate(names):
            if not mask[j]or 'helper'not in name:continue
            trace=np.trace(a[j][:3,:3].T@b[j][:3,:3]);angle=math.degrees(math.acos(np.clip((trace-1)/2,-1,1)))
            helpers.append(dict(name=name,angle_degrees=angle,local_offset_delta_native_units=float(np.linalg.norm(a[j][:3,3]-b[j][:3,3]))))
        mesh=decode_mesh((root/(side+'-arm.bmd6model')).read_bytes());old_palette=skin(arm,source);new_palette=skin(arm,transferred)
        surfaces=[];baseline_tri=[];posed_tri=[];ids=[];wrist=names.index(side+'_wrist_jnt');origin=source[wrist,:3,3]
        for number,surface in enumerate(mesh['surfaces']):
            before=skin_positions(surface['vertices'],old_palette);after=skin_positions(surface['vertices'],new_palette)
            tri=np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3);edges=tri[:,[[0,1],[1,2],[2,0]]].reshape(-1,2)
            old=np.linalg.norm(before[edges[:,0]]-before[edges[:,1]],axis=1);new=np.linalg.norm(after[edges[:,0]]-after[edges[:,1]],axis=1);valid=old>1e-5
            delta=np.linalg.norm(after-before,axis=1)
            surfaces.append(dict(surface=number,material=surface['material'],maximum_displacement_native_units=float(delta.max()),maximum_edge_ratio=float(np.max(new[valid]/old[valid])),maximum_edge_absolute_change_native_units=float(np.max(np.abs(new-old)))))
            if not any(n in surface['material']for n in ('player_emily_hands.material','player_emily_gloves.material')):continue
            for tri_id,triangle in enumerate(tri):
                if not np.all(np.linalg.norm(before[triangle]-origin,axis=1)<.25):continue
                regions=Counter()
                for vertex in triangle:
                    for joint,weight in zip(surface['vertices'][vertex][2],surface['vertices'][vertex][1]):
                        if not weight:continue
                        name=names[joint];digit=next((d for d in ('thumb','index','medium','ring','pinky')if d in name),None)
                        regions[digit or ('medium'if 'middle'in name else 'palm')]+=weight
                baseline_tri.append(before[triangle]);posed_tri.append(after[triangle]);ids.append(dict(surface=number,triangle=tri_id,region=regions.most_common(1)[0][0]))
        collision=evaluate(np.asarray(baseline_tri),np.asarray(posed_tri),ids)
        previous=np.asarray(next(item for item in authored['arms']if item['side']==side)['authored_transform_matrices'])
        render(arm,[('Verified Empty native open',source),('Sword locals (occupied)',transferred),('Previous authored endpoint',previous)],root/(side+'-arm.bmd6model'),args.output/(side+'-native-sword-transfer-review.png'))
        state=next(h for h in occupied['native_hand_state']['hands']if h['side']==side)
        summaries.append(dict(side=side,occupied_graph=state['equip_name'],occupied_slot=state['slot'],same_exact_rig=True,main_changes=changes(arm,native,side),helpers=helpers,surfaces=surfaces,contacts=collision,
            transferred_transform_matrices=[t.tolist()for t in transferred],transfer_origin='Derived from occupied native Sword local transforms; not captured Empty fist'))
    result=dict(game_memory_access=False,game_memory_writes=False,production_activation=False,empty_fist_captured=False,headset_acceptance=False,
        occupied_source=str(args.occupied.resolve()),occupied_sha256=hashlib.sha256(args.occupied.read_bytes()).hexdigest(),open_sha256=hashlib.sha256(args.open.read_bytes()).hexdigest(),
        limits=['Occupied shaft grasp is not an Empty fist','Native source has a weapon-shaped cavity','Local transfer contact/skin diagnostics do not certify headset appearance'],arms=summaries)
    target=args.output/'occupied-native-sword-analysis.json';target.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps([dict(side=a['side'],flexed=sum(j['flexed']for j in a['main_changes']),helper_offset_max=max(h['local_offset_delta_native_units']for h in a['helpers']),contacts=a['contacts']['introduced_region_pairs'],surfaces=a['surfaces'])for a in summaries]))

if __name__=='__main__':main()
