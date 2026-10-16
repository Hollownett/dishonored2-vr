"""Read-only hashed run qualification for private Empty-source use; no flag rewriting."""
import hashlib,json
from pathlib import Path
from verify_native_hand_source_run import verify

def validate_inactive_carry(pose):
 state=pose.get('native_hand_state',{});before=pose.get('carry_before');after=pose.get('carry_after')
 if pose.get('sustained_owned_state_bits')!=3:raise ValueError('Not sustained dual hand owner')
 if pose.get('carry_consistent')is not True or before!=after or not isinstance(before,dict):raise ValueError('Unstable per-pose carry tuple')
 if before.get('typed_owner_verified')is not True or before.get('carrying')is not False or before.get('active')!=[False,False,False]or[s.get('index')for s in before.get('states',[])]!=[49,45,50]:raise ValueError('Unknown/active native carry, including HoldObject50')
 if any(not s.get('state')or not s.get('declaration')for s in before.get('states',[])):raise ValueError('Missing typed carry state identity')
 if before.get('body_identity')!=state.get('body_identity'):raise ValueError('Carry body generation mismatch')
 if any(not before.get(k)for k in ('player','body','machine','pool'))or before['player']!=state.get('player')or before['body']!=state.get('body'):raise ValueError('Carry owner mismatch')
 return True

def validate_empty_carry(pose):
 validate_inactive_carry(pose)
 if pose.get('sustained_empty_bits')!=3:raise ValueError('Not sustained dual Empty owner')
 hands=pose.get('native_hand_state',{}).get('hands',[])
 if [h.get('side')for h in hands]!=['left','right']or any(h.get('slot')!=0 or h.get('equip_name')!='Empty'or h.get('node_ownership_verified')is not True for h in hands):raise ValueError('Exact Empty equipment owner missing')
 return True

def run_evidence(pose_path,companion_path):
 pose_path=Path(pose_path);companion_path=Path(companion_path);pose=json.loads(pose_path.read_text(encoding='utf-8-sig'));companion=json.loads(companion_path.read_text(encoding='utf-8-sig'))
 if companion.get('schema')!='D2VR-native-source-run-provenance-v1':raise ValueError('Wrong source companion schema')
 evidence={}
 for key in ('capture','run_config','diagnostics'):
  entry=companion['evidence'][key];data=Path(entry['path']).read_bytes()
  if hashlib.sha256(data).hexdigest()!=entry['sha256']:raise ValueError('Archived evidence hash changed: '+key)
  evidence[key]=json.loads(data.decode('utf-8-sig'))
 verify(evidence['capture'],evidence['run_config'],evidence['diagnostics'])
 entries=[x for x in companion.get('poses',[])if Path(x['path']).resolve()==pose_path.resolve()]
 if len(entries)!=1 or entries[0].get('tracked_arm_writes_absent_verified')is not True or entries[0]['sha256']!=hashlib.sha256(pose_path.read_bytes()).hexdigest():raise ValueError('Pose not covered by exact writer-absence evidence')
 if pose.get('pid')!=companion.get('pid')or pose.get('executable_sha256')!=companion.get('executable_sha256'):raise ValueError('Pose/run identity mismatch')
 return dict(companion=str(companion_path.resolve()),companion_sha256=hashlib.sha256(companion_path.read_bytes()).hexdigest(),tracked_arm_writes_absent_by_archived_run=True,original_pose_flags_unchanged=True,authored_fist_captured=False,authored_endpoint_certified=False)

def source_evidence(pose_path,companion_path):
 result=run_evidence(pose_path,companion_path);validate_empty_carry(json.loads(Path(pose_path).read_text(encoding='utf-8-sig')));result['per_pose_empty_and_inactive_carry_verified']=True;return result
