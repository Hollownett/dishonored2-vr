"""The launcher's numbers must equal the proxy's (linux/proxy/proxy_core.hpp start gate), so the two ports cannot drift: the default start
(window + 20 s) is only documented and logged by the launcher, the fallback wait is the proxy's legacy 45 s, and docs/LAUNCHER.md says both."""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
core = (root / "linux/proxy/proxy_core.hpp").read_text(encoding="utf-8")
cs = (root / "launcher/ProxyInstall.cs").read_text(encoding="utf-8")
docs = (root / "docs/LAUNCHER.md").read_text(encoding="utf-8")


def const(text, pattern):
    m = re.search(pattern, text)
    assert m, pattern
    return int(m.group(1))


settle_ms = const(core, r"kDefaultSettleMs\s*=\s*(\d+)u?;")
legacy_s = const(core, r"kLegacySettleSeconds\s*=\s*(\d+)u?;")
start_s = const(cs, r"DefaultStartSeconds\s*=\s*(\d+);")
fallback_s = const(cs, r"FallbackDelaySeconds\s*=\s*(\d+);")
assert settle_ms == start_s * 1000, (settle_ms, start_s)
assert legacy_s == fallback_s, (legacy_s, fallback_s)
assert f"{start_s} s" in docs and f"{fallback_s} s" in docs, "docs/LAUNCHER.md does not state both numbers"
print(f"start_gate_parity_check ok: start {start_s} s, fallback {fallback_s} s")
