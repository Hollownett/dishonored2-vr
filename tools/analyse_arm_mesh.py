"""Decode the exact installed planar arm meshes for offline skinning checks.

Layout comes from the pinned native loaders and read-only vertex-element report.
Writes a private diagnostic fixture, never engine memory or game resources.
"""
import argparse
import hashlib
import io
import json
import math
from pathlib import Path
import struct
from extract_arm_assets import exact
from rig_fields import GAME_HASH

def decode_mesh(data,palette_joints=69):
    if palette_joints not in (20,69):raise ValueError('Unverified native body/arm palette size')
    if not 100<=len(data)<=32*1024*1024 or data[:4]!=b'MM6B' or data[-4:]!=b'MM6B':
        raise ValueError('Invalid compiled MD6 arm mesh')
    stream=io.BytesIO(data);exact(stream,4)
    def values(fmt): return struct.unpack(fmt,exact(stream,struct.calcsize(fmt)))
    def count(): return values('<I')[0]
    def name():
        n=count()
        if not 0<n<=4096: raise ValueError('Invalid MD6 string length')
        return exact(stream,n).decode('utf-8')
    skeleton=name();exact(stream,2);values('<6f');n=count()
    if not 1<=n<=64: raise ValueError('Excessive MD6 surface count')
    surfaces=[]
    for _ in range(n):
        surface,material=name(),name();kind=count();exact(stream,5)
        if exact(stream,4)!=b'BTRI' or count()!=0x10002: raise ValueError('Unverified MD6 geometry version')
        cpu,gpu=count(),count();transform=values('<10f');vertices=count();stream_bytes=values('<Q')[0]
        if cpu!=10 or gpu!=10 or transform!=(1.,1.,1.,0.,0.,0.,1.,1.,0.,0.) or stream_bytes!=0:
            raise ValueError('Unverified arm vertex format, transform or streamed payload')
        if not 0<vertices<=500000: raise ValueError('Excessive vertex count')
        raw=exact(stream,vertices*44);indices=count()
        if not 0<indices<=3000000 or indices%3: raise ValueError('Invalid mesh index count')
        index_bytes=exact(stream,indices*2)
        if max(values[0] for values in struct.iter_unpack('<H',index_bytes))>=vertices:
            raise ValueError('Mesh triangle index exceeds its own vertex stream')
        bounds=values('<6f');count();ranges=count()
        if not 1<=ranges<=1024: raise ValueError('Invalid native draw range count')
        exact(stream,ranges*12+48)
        decoded=[];skin_start=vertices*28
        for vertex in range(vertices):
            position=struct.unpack_from('<3f',raw,vertex*12)
            if any(not math.isfinite(v) or not bounds[a]-1e-5<=v<=bounds[a+3]+1e-5 for a,v in enumerate(position)):
                raise ValueError('Position stream disagrees with native surface bounds')
            at=skin_start+vertex*16;weights=raw[at:at+8];joints=raw[at+8:at+16]
            if sum(weights)!=255 or any(j>=palette_joints for w,j in zip(weights,joints) if w):
                raise ValueError('Native eight-weight skin record exceeds the owned palette')
            decoded.append((position,weights,joints))
        surfaces.append(dict(name=surface,material=material,kind=kind,vertices=decoded,
            indices=index_bytes,bounds=bounds,vertices_count=vertices,indices_count=indices))
    return dict(skeleton=skeleton,surfaces=surfaces,undecoded_footer_bytes=len(data)-stream.tell())

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pose-report',type=Path,required=True)
    args=parser.parse_args();workspace=Path(__file__).resolve().parents[1]
    report=args.pose_report.resolve()
    if not report.is_relative_to(workspace): raise ValueError('Use a workspace-owned read-only pose report')
    evidence=json.loads(report.read_text())
    if evidence['executable_sha256']!=GAME_HASH or evidence['game_memory_writes'] is not False:
        raise ValueError('Require the pinned executable and read-only pose report')
    root=workspace/'artifacts/research/native-arm-assets'
    extraction=json.loads((root/'extraction.json').read_text())
    fixture=bytearray(b'D2VRMS01');fixture.extend(struct.pack('<I',2));result=[]
    arms={arm['side']:arm for arm in evidence['body_samples'][0]['arms']}
    for side in ('left','right'):
        arm=arms[side];skin=arm['skinning_layout'];source=root/(side+'-arm.bmd6model');data=source.read_bytes()
        digest=hashlib.sha256(data).hexdigest()
        if not any(entry['side']==side and entry['sha256']==digest for entry in extraction['entries']):
            raise ValueError('Arm resource changed after bounded extraction')
        mesh=decode_mesh(data);surfaces=arm['geometry_layout']['surfaces']
        if not mesh['skeleton'].endswith(f'/player_{side}arm_emily.md6skl') or arm['skeleton_joints']!=69 or \
                not arm['skeleton_layout']['parent_hierarchy_verified'] or not skin['source_unchanged'] or \
                not skin['output_unchanged'] or skin['maximum_absolute_error'] is None or skin['maximum_absolute_error']>1e-4:
            raise ValueError('Mesh skeleton or native inverse-bind comparison is not stable')
        if len(surfaces)!=len(mesh['surfaces']): raise ValueError('Live mesh surface count differs')
        if len(arm['sampled_joints'])!=69 or [j['joint'] for j in arm['sampled_joints']]!=list(range(69)) or \
                len(skin['inverse_bind_rows'])!=69 or any(len(row)!=12 for row in skin['inverse_bind_rows']):
            raise ValueError('Native pose or inverse-bind record order differs')
        named=[j['name'] for j in arm['sampled_joints']]
        grip_roles=[f'{side}_wrist_jnt']+[f'{side}_{finger}_02_jnt' for finger in ('index','medium','ring','pinky')]
        if [named.index(role) for role in grip_roles]!=[4,16,27,38,49]:
            raise ValueError('Named wrist/knuckle indices differ from the offline mesh replay')
        helper_roles=[f'{side}_lower_armhelper{i}_jnt' for i in (1,2,3)]+[f'{side}_wrist_rotation_jnt']
        if [named.index(role) for role in helper_roles]!=[64,65,66,67] or \
                any(arm['skeleton_layout']['parent_indices'][i]!=3 for i in (64,65,66,67)):
            raise ValueError('Named forearm helper indices or parents differ')
        for live,parsed in zip(surfaces,mesh['surfaces']):
            elements=[(e['semantic'],e['index'],e['input_slot'],e['offset']) for e in live['vertex_elements']]
            expected=[('VERTEX_POSITION',0,0,0),('VERTEX_TEXCOORD',0,1,0),('VERTEX_TEXCOORD',1,1,4),
                ('VERTEX_NORMAL',0,2,0),('VERTEX_TANGENT',0,3,0),('VERTEX_SKINWEIGHTS',0,4,0),
                ('VERTEX_SKINWEIGHTS',1,4,4),('VERTEX_SKININDICES',0,4,8),('VERTEX_SKININDICES',1,4,12)]
            packed_formats=[151453952,84345088,84345088,168034560,168034560,168100096,168100096,167969024,167969024]
            if live['format_id']!=10 or live['vertex_stride']!=44 or elements!=expected or \
                    [e['packed_format'] for e in live['vertex_elements']]!=packed_formats or \
                    live['vertices']!=parsed['vertices_count'] or live['indices']!=parsed['indices_count']:
                raise ValueError('Packed arm stream does not match the actual native shader elements')
        fixture.extend(struct.pack('<69h',*arm['skeleton_layout']['parent_indices']))
        for joint in arm['sampled_joints']:
            matrix=[]
            for column in joint['rotation_columns']+[joint['position']]: matrix.extend(column+[0.])
            fixture.extend(struct.pack('<16f',*matrix))
        for inverse in skin['inverse_bind_rows']: fixture.extend(struct.pack('<12f',*inverse))
        fixture.extend(struct.pack('<I',len(mesh['surfaces'])))
        for surface in mesh['surfaces']:
            fixture.extend(struct.pack('<I',surface['vertices_count']))
            for position,weights,joints in surface['vertices']: fixture.extend(struct.pack('<3f',*position)+weights+joints)
            fixture.extend(struct.pack('<I',surface['indices_count'])+surface['indices'])
        result.append(dict(side=side,mesh_sha256=digest,surfaces=len(surfaces),vertices=sum(s['vertices_count'] for s in mesh['surfaces']),
            all_weight_sums_255=True,nonzero_joint_indices_within_owned_69_palette=True,
            native_shader_vertex_elements_match=True,undecoded_footer_bytes=mesh['undecoded_footer_bytes']))
        result[-1]['hand_bind_joint_names']=grip_roles
        result[-1]['hand_bind_joint_indices']=[4,16,27,38,49]
        result[-1]['forearm_helper_joint_names']=helper_roles
        result[-1]['forearm_helper_joint_indices']=[64,65,66,67]
    (root/'mesh-fixture.bin').write_bytes(fixture)
    (root/'mesh-analysis.json').write_text(json.dumps(dict(pose_report=str(report),game_memory_writes=False,
        geometry_deformation_in_headset_verified=False,arms=result),indent=2)+'\n')
    print(json.dumps(dict(fixture_bytes=len(fixture),arms=result,game_memory_writes=False)))

if __name__=='__main__':main()
