"""Extract and verify the owned Emily body mesh for private offline VR diagnostics."""
import argparse
import hashlib
import json
from pathlib import Path
from extract_arm_assets import index_entries,resource_path,exact,unpack_asset,MAX_ASSET
from analyse_arm_mesh import decode_mesh
from rig_fields import GAME_HASH


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pose-report',type=Path,required=True)
    parser.add_argument('--game',type=Path,default=Path(r'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2'))
    args=parser.parse_args();workspace=Path(__file__).resolve().parents[1];game=args.game.resolve()
    report=args.pose_report.resolve()
    if not report.is_relative_to(workspace):raise ValueError('Require a workspace pose report')
    v=json.loads(report.read_text(encoding='utf-8-sig'))
    if v['executable_sha256']!=GAME_HASH or v['game_memory_writes'] is not False:raise ValueError('Require pinned read-only pose evidence')
    body=v['rig_samples'][0];surfaces=body.get('geometry_layout',{}).get('surfaces')
    if body['skeleton_joints']!=20 or body['pose_state']!=3 or not surfaces:raise ValueError('Owned complete 20-joint body layout is missing')
    with (game/'Dishonored2.exe').open('rb') as f:
        if hashlib.file_digest(f,'sha256').hexdigest()!=GAME_HASH:raise ValueError('Unsupported executable')
    source='models/characters/player/emily/player_body_emily.md6mesh';candidates=[]
    for container in ('game1','game2','game3','game4'):
        index=game/'base'/(container+'_patch.index')
        if not index.is_file() or index.stat().st_size>64*1024*1024:raise ValueError('Final bounded patch index is missing')
        with index.open('rb') as f:
            for entry in index_entries(f):
                if entry['type']=='baseModel' and entry['source'].replace('\\','/').lower()==source and entry['size'] and entry['packed']:
                    candidates.append((container,index,entry))
    if len(candidates)!=1:raise ValueError('Native Emily body resource is not unique')
    container,index,entry=candidates[0]
    if max(entry['size'],entry['packed'])>MAX_ASSET:raise ValueError('Body resource exceeds bounded extraction size')
    resource=resource_path(game/'base',container,entry['resource_id'])
    if entry['offset']<4 or entry['offset']+entry['packed']>resource.stat().st_size:raise ValueError('Body resource exceeds its container')
    with resource.open('rb') as f:
        if exact(f,4)!=b'\x04SER':raise ValueError('Resource magic differs')
        f.seek(entry['offset']);data=unpack_asset(exact(f,entry['packed']),entry['size'])
    mesh=decode_mesh(data,20)
    if not mesh['skeleton'].endswith('/player_body_emily.md6skl') or len(mesh['surfaces'])!=len(surfaces):raise ValueError('Body declaration/surface count differs from owned rig')
    for native,decoded in zip(surfaces,mesh['surfaces']):
        if native['vertices']!=decoded['vertices_count'] or native['indices']!=decoded['indices_count'] or native['vertex_stride']!=44:
            raise ValueError('Native body surface counts/layout differ')
        if any(abs(decoded['bounds'][i]-native['bounds'][i//3][i%3])>1e-5 for i in range(6)):
            raise ValueError('Native body geometry bounds differ')
    output=workspace/'artifacts/research/native-body-assets';output.mkdir(exist_ok=True)
    if output.is_relative_to(game):raise ValueError('Keep private diagnostics outside the game installation')
    target=output/'body-emily.bmd6model'
    if target.exists() and target.read_bytes()!=data:raise ValueError('Preserve the existing body asset before replacement')
    if not target.exists():target.write_bytes(data)
    evidence={'executable_sha256':GAME_HASH,'game_files_modified':False,'private_diagnostic_only':True,
        'source':str(index),'resource':str(resource),'entry':entry,'asset_sha256':hashlib.sha256(data).hexdigest(),
        'owned_model':body['model'],'skeleton':mesh['skeleton'],'vertices':sum(x['vertices_count'] for x in mesh['surfaces']),
        'indices':sum(x['indices_count'] for x in mesh['surfaces']),'all_weights_sum_255':True,
        'all_nonzero_weight_indices_within_20_joints':True,'footer_material_metadata_complete':False}
    (output/'extraction.json').write_text(json.dumps(evidence,indent=2)+'\n')
    print(json.dumps(evidence));return 0


if __name__=='__main__':raise SystemExit(main())
