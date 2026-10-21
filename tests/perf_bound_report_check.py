"""perf_bound_report: verdicts, invalid runs, adapter refusal, result-file-only runs, bad runs and before/after delta."""
import io, json, sys, tempfile
from contextlib import redirect_stdout
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import perf_bound_report as r

NV = {"name": "NVIDIA GeForce RTX 3070 Laptop GPU", "vendor": 4318, "vram_mb": 8192}

def split(gpu, render=.5, game=.5, frames=900, p50=16.0, eye1=6.0):
    return {"interval_ms_p50": p50, "interval_ms_p95": p50 + 3, "eye0_ms_p50": 7.0, "eye1_ms_p50": eye1,
            "hook_ms_p50": 1.5, "eye1_share": eye1 / p50, "gpu_busy": gpu, "render_thread_busy": render,
            "game_thread_busy": game, "frames": frames, "split_skipped": 0}

def status(gpu, adapter=NV, gameplay=True, **kw):
    # the real layout: frame_split = {"adapter": ..., "split": {...}}
    return {"gameplay": gameplay, "frame_split": {"adapter": adapter, "split": split(gpu, **kw)}}

def run(d, name, doc, mode="xr", knobs=None, file="xr-live-status.json", **info_extra):
    p = Path(d) / name
    (p / "artifacts").mkdir(parents=True)
    (p / "artifacts" / file).write_text(json.dumps(doc))
    info = {"probe_mode": mode, **info_extra}
    if knobs is not None:
        info["user_knobs"] = knobs
    (p / "run-info.json").write_text(json.dumps(info))
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
    for text in ("mode xr", "source xr-live-status.json", "NVIDIA GeForce RTX 3070", "interval p50 16.00 p95 19.00", "eye0 7.00 eye1 6.00",
                 "eye1_share 0.38", "hook 1.50", "gpu_busy 0.95", "render 0.50 game 0.50", "build knobs: unknown"):
        assert text in out, (text, out)
    # verdict_cpu_render / verdict_cpu_game
    assert verdict(run(d, "cr", status(.5, render=.92, game=.6)))[0] == "CPU-BOUND(render)"
    assert verdict(run(d, "cg", status(.5, render=.6, game=.9)))[0] == "CPU-BOUND(game)"
    # verdict_mixed and the boundaries
    assert verdict(run(d, "m", status(.8, render=.9)))[0] == "MIXED"
    assert verdict(run(d, "m2", status(.5, render=.7, game=.7)))[0] == "MIXED"
    assert verdict(run(d, "m3", status(.70, render=.95)))[0] == "MIXED"
    assert verdict(run(d, "b1", status(.90)))[0] == "GPU-BOUND"
    assert verdict(run(d, "b2", status(.5, render=.85)))[0] == "CPU-BOUND(render)"
    # no GPU timer: the verdict word stays in the fixed set; the row carries the note
    v, out = verdict(run(d, "n", status(None, render=.95)))
    assert v == "MIXED" and "no gpu_busy" in out, out
    assert verdict(run(d, "nb", status(.5, render=None, game=None)))[0] == "MIXED"
    # run_without_gameplay_is_invalid: listed, never averaged in
    bad = run(d, "menu", status(.95, gameplay=False))
    assert verdict(bad)[0] == "INVALID"
    v, out = verdict(run(d, "ok", status(.5, render=.92)), bad)
    assert v == "CPU-BOUND(render)" and "INVALID (no gameplay frames)" in out, out
    assert verdict(run(d, "nf", status(.95, frames=0)), run(d, "ok2", status(.95)))[0] == "GPU-BOUND"
    # rejects_wrong_adapter (and there is no way around it)
    amd = {"name": "AMD Radeon(TM) Graphics", "vendor": 4098, "vram_mb": 512}
    v, out = verdict(run(d, "amd", status(.95, adapter=amd)))
    assert v == "INVALID" and "INVALID (wrong adapter)" in out, out
    assert "INVALID (no adapter info)" in verdict(run(d, "na", status(.95, adapter=None)))[1]
    # adapter.txt is the fallback when the block has no adapter
    p = run(d, "txt", status(.95, adapter=None)); Path(p, "adapter.txt").write_text(json.dumps(NV) + "\n")
    assert verdict(p)[0] == "GPU-BOUND"
    # mixed valid runs do not average into one call
    assert verdict(run(d, "x1", status(.95)), run(d, "x2", status(.5, render=.9)))[0] == "MIXED"
    # mode comes from run-info.json
    assert "mode paired" in verdict(run(d, "pm", status(.95), mode="paired"))[1] and "mode single" in verdict(run(d, "sm", status(.95), mode="single"))[1]
    assert "mode ?" in verdict(run(d, "om", status(.95), mode="bogus"))[1]
    # paired rows say the GPU trace is in the numbers; a failed run is INVALID with its reason even if stale data is there
    assert "paired includes GPU-trace overhead" in verdict(run(d, "pt", status(.95), mode="paired"))[1]
    assert "paired includes" not in verdict(run(d, "st", status(.95), mode="single"))[1]
    v, out = verdict(run(d, "fail", status(.95), return_code=1, reason="StartProbe returned 1"))
    assert v == "INVALID" and "INVALID (run failed: StartProbe returned 1)" in out, out
    assert "INVALID (run failed: the game exited" in verdict(run(d, "fail2", status(.95), return_code=0, reason="the game exited during the session"))[1]
    assert "run failed: return code 2" in verdict(run(d, "fail3", status(.95), return_code=2))[1]
    v, out = verdict(run(d, "fail4", {"gameplay": True}, return_code=1, reason="no StartProbe begin"))
    assert "INVALID (run failed: no StartProbe begin)" in out, out
    assert verdict(run(d, "okrc", status(.95), return_code=0, reason=""))[0] == "GPU-BOUND"
    # flat run: only dxgi-probe-result.json, no gameplay flag; the row says so
    flat = run(d, "flat", {"presents": 500, "frame_split": {"adapter": NV, "split": split(None, frames=450)}}, mode="single",
               file="dxgi-probe-result.json")
    v, out = verdict(flat)
    assert v == "MIXED" and "source dxgi-probe-result.json" in out and "no gameplay flag" in out and "mode single" in out, out
    # the status file wins when both exist
    both = run(d, "both", status(.95)); Path(both, "artifacts", "dxgi-probe-result.json").write_text(json.dumps(
        {"frame_split": {"adapter": NV, "split": split(.5, render=.95)}}))
    assert verdict(both)[0] == "GPU-BOUND"
    # a status without a frame_split block falls back to the result file
    fb = run(d, "fb", {"gameplay": True}); Path(fb, "artifacts", "dxgi-probe-result.json").write_text(json.dumps(
        {"frame_split": {"adapter": NV, "split": split(.95)}}))
    assert "source dxgi-probe-result.json" in verdict(fb)[1]
    # one bad run does not kill the report: missing file, missing block, junk, non-string adapter
    good = run(d, "good", status(.5, render=.92))
    (Path(d) / "empty").mkdir()
    noblock = run(d, "noblock", {"gameplay": True})
    junk = run(d, "junk", {}); Path(junk, "artifacts", "xr-live-status.json").write_text("{not json")
    badname = run(d, "badname", status(.95, adapter={"name": 5, "vendor": 1}))
    nolist = run(d, "nolist", status(.95, adapter=["x"]))
    badnum = run(d, "badnum", {"gameplay": True, "frame_split": {"adapter": NV, "split": {"frames": "many", "gpu_busy": "x"}}})
    rc, out = main(good, str(Path(d) / "does-not-exist"), str(Path(d) / "empty"), noblock, junk, badname, nolist, badnum)
    assert rc == 0, out
    assert out.count("INVALID") >= 7 and "VERDICT: CPU-BOUND(render)" in out, out
    assert "INVALID (no status or result file)" in out and "INVALID (unreadable status)" in out, out
    lines = out.splitlines()
    assert next(l for l in lines if l.startswith("badname")).endswith("INVALID (no adapter info)")
    # user knobs in build/ are printed per row; differing knob sets taint the delta
    kn = run(d, "kn", status(.95), knobs=["aa-mode", "fsr"])
    assert "build knobs: aa-mode, fsr" in verdict(kn)[1]
    assert "build knobs: none" in verdict(run(d, "kn0", status(.95), knobs=[]))[1]
    # before_after_delta
    before_json = str(Path(d) / "before.json")
    assert main(run(d, "b", status(.95, p50=16.0, eye1=6.0), knobs=[]), "--json", before_json)[0] == 0
    rc, out = main(run(d, "a", status(.80, p50=13.0, eye1=5.0), knobs=[]), "--before", before_json)
    assert rc == 0
    delta = out.splitlines()[-1]
    assert delta.startswith("delta vs before (1 valid run(s) -> 1): interval p50 -3.00ms / interval p95 -3.00ms"), delta
    assert "gpu_busy -0.15" in delta and "eye1 -1.00ms" in delta and "WARNING" not in delta, delta
    assert "WARNING" in main(run(d, "a2", status(.80), knobs=["aa-mode"]), "--before", before_json)[1]
    assert "n/a (no valid run" in main(bad, "--before", before_json)[1]
    # a bad --before file is an error message, not a traceback
    nb = Path(d) / "nb.json"; nb.write_text('{"gameplay":true}')
    assert r.main([good, "--before", str(nb)]) == 1
    # the removed escape hatch is gone
    try:
        r.main([good, "--any-adapter"])
        raise AssertionError("--any-adapter must not exist")
    except SystemExit as e:
        assert e.code == 2
print('PASS: perf bound report verdicts, invalid runs, adapter, result file, bad runs and delta')
