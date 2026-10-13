"""Projection request/parser integrity using temporary files only."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import request_vr_capture as capture


class CaptureCheck(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.root=Path(self.temporary.name)
        self.nonce='a'*32

    def tearDown(self):
        self.temporary.cleanup()

    def complete_pair(self):
        for stage in capture.STAGES:
            stem=self.root/f'xr-request-{self.nonce}-{stage}'
            stem.with_suffix('.rgba').write_bytes(bytes((10,20,30,255))*2)
            stem.with_suffix('.json').write_text(json.dumps({'complete':True,'request_id':self.nonce,
                'headset_compositor':False,'runtime_submission_unverified':True,'width':2,'height':1,
                'frame':7,'pose_epoch':3}))

    def metadata(self,stage,**changes):
        path=self.root/f'xr-request-{self.nonce}-{stage}.json'
        data=json.loads(path.read_text());data.update(changes);path.write_text(json.dumps(data))

    def test_pending_request_is_never_overwritten_and_nonce_is_valid(self):
        nonce=capture.request_capture(self.root,'owned-fixture')
        self.assertRegex(nonce,r'^[0-9a-f]{32}$')
        pending=self.root/'capture-eye-pair.request';before=pending.read_bytes()
        with self.assertRaises(FileExistsError):capture.request_capture(self.root,'second')
        self.assertEqual(pending.read_bytes(),before)
        self.assertFalse(list(self.root.glob('*.tmp')))

    def test_partial_or_missing_metadata_waits_without_conversion(self):
        self.assertIsNone(capture.read_pair(self.root,self.nonce))
        self.complete_pair()
        (self.root/f'xr-request-{self.nonce}-destination-right.json').write_text('{"complete":')
        self.assertIsNone(capture.read_pair(self.root,self.nonce))
        self.assertIsNone(capture.wait_capture(self.root,self.nonce,0))
        self.assertFalse(list(self.root.glob('*.png')))

    def test_nonce_frame_and_pose_mismatches_are_rejected(self):
        for changes in ({'request_id':'b'*32},{'frame':8},{'pose_epoch':4}):
            self.complete_pair();self.metadata('source-right',**changes)
            with self.assertRaises(ValueError):capture.read_pair(self.root,self.nonce)

    def test_complete_metadata_with_partial_pixels_is_rejected(self):
        self.complete_pair()
        (self.root/f'xr-request-{self.nonce}-source-left.rgba').write_bytes(b'partial')
        with self.assertRaises(ValueError):capture.read_pair(self.root,self.nonce)

    def test_conversion_preserves_existing_pngs(self):
        self.complete_pair();records=capture.read_pair(self.root,self.nonce)
        output=records[1][0].with_suffix('.png');output.write_bytes(b'preserve-original')
        with self.assertRaises(FileExistsError):capture.convert_pair(records)
        self.assertEqual(output.read_bytes(),b'preserve-original')
        self.assertEqual(list(self.root.glob('*.png')),[output])

    def test_valid_pair_converts_once_without_changing_raw_data(self):
        self.complete_pair();records=capture.read_pair(self.root,self.nonce)
        originals={path:path.read_bytes() for path,_ in records}
        outputs=capture.convert_pair(records)
        self.assertEqual(len(outputs),4)
        for path in outputs:self.assertTrue(path.read_bytes().startswith(b'\x89PNG\r\n\x1a\n'))
        self.assertEqual({path:path.read_bytes() for path,_ in records},originals)
        with self.assertRaises(FileExistsError):capture.convert_pair(records)


if __name__=='__main__':unittest.main()
