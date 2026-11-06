# v1041: Op::gun_drop must not touch held_want_ (the caller's wish for the hand's copy). Headset v1040: a wheel choice that replaced a
# held copy with another asked for the new copy, then the displaced copy's gun_drop nulled that want: "selected, not shown until I re-grip".
from pathlib import Path
import sys

src = Path(sys.argv[1] if len(sys.argv) > 1 else Path(__file__).resolve().parent.parent / "src" / "native_weapon_hands.hpp").read_text(encoding="utf-8")
begin = src.index("static bool gun_drop(std::uintptr_t base")
end = src.index("static bool gun_pickup(", begin)
body = src[begin:end]
assert "held_want_[side].store(nullptr" not in body, "gun_drop clears the caller's held_want_"
assert "std::strcmp(pool_[held_[side].pe].def,def)" in body, "gun_drop must release the held entry only when it is the dropped def"
# a spawn failure of the held copy is retried (bounded), not latched until the def changes
hs = src[src.index("static void service_held("):]
hs = hs[:hs.index("static void service_bash") if "static void service_bash" in hs else 4000]
assert "h.tries>=12" in hs and "h.retry_at" in hs, "held-copy spawn failure must retry"
print("native_weapon_hands_drop_want_check: ok")
