"""Runner boundaries/cache invalidation; no compiler, game or desktop required."""
import importlib.util
import json
from pathlib import Path
import tempfile
import struct
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('playground', Path(__file__).resolve().parents[1] / 'tools/playground.py')
playground = importlib.util.module_from_spec(spec)
spec.loader.exec_module(playground)
playground.sys.path.insert(0, str(playground.ROOT/'tools'))
import playground_native


class PlaygroundCheck(unittest.TestCase):
    def test_native_module_retries_loader_snapshot_change(self):
        reader = object.__new__(playground_native.NativeReader)
        reader.snapshot = Mock(side_effect=[playground_native.c.c_void_p(-1).value, 1])
        reader.mfirst = Mock(return_value=True)
        reader.close = Mock()
        with patch.object(playground_native.c, 'get_last_error', return_value=24, create=True), patch.object(playground_native.time, 'sleep') as pause:
            self.assertIsNotNone(reader.module(123))
        self.assertEqual(reader.snapshot.call_count, 2)
        reader.close.assert_called_once_with(1)
        pause.assert_called_once_with(.01)
        self.assertEqual(reader.module_error, 0)

    def test_native_module_does_not_retry_other_errors(self):
        reader = object.__new__(playground_native.NativeReader)
        reader.snapshot = Mock(return_value=playground_native.c.c_void_p(-1).value)
        reader.close = Mock()
        with patch.object(playground_native.c, 'get_last_error', return_value=5, create=True), patch.object(playground_native.time, 'sleep') as pause:
            self.assertIsNone(reader.module(123))
        reader.snapshot.assert_called_once_with(24, 123)
        reader.close.assert_not_called()
        pause.assert_not_called()
        self.assertEqual(reader.module_error, 5)

    def test_native_module_loader_retry_is_bounded(self):
        reader = object.__new__(playground_native.NativeReader)
        reader.snapshot = Mock(return_value=playground_native.c.c_void_p(-1).value)
        reader.close = Mock()
        with patch.object(playground_native.c, 'get_last_error', return_value=24, create=True), patch.object(playground_native.time, 'sleep') as pause:
            self.assertIsNone(reader.module(123))
        self.assertEqual(reader.snapshot.call_count, 10)
        self.assertEqual(pause.call_count, 9)
        reader.close.assert_not_called()
        self.assertEqual(reader.module_error, 24)

    def test_native_mode_index_flattens_past_earlier_monitor_counts(self):
        values={'r_mode':2,'renderer_diagnostics':{'selected_monitor_index':-1,'monitors':[
            {'monitor_index':0,'modes':[{'array_index':0,'width':640,'height':480}]},
            {'monitor_index':1,'modes':[{'array_index':0,'width':800,'height':600},
                                      {'array_index':1,'width':1280,'height':720}]}]}}
        self.assertEqual(playground_native.resolve_native_mode(values),
            {'monitor_index':1,'array_index':1,'width':1280,'height':720})
        self.assertEqual(playground_native.resolve_native_mode(dict(values,r_mode=3)),{'invalid':True})

    def test_native_table_can_initialize_after_dimension_grace_without_ready(self):
        expected=playground_native.expected_values(Path('stage'),Path('assets'))
        pending=dict(expected,renderer_diagnostics={'monitors':[]})
        consecutive=0;mismatch=None
        for now in range(5):
            consecutive,mismatch=playground_native.advance_guards(pending,expected,consecutive,mismatch,now)
            self.assertEqual(consecutive,0)
            self.assertIsNone(mismatch)
            self.assertFalse(playground_native.verified_small_mode(pending))
        valid=dict(expected,renderer_diagnostics={'selected_monitor_index':0,'monitors':[
            {'monitor_index':0,'modes':[{'array_index':0,'width':640,'height':480}]}]})
        selecting=dict(expected,renderer_diagnostics=dict(valid['renderer_diagnostics'],selected_monitor_index=-1))
        self.assertTrue(playground_native.small_mode_state(selecting))
        self.assertEqual(playground_native.advance_guards(selecting,expected,0,None,4),(1,None))
        for now in range(5,8):
            consecutive,mismatch=playground_native.advance_guards(valid,expected,consecutive,mismatch,now)
        self.assertEqual(consecutive,3)
        drift=dict(valid,r_windowWidth=1920)
        consecutive,mismatch=playground_native.advance_guards(drift,expected,consecutive,mismatch,8)
        self.assertEqual((consecutive,mismatch),(0,8))
        with self.assertRaises(RuntimeError): playground_native.advance_guards(drift,expected,0,mismatch,11)
        wrong=dict(expected,renderer_diagnostics={'selected_monitor_index':0,'monitors':[
            {'monitor_index':0,'modes':[{'array_index':0,'width':1920,'height':1080}]}]})
        with self.assertRaises(RuntimeError): playground_native.advance_guards(wrong,expected,0,None,0)

    def test_native_mode_diagnostics_are_bounded_and_decode_actual_table(self):
        base=0x140000000;table=0x2000;modes=0x3000
        record=bytearray(0x40)
        struct.pack_into('<Qii',record,0,123,1920,1080)
        record[0x18]=1
        struct.pack_into('<QI',record,0x20,modes,2)
        memory={base+0x32faf90:struct.pack('<QI',table,1),table:bytes(record),
                modes:struct.pack('<II',(1280<<16)|720,(1920<<16)|1080)}
        def read(address,size):
            self.assertLessEqual(size,8*0x40 if address==table else 1024)
            return memory[address][:size]
        result=playground_native.native_modes(read,base)
        self.assertEqual(result[0]['modes'][0],{'array_index':0,'width':1280,'height':720})
        memory[base+0x32faf90]=struct.pack('<QI',table,9)
        with self.assertRaises(ValueError): playground_native.native_modes(read,base)
        memory[base+0x32faf90]=struct.pack('<QI',table,1)
        struct.pack_into('<I',record,0x28,257);memory[table]=bytes(record)
        with self.assertRaises(ValueError): playground_native.native_modes(read,base)

    def test_native_contract_disables_backend_and_exclusive_fullscreen(self):
        values=playground_native.expected_values(Path('owned-stage'),Path('read-only-assets'))
        self.assertEqual(values['net_usePlatformBackend'],0)
        self.assertEqual(values['r_fullscreen'],0)
        self.assertEqual(values['r_mode'],0)
        self.assertEqual(values['steam_remote_interface'],0)
        self.assertEqual(values['steam_user_interface'],0)
        self.assertEqual(values['fs_savepath'],str(Path('owned-stage')/'fs-save'))
        self.assertEqual((values['r_windowWidth'],values['r_windowHeight']),(640,480))

    def test_native_transient_dimensions_never_relax_save_or_steam_guards(self):
        expected=playground_native.expected_values(Path('stage'),Path('assets'))
        values=dict(expected,r_windowWidth=1920,r_windowHeight=1080)
        self.assertEqual(playground_native.validate_sample(values,expected),(True,False))
        for key,value in [('net_usePlatformBackend',1),('r_fullscreen',1),
                          ('steam_remote_interface',123),('fs_savepath','shared-saves')]:
            changed=dict(values);changed[key]=value
            self.assertEqual(playground_native.validate_sample(changed,expected),(False,False))

    def test_native_seed_config_is_new_and_owned(self):
        with tempfile.TemporaryDirectory() as folder:
            stage=Path(folder);(stage/'fs-save').mkdir()
            cfg=playground_native.seed_config(stage)
            self.assertTrue(cfg.is_relative_to(stage/'fs-save'))
            self.assertIn('r_windowWidth "640"',cfg.read_text())
            self.assertIn('configVersion 10',cfg.read_text())
            self.assertIn('r_mode "0"',cfg.read_text())

    def test_live_guard_only_ready_for_fresh_strict_owned_samples(self):
        expected=playground_native.expected_values(Path('stage'),Path('assets'))
        actual=dict(expected,renderer_diagnostics={'selected_monitor_index':0,'monitors':[
            {'monitor_index':0,'modes':[{'array_index':0,'width':640,'height':480}]}]})
        exe=Path('stage/Dishonored2.exe')
        def report(streak=3,status='monitoring',values=actual,path=exe):
            return playground_native.live_guard('a'*32,123,0x140000000,path,values,expected,streak,status,exe)
        self.assertTrue(report()['ready'])
        self.assertFalse(report(streak=2)['ready'])
        self.assertFalse(report(status='stopped')['ready'])
        self.assertFalse(report(values=None)['ready'])
        self.assertFalse(report(path=Path('foreign/Dishonored2.exe'))['ready'])
        self.assertFalse(report(values=dict(expected,net_usePlatformBackend=1))['ready'])
        self.assertFalse(report(values=dict(expected,r_windowWidth=1920))['ready'])
        self.assertFalse(report(values=expected)['ready'])
        self.assertFalse(report(values=dict(actual,r_mode=16))['ready'])
        wrong=dict(actual,renderer_diagnostics={'selected_monitor_index':0,'monitors':[
            {'monitor_index':0,'modes':[{'array_index':0,'width':1280,'height':720}]}]})
        self.assertFalse(report(values=wrong)['ready'])

    def test_live_guard_atomic_publication_leaves_no_temporary_files(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'native-live.json'
            playground_native.atomic_json(path,{'ready':False,'nonce':'first'})
            playground_native.atomic_json(path,{'ready':True,'nonce':'second'})
            self.assertEqual(json.loads(path.read_text()),{'ready':True,'nonce':'second'})
            self.assertEqual([p.name for p in Path(folder).iterdir()],['native-live.json'])

    def test_vendor_and_fixture_changes_invalidate_cache(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(playground, 'ROOT', Path(folder)):
            root = Path(folder)
            for part in ('src', 'tests', 'third_party/vendor'):
                (root / part).mkdir(parents=True, exist_ok=True)
            source = root / 'tests/fixture.cpp'
            source.write_text('int main() {}')
            header = root / 'third_party/vendor/api.h'
            header.write_text('baseline')
            before = playground.dependency_digest(source)
            header.write_text('new API')
            self.assertNotEqual(before, playground.dependency_digest(source))
            before = playground.dependency_digest(source)
            (root / 'tests/player_fixture.hpp').write_text('new rig')
            self.assertNotEqual(before, playground.dependency_digest(source))

    def test_manifest_cannot_launch_game_or_escape_tests(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'tests').mkdir()
            (root / 'game.exe').write_text('not a fixture')
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps({'game': {'source': '../game.exe', 'suites': ['fast']}}))
            with patch.object(playground, 'ROOT', root), patch.object(playground, 'MANIFEST', manifest):
                with self.assertRaises(ValueError):
                    playground.load_manifest()

    def test_failure_preserves_output_and_exit_status(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            result = playground.run_command([playground.sys.executable, '-c',
                'print("fixture failed"); raise SystemExit(9)'], root, root / 'test.log', 10)
            self.assertEqual(result['exit_code'], 9)
            self.assertIn('fixture failed', (root / 'test.log').read_text())
            self.assertFalse(result['timeout'])

    def test_timeout_is_a_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            result = playground.run_command([playground.sys.executable, '-c',
                'import time; time.sleep(10)'], root, root / 'test.log', .1)
            self.assertTrue(result['timeout'])
            self.assertNotEqual(result['exit_code'], 0)


if __name__ == '__main__':
    unittest.main()
