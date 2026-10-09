"""Test-only fixture; cannot be installed as a hand reference archive."""
import argparse
import json
from pathlib import Path
import struct
from export_native_hand_poses import verified_arm

def transform(matrix):
    return struct.pack('<16f',*(matrix[row][column]for column in range(4)for row in range(4)))

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--candidate',type=Path,required=True);args=parser.parse_args()
    candidate=json.loads(args.candidate.read_text());report=json.loads(Path(candidate['source_readonly_report']).read_text());output=bytearray(b'D2VRTS01')
    for item in candidate['arms']:
        side=item['side'];arm=verified_arm(report,side);names=arm['joint_names'];parents=arm['skeleton_layout']['parent_indices']
        chains=[[names.index(f'{side}_{digit}_0{segment+(1 if digit=="thumb"else 2)}_jnt')for segment in range(3)]for digit in ('thumb','index','medium','ring','pinky')]
        helpers=[names.index(side+suffix)for suffix in ('_helper_thumb_0_1stdorsalmuscle_l_jnt','_helper_thumb_tendon_l_jnt')]
        end=names.index(side+('_index_end_jnt'if side=='left'else '_thumb_end_jnt'))
        output.extend(struct.pack('<69h15I2II',*parents,*(j for chain in chains for j in chain),*helpers,end))
        for node in arm['sampled_joints']:
            axes=node['rotation_columns'];position=node['position'];matrix=[[axes[column][row]for column in range(3)]+[position[row]]for row in range(3)]+[[0.,0.,0.,1.]]
            output.extend(transform(matrix))
        for matrix in item['authored_transform_matrices']:output.extend(transform(matrix))
    target=args.candidate.parent/'runtime-sweep-test-fixture.bin';target.write_bytes(output);print(target)

if __name__=='__main__':main()
