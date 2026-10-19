import re
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))

import wine_support  # noqa: E402

PROXY = Path(__file__).resolve().parents[1] / "proxy"


def _msvc_token(t: str) -> str:
    """Paths take backslashes; a /flag keeps its leading slash (its attached path, as in /Fe:x/y or /Ix/y, converts)."""
    if t.startswith("/"):
        return "/" + t[1:].replace("/", "\\")
    return t.replace("/", "\\")


class ProxyBuildTests(unittest.TestCase):
    """Text checks on the build files; need no toolchain."""

    def test_build_cmd_matches_build_py(self):
        build = wine_support.load_proxy_build()
        lines = (PROXY / "build.cmd").read_text(encoding="utf-8").splitlines()
        cl_lines = [ln for ln in lines if ln.startswith("cl ")]
        self.assertEqual(len(cl_lines), 1, lines)
        self.assertEqual(cl_lines[0], "cl " + " ".join(_msvc_token(t) for t in build.cl_args("proxy")))
        self.assertEqual(lines[0], "@echo off")
        self.assertIn(r"if not exist build\linux-proxy mkdir build\linux-proxy", lines)

    def test_def_exports_only_directinput8create(self):
        self.assertEqual(re.findall(r"^\s+(\w+)", (PROXY / "dinput8.def").read_text(), re.M), ["DirectInput8Create"])


if __name__ == "__main__":
    unittest.main()
