import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import stealth_session_report as report  # noqa: E402


def mkstatus(enq=0, stealth=(0, 0, 0, 0, 0), reject=(0, 0, 0, 0, 0), ga=0, swings=0, hh=(0, 0, 0)):
    return {"sword_contact": {"enqueued": enq, "capture_rejected": 0, "consumer_rejected": 0,
                              "stealth": list(stealth), "stealth_reject": list(reject), "hit_height": list(hh)},
            "takedown": {"swings": swings, "swings_with_kill": stealth[2], "ga_offered_frames": ga, "ga_offered_swings": 0}}


def report_buckets_sum_to_contacts():
    v = report.verdict(mkstatus(10, (2, 0, 2, 0, 0), (3, 2, 1, 1, 1)), [])
    assert v["contacts"] == 10 and sum(v["buckets"].values()) == 10, v


def buckets_over_count_is_reported():
    v = report.verdict(mkstatus(2, (0, 0, 0, 0, 0), (3, 2, 0, 0, 0)), [])
    assert v["contacts"] == 5 and sum(v["buckets"].values()) == 5 and v["buckets"]["unaccounted"] == 0, v
    assert any("buckets sum to 5, enqueued=2" in n for n in v["notes"]), v


def buckets_shortfall_is_unaccounted():
    v = report.verdict(mkstatus(10, (0, 0, 0, 0, 0), (3, 0, 0, 0, 0)), [])
    assert v["buckets"]["unaccounted"] == 7 and v["buckets"]["other"] == 0 and sum(v["buckets"].values()) == 10, v


def held_outnumber_rejects_is_unresolved():
    v = report.verdict(mkstatus(8, (5, 0, 0, 0, 0), (3, 0, 0, 0, 0)), [])
    assert (v["likely_cause"], v["fix_task"]) == ("unresolved", "collect again"), v


def slow_dominates_points_to_speed_gate():
    v = report.verdict(mkstatus(7, reject=(0, 7, 0, 0, 0)), [])
    assert (v["likely_cause"], v["fix_task"]) == ("slow", "Task 10 cause S"), v


def not_behind_dominates():
    v = report.verdict(mkstatus(6, reject=(6, 0, 0, 0, 0)), [])
    assert (v["likely_cause"], v["fix_task"]) == ("not_behind", "Task 10 cause B"), v


def tie_prefers_not_behind():
    v = report.verdict(mkstatus(4, reject=(2, 2, 0, 0, 0)), [])
    assert v["likely_cause"] == "not_behind", v


def not_unaware_dominates():
    v = report.verdict(mkstatus(5, reject=(0, 0, 5, 0, 0)), [])
    assert (v["likely_cause"], v["fix_task"]) == ("not_unaware", "Task 10 cause U"), v


def not_damageable_dominates():
    v = report.verdict(mkstatus(5, reject=(0, 0, 0, 5, 0)), [])
    assert (v["likely_cause"], v["fix_task"]) == ("not_damageable", "Task 10 cause D"), v


def held_but_never_killed():
    v = report.verdict(mkstatus(4, (4, 0, 0, 4, 0), ga=0), [])
    assert (v["stage"], v["likely_cause"], v["fix_task"]) == ("dropped", "hold_expired_no_native_offer", "Task 10 cause W"), v


def held_native_offered_but_no_kill():
    v = report.verdict(mkstatus(4, (4, 0, 0, 4, 0), ga=50), [])
    assert (v["likely_cause"], v["fix_task"]) == ("engine_rule_rejects", "Task 10 cause K"), v


def held_native_took_it():
    v = report.verdict(mkstatus(3, (3, 3, 0, 0, 0)), [])
    assert (v["stage"], v["likely_cause"]) == ("dropped", "native_took_over"), v
    assert any("not a miss" in n for n in v["notes"]), v


def killed_ok():
    v = report.verdict(mkstatus(3, (3, 0, 3, 0, 0)), [])
    assert (v["stage"], v["likely_cause"]) == ("killed", "none"), v


def no_contacts_means_blade_never_touched():
    v = report.verdict(mkstatus(0, swings=5), [])
    assert (v["likely_cause"], v["fix_task"]) == ("no_contact_near_pass", "Task 8"), v


def head_hits_only_above_threshold():
    v = report.verdict(mkstatus(9, (9, 0, 9, 0, 0), hh=(0, 0, 9)), [])
    assert "head-height contacts only" in v["notes"], v
    v = report.verdict(mkstatus(9, (9, 0, 9, 0, 0), hh=(1, 0, 9)), [])
    assert "head-height contacts only" not in v["notes"], v


def missing_fields_say_so():
    old = {"sword_contact": {"enqueued": 3, "stealth": [1, 0, 0, 0, 0]}}
    assert report.verdict(old, [])["likely_cause"] == "unknown_old_build"
    assert report.verdict({}, [])["likely_cause"] == "unknown_old_build"


def contact_speed_rows_are_summarised():
    rows = [{"what": "contact_hit", "value": "1.0"}, {"what": "contact_hit", "value": "2.0"}, {"what": "slash", "value": "9"}]
    v = report.verdict(mkstatus(7, reject=(0, 7, 0, 0, 0)), rows)
    assert any("median 1.50" in n for n in v["notes"]), v


def cli_on_fixtures():
    expected = {"all_ok": "none", "slow": "slow", "not_unaware": "not_unaware", "not_damageable": "not_damageable",
                "not_behind": "not_behind", "ga_never_offered": "hold_expired_no_native_offer"}
    for name, cause in expected.items():
        path = ROOT / "tests" / "fixtures" / "stealth_session" / (name + ".json")
        out = subprocess.run([sys.executable, str(ROOT / "tools" / "stealth_session_report.py"), str(path)],
                             capture_output=True, text=True, check=True).stdout
        assert json.loads(out[:out.rindex("}") + 1])["likely_cause"] == cause, (name, out)
    csv_path = ROOT / "tests" / "fixtures" / "stealth_session" / "combat.csv"
    out = subprocess.run([sys.executable, str(ROOT / "tools" / "stealth_session_report.py"),
                          str(ROOT / "tests" / "fixtures" / "stealth_session" / "slow.json"), str(csv_path)],
                         capture_output=True, text=True, check=True).stdout
    assert "median 0.90" in out and "swing_rows=2" in out, out


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if callable(fn) and getattr(fn, "__module__", None) == "__main__":
            fn()
            print("ok", name)
