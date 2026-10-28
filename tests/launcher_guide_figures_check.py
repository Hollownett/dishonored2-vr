"""Validate a locally generated guide against the shared on-disk contract (no game needed).

Usage: python tests/launcher_guide_figures_check.py artifacts/guide-figures
Generated assets stay outside version control.
"""
import hashlib
import json
from pathlib import Path
import struct
import sys


directory = Path(sys.argv[1])
manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8-sig"))
short = "sword_throw enemy_grab".split()
tall = []
for figure in short + tall:
    for character in ("corvo", "emily"):
        key = f"guide_{figure}_{character}"
        entry = manifest["keys"][key]
        expected = (864, 804 if figure in tall else 232)
        png = (directory / entry["file"]).read_bytes()
        raw = (directory / entry["raw"]).read_bytes()
        assert png[:8] == b"\x89PNG\r\n\x1a\n", key
        assert struct.unpack(">II", png[16:24]) == expected, key
        assert struct.unpack("<II", raw[:8]) == expected, key
        assert (entry["width"], entry["height"]) == expected, key
        assert len(raw) == 8 + expected[0] * expected[1] * 4, key
        assert all(a == 255 for a in raw[11::4]), f"{key}: slot must be opaque"
        assert hashlib.sha256(png).hexdigest() == entry["sha256"], key
        assert hashlib.sha256(raw).hexdigest() == entry["raw_sha256"], key
        assert entry["generation_version"] == manifest["guide_generation_version"], key
        assert entry["source_identity"], key
assert (directory / "generation.txt").read_text().strip() == manifest["generation"]
print("PASS: all 4 figures, dimensions, opaque RGBA, hashes, source and generation marker")
