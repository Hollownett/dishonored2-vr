"""Gameplay script version 8: one engine console line per segment (the Linux sim teleport, `setviewpos`).

The compiler (tools/simulated_gameplay_script.py) emits v8 only when a segment has "console"; each v8 row ends with one token,
'-' or the percent-encoded line ('%20' a space, '%25' a percent). The decoded line is at most 200 chars from [A-Za-z0-9 ._-] and
starts with a letter. The DLL side (src/simulated_gameplay_script.hpp take_console, fired in src/dxgi_probe.cpp through the
'console' test-command path) is pinned here by source routing; its parse is exercised by tests/simulated_gameplay_script_check.cpp.
"""
import importlib.util
import json
import re
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from simulated_gameplay_script import compile_script  # noqa: E402

_spec = importlib.util.spec_from_file_location('d2vr_scenario_lib', ROOT / 'tools/linux/scenario_lib.py')
LIB = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(LIB)


def seg(i, **k):
    return dict({'start_ms': i * 100, 'end_ms': i * 100 + 100, 'input': {'move_x': 0.0}}, **k)


def decode(token):
    if token == '-':
        return None
    out, i = '', 0
    while i < len(token):
        if token[i] == '%':
            out += {'%20': ' ', '%25': '%'}[token[i:i + 3]]
            i += 3
        else:
            out += token[i]
            i += 1
    return out


class ConsoleCheck(unittest.TestCase):
    def compile(self, segments, seconds=10):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'script.json'
            path.write_text(json.dumps({'segments': segments}), encoding='utf-8')
            return compile_script(path, root, seconds)

    def test_round_trip(self):
        _, transport = self.compile([seg(0, console='setviewpos 1.5 -2.25 0.5 90'), seg(1)])
        rows = transport.splitlines()
        self.assertEqual(rows[0], 'D2VR_GAMEPLAY_SCRIPT 8 10000 2')
        self.assertEqual(rows[1].split()[-1], 'setviewpos%201.5%20-2.25%200.5%2090')
        self.assertEqual(decode(rows[1].split()[-1]), 'setviewpos 1.5 -2.25 0.5 90')
        self.assertEqual(rows[2].split()[-1], '-')  # '-' means none
        self.assertIsNone(decode('-'))
        self.assertEqual(len(rows[1].split()), 41)  # the v7 row (40 fields) plus the token

    def test_rejects(self):
        for bad in ('a' * 201, 'god;quit', 'echo 50%', '1god', ' god', '', 'god\n', 3, ['god']):
            with self.subTest(line=repr(bad)[:30]), self.assertRaises(ValueError):
                self.compile([seg(0, console=bad)])
        self.compile([seg(0, console='a' * 200)])  # the limit itself is fine

    def test_older_versions_unchanged(self):
        _, transport = self.compile([seg(0, touch={'right_trigger': True})])
        self.assertEqual(transport.splitlines()[0], 'D2VR_GAMEPLAY_SCRIPT 7 10000 1')  # no console key: still v7
        self.assertFalse(transport.splitlines()[1].split()[-1].startswith('-'))

    def test_scenario_lib_helper(self):
        s = LIB.console(5000, 'setviewpos 1 2 3 90')
        self.assertEqual((s['start_ms'], s['console']), (5000, 'setviewpos 1 2 3 90'))
        self.assertTrue(0 < s['end_ms'] - s['start_ms'] <= 20)  # about one frame
        self.assertEqual(LIB.setviewpos(1, -2.5, 3.25, 90), 'setviewpos 1.000 -2.500 3.250 90.0')
        self.compile([seg(0), LIB.console(100, LIB.setviewpos(1, -2.5, 3.25, 90)), dict(seg(2), start_ms=200 + 11)])

    def test_dll_routing(self):
        header = (ROOT / 'src/simulated_gameplay_script.hpp').read_text(encoding='utf-8')
        self.assertIn('(version<1 || version>8)', header)
        self.assertIn('if(version>=8) {std::string token;if(!(source>>token) || !decode_console(token,s.console))', header)
        self.assertIn('segments_=std::move(parsed);console_next_=0;', header)
        probe = (ROOT / 'src/dxgi_probe.cpp').read_text(encoding='utf-8')
        fire = re.findall(r'for\(const auto& line:simulated_gameplay_script\.take_console\(elapsed,gameplay\)\) \{d2vr::NativeInteractionAim::request_console\(line\);', probe)
        self.assertEqual(len(fire), 1, 'the script runner fires each due line once through request_console')
        self.assertIn('if(verb=="console")', probe)  # the same test-command path


if __name__ == '__main__':
    unittest.main()
