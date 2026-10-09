"""Qualified source rejects active/unknown/stale carry, wrong equipment and changed hashes."""
import copy,json,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from read_empty_source_evidence import validate_empty_carry,source_evidence
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets/native-empty-carryqualified-v96-20261009';posepath=root/'pose-0172.json';companion=root/'run-provenance.json';raw=posepath.read_bytes();pose=json.loads(raw);assert validate_empty_carry(pose);q=source_evidence(posepath,companion);assert q['tracked_arm_writes_absent_by_archived_run']and q['per_pose_empty_and_inactive_carry_verified'];assert posepath.read_bytes()==raw;assert pose['tracked_arm_writes_absent_verified']is False
for mutate in [lambda p:p.update(carry_consistent=False),lambda p:p['carry_after'].update(player=1),lambda p:p['carry_before']['active'].__setitem__(0,True),lambda p:p['carry_before'].update(typed_owner_verified=False),lambda p:p['native_hand_state']['hands'][1].update(slot=42),lambda p:p['native_hand_state']['hands'][0].update(equip_name='Blink'),lambda p:p['native_hand_state'].update(body_identity='stale'),lambda p:p['carry_before']['states'][0].update(state=0)]:
 bad=copy.deepcopy(pose);mutate(bad)
 try:validate_empty_carry(bad)
 except ValueError:pass
 else:raise AssertionError('Unsafe Empty source accepted')
with tempfile.TemporaryDirectory(prefix='empty-source-check-')as folder:
 for key in ('pose','archive'):
  data=json.loads(companion.read_text())
  if key=='pose':next(x for x in data['poses']if Path(x['path']).resolve()==posepath.resolve())['sha256']='0'*64
  else:data['evidence']['diagnostics']['sha256']='0'*64
  target=Path(folder)/('bad-'+key+'.json');target.write_text(json.dumps(data))
  try:source_evidence(posepath,target)
  except ValueError:pass
  else:raise AssertionError('Changed source evidence accepted')
print('PASS: exact qualified source hashes; before/after carry, generation/equipment/unknown guards; original flags unchanged')
