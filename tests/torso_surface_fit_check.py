"""Skin the extracted native meshes with actual Driver fit palettes."""
from pathlib import Path
import hashlib
import json
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from analyse_arm_mesh import decode_mesh
from render_body_rig import skin_positions
from build_player_asset_pack import parse_skeleton

payload_path=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'build/torso-fit-palettes.json'
payload=json.loads(payload_path.read_text())
inverse=np.asarray(payload['inverse']).reshape(20,4,4).transpose(0,2,1)
hashes={'emily':'5a8bbefd7b9124694c59d1f58cacc3c207b15ffdb30c00decb5e9c2ea18a9051',
        'corvo':'0cfc0f83868fdc9f224b741cf70b4e723821c3cec17b331eef206acac8805ea3'}
arm_hashes={'emily':['af4d5d80e97d10bd3f04f8baf9895bff26c6ed5b78ad8cb6316b5caa243e496c',
                     'ea325d3d57e3cf0e36b8d51dd20d9a02b7d5ca6662addb6300eddf468d2654c7'],
            'corvo':['76bbf7e55ac12f3344d19b07f8169da6077ed07d40bec58c78c063955a29f787',
                     '8c91f3061ab916cae574cdf550f430617e333d7ce384df6fc10e2527aee9b1eb']}
wrapper_hashes={'emily':'71a340e0ab7443e89dfe64d0b4c37aced10ee0c7c4ba946de7eb8b492adc02bd',
                'corvo':'304421c829e4a671d250980d63ff1c80b69b2f96f657097561e568a83902a811'}
results=[]
for case in payload['characters']:
    name=case['name']
    wrapper=ROOT/f'artifacts/fullbody-assets/raw/models/characters/player/{name}/body.md6'
    wrapper_raw=wrapper.read_bytes();assert hashlib.sha256(wrapper_raw).hexdigest()==wrapper_hashes[name]
    wrapper_text=wrapper_raw.decode('utf-8')
    assert f'mesh "models/characters/player/{name}/player_body_{name}.md6mesh"' in wrapper_text
    assert 'skeleton "models/characters/player/emily/player_body_emily.md6skl"' in wrapper_text
    path=ROOT/f'artifacts/fullbody-assets/raw/models/characters/player/{name}/player_body_{name}.md6mesh'
    raw=path.read_bytes();assert hashlib.sha256(raw).hexdigest()==hashes[name]
    mesh=decode_mesh(raw,20);vertices=[v for s in mesh['surfaces'] for v in s['vertices']]
    points=np.asarray([v[0] for v in vertices]);weights=np.asarray([list(v[1]) for v in vertices])
    joints=np.asarray([list(v[2]) for v in vertices])
    spinal_mass=(weights*np.isin(joints,[2,3,4])).sum(axis=1)
    section=(np.abs(points[:,2]-1.35)<=.0125)&(spinal_mass==255)
    assert section.sum()>50
    native_width=np.ptp(points[section,1]);assert abs(native_width-case['width'])<1e-9
    animated=np.asarray(case['palette']).reshape(20,4,4).transpose(0,2,1)
    palette=(animated@inverse)[:,:3,:]
    posed=skin_positions(vertices,palette)
    # Polar rotation strips the intentional transverse affine scale before
    # finding the chest heading. Measure width in that heading, not world Y.
    u,_,vt=np.linalg.svd(animated[4,:3,:3]);rotation=u@vt
    bind_rotation=np.linalg.inv(inverse[4,:3,:3])
    left=rotation@bind_rotation.T@np.array([0.,1.,0.])
    width=np.ptp(posed[section]@left)
    assert abs(width/native_width-1.4)<.015,(name,width,native_width)
    assert np.isfinite(posed).all()
    cuffs=[]
    for side,side_name in enumerate(('left','right')):
        arm_path=ROOT/f'artifacts/fullbody-assets/raw/models/characters/player/{name}/player_{side_name}arm_{name}.md6mesh'
        arm_raw=arm_path.read_bytes();assert hashlib.sha256(arm_raw).hexdigest()==arm_hashes[name][side]
        arm_mesh=decode_mesh(arm_raw,69)
        arm_vertices=[v for s in arm_mesh['surfaces'] for v in s['vertices']]
        skel=parse_skeleton(ROOT/f'artifacts/fullbody-assets/raw/models/characters/player/emily/player_{side_name}arm_emily.md6skl')
        arm_inverse=skel['inverse']
        fitted=np.asarray(case['arm_palettes'][side]).reshape(69,4,4).transpose(0,2,1)
        baseline=np.asarray(case['arm_baseline'][side]).reshape(69,4,4).transpose(0,2,1)
        fitted_points=skin_positions(arm_vertices,(fitted@arm_inverse)[:,:3,:])
        baseline_points=skin_positions(arm_vertices,(baseline@arm_inverse)[:,:3,:])
        arm_weights=np.asarray([list(v[1]) for v in arm_vertices]);arm_joints=np.asarray([list(v[2]) for v in arm_vertices])
        # Pure cuff vertices must receive the same transverse factor as the
        # body; measured upper-arm centres and wrist skin remain independent.
        cuff=(arm_weights*np.isin(arm_joints,[0,1])).sum(axis=1)==255
        assert cuff.sum()>10
        ratio=np.ptp(fitted_points[cuff]@left)/np.ptp(baseline_points[cuff]@left)
        assert abs(ratio-1.4)<.002,(name,side_name,ratio)
        cuffs.append(dict(side=side_name,vertices=int(cuff.sum()),transverse_ratio=float(ratio)))
    results.append(dict(character=name,asset_sha256=hashes[name],chest_vertices=int(section.sum()),
                        height_scale=case['height_scale'],
                        native_width_metres=float(native_width),fitted_width_metres=float(width),
                        target_width_metres=float(native_width*1.4),headset_verified=False,
                        cuff_affine_checks=cuffs,material_seam_verified=False))
output=Path(sys.argv[2]) if len(sys.argv)>2 else ROOT/'artifacts/research/torso-surface-fit'
output.mkdir(parents=True,exist_ok=True)
(output/'mesh-fit-check.json').write_text(json.dumps(results,indent=2)+'\n')
print('PASS: actual Driver affine palettes widen both hash-verified native eight-weight chest meshes')
print(json.dumps(results))
