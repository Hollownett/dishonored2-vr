"""Every knob file in tools/default_features.json must be read by the DLL (so a typo cannot silently do nothing),
and the launcher must embed the list."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
features = json.loads((root / "tools/default_features.json").read_text())["features"]
source = (root / "src/dxgi_probe.cpp").read_text(encoding="utf-8")
for f in features:
    for key in ("id", "file", "requires_dll_knob"):
        if key in f:
            assert re.fullmatch(r"[a-z0-9-]+", f[key]), f"{f.get('id')}: bad {key} {f[key]!r}"
    if f.get("requires_dll_knob"):
        continue
    assert f'L"build"/L"{f["file"]}"' in source, f'{f["id"]}: knob {f["file"]} is not read in src/dxgi_probe.cpp'
csproj = (root / "launcher/DishonoredVRLauncher.csproj").read_text(encoding="utf-8")
assert re.search(r'EmbeddedResource Include="[^"]*default_features\.json"[^>]*LogicalName="features\.json"', csproj), "launcher does not embed default_features.json"
print("launcher_features_source_check ok:", len(features), "features")
