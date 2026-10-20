using DishonoredVR;

int failures = 0;
void Check(string name, bool ok) { Console.WriteLine((ok ? "PASS " : "FAIL ") + name); if (!ok) failures++; }

var list = Features.Load(File.OpenRead(Path.Combine(AppContext.BaseDirectory, "default_features.json")));
Feature F(string id) => list.First(f => f.Id == id);

string NewBuild() { var d = Path.Combine(Path.GetTempPath(), "d2vrfeat-" + Guid.NewGuid().ToString("N"), "build"); Directory.CreateDirectory(d); return d; }
string Listing(string d) => string.Join("|", Directory.GetFiles(d).OrderBy(x => x, StringComparer.Ordinal).Select(f => Path.GetFileName(f) + "=" + Convert.ToHexString(File.ReadAllBytes(f))));
bool Has(string d, string f) => File.Exists(Path.Combine(d, f));
string[] Ids(string d, string key) => System.Text.Json.JsonDocument.Parse(File.ReadAllText(Path.Combine(d, "launcher-features.json"))).RootElement.GetProperty(key).EnumerateArray().Select(x => x.GetString()!).ToArray();

Check("list_has_no_unwanted_rows", !list.Any(f => f.Id is "hold-y-menu" or "corpse-grab") && list.Count == list.Select(f => f.Id).Distinct().Count());

{   // fresh_home_enables_every_default
    var d = NewBuild(); Features.Apply(d, list);
    bool ok = true;
    foreach (var f in list)
    {
        bool shouldExist = f.IsEnable && f.Default;
        ok &= Has(d, f.File) == shouldExist;
    }
    ok &= Ids(d, "seeded").OrderBy(x => x).SequenceEqual(list.Select(f => f.Id).OrderBy(x => x));
    Check("fresh_home_enables_every_default", ok && Has(d, "full-body") && !Has(d, "no-hand-blink"));
    Check("default_false_row_not_created", !F("power-gestures").Default && !Has(d, "power-gestures"));
    Check("default_false_row_reports_off_and_set_on_creates", !Features.State(d, list)["power-gestures"].On && Features.State(d, list)["full-body"].On);
    Features.Set(d, F("power-gestures"), true);
    Check("set_on_creates_default_false_row", Has(d, "power-gestures") && Features.State(d, list)["power-gestures"].On);
    Features.Apply(d, list);
    Check("default_false_row_survives_apply_after_set", Has(d, "power-gestures"));
}
{   // apply_is_idempotent
    var d = NewBuild(); Features.Apply(d, list); var a = Listing(d); int n = Features.Apply(d, list);
    Check("apply_is_idempotent", a == Listing(d) && n == 0 && !Directory.GetFiles(d, "*.tmp").Any());
}
{   // disabled_feature_stays_off_after_apply
    var d = NewBuild(); Features.Apply(d, list);
    Features.Set(d, F("full-body"), false); Features.Apply(d, list);
    Check("disabled_feature_stays_off_after_apply", !Has(d, "full-body") && !Features.State(d, list)["full-body"].On && Ids(d, "off").Contains("full-body"));
    Features.Set(d, F("hand-blink"), false);
    bool wrote = Has(d, "no-hand-blink"); Features.Apply(d, list);
    Check("disable_kind_off_writes_no_file_and_stays", wrote && Has(d, "no-hand-blink") && !Features.State(d, list)["hand-blink"].On);
    Features.Set(d, F("hand-blink"), true);
    Check("disable_kind_on_removes_no_file", !Has(d, "no-hand-blink") && Features.State(d, list)["hand-blink"].On && !Ids(d, "off").Contains("hand-blink"));
}
{   // handmade_negative_knob_is_adopted
    var d = NewBuild(); File.WriteAllText(Path.Combine(d, "no-stealth-contact"), "");
    Features.Apply(d, list);
    var st = Features.State(d, list)["stealth-contact"];
    Check("handmade_negative_knob_is_adopted", Has(d, "no-stealth-contact") && !st.On && st.Adopted && Ids(d, "off").Contains("stealth-contact"));
}
{   // handmade_positive_knob_is_kept
    var d = NewBuild(); File.WriteAllText(Path.Combine(d, "power-gestures"), "");
    Features.Apply(d, list);
    Check("handmade_positive_knob_is_kept", Has(d, "power-gestures") && Features.State(d, list)["power-gestures"].On);
}
{   // unknown_files_untouched
    var d = NewBuild(); File.WriteAllText(Path.Combine(d, "hud-scale"), "0.75"); File.WriteAllBytes(Path.Combine(d, "fsr4.off"), Array.Empty<byte>());
    Features.Apply(d, list);
    Check("unknown_files_untouched", File.ReadAllText(Path.Combine(d, "hud-scale")) == "0.75" && Has(d, "fsr4.off") && new FileInfo(Path.Combine(d, "fsr4.off")).Length == 0);
}
{   // corrupt_state_file_is_replaced_safely
    var d = NewBuild(); File.WriteAllText(Path.Combine(d, "launcher-features.json"), "{garbage");
    bool threw = false; try { Features.Apply(d, list); } catch { threw = true; }
    Check("corrupt_state_file_is_replaced_safely", !threw && Has(d, "launcher-features.json.bad") && Has(d, "full-body") && Ids(d, "seeded").Length == list.Count);
}
{   // requires_dll_knob_skipped_when_dll_lacks_it
    var d = NewBuild();
    var extra = list.Append(new Feature("future", "future-knob", "disable", "Future", "x", true, "no-future-knob")).ToList();
    Features.Apply(d, extra, new HashSet<string> { "full-body" });
    bool skipped = !Ids(d, "seeded").Contains("future");
    var d2 = NewBuild(); Features.Apply(d2, extra, new HashSet<string> { "no-future-knob" });
    Check("requires_dll_knob_skipped_when_dll_lacks_it", skipped && Ids(d2, "seeded").Contains("future"));
}
{   // scan
    var bytes = System.Text.Encoding.Unicode.GetBytes("\0build\0no-hand-blink\0");
    Check("scan_knobs_finds_utf16_names", Features.ScanKnobs(bytes).Contains("no-hand-blink"));
}
Console.WriteLine(failures == 0 ? "ALL PASS" : failures + " FAILED");
return failures == 0 ? 0 : 1;
