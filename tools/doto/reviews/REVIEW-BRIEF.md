# DOTO layout review: shared brief for helper agents

Goal: the Dishonored 2 VR mod (src/) pins exact Dishonored2.exe addresses and struct field offsets. Death of the Outsider
(Dishonored_DO.exe, same engine family, VoidEngine v1.145 vs v1.77.9) has the same code mostly, but SOME struct layouts moved.
tools/doto/port.py generates build/doto-src/src/ (DOTO build) automatically:
  - addresses (code, vtables, imports, globals) are translated (artifacts/doto-lab/port-report.json "rva"/"poison");
  - offsets are rewritten when evidence is clear ("offset_moved"); doubtful ones are in "offset_review";
  - prologue/instruction byte checks are regenerated when only relocations/voted layout moves differ;
    real differences stay D2 ("bytes" entries with status "semantic"), so that check fails and the feature is skipped.
Your job: for YOUR files, find every struct offset / byte check whose DOTO value differs and is NOT already handled, decide
the right DOTO value with evidence, and list D2-only assumptions.

Tools (read-only; run from the repo root with `python`):
  python tools/doto/translate.py 0xRVA ...        D2 rva -> DOTO rva (+ how it was paired) and the global offset vote
  python tools/doto/fn_diff.py 0xRVA [--all] [--grep 0x140]   aligned D2/DOTO disassembly of the function containing RVA;
                                                  '*' marks operand differences (struct offsets that moved show here)
  python tools/doto/who_shifts.py 0x9c 0xa8 ...   changed functions where these displacements map to other values
  python tools/doto/live.py game|views|rtti A|fields A N   READ-ONLY view of the running Dishonored_DO.exe (it is at the
                                                  main menu; there may be no gameplay objects yet)
  Compare src/<file> with build/doto-src/src/<file> (git diff --no-index) to see what the generator already changed.
  artifacts/doto-lab/port-report.json: per-literal decisions (file/line/col/text/to/why).

Rules: do NOT edit src/, tools/, build/ or run tools/doto/port.py (one shared output tree). Do NOT write to, inject into,
suspend or debug the game, send it input, or launch/close it. Only write your one output file:
artifacts/doto-lab/review/<your-name>.json with this shape:
{
  "sites": [ {"file":"src/x.hpp", "contains":"<a substring unique to that source line>", "from":"0x30c", "to":"0x31c",
              "why":"fn 0x7f6a80->0x85d170: mov [rcx+0x30c] -> [rcx+0x31c] (fn_diff)"} ],
  "bytes": [ {"file":"src/x.hpp", "d2_at":"0x3742bf", "why":"only the field offset moved; the detour does not read it",
              "safe":true} ],
  "blockers": [ "short text: what cannot work in DOTO and why (e.g. the Heart does not exist)" ],
  "notes": [ "anything else the coordinator must know" ]
}
"from"/"to" are the literal as written in the D2 source line and its DOTO value. Add a site only with concrete evidence
(an aligned instruction pair in a function that the code depends on, or a live read). If a whole hook's semantics changed,
say so in notes instead of forcing bytes. Keep the final report short: counts, the riskiest items, what stays unresolved.
