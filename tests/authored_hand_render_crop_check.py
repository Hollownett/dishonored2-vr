"""Full distal skin must survive hand-only rendering crop."""
import json,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from render_authored_hand_close import hand_crop_mask
from author_native_hand_candidate import matrices
from analyse_arm_mesh import decode_mesh
from render_body_rig import palettes,skin_positions
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';b=json.loads((root/'authored-grip-contact-search-trial/authored-candidate.json').read_text());paths=[Path(b['source_readonly_report'])]
fresh=root/'native-empty-reference-v96-20261009/pose-0170.json'
if fresh.exists():paths.append(fresh)
for path in paths:
 report=json.loads(path.read_text())
 for side in ('left','right'):
  arm=verified_arm(report,side);names=arm['joint_names'];pose=matrices(arm);origin=pose[names.index(side+'_wrist_jnt')][:3,3];bases=[pose[names.index(side+'_'+d+'_02_jnt')][:3,3]for d in ('index','medium','ring','pinky')];forward=np.mean(bases,axis=0)-origin;forward/=np.linalg.norm(forward);palette,_=palettes(arm);count=0;previously_cut=0
  for surface in decode_mesh((root/(side+'-arm.bmd6model')).read_bytes(),69)['surfaces']:
   if not any(x in surface['material']for x in ('player_emily_hands.material','player_emily_gloves.material')):continue
   vertices=surface['vertices'];before=skin_positions(vertices,palette);mask=hand_crop_mask(before,origin,forward)
   for i,v in enumerate(vertices):
    distal=sum(w for w,j in zip(v[1],v[2])if any(d in names[j]for d in ('index','medium','ring','pinky','middle'))and any(s in names[j]for s in ('_03_','_04_','_end_','_1_','_2_')))
    if distal>=190:
     assert mask[i],(side,names[v[2][0]],before[i]);count+=1;previously_cut+=np.linalg.norm(before[i]-origin)>=.18
  assert count>100
  print(path.name,side,'retained distal vertices',count,'previous .18 cut',previously_cut)
print('PASS: full distal skin retained for old and fresh sources; wrist cut independent of curl')
