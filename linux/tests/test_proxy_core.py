import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import wine_support  # noqa: E402
from d2vr_linux import modhome  # noqa: E402

REPO = Path(__file__).resolve().parents[2]
HEADER = REPO / "linux" / "proxy" / "proxy_core.hpp"
PROBE_CS = REPO / "launcher" / "Probe.cs"


class ProxyCoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not wine_support.toolchain_available():
            raise unittest.SkipTest("clang-cl/xwin/wine toolchain missing: run tools/linux/setup-toolchain.sh")
        cls.built = wine_support.load_proxy_build().build({"core_check"})
        wine_support.start_wine()

    @classmethod
    def tearDownClass(cls):
        if wine_support.toolchain_available():
            wine_support.stop_wine()

    def test_core_check_under_wine(self):
        exe = self.built["core_check"]
        # Temp files, not pipes: a wineserver started by this run would inherit the pipes and block reads.
        with tempfile.TemporaryFile() as out:
            r = subprocess.run(["wine", str(exe)], env=wine_support.wine_env(), stdout=out,
                               stderr=subprocess.STDOUT, timeout=120)
            out.seek(0)
            r.stdout, r.stderr = out.read().decode("utf-8", "replace"), ""
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("proxy_core_check: all passed", r.stdout)


class ConstantsTests(unittest.TestCase):
    """Pure text checks; need no toolchain."""

    def test_constants_match_windows_sources(self):
        hpp = HEADER.read_text(encoding="utf-8")
        cs = PROBE_CS.read_text(encoding="utf-8")

        expr = re.search(r"const uint RequiredCapabilities = ([\d |]+);", cs).group(1)
        value = eval(expr, {"__builtins__": {}})  # digits and '|' only
        self.assertEqual(value, 61017)
        self.assertRegex(hpp, r"kRequiredCapabilities\s*=\s*%d;" % value)

        fmt = re.search(r'ProbeConfig\(int pid\) => \$"\{pid\}(.*)\\n";', cs).group(1)
        self.assertIn(fmt + "\\n", hpp)

        self.assertIn('kProxyMarker[] = "%s"' % modhome.PROXY_MARKER.decode(), hpp)
        self.assertIn('kStartTrigger[] = L"%s"' % modhome.START_TRIGGER, hpp)
        self.assertIn('kStopRequest[] = L"%s"' % modhome.STOP_REQUEST, hpp)


if __name__ == "__main__":
    unittest.main()
