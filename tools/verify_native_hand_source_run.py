"""Attach hashed run evidence to new captures; never certifies authored fist quality."""
import argparse
import hashlib
import json
from pathlib import Path
from rig_fields import GAME_HASH


def verify(capture,config,diagnostics):
    if capture.get('executable_sha256')!=GAME_HASH or config.get('executable_sha256')!=GAME_HASH:raise ValueError('Pinned executable provenance missing')
    if not capture.get('pid') or capture['pid']!=config.get('pid') or capture['pid']!=diagnostics.get('pid'):raise ValueError('Run PID mismatch')
    start,end=capture.get('started_unix'),capture.get('ended_unix')
    if not isinstance(start,(int,float)) or not isinstance(end,(int,float)) or not config.get('started_unix',float('inf'))<=start<=end<=config.get('ended_unix',-1):raise ValueError('Capture interval not covered by run')
    for key in ('tracked_arms_requested','arm_palette_copy_requested'):
        if config.get(key) is not False or diagnostics.get(key) is not False:raise ValueError('Native pose writer enabled or unknown: '+key)
    for key in ('native_arm_pose_calls','native_arm_copy_calls'):
        if diagnostics.get(key)!=[0,0]:raise ValueError('Native palette writes present or unknown: '+key)
    if diagnostics.get('hook_disabled') is not True or diagnostics.get('first_chance_fault_records')!=0:raise ValueError('Final clean diagnostics required')
    for sample in capture.get('samples',[]):
        if 'pose_report' in sample:
            state=sample.get('state',{})
            if state.get('carry_consistent') is not True or state.get('native_carry',{}).get('typed_owner_verified') is not True:raise ValueError('Per-pose carry evidence missing')
    return True


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',type=Path,required=True);p.add_argument('--run-config',type=Path,required=True);p.add_argument('--diagnostics',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('Refuse overwrite companion')
    def load(path):return json.loads(path.read_text(encoding='utf-8-sig'))
    capture,config,diag=map(load,(a.capture,a.run_config,a.diagnostics));verify(capture,config,diag)
    evidence={name:dict(path=str(path.resolve()),sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for name,path in (('capture',a.capture),('run_config',a.run_config),('diagnostics',a.diagnostics))}
    poses=[]
    for sample in capture['samples']:
        if 'pose_report' not in sample:continue
        path=a.capture.parent/sample['pose_report']
        if path.resolve().parent!=a.capture.resolve().parent:raise ValueError('Pose path escapes capture')
        pose=load(path)
        if pose.get('pid')!=capture['pid'] or pose.get('executable_sha256')!=GAME_HASH or not capture['started_unix']<=pose.get('captured_unix',-1)<=capture['ended_unix']:raise ValueError('Pose run mismatch')
        if pose.get('carry_consistent') is not True or pose.get('carry_before')!=pose.get('carry_after') or pose.get('carry_before')!=sample['state']['native_carry']:raise ValueError('Pose carry tuple mismatch')
        poses.append(dict(path=str(path.resolve()),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),tracked_arm_writes_absent_verified=True))
    if not poses:raise ValueError('No pose evidence')
    result=dict(schema='D2VR-native-source-run-provenance-v1',pid=capture['pid'],executable_sha256=GAME_HASH,evidence=evidence,poses=poses,tracked_arm_writes_absent_verified=True,authored_pose_confirmed=False,authored_fist_captured=False,scope='matching archived run configuration and final diagnostics; no visual certification')
    a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(output=str(a.output),poses=len(poses))))

if __name__=='__main__':main()
