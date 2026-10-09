"""Embed the pinned upstream shader without a runtime source-file dependency."""
import hashlib
import json
from pathlib import Path

root=Path(__file__).resolve().parents[1]/'third_party'/'smaa'
manifest=json.loads((root/'source.json').read_text())
for entry in manifest['files']:
    if hashlib.sha256((root/entry['path']).read_bytes()).hexdigest()!=entry['sha256']:
        raise RuntimeError('Vendored SMAA source differs: '+entry['path'])
# Upstream comments contain Latin-1 characters. Retain them as Unicode; the
# shader statements are ASCII. Split below MSVC's single-literal limit.
source=(root/'SMAA.hlsl').read_bytes().decode('latin-1')
assert ')D2SMAA"' not in source
chunks=[source[i:i+12000] for i in range(0,len(source),12000)]
(root/'shader_source.hpp').write_text('#pragma once\n// Generated from pinned SMAA.hlsl; upstream license is retained.\nnamespace d2vr { inline constexpr char smaa_hlsl[]=\n'+
    '\n'.join('R"D2SMAA('+chunk+')D2SMAA"' for chunk in chunks)+'; }\n',encoding='utf-8')
