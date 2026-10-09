"""Verify real native-object movement from the simulated controller-grab trace."""
import argparse
import csv
import json
import math
from pathlib import Path

def analyze(folder):
    result=json.loads((folder/'dxgi-probe-result.json').read_text())
    rows=list(csv.DictReader((folder/'native-physical-grab.csv').open()))
    for row in rows:
        for key in ('x','y','z','target_x','target_y','target_z','vx','vy','vz'):
            row[key]=float(row[key])
            if not math.isfinite(row[key]):raise ValueError('Nonfinite native grab evidence')
        row['release']=int(row['release'])
    holds=[r for r in rows if not r['release']]
    if not holds:raise ValueError('No native object motor samples')
    errors=sorted(math.dist([r[k] for k in ('x','y','z')],[r[k] for k in ('target_x','target_y','target_z')]) for r in holds)
    travel=max(r['z'] for r in holds)-min(r['z'] for r in holds)
    releases=[dict(entity_id=int(r['entity_id']),speed=math.hypot(r['vx'],r['vy'],r['vz'])) for r in rows if r['release']]
    passed=(result.get('physical_grab_hook_disabled') is True and result.get('physical_grab_picked',0)>=1 and
            result.get('physical_grab_dropped',0)>=1 and result.get('physical_grab_lost_ownership')==0 and
            not result.get('callback_failed') and travel>.15 and max(errors)<.15 and all(r['speed']<=6.001 for r in releases))
    return dict(passed=passed,simulated_controllers=result.get('simulated_vr') is True,native_game_object=True,
                motor_samples=len(holds),pickups=result.get('physical_grab_picked'),releases=releases,
                vertical_travel_native_units=travel,maximum_target_error_native_units=max(errors),p95_target_error_native_units=errors[int((len(errors)-1)*.95)],
                native_world_metric_scale_verified=False,
                motion_mode_changed=False,headset_acceptance=False,
                limits='Single nearby dynamic prop; does not establish large-object, weapon, character, collision-force, or full body acceptance.')

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('folder',type=Path);args=parser.parse_args()
    report=analyze(args.folder)
    (args.folder/'native-physical-grab-analysis.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2));return 0 if report['passed'] else 1

if __name__=='__main__':raise SystemExit(main())
