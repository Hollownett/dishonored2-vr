"""Bounded offline spread trial; never a production or captured pose archive."""
import json
from pathlib import Path
from author_native_hand_candidate import authored,render,matrices
from export_native_hand_poses import verified_arm

def main():
    root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets'
    source=json.loads((root/'authored-grip-spread-trial/authored-candidate.json').read_text())
    report=json.loads(Path(source['source_readonly_report']).read_text())
    output=root/'authored-grip-right-spread-trial';output.mkdir(exist_ok=True)
    for item in source['arms']:
        arm=verified_arm(report,item['side']);e=item['evidence']
        parameters=e['thumb_opposition_rotation_vector_radians']+e['thumb_flex_radians']
        spread={'ring':-4,'pinky':-8}if item['side']=='left'else {'medium':2,'ring':-8,'pinky':-16}
        full,evidence=authored(arm,driven_helpers=True,relaxed_knuckles=True,thumb_clearance=e['thumb_clearance_native_units'],thumb_parameters=parameters,finger_splay_degrees=spread)
        released,index_evidence=authored(arm,0.,True,True,thumb_clearance=e['thumb_clearance_native_units'],thumb_parameters=parameters,finger_splay_degrees=spread)
        evidence['offline_finger_splay_degrees']=spread
        item.update(evidence=evidence,index_evidence=index_evidence,authored_transform_matrices=[m.tolist()for m in full],
                    surfaces=render(arm,[('Native idle',matrices(arm)),('Authored wide spread',full),('Index released',released)],root/(item['side']+'-arm.bmd6model'),output/(item['side']+'-authored-candidate.png')))
    (output/'authored-candidate.json').write_text(json.dumps(source,indent=2)+'\n')
    print(output)

if __name__=='__main__':main()
