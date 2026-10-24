"""Every knob file in tools/default_features.json must be read by the DLL (so a typo cannot silently do nothing),
and the launcher must embed the list."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
features = json.loads((root / "tools/default_features.json").read_text())["features"]
source = (root / "src/dxgi_probe.cpp").read_text(encoding="utf-8")
files = [f["file"] for f in features]
assert len(files) == len(set(files)), "duplicate feature file"
ids = [f["id"] for f in features]
assert len(ids) == len(set(ids)), "duplicate feature id"
for f in features:
    for key in ("id", "file", "requires_dll_knob"):
        if key in f:
            assert re.fullmatch(r"[a-z0-9-]+", f[key]), f"{f.get('id')}: bad {key} {f[key]!r}"
    if f.get("requires_dll_knob"):
        continue
    assert f'L"build"/L"{f["file"]}"' in source, f'{f["id"]}: knob {f["file"]} is not read in src/dxgi_probe.cpp'
csproj = (root / "launcher/DishonoredVRLauncher.csproj").read_text(encoding="utf-8")
assert re.search(r'EmbeddedResource Include="[^"]*default_features\.json"[^>]*LogicalName="features\.json"', csproj), "launcher does not embed default_features.json"
# Task 12: WinForms common dialogs need an STA thread; top-level statements run MTA and OpenFileDialog.ShowDialog threw ThreadStateException.
program = (root / "launcher/Program.cs").read_text(encoding="utf-8")
assert re.search(r"\[STAThread\]\s*static\s+int\s+Main\s*\(", program), "launcher entry point must be [STAThread] static int Main"
assert not re.search(r"^Application\.Run\(", program, re.M), "launcher must not use top-level statements (MTA)"
pick = program[program.index("void PickDlssProvider()"):]
pick = pick[:pick.index("\n    }\n")]
try_at, dialog_at = pick.find("try"), pick.find(".ShowDialog(")
assert 0 <= try_at < dialog_at, "ShowDialog in PickDlssProvider must be inside the try"
assert "OpenFileDialog" in pick and pick.find("OpenFileDialog") > try_at, "the dialog must be created inside the try"
assert (root / "launcher/DlssFinder.cs").is_file() and "DlssFinder.cs" in (root / "tests/launcher_features/LauncherFeaturesTests.csproj").read_text(encoding="utf-8"), "DlssFinder must be compiled into the tests"
auto = program[program.index("async Task AutoFindDlss()"):]
auto = auto[:auto.index("\n    }\n")]
assert 0 <= auto.find("IsDisposed || Disposing || !IsHandleCreated") < auto.find("RefreshDlssChoices()"), "AutoFindDlss must guard a closed form before touching the UI"
assert re.search(r"FormClosed \+= [^\n]*dlssCts\?\.Cancel\(\)", program), "FormClosed must cancel the auto-find budget"
assert re.search(r"if \(shotFile == null && !exitAfterShot\) _ = AutoFindDlss\(\)", program), "--screenshot/--exit review runs must skip the DLSS auto-find"
print("launcher_features_source_check ok:", len(features), "features")
