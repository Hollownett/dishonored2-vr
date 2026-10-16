"""Source contract guard only; does not compile or execute GPU work."""
from pathlib import Path
import re

source = (Path(__file__).resolve().parents[1] / "src/fsr4_eye_upscaler.hpp").read_text(encoding="utf-8")
phases = re.search(r"enum class Phase\s*\{([^}]+)\}", source).group(1)
assert [name.strip() for name in phases.split(",")] == [
    "inputs_begin", "inputs_ready", "output_ready", "output_copied",
    "cpu_flush", "ffx_gpu", "ffx_dropped", "cpu_record", "cpu_handoff", "cpu_dispatch",
]
assert "slot.timestamp_pending=false;slot.diagnostics.callback=nullptr;" in source
assert "drop_pending_diagnostics(*state_);state_->retired=true;state_.reset();" in source
assert "finish_diagnostics(slot,Phase::ffx_dropped,context);drop_pending_diagnostics(s);" in source
assert "if(!timed) finish_diagnostics(slot,Phase::ffx_dropped,context);" in source
assert "finish_diagnostics(slot,Phase::ffx_gpu,s.context11.Get(),elapsed);" in source
assert "else finish_diagnostics(slot,Phase::ffx_dropped,s.context11.Get());" in source
dispatch = source[source.index("    bool dispatch("):]
assert dispatch.count("return fail(") == 1  # Only the drop-emitting rejection helper.
assert dispatch.count("return rejected(") == 4
print("PASS: selected FFX diagnostics terminate as measured result or explicit drop")
