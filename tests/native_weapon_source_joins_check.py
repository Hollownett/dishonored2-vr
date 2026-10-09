"""Offline join validator: exact identity and data rejection checks."""
import csv
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    'source_joins', Path(__file__).resolve().parents[1] / 'tools/check_native_weapon_source_joins.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SourceJoins(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.run = Path(self.temp.name)
        common = dict(receipt_matched='true', receipt_reason='0', receipt_serial='1',
                      native_frame='4', receipt_frame='4', side='1', animation='10', model='20',
                      receipt_game='30', receipt_source='1000', receipt_remap='40',
                      receipt_epoch='2', receipt_producer_caller_rva=str(0x5508f8))
        self.upload = dict(common, source='1000', palette_offset='0')
        self.attachment = dict(common, source='1000', joint='0', leaf_count='1', receipt_producer_stage='3')
        self.matrix = [1.0 if i in (0, 5, 10) else 0.0 for i in range(16)]
        self.attachment.update({'source_matrix'+str(i): str(value) for i, value in enumerate(self.matrix)})

    def write(self, uploads=None):
        for name, rows in [('native-weapon-attachment.csv', [self.attachment]),
                           ('native-arm-upload.csv', uploads or [self.upload])]:
            with (self.run/name).open('w', newline='') as file:
                writer = csv.DictWriter(file, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
        (self.run/'native-arm-palettes.bin').write_bytes(struct.pack('<16f', *self.matrix))

    def rejects(self):
        self.write()
        with self.assertRaises(ValueError):
            MODULE.check(self.run)

    def test_exact_join(self):
        self.write()
        result = MODULE.check(self.run)
        self.assertEqual(result['strict_joins'], 1)
        self.assertEqual(result['maximum_matrix_csv_error'], 0)
        self.assertFalse(result['model_world_frame_coherence_verified'])

    def test_duplicate_upload(self):
        self.write([self.upload, self.upload])
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            MODULE.check(self.run)

    def test_wrong_owner(self):
        self.attachment['model'] = '21'
        self.rejects()

    def test_wrong_native_frame(self):
        self.attachment['receipt_frame'] = '3'
        self.rejects()

    def test_wrong_socket_address(self):
        self.attachment['source'] = '1001'
        self.rejects()

    def test_wrong_matrix(self):
        self.attachment['source_matrix0'] = '1.01'
        self.rejects()

    def test_no_tick_based_fallback(self):
        self.attachment['receipt_serial'] = '2'
        self.rejects()

    def test_out_of_range_socket(self):
        self.attachment['joint'] = '69'
        self.rejects()


if __name__ == '__main__':
    unittest.main()
