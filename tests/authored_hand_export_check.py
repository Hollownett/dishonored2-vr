"""Private authored export provenance tests; no target access or certification."""
import argparse
import json
from pathlib import Path
import struct
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from export_authored_hand_bank import encode,checksum

def main():
    p=argparse.ArgumentParser();p.add_argument('--candidate',type=Path,required=True);p.add_argument('--review',type=Path,required=True);args=p.parse_args()
    root=Path(__file__).resolve().parents[1]/'artifacts/research/native-arm-assets';meshes=[root/'left-arm.bmd6model',root/'right-arm.bmd6model']
    binary,hashes=encode(args.candidate,args.review,meshes)
    assert binary[:8]==b'D2VRAH01'and binary[:8]!=b'D2VRHP01'
    assert struct.unpack_from('<I',binary,8)[0]==3 and len(hashes)==6
    assert struct.unpack_from('<Q',binary,396)[0]==checksum(binary[404:])
    def reject(candidate,review):
        try:encode(candidate,review,meshes)
        except ValueError:return
        raise AssertionError('Invalid authored provenance accepted')
    with tempfile.TemporaryDirectory()as temporary:
        target=Path(temporary)/'candidate.json';document=json.loads(args.candidate.read_text())
        for key,value in [('captured_fist',True),('pose_origin','captured_native_fist'),('source_sha256','0'*64)]:
            altered=dict(document);altered[key]=value;target.write_text(json.dumps(altered));reject(target,args.review)
        altered=dict(document);altered['arms']=document['arms'];target.write_text(json.dumps(altered));reject(target,args.review)
        review=Path(temporary)/'review.json';data=json.loads(args.review.read_text());data['samples']=data['samples'][:-1];review.write_text(json.dumps(data));reject(args.candidate,review)
    print('PASS: authored/captured separation, binary checksum and captured-origin/source/stale-review/incomplete-sweep rejection')

if __name__=='__main__':main()
