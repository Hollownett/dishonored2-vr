"""Private actual-C++ interpolation sweep: mesh strain, contacts and accessory leaves."""
import argparse
from collections import Counter
import json
import hashlib
from pathlib import Path
import struct
import numpy as np
from analyse_arm_mesh import decode_mesh
from author_native_hand_candidate import skin,render
from check_authored_hand_contacts import evaluate
from export_native_hand_poses import verified_arm
from render_body_rig import palettes,skin_positions


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--candidate',type=Path,required=True);args=parser.parse_args()
    candidate=json.loads(args.candidate.read_text());report=json.loads(Path(candidate['source_readonly_report']).read_text());root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'
    output=args.candidate.parent;data=(output/'runtime-sweep-test-output.bin').read_bytes()
    if data[:8]!=b'D2VRSO02'or struct.unpack_from('<I',data,8)[0]!=112 or len(data)!=12+112*(16+69*64):raise ValueError('Wrong private C++ sweep output')
    records=[];at=12
    for _ in range(112):
        side,mode,grip,index=struct.unpack_from('<IIfI',data,at);at+=16
        pose=np.frombuffer(data,dtype='<f4',count=69*16,offset=at).reshape(69,4,4).transpose(0,2,1).astype(float);at+=69*64
        records.append(dict(side=side,mode=mode,thumb_envelope=mode>=2,preserve_accessory_end=bool(mode&1),grip=float(grip),index=index,pose=pose))
    summaries=[]
    for side_index,side in enumerate(('left','right')):
        arm=verified_arm(report,side);mesh=decode_mesh((root/(side+'-arm.bmd6model')).read_bytes());original,_=palettes(arm)
        before=[skin_positions(s['vertices'],original)for s in mesh['surfaces']];skin_tri=[];ids=[]
        wrist=arm['joint_names'].index(side+'_wrist_jnt');origin=np.array(arm['sampled_joints'][wrist]['position'])
        for number,surface in enumerate(mesh['surfaces']):
            if not any(n in surface['material']for n in ('player_emily_hands.material','player_emily_gloves.material')):continue
            triangles=np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3)
            for triid,tri in enumerate(triangles):
                if not np.all(np.linalg.norm(before[number][tri]-origin,axis=1)<.25):continue
                region=Counter()
                for vertex in tri:
                    for joint,weight in zip(surface['vertices'][vertex][2],surface['vertices'][vertex][1]):
                        if not weight:continue
                        name=arm['joint_names'][joint];digit=next((d for d in ('thumb','index','medium','ring','pinky')if d in name),None)
                        region[digit or ('medium'if 'middle'in name else 'palm')]+=weight
                skin_tri.append(before[number][tri]);ids.append(dict(surface=number,triangle=triid,region=region.most_common(1)[0][0]))
        contact_cache={};skins={};renders=[]
        for record in records:
            if record['side']!=side_index:continue
            palette=skin(arm,record['pose']);posed=[skin_positions(s['vertices'],palette)for s in mesh['surfaces']]
            key=(record['grip'],record['index'],record['thumb_envelope']);materials=[]
            for number,surface in enumerate(mesh['surfaces']):
                triangles=np.frombuffer(surface['indices'],dtype='<u2').reshape(-1,3);edges=triangles[:,[[0,1],[1,2],[2,0]]].reshape(-1,2)
                old=np.linalg.norm(before[number][edges[:,0]]-before[number][edges[:,1]],axis=1);new=np.linalg.norm(posed[number][edges[:,0]]-posed[number][edges[:,1]],axis=1);valid=old>1e-5
                pair_error=0.
                if record['preserve_accessory_end']:
                    pair_error=float(np.max(np.abs(posed[number]-skins[key][number])))
                materials.append(dict(surface=number,material=surface['material'],maximum_displacement_native_units=float(np.linalg.norm(posed[number]-before[number],axis=1).max()),
                    maximum_edge_ratio=float((new[valid]/old[valid]).max()),maximum_end_preservation_skin_difference_native_units=pair_error))
            if not record['preserve_accessory_end']:
                skins[key]=posed
                after=np.array([posed[i['surface']][np.frombuffer(mesh['surfaces'][i['surface']]['indices'],dtype='<u2').reshape(-1,3)[i['triangle']]]for i in ids])
                contact_cache[key]=evaluate(np.array(skin_tri),after,ids)
            else:
                for material in materials:
                    if any(s in material['material']for s in ('player_emily_hands.material','player_emily_gloves.material','player_emily_sleeves.material')):
                        if material['maximum_end_preservation_skin_difference_native_units']!=0:raise ValueError('Accessory end changes hand/cuff skin')
                if record['thumb_envelope']and((record['grip']==0 and record['index']==0)or(record['grip']==0 and record['index']==1)or(record['grip']==.5 and record['index']==1)or record['grip']==1):
                    renders.append((f'Envelope screw grip{record["grip"]:.2f} index{record["index"]}',list(record['pose'])))
            summaries.append(dict(side=side,thumb_envelope=record['thumb_envelope'],preserve_accessory_end=record['preserve_accessory_end'],grip=record['grip'],index=record['index'],materials=materials,
                contacts=contact_cache[key],accessory_end_preservation_hand_cuff_bit_exact=bool(record['preserve_accessory_end'])))
        render(arm,renders,root/(side+'-arm.bmd6model'),output/(side+'-actual-screw-accessory-end-review.png'))
        print(json.dumps(dict(side=side,poses=56,hand_cuff_unchanged_by_end_preservation=True)),flush=True)
    target=output/'actual-runtime-sweep.json';target.write_text(json.dumps(dict(pose_origin='offline_authored_actual_cpp_screw_interpolation',game_memory_access=False,
        captured_fist=False,production_activation=False,no_clipping_certified=False,
        candidate_sha256=hashlib.sha256(args.candidate.read_bytes()).hexdigest(),runtime_output_sha256=hashlib.sha256(data).hexdigest(),samples=summaries),indent=2)+'\n')
    print(target)


if __name__=='__main__':main()
