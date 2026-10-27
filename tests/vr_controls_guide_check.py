"""Fails if the README guide drifts from docs/VR-CONTROLS.md, or the launcher stops embedding it."""
import re
from pathlib import Path

root = Path(__file__).resolve().parent.parent
doc = (root / "docs" / "VR-CONTROLS.md").read_text(encoding="utf-8").splitlines()
readme = (root / "README.md").read_text(encoding="utf-8").splitlines()

start = readme.index("## VR controls and gestures")
end = next((i for i in range(start + 1, len(readme)) if readme[i].startswith("## ")), len(readme))
section = readme[start + 1:end]

def parts(lines, level):
    marks = [l for l in lines if l.startswith("#" * level + " ") or l.startswith("- **")]
    return [m[1:] if m.startswith("#") and level == 3 else m for m in marks]

want = [l for l in doc if l.startswith("## ") or l.startswith("- **")]
want = [("#" + l) if l.startswith("## ") else l for l in want]  # ## -> ###
got = [l for l in section if l.startswith("### ") or l.startswith("- **")]
assert got == want, "README section differs from docs/VR-CONTROLS.md"
assert len(want) > 20

csproj = (root / "launcher" / "DishonoredVRLauncher.csproj").read_text(encoding="utf-8")
assert re.search(r'Include="\$\(RepoRoot\)\\docs\\VR-CONTROLS\.md"\s+LogicalName="home/VR-CONTROLS\.md"', csproj), "csproj embed missing"
print("PASS")
