"""Export private deliberately authored D2VRAH01; never captured-fist certification."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import numpy as np
from export_native_hand_poses import verified_arm
from author_native_hand_candidate import matrices
from analyse_arm_mesh import decode_mesh
from rig_fields import GAME_HASH

def checksum(data):
    value=14695981039346656037
    for byte in data:value=((value^byte)*1099511628211)&((1<<64)-1)
    return value

def encode(candidate_path,review_path,mesh_paths):
    candidate=json.loads(candidate_path.read_text());source=Path(candidate['source_readonly_report'])
    report=json.loads(source.read_text());review=json.loads(review_path.read_text())
    if candidate.get('pose_origin')!='deliberately_authored_installed_native_69_rig' or candidate.get('captured_fist')is not False or candidate.get('production_archive_exported')is not False:
        raise ValueError('Require explicit unactivated authored provenance')
    if hashlib.sha256(source.read_bytes()).hexdigest()!=candidate['source_sha256']:raise ValueError('Authored source hash differs')
    if review.get('pose_origin')!='offline_authored_actual_cpp_screw_interpolation' or review.get('no_clipping_certified')is not False or len(review.get('samples',[]))!=112:
        raise ValueError('Require complete actual runtime mesh sweep with declared limits')
    if review.get('candidate_sha256')!=hashlib.sha256(candidate_path.read_bytes()).hexdigest() or review.get('runtime_output_sha256')!=hashlib.sha256((review_path.parent/'runtime-sweep-test-output.bin').read_bytes()).hexdigest():
        raise ValueError('Runtime mesh review does not match this candidate/output')
    paths=[source,candidate_path,*mesh_paths,review_path]
    hashes=[GAME_HASH]+[hashlib.sha256(p.read_bytes()).hexdigest()for p in paths]
    payload=bytearray()
    for side_index,side in enumerate(('left','right')):
        arm=verified_arm(report,side);names=arm['joint_names'];parents=arm['skeleton_layout']['parent_indices']
        item=next(a for a in candidate['arms']if a['side']==side)
        opened=np.asarray(matrices(arm));closed=np.asarray(item['authored_transform_matrices'],dtype=float)
        if closed.shape!=(69,4,4)or not np.isfinite(closed).all():raise ValueError('Invalid authored palette')
        for pose in (opened,closed):
            for transform in pose:
                rotation=transform[:3,:3]
                if np.max(np.abs(rotation.T@rotation-np.eye(3)))>2e-4 or abs(np.linalg.det(rotation)-1)>2e-4:raise ValueError('Nonrigid authored transform')
        chains=[[names.index(f'{side}_{digit}_0{segment+(1 if digit=="thumb"else 2)}_jnt')for segment in range(3)]for digit in ('thumb','index','medium','ring','pinky')]
        helpers=[names.index(side+suffix)for suffix in ('_helper_thumb_0_1stdorsalmuscle_l_jnt','_helper_thumb_tendon_l_jnt')]
        leaf=names.index(side+('_index_end_jnt'if side=='left'else '_thumb_end_jnt'))
        if leaf in parents:raise ValueError('Accessory endpoint is not leaf')
        mesh=decode_mesh(mesh_paths[side_index].read_bytes())
        for surface in mesh['surfaces']:
            if any(n in surface['material']for n in ('player_emily_hands.material','player_emily_gloves.material','player_emily_sleeves.material')):
                if any(any(j==leaf and w for j,w in zip(vertex[2],vertex[1]))for vertex in surface['vertices']):raise ValueError('Accessory endpoint also influences hand/cuff')
        def subtree(joint,root):
            for _ in range(69):
                if joint==root:return True
                joint=parents[joint]
                if joint<0:return False
            raise ValueError('Cyclic hierarchy')
        roots=[chains[0][0]]+[parents[chain[0]]for chain in chains[1:]]+helpers
        bones={j for chain in chains for j in chain}
        roles=bytes((int(any(subtree(j,root)for root in roots))| (2 if j in bones else 0)| (4 if j==leaf else 0))for j in range(69))
        payload.extend(struct.pack('<I',side_index))
        for name in names:
            encoded=name.encode('ascii')
            if len(encoded)>=96:raise ValueError('Native name exceeds bank bound')
            payload.extend(encoded+b'\0'*(96-len(encoded)))
        payload.extend(struct.pack('<69h15I2II',*parents,*(j for chain in chains for j in chain),*helpers,leaf));payload.extend(roles)
        for rows in arm['skinning_layout']['inverse_bind_rows']:payload.extend(struct.pack('<12f',*rows))
        for pose in (opened,closed):
            for transform in pose:payload.extend(struct.pack('<16f',*transform.T.ravel()))
    # flags: authored origin + acknowledged unresolved limits; acceptance bit absent.
    return b'D2VRAH01'+struct.pack('<I',3)+''.join(hashes).encode('ascii')+struct.pack('<Q',checksum(payload))+payload,hashes

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--candidate',type=Path,required=True);p.add_argument('--review',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args()
    root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'
    data,hashes=encode(args.candidate,args.review,[root/'left-arm.bmd6model',root/'right-arm.bmd6model'])
    args.output.write_bytes(data)
    manifest=dict(format='D2VRAH01',pose_origin='deliberately_authored_installed_native_69_rig',metadata_acceptance=False,captured_fist=False,production_activation=False,
        hashes=dict(zip(('executable','source','candidate','left_mesh','right_mesh','mesh_review'),hashes)),archive_sha256=hashlib.sha256(data).hexdigest(),payload_checksum_fnv64=struct.unpack_from('<Q',data,396)[0],
        thumb_envelope='max(grip,0.5*index)',accessory_leaf_global_preserved=True,
        limits=['Introduced partial-grip and self contacts remain','Accessory deformation unresolved; exact visible primitive correlation required','Native units to metres unverified','No headset acceptance'],
        mandatory_runtime_gates=['explicit opt-in','same exact side/rig/names/inverse','fresh owned Empty/null occupancy','verified native idle action','exact visible primitive safety'])
    args.output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n');print(args.output)

if __name__=='__main__':main()
