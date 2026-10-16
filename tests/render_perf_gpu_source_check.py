"""CPU-only guardrails for the isolated diagnostic prototype; not GPU validation."""
from pathlib import Path
import re
import unittest

SOURCE = (Path(__file__).resolve().parents[1] / "src/render_perf_gpu.hpp").read_text()

class ProfilerSourceChecks(unittest.TestCase):
    def test_callback_admission_is_nonblocking_and_frame_identity_is_accepted(self):
        self.assertIn("std::try_to_lock", SOURCE)
        self.assertIn("bool begin_operation(", SOURCE)
        self.assertIn("void end_operation(", SOURCE)
        self.assertIn("bool active_frame(", SOURCE)
        self.assertIn("bool begin_frame(", SOURCE)
        self.assertIn("operation_depth_ &&", SOURCE)
        self.assertIn("active.retirement_reported=true", SOURCE)
        self.assertIn("category>3 && category!=12", SOURCE)
        self.assertIn("hud_category_quota_=32", SOURCE)
        self.assertIn("hud_category_eligible_[category_index],14", SOURCE)

    def test_no_render_submission_wait_or_readback(self):
        for call in ("Flush", "Sleep", "Map", "CopyResource", "ExecuteCommandList"):
            self.assertIsNone(re.search(r"(?:->|\.)\s*" + call + r"\s*\(", SOURCE))
        self.assertIn("D3D11_ASYNC_GETDATA_DONOTFLUSH", SOURCE)

    def test_one_clock_per_frame(self):
        self.assertEqual(SOURCE.count("->Begin("), 1)
        self.assertIn("frame_count=8,span_count=128,mark_count=32,hud_count=2048,row_count=4096", SOURCE)

    def test_replay_and_identity_contract(self):
        self.assertIn("unique.id=hud_record.tag.ticket", SOURCE)
        self.assertIn("hud_record.tag.ticket,6,hud_record.tag.category", SOURCE)
        self.assertIn("inside(start_timestamp,end_timestamp,first,last)", SOURCE)
        self.assertIn("active.present==present && active.epoch==epoch", SOURCE)
        self.assertIn("GetCurrentThreadId()==owner_thread_", SOURCE)
        self.assertIn("set_recording_frame", SOURCE)
        self.assertIn("hud_stride_=120", SOURCE)
        self.assertIn("seen_present_%hud_stride_", SOURCE)

    def test_list_identity_does_not_pin_scene_resources(self):
        self.assertNotIn("Ptr<ID3D11CommandList>", SOURCE)
        self.assertIn("std::uintptr_t list{}", SOURCE)
        finish = SOURCE.split("void finished(", 1)[1].split("void executing(", 1)[0]
        self.assertLess(finish.index("*old=hud_record.next"), finish.index("if(!unmapped_) return"))
        self.assertNotIn("hud_record.list->", SOURCE)
        self.assertNotIn("hud_record.list.Get()", SOURCE)

if __name__ == "__main__":
    unittest.main()
