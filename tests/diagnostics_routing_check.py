"""v1046: dxgi_probe.cpp keeps every diagnostic file behind d2vr::diag::Dir (off by default); only the functional files use .always()."""
import re,sys,pathlib
src=pathlib.Path(__file__).resolve().parent.parent/"src"/"dxgi_probe.cpp"
text=src.read_text(encoding="utf-8")
fail=[]
if "static d2vr::diag::Dir output_directory;" not in text: fail.append("output_directory is not a d2vr::diag::Dir")
if not re.search(r"d2vr::diag::set\(fs::exists\(root/L\"build\"/L\"diagnostics\"\)\)",text): fail.append("StartProbe does not read build/diagnostics into diag::set")
if "no-diagnostics\"" in text and "fs::exists(root/L\"build\"/L\"no-diagnostics\")" in text: fail.append("retired no-diagnostics knob is still read")
# every .always() use must be one of the functional files / roots
allowed={"aa-gpu-caps.txt","vr-command.txt","vr-command-result.txt","xr-live-status.json","dxgi-probe-result.json","dxgi-probe-failure.txt","ngx"}
used=set()
for m in re.finditer(r"\.always\(\)(?:\.parent_path\(\)|/L\"([^\"]+)\")?",text):
    if m.group(1): used.add(m.group(1))
extra=used-allowed
if extra: fail.append("unexpected .always() files: %s"%sorted(extra))
missing=allowed-used
if missing: fail.append("functional files no longer written via .always(): %s"%sorted(missing))
# no ofstream composes onto a plain directory path
for i,line in enumerate(text.split("\n"),1):
    if "std::ofstream" in line and re.search(r"\b(root|artifacts_dir)\s*/\s*L\"artifacts\"",line): fail.append("line %d writes via root/artifacts"%i)
    if re.search(r"output_directory\.always\(\)/",line) and not any(f in line for f in allowed): fail.append("line %d: always() with a diagnostic name"%i)
    if re.search(r"\boutput\.always\(\)/",line) and not any(f in line for f in allowed): fail.append("line %d: output.always() with a diagnostic name"%i)
# the status when off carries exactly the three fields the launcher and proxy read
if not re.search(r'\\"gameplay\\".*\\"logic_mask\\".*\\"game_layers_submitted\\".*\\"diagnostics\\":false',text.replace("\n"," ")): fail.append("off-mode status fields missing")
# image captures return before the GPU copy
if "diag::on()?d2vr::readback_rgba(" not in text: fail.append("readback wrapper missing")
if re.search(r"(?<!diag_)(?<!::)\breadback_rgba(_box)?\(",text.replace("d2vr::readback_rgba","")): fail.append("raw readback_rgba call outside the wrapper")
if fail: print("\n".join(fail));sys.exit(1)
print("diagnostics_routing_check OK")
