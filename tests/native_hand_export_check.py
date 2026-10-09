"""Offline hand-reference provenance and rig mismatch regression checks."""
import copy
import math
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from export_native_hand_poses import encode_references
from rig_fields import GAME_HASH


def report(closed):
    arms = []
    for side in ('left', 'right'):
        names = [f'unused_{i}' for i in range(69)]
        names[0] = side+'_wrist_jnt'
        names[60] = side+'_helper_thumb_0_1stdorsalmuscle_l_jnt'
        names[61] = side+'_helper_thumb_tendon_l_jnt'
        parents = [0]*69
        parents[0] = -1
        nodes = [dict(joint=i, name=names[i], position=[0., 0., 0.], rotation_columns=[[1., 0., 0.], [0., 1., 0.], [0., 0., 1.]]) for i in range(69)]
        for finger, digit in enumerate(('thumb', 'index', 'medium', 'ring', 'pinky')):
            base = 5+finger*4
            x = .05-.02*finger
            if finger:
                nodes[base-1]['position'] = [x, .075, 0.]
                parents[base] = base-1
            y, z = .1, 0.
            for segment in range(3):
                joint = base+segment
                names[joint] = f'{side}_{digit}_0{segment+(1 if finger == 0 else 2)}_jnt'
                if segment:
                    parents[joint] = joint-1
                    previous_angle = segment*.5 if closed else 0.
                    y += .025*math.cos(previous_angle)
                    z += .025*math.sin(previous_angle)
                angle = (segment+1)*.5 if closed else 0.
                nodes[joint]['position'] = [x, y, z]
                nodes[joint]['rotation_columns'] = [[1., 0., 0.], [0., math.cos(angle), math.sin(angle)], [0., -math.sin(angle), math.cos(angle)]]
        for i, node in enumerate(nodes):
            node['name'] = names[i]
        arms.append(dict(side=side, skeleton_joints=69, joint_names=names, sampled_joints=nodes,
            skeleton_layout=dict(parent_indices=parents, parent_hierarchy_verified=True),
            skinning_layout=dict(inverse_bind_rows=[[1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0.]]*69,
                                 source_unchanged=True, output_unchanged=True, maximum_absolute_error=0)))
    return dict(executable_sha256=GAME_HASH, game_memory_writes=False, body_samples=[dict(arms=arms)])


captures = {(side, pose): dict(report=report(pose == 'closed'), unmodified_native_palette=True,
    authored_pose_confirmed=True, authored_state='synthetic regression fixture', visual_evidence='synthetic.png')
    for side in ('left', 'right') for pose in ('open', 'closed')}
binary, arms = encode_references(captures)
assert binary[:8] == b'D2VRHP01' and len(binary) == 24780 and len(arms) == 2
for kind in ('missing-provenance', 'same-pose', 'wrong-build', 'wrong-rig', 'stretched-bone'):
    bad = copy.deepcopy(captures)
    target = bad[('left', 'closed')]
    if kind == 'missing-provenance':
        target['unmodified_native_palette'] = False
    elif kind == 'same-pose':
        target['report'] = report(False)
    elif kind == 'wrong-build':
        target['report']['executable_sha256'] = '0'*64
    elif kind == 'wrong-rig':
        target['report']['body_samples'][0]['arms'][0]['skinning_layout']['inverse_bind_rows'][0][3] = .1
    else:
        target['report']['body_samples'][0]['arms'][0]['sampled_joints'][10]['position'][0] += .1
    try:
        encode_references(bad)
    except ValueError:
        pass
    else:
        raise AssertionError(f'Accepted {kind}')
print('PASS: offline native hand archive format, authored provenance, nonidentical endpoints, pinned build, exact rig identity and bone geometry')
