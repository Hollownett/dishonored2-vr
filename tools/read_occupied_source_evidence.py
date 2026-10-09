"""Qualified native occupied Sword source; never certifies an Empty fist."""
import json
from pathlib import Path
from read_empty_source_evidence import run_evidence,validate_inactive_carry
def occupied_source_evidence(pose,companion):
 result=run_evidence(pose,companion);report=json.loads(Path(pose).read_text(encoding='utf-8-sig'));validate_inactive_carry(report);hands=report['native_hand_state']['hands']
 if [h.get('side')for h in hands]!=['left','right']or any(h.get('node_ownership_verified')is not True for h in hands)or hands[0].get('equip_name')!='Empty'or hands[0].get('slot')!=0 or hands[1].get('equip_name')!='Sword'or not hands[1].get('slot'):raise ValueError('Require owned left Empty/right occupied Sword')
 result.update(per_pose_occupied_sword_and_inactive_carry_verified=True,occupied_endpoint_is_Empty_fist=False);return result
