import importlib.util
import json
from pathlib import Path
import tempfile


root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("audit_native_evidence", root / "tools/audit_native_evidence.py")
audit_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit_module)
assert audit_module.MAX_ROWS >= 72000 and audit_module.MAX_FILE == 128 * 1024 * 1024

with tempfile.TemporaryDirectory() as temporary:
    run = Path(temporary)
    (run / "recording/final/artifacts").mkdir(parents=True)
    identity = {"pid": 123, "sha256": "a" * 64}
    (run / "recording/manifest.json").write_text(json.dumps({"state": "complete", "pid": 123, "loaded_identity": identity}))
    (run / "candidate-at-launch.json").write_text(json.dumps({"dll_name": "d2vr_render_probe_arm_v557.dll", "sha256": identity["sha256"]}))
    (run / "recording/status.jsonl").write_text(json.dumps({"status": {"pid": 123, "tick_ms": 2000}}) + "\n")
    palette = {"schema_version": 1, "source": "owned_native_palette", "pid": 123, "source_kind": 0, "tick": 1000}
    reader = {"schema_version": 1, "source": "attachment_socket_reader", "pid": 123, "source_kind": 3,
              "tick": 1500, "observation_only": True, "consumer_frame_verified": False, "token": 41, "stage": 6,
              "substituted": True, "consumer_returned": True}
    trace = run / "recording/final/artifacts/synchronized-diagnostics.jsonl"

    def write_rows(rows):
        trace.write_text("".join(json.dumps(row) + "\n" for row in rows))

    terminal = reader | {"source": "arm_socket_terminal", "source_kind": 1, "tick": 1700}
    write_rows([palette, reader, reader | {"source_kind": 4, "tick": 1600}, terminal])
    report = audit_module.audit(run)
    assert report["counts"]["complete_records"] == 1
    assert report["counts"]["attachment_socket_reader_records"] == 2
    assert report["counts"]["arm_socket_terminal_records"] == 1
    assert report["coverage"]["trace_last_tick"] == 1000
    assert all(value is False for key, value in report["conclusions"].items() if key.endswith("_established"))
    assert report["consumers"]["1"]["metrics"]["consumer_palm_to_grip_metres"]["samples"] == 0
    for changed in ({"pid": 124}, {"source_kind": 2}, {"consumer_frame_verified": True}, {"observation_only": False}, {"source": "unknown"}):
        write_rows([palette, reader | changed])
        try:
            audit_module.audit(run)
        except ValueError:
            pass
        else:
            raise AssertionError(f"Accepted invalid reader record: {changed}")
    write_rows([palette, terminal | {"source_kind": 3}])
    try:
        audit_module.audit(run)
    except ValueError:
        pass
    else:
        raise AssertionError("Accepted terminal observation in reader bucket")
    for observation in (reader, terminal):
        write_rows([observation])
        try:
            audit_module.audit(run)
        except ValueError as error:
            assert "No complete trace records" in str(error)
        else:
            raise AssertionError("Compact-only evidence created palette coverage")

print("PASS mixed attachment-reader records remain observational and outside palette metrics/coverage")
