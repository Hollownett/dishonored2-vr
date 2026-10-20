"""Corpse grab routing guards. Grows in Tasks 10-12; Task 5 pins the diagnostics wiring in src/dxgi_probe.cpp."""
from pathlib import Path
import re
import sys


def check(source: str) -> None:
    status = source.index('\\"corpse\\":')
    carry = source.index('\\"carry\\":')
    assert status < carry, '"corpse": must precede "carry": in the status line'
    assert "corpse_diagnostics.json()" in source[status - 200:status + 200]
    carry_stops = [m.start() for m in re.finditer(r"carry_trace\.stop\(\)", source)]
    assert len(carry_stops) >= 5, "expected the known carry_trace.stop() sites"
    for pos in carry_stops:
        line_start = source.rfind("\n", 0, pos) + 1
        line_end = source.index("\n", pos)
        assert "corpse_trace.stop()" in source[line_start:line_end], "corpse_trace.stop() must sit with every carry_trace.stop()"
    assert source.count("corpse_trace.stop()") >= len(carry_stops)
    assert 'L"bonelab-trace"' in source and "native-corpse-trace.csv" in source


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    check((root / "src" / "dxgi_probe.cpp").read_text(encoding="utf-8"))
    print("corpse_routing_check ok")
    sys.exit(0)
