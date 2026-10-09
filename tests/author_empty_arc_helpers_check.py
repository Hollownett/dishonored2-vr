"""Offline helper pivot-arc regression, no native animation claims."""
import json,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from compare_empty_arc_helpers import ArcWebClosure
from author_empty_contact_closure import locals_of
from export_native_hand_poses import verified_arm
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';b=json.loads((root/'authored-grip-contact-search-trial/authored-candidate.json').read_text());r=json.loads(Path(b['source_readonly_report']).read_text())
for old in b['arms']:
 m=ArcWebClosure(verified_arm(r,old['side']),old['authored_transform_matrices'],root/(old['side']+'-arm.bmd6model'));p=np.array([1.25,.8,1.2]*4+[1.,1.3,1.]+[0.]*4+[1.,1.3]*4+[0.]*12);local=locals_of(m.pose(p),m.parents)
 for j in m.finger_helpers:
  if '_bot_'not in m.names[j]:continue
  import re
  found=re.search(r'helper_(index|middle|ring|pinky)_(\d)_',m.names[j]);digit=('index','middle','ring','pinky').index(found[1]);child=m.main[digit][int(found[2])];pivot=m.native[child][:3,3]
  assert abs(np.linalg.norm(local[j][:3,3]-pivot)-np.linalg.norm(m.native[j][:3,3]-pivot))<1e-6
 for j in sum(m.main,[]):assert np.linalg.norm(local[j][:3,3]-m.native[j][:3,3])<1e-6
 released=locals_of(m.pose(p,True),m.parents)
 for j in m.finger_helpers:
  if 'helper_index_'in m.names[j]:assert np.max(np.abs(released[j]-m.native[j]))<1e-5
print('PASS: exact helper pivot radius, unchanged main lengths, source-exact index release')
