"""Make an offline IK fixture from verified hierarchy and native upload records."""
import argparse
import csv
import json
from pathlib import Path
import struct


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pose-report',type=Path,required=True)
    parser.add_argument('--hierarchy-report',type=Path,required=True)
    parser.add_argument('--upload-run',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    workspace=Path(__file__).resolve().parents[1]
    for path in (args.pose_report,args.hierarchy_report,args.upload_run,args.output):
        if not path.resolve().is_relative_to(workspace):
            parser.error('Use workspace diagnostic artifacts only')
    source=json.loads(args.pose_report.read_text())
    current=json.loads(args.hierarchy_report.read_text())
    game_hash='b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180'
    if any(report.get('executable_sha256')!=game_hash or report.get('game_memory_writes') is not False
           for report in (source,current)):
        raise ValueError('Require read-only reports from the pinned game build')
    old={arm['side']:arm for arm in source['body_samples'][0]['arms']}
    new={arm['side']:arm for arm in current['body_samples'][0]['arms']}
    rows=list(csv.DictReader((args.upload_run/'native-arm-upload.csv').open(newline='')))
    data=(args.upload_run/'native-arm-palettes.bin').read_bytes()
    if not data or len(data)>1024*1024 or len(data)%4476:
        raise ValueError('Invalid bounded native palette archive')
    fixture=bytearray(b'D2VRAF01')
    evidence=[]
    for side_index,side in enumerate(('left','right')):
        prior,live=old[side],new[side]
        if prior['joint_names']!=live['joint_names'] or live['skeleton_joints']!=69 or \
                live['skeleton_layout']['parent_hierarchy_verified'] is not True:
            raise ValueError('Joint order or verified hierarchy differs')
        names=live['joint_names']
        chain=[names.index(side+suffix) for suffix in ('_arm_jnt','_elbow_jnt','_wrist_jnt')]
        rows_for_side=[row for row in rows if int(row['side'])==side_index and int(row['model'])==prior['model']]
        if not rows_for_side:
            raise ValueError('Native upload model does not match its named pose report')
        row=rows_for_side[0];offset=int(row['palette_offset'])
        if int(row['caller_count'])!=69 or offset<0 or offset%4476 or offset+4476>len(data):
            raise ValueError('Invalid native upload record')
        fixture.extend(struct.pack('<69h',*live['skeleton_layout']['parent_indices']))
        fixture.extend(struct.pack('<3I',*chain))
        fixture.extend(data[offset:offset+4476])
        evidence.append(dict(side=side,chain=chain,upload_model=int(row['model']),upload_offset=offset,
                             named_upload_report_pid=source['pid'],hierarchy_report_pid=current['pid']))
    if len(fixture)!=9260:
        raise ValueError('Unexpected offline fixture size')
    args.output.write_bytes(fixture)
    args.output.with_suffix('.json').write_text(json.dumps(dict(game_memory_writes=False,
        renderer_pose_substitution=False,offline_simulation_only=True,arms=evidence),indent=2)+'\n')
    print(json.dumps(dict(fixture=str(args.output),bytes=len(fixture),offline_simulation_only=True)))


if __name__=='__main__':
    main()
