"""perf_bound_report: verdicts, invalid runs, adapter refusal and before/after delta."""
import io, json, sys, tempfile
from contextlib import redirect_stdout
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import perf_bound_report as r

NV = {"name": "NVIDIA GeForce RTX 3070 Laptop GPU", "vendor": 4318, "vram_mb": 8192}

def status(gpu, render=.5, game=.5, adapter=NV, gameplay=True, frames=900, p50=16.0, eye1=6.0):
    return {"gameplay": gameplay, "adapter": adapter,
            "frame_split": {"interval_ms_p50": p50, "interval_ms_p95": p50 + 3, "eye0_ms_p50": 7.0, "eye1_ms_p50": eye1,
                            "hook_ms_p50": 1.5, "eye1_share": eye1 / p50, "gpu_busy": gpu, "render_thread_busy": render,
                            "game_thread_busy": game, "frames": frames, "split_skipped": 0}}

def run(d, name, doc, mode=6):
    p = Path(d) / name
    (p / "artifacts").mkdir(parents=True)
    (p / "artifacts" / "xr-live-status.json").write_text(json.dumps(doc))
    (p / "run-info.json").write_text(json.dumps({"override": f"{{pid}} 0 {mode} 1"}))
    return str(p)

def main(*args):
    out = io.StringIO()
    with redirect_stdout(out):
        rc = r.main(list(args))
    return rc, out.getvalue()

def verdict(*args):
    rc, out = main(*args)
    assert rc == 0, out
    return next(l for l in out.splitlines() if l.startswith("VERDICT: "))[9:], out

with tempfile.TemporaryDirectory() as d:
    # verdict_gpu_bound
    v, out = verdict(run(d, "g", status(.95)))
    assert v == "GPU-BOUND", v
    # a row carries every column
    for text in ("mode xr", "NVIDIA GeForce RTX 3070", "interval p50 16.00 p95 19.00", "eye0 7.00 eye1 6.00", "eye1_share 0.38", "hook 1.50", "gpu_busy 0.95", "render 0.50 game 0.50"):
        assert text in out, (text, out)
    # verdict_cpu_render
    assert verdict(run(d, "cr", status(.5, render=.92, game=.6)))[0] == "CPU-BOUND(render)"
    # verdict_cpu_game
    assert verdict(run(d, "cg", status(.5, render=.6, game=.9)))[0] == "CPU-BOUND(game)"
    # verdict_mixed: nothing decisive, and the boundaries
    assert verdict(run(d, "m", status(.8, render=.9)))[0] == "MIXED"
    assert verdict(run(d, "m2", status(.5, render=.7, game=.7)))[0] == "MIXED"
    assert verdict(run(d, "m3", status(.70, render=.95)))[0] == "MIXED"       # gpu_busy must be < .70
    assert verdict(run(d, "b1", status(.90)))[0] == "GPU-BOUND"
    assert verdict(run(d, "b2", status(.5, render=.85)))[0] == "CPU-BOUND(render)"
    # no GPU timer: cannot call it
    assert verdict(run(d, "n", status(None, render=.95)))[0] == "MIXED (no gpu_busy)"
    # null thread busy (coarse clock) is not a CPU-bound call
    assert verdict(run(d, "nb", status(.5, render=None, game=None)))[0] == "MIXED"
    # run_without_gameplay_is_invalid: listed, never averaged in
    bad = run(d, "menu", status(.95, gameplay=False))
    assert verdict(bad)[0] == "INVALID (no usable run)"
    v, out = verdict(run(d, "ok", status(.5, render=.92)), bad)
    assert v == "CPU-BOUND(render)" and "INVALID (no gameplay frames)" in out, out
    assert verdict(run(d, "nf", status(.95, frames=0)), run(d, "ok2", status(.95)))[0] == "GPU-BOUND"
    # rejects_wrong_adapter
    amd = {"name": "AMD Radeon(TM) Graphics", "vendor": 4098, "vram_mb": 512}
    v, out = verdict(run(d, "amd", status(.95, adapter=amd)))
    assert v == "INVALID (no usable run)" and "INVALID (wrong adapter)" in out, out
    assert verdict(run(d, "amd2", status(.95, adapter=amd)), "--any-adapter")[0] == "GPU-BOUND"
    assert "INVALID (no adapter info)" in verdict(run(d, "na", status(.95, adapter=None)))[1]
    # adapter.txt is the fallback when the status has no block
    p = run(d, "txt", status(.95, adapter=None)); Path(p, "adapter.txt").write_text(json.dumps(NV) + "\n")
    assert verdict(p)[0] == "GPU-BOUND"
    # mixed valid runs do not average into one call
    assert verdict(run(d, "x1", status(.95)), run(d, "x2", status(.5, render=.9)))[0] == "MIXED"
    # modes come from the run's override config
    assert "mode paired" in verdict(run(d, "pm", status(.95), mode=3))[1] and "mode single" in verdict(run(d, "sm", status(.95), mode=2))[1]
    # before_after_delta
    before_json = str(Path(d) / "before.json")
    assert main(run(d, "b", status(.95, p50=16.0, eye1=6.0)), "--json", before_json)[0] == 0
    rc, out = main(run(d, "a", status(.80, p50=13.0, eye1=5.0)), "--before", before_json)
    assert rc == 0
    delta = out.splitlines()[-1]
    assert delta.startswith("delta vs before (1 valid run(s) -> 1): interval p50 -3.00ms / interval p95 -3.00ms"), delta
    assert "gpu_busy -0.15" in delta and "eye1 -1.00ms" in delta, delta
    # an invalid run on either side is not in the delta
    assert "n/a (no valid run" in main(bad, "--before", before_json)[1]
    # missing block is an error, not a crash
    nb = Path(d) / "nb.json"; nb.write_text('{"gameplay":true}')
    assert r.main([str(nb)]) == 1
print('PASS: perf bound report verdicts, invalid runs, adapter and delta')
