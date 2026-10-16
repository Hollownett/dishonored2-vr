"""Synthetic three-state qualification; unchanged historical two-state provenance."""
import copy,json,sys,tempfile,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from read_empty_source_evidence import validate_empty_carry,source_evidence,run_evidence
root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets/native-empty-carryqualified-v96-20261009';posepath=root/'pose-0172.json';companion=root/'run-provenance.json';raw=posepath.read_bytes();historical=json.loads(raw)
digest=hashlib.sha256(raw).hexdigest();companion_raw=companion.read_bytes()
assert digest=='15af42a502788b42cffe7a052d2ddffff34586d47cafb6318b2fefa0aac02927'
assert [state['index']for state in historical['carry_before']['states']]==[49,45]
for operation in (lambda:validate_empty_carry(historical),lambda:source_evidence(posepath,companion)):
 try:operation()
 except ValueError:pass
 else:raise AssertionError('Legacy unknown HoldObject50 accepted')
q=run_evidence(posepath,companion);assert q['tracked_arm_writes_absent_by_archived_run']and 'per_pose_empty_and_inactive_carry_verified'not in q
assert historical['tracked_arm_writes_absent_verified']is False
carry=dict(typed_owner_verified=True,carrying=False,active=[False,False,False],player=0x20000,body=0x30000,machine=0x40000,pool=0x50000,body_identity='synthetic',states=[dict(index=index,state=0x60000+offset*0x1000,declaration=0x70000+offset*0x1000)for offset,index in enumerate((49,45,50))])
pose=dict(synthetic_fixture=True,sustained_owned_state_bits=3,sustained_empty_bits=3,carry_consistent=True,carry_before=copy.deepcopy(carry),carry_after=copy.deepcopy(carry),native_hand_state=dict(player=0x20000,body=0x30000,body_identity='synthetic',hands=[dict(side=side,slot=0,equip_name='Empty',node_ownership_verified=True)for side in ('left','right')]))
assert validate_empty_carry(pose)
for mutate in [lambda p:p.update(carry_consistent=False),lambda p:p['carry_after'].update(player=1),lambda p:p['carry_before']['active'].__setitem__(0,True),lambda p:p['carry_before'].update(typed_owner_verified=False),lambda p:p['native_hand_state']['hands'][1].update(slot=42),lambda p:p['native_hand_state']['hands'][0].update(equip_name='Blink'),lambda p:p['native_hand_state'].update(body_identity='stale'),lambda p:p['carry_before']['states'][0].update(state=0)]:
 bad=copy.deepcopy(pose);mutate(bad)
 try:validate_empty_carry(bad)
 except ValueError:pass
 else:raise AssertionError('Unsafe Empty source accepted')
for mutate in (lambda carry:carry['active'].__setitem__(2,True),lambda carry:carry['states'].pop(),lambda carry:carry['states'][2].update(index=49),lambda carry:carry.update(active=[False,False]),lambda carry:carry['states'][2].update(declaration=0)):
 bad=copy.deepcopy(pose);mutate(bad['carry_before']);bad['carry_after']=copy.deepcopy(bad['carry_before'])
 try:validate_empty_carry(bad)
 except ValueError:pass
 else:raise AssertionError('Unknown/active synthetic HoldObject50 accepted')
with tempfile.TemporaryDirectory(prefix='empty-source-check-')as folder:
 for key in ('pose','archive'):
  data=json.loads(companion.read_text())
  if key=='pose':next(x for x in data['poses']if Path(x['path']).resolve()==posepath.resolve())['sha256']='0'*64
  else:data['evidence']['diagnostics']['sha256']='0'*64
  target=Path(folder)/('bad-'+key+'.json');target.write_text(json.dumps(data))
  try:run_evidence(posepath,target)
  except ValueError:pass
  else:raise AssertionError('Changed source evidence accepted')
assert posepath.read_bytes()==raw and hashlib.sha256(posepath.read_bytes()).hexdigest()==digest and companion.read_bytes()==companion_raw
print('PASS: synthetic three-state Empty guards; historical two-state qualification rejects; unchanged archive hashes/provenance')
