"""perf_run_report: line format and before/after delta."""
import io, json, sys, tempfile
from contextlib import redirect_stdout
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import perf_run_report as r

def perf(p50, p95, miss, cpu, frac):
    return {"perf": {"pace_ms_p50": p50, "pace_ms_p95": p95, "missed_fraction": miss, "mod_cpu_ms_mean": cpu,
                     "mod_cpu_ms_p95": cpu * 2, "fraction": frac, "period_ms": 11.11, "frames": 900}}

def run(*args):
    out = io.StringIO()
    with redirect_stdout(out):
        assert r.main(list(args)) == 0
    return out.getvalue()

with tempfile.TemporaryDirectory() as d:
    a, b = Path(d) / 'a.json', Path(d) / 'b.json'
    a.write_text(json.dumps(perf(11.2, 14.5, .02, 3.1, 1.0)))
    b.write_text(json.dumps(perf(11.0, 12.0, .01, 3.5, .77)))
    # report_formats_line
    line = run(str(a)).strip()
    assert line == "p50 11.20 ms / p95 14.50 ms / missed 2.0% / mod cpu 3.10 ms (p95 6.20) / fraction 1.00", line
    # before_after_delta
    out = run(str(b), '--before', str(a)).splitlines()
    assert out[0].startswith("p50 11.00 ms"), out
    assert out[1] == "delta vs before: p50 -0.20ms / p95 -2.50ms / missed -1.00% / mod cpu +0.40ms / fraction -0.23", out
    # missing block is an error, not a crash
    c = Path(d) / 'c.json'; c.write_text('{"gameplay":true}')
    assert r.main([str(c)]) == 1
print('PASS: perf run report line and delta')
