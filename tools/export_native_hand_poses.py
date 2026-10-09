"""Export private native open/fist references from labelled, unmodified captures.

This does not read/write live game memory and cannot certify headset hand quality.
Each of the four manifest entries names a read-only rig report and visual evidence.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from rig_fields import GAME_HASH


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def local(pose, parents, joint):
    parent = pose[parents[joint]]
    node = pose[joint]
    axes = [node['rotation_columns'][i] for i in range(3)]
    parent_axes = parent['rotation_columns']
    offset = [a-b for a, b in zip(node['position'], parent['position'])]
    return [[dot(a, b) for b in parent_axes] for a in axes], [dot(offset, a) for a in parent_axes]


def verified_arm(report, side):
    if report.get('executable_sha256') != GAME_HASH or report.get('game_memory_writes') is not False:
        raise ValueError('Require a read-only report from the pinned executable')
    arms = report['body_samples'][0]['arms']
    found = [arm for arm in arms if arm['side'] == side]
    if len(found) != 1:
        raise ValueError('Native capture has ambiguous hand ownership')
    arm = found[0]
    parents = arm['skeleton_layout']['parent_indices']
    names = arm['joint_names']
    pose = arm['sampled_joints']
    skin = arm['skinning_layout']
    if arm['skeleton_joints'] != 69 or arm['skeleton_layout']['parent_hierarchy_verified'] is not True or \
            len(parents) != 69 or len(names) != 69 or len(set(names)) != 69 or len(pose) != 69 or \
            len(skin['inverse_bind_rows']) != 69 or skin['source_unchanged'] is not True or skin['output_unchanged'] is not True or \
            skin['maximum_absolute_error'] is None or skin['maximum_absolute_error'] > 1e-4:
        raise ValueError('Native pose, hierarchy or inverse bind is not stable/verified')
    for joint, node in enumerate(pose):
        if node['joint'] != joint or node['name'] != names[joint]:
            raise ValueError('Named native joint order changed')
        at = joint
        for _ in range(69):
            at = parents[at]
            if at == -1:
                break
            if not 0 <= at < 69:
                raise ValueError('Native hierarchy exceeds the palette')
        else:
            raise ValueError('Native hierarchy contains a cycle')
        axes = node['rotation_columns']
        if len(axes) != 3 or any(len(a) != 3 for a in axes) or len(node['position']) != 3 or \
                any(not math.isfinite(v) or abs(v) > 1e7 for a in axes+[node['position']] for v in a) or \
                any(abs(dot(axes[i], axes[j])-(i == j)) > .03 for i in range(3) for j in range(3)):
            raise ValueError('Native palette is not rigid/finite')
    return arm


def encode_references(captures):
    """captures[(side, pose)] supplies already-loaded reports + provenance."""
    output = bytearray(b'D2VRHP01'+GAME_HASH.encode('ascii'))
    summaries = []
    for side_index, side in enumerate(('left', 'right')):
        selected = []
        for state in ('open', 'closed'):
            capture = captures[(side, state)]
            if capture.get('unmodified_native_palette') is not True or capture.get('authored_pose_confirmed') is not True or \
                    not isinstance(capture.get('authored_state'), str) or not capture['authored_state'].strip() or \
                    not capture.get('visual_evidence'):
                raise ValueError('Explicit authored pose provenance is required; grip input is not pose evidence')
            selected.append(verified_arm(capture['report'], side))
        opened, closed = selected
        if opened['joint_names'] != closed['joint_names'] or \
                opened['skeleton_layout']['parent_indices'] != closed['skeleton_layout']['parent_indices'] or \
                opened['skinning_layout']['inverse_bind_rows'] != closed['skinning_layout']['inverse_bind_rows']:
            raise ValueError('Open/closed captures belong to different native rigs')
        names = opened['joint_names']
        parents = opened['skeleton_layout']['parent_indices']
        chains = [[names.index(f'{side}_{digit}_0{segment+(1 if digit == "thumb" else 2)}_jnt')
                   for segment in range(3)] for digit in ('thumb', 'index', 'medium', 'ring', 'pinky')]
        helpers = [names.index(side+suffix) for suffix in ('_helper_thumb_0_1stdorsalmuscle_l_jnt', '_helper_thumb_tendon_l_jnt')]
        wrist = names.index(side+'_wrist_jnt')
        roots = [chains[0][0]]+[parents[chain[0]] for chain in chains[1:]]+helpers
        if len(set(roots)) != len(roots) or parents[chains[0][0]] != wrist or any(parents[root] != wrist for root in roots) or \
                any(parents[chain[1]] != chain[0] or parents[chain[2]] != chain[1] for chain in chains):
            raise ValueError('Named finger roots do not belong to the verified wrist')
        mask = []
        for joint in range(69):
            at = joint
            for _ in range(69):
                if at in roots:
                    mask.append(joint)
                    break
                at = parents[at]
                if at < 0:
                    break
        bones = {joint for chain in chains for joint in chain}
        flexed = 0
        for joint in mask:
            a, pa = local(opened['sampled_joints'], parents, joint)
            b, pb = local(closed['sampled_joints'], parents, joint)
            if math.dist(pa, pb) > (.002 if joint in bones else .06):
                raise ValueError('Native finger reference geometry differs')
            if joint in bones and sum(dot(x, y) for x, y in zip(a, b)) < 2.98:
                flexed += 1
        if flexed < 4:
            raise ValueError('Closed reference does not contain authored finger flexion')
        output.extend(struct.pack('<I69h15I2I', side_index, *parents, *(j for chain in chains for j in chain), *helpers))
        for rows in opened['skinning_layout']['inverse_bind_rows']:
            if len(rows) != 12 or any(not math.isfinite(v) for v in rows):
                raise ValueError('Invalid native inverse bind rows')
            output.extend(struct.pack('<12f', *rows))
        for arm in selected:
            for node in arm['sampled_joints']:
                matrix = [value for column in node['rotation_columns']+[node['position']] for value in column+[0.]]
                output.extend(struct.pack('<16f', *matrix))
        summaries.append(dict(side=side, flexed_main_bones=flexed, affected_joints=mask,
                              finger_chains=chains, palm_helpers=helpers))
    return bytes(output), summaries


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[1]
    if not args.manifest.resolve().is_relative_to(workspace) or not args.output.resolve().is_relative_to(workspace):
        raise ValueError('Use workspace-owned private artifacts')
    manifest = json.loads(args.manifest.read_text())
    captures = {}
    provenance = []
    for side in ('left', 'right'):
        for state in ('open', 'closed'):
            capture = dict(manifest[f'{side}_{state}'])
            report_path = (args.manifest.parent/capture.pop('report')).resolve()
            evidence_path = (args.manifest.parent/capture['visual_evidence']).resolve()
            if not report_path.is_relative_to(workspace) or not evidence_path.is_relative_to(workspace) or not evidence_path.is_file():
                raise ValueError('Native capture requires workspace-owned visual evidence')
            capture['report'] = json.loads(report_path.read_text())
            captures[(side, state)] = capture
            provenance.append(dict(side=side, state=state, report=str(report_path),
                                   report_sha256=hashlib.sha256(report_path.read_bytes()).hexdigest(),
                                   authored_state=capture['authored_state'], visual_evidence=str(evidence_path)))
    binary, arms = encode_references(captures)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(binary)
    args.output.with_suffix('.json').write_text(json.dumps(dict(executable_sha256=GAME_HASH, game_memory_writes=False,
        native_pose_provenance=provenance, arms=arms, reference_sha256=hashlib.sha256(binary).hexdigest(),
        private_native_assets=True, headset_fist_verified=False, mesh_replay_required=True), indent=2)+'\n')
    print(json.dumps(dict(output=str(args.output), bytes=len(binary), native_reference_arms=2, headset_fist_verified=False)))


if __name__ == '__main__':
    main()
