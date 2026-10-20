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

Feature Row(string id, string file, string kind, bool def = true) => new(id, file, kind, id, id, def);
IReadOnlyList<Feature> LoadText(string text) => Features.Load(new MemoryStream(System.Text.Encoding.UTF8.GetBytes(text)));
void WriteState(string d, string text) => File.WriteAllText(Path.Combine(d, "launcher-features.json"), text);
string StateText(string d) => File.ReadAllText(Path.Combine(d, "launcher-features.json"));

{   // 1. off is authoritative
    var d = NewBuild(); WriteState(d, "{\"version\":1,\"off\":[\"full-body\",\"hand-blink\"],\"seeded\":[]}");
    Features.Apply(d, list);
    Check("off_entry_for_unseeded_id_stays_off", !Has(d, "full-body") && Has(d, "no-hand-blink") && Has(d, "no-hand-clamp") == false && Ids(d, "seeded").Contains("full-body"));
    // seeded + off + file reappeared (enable): off wins
    File.WriteAllText(Path.Combine(d, "full-body"), "");
    Features.Apply(d, list);
    Check("off_wins_over_reappeared_file", !Has(d, "full-body"));
}
{   // 2. list evolution
    var d = NewBuild();
    Features.Apply(d, new[] { Row("x", "x-old", "enable") });
    Features.Set(d, Row("x", "x-old", "enable"), false);
    Features.Apply(d, new[] { Row("x", "x-new", "disable") });
    Check("kind_change_keeps_off", Has(d, "x-new") && !Has(d, "x-old") && Ids(d, "off").Contains("x"));
    var d2 = NewBuild(); Features.Apply(d2, new[] { Row("y", "y-old", "enable") });
    Features.Apply(d2, new[] { Row("y", "y-new", "enable") });
    Check("file_rename_seeds_new_default", Has(d2, "y-new") && Has(d2, "y-old"));
    var d3 = NewBuild(); Features.Apply(d3, new[] { Row("z", "z-knob", "enable") });
    Features.Apply(d3, new[] { Row("w", "w-knob", "enable") });
    Check("removed_row_untouched", Has(d3, "z-knob") && Ids(d3, "seeded").Contains("z") && Ids(d3, "seeded").Contains("w"));
    var d4 = NewBuild(); WriteState(d4, "{\"version\":1,\"off\":[],\"seeded\":[\"full-body\"]}");
    Features.Apply(d4, list);
    var doc = System.Text.Json.JsonDocument.Parse(StateText(d4)).RootElement;
    Check("v1_state_upgrades", doc.GetProperty("version").GetInt32() == 2 && !Has(d4, "full-body") && doc.GetProperty("applied").GetProperty("full-body").GetProperty("kind").GetString() == "enable");
}
{   // 3. read errors abort untouched
    var d = NewBuild(); Directory.CreateDirectory(Path.Combine(d, "launcher-features.json"));
    bool typed = false; try { Features.Apply(d, list); } catch (Features.FeaturesStateException) { typed = true; }
    Check("unreadable_state_aborts_untouched", typed && !Has(d, "full-body") && Directory.Exists(Path.Combine(d, "launcher-features.json")));
}
{   // 4/5. malformed list (via fixtures below); bad list never applies
    bool rejected = false; try { LoadText("{\"features\":5}"); } catch (Exception) { rejected = true; }
    Check("malformed_list_rejected", rejected);
}
{   // 7. valued full-body survives off/on
    var d = NewBuild(); File.WriteAllText(Path.Combine(d, "full-body"), "0.5 0.2");
    Features.Apply(d, list);
    Features.Set(d, F("full-body"), false);
    bool off = !Has(d, "full-body");
    Features.Set(d, F("full-body"), true);
    Check("full_body_tuning_survives_off_on", off && File.ReadAllText(Path.Combine(d, "full-body")) == "0.5 0.2" && !Has(d, "full-body.off-by-launcher"));
}
{   // 6. shared fixtures
    var dir = Path.Combine(AppContext.BaseDirectory, "fixtures");
    foreach (var file in Directory.GetFiles(dir, "*.json").OrderBy(x => x))
    {
        var j = System.Text.Json.JsonDocument.Parse(File.ReadAllText(file)).RootElement;
        var name = Path.GetFileNameWithoutExtension(file); var kind = j.GetProperty("kind").GetString();
        if (kind == "list")
        {
            bool ok = true; try { LoadText(j.GetProperty("content").GetString()!); } catch (Exception) { ok = false; }
            Check("fixture " + name, ok == (j.GetProperty("expect").GetString() == "ok"));
        }
        else if (kind == "state")
        {
            var d = NewBuild(); WriteState(d, j.GetProperty("content").GetString()!);
            var expect = j.GetProperty("expect").GetString();
            bool aborted = false; try { Features.Apply(d, list); } catch (Features.FeaturesStateException) { aborted = true; }
            bool bad = Has(d, "launcher-features.json.bad");
            Check("fixture " + name, expect == "abort" ? aborted && !bad && !Has(d, "full-body") && StateText(d) == j.GetProperty("content").GetString() : !aborted && bad == (expect == "corrupt"));
        }
        else
        {
            var found = Features.ScanKnobs(Convert.FromHexString(j.GetProperty("hex").GetString()!));
            Check("fixture " + name, j.GetProperty("contains").EnumerateArray().All(x => found.Contains(x.GetString()!)) && !j.GetProperty("absent").EnumerateArray().Any(x => found.Contains(x.GetString()!)));
        }
    }
}

{   // round 2
    var fb = F("full-body");
    var d = NewBuild(); string P(string n) => Path.Combine(d, n);
    File.WriteAllText(P("full-body"), "A"); Features.Apply(d, list);
    Features.Set(d, fb, false);
    File.WriteAllText(P("full-body"), "B"); Features.Apply(d, list);  // off wins: B is set aside too
    Check("second_retire_keeps_first_saved_copy", !Has(d, "full-body") && File.ReadAllText(P("full-body.off-by-launcher")) == "A" && File.ReadAllText(P("full-body.off-by-launcher.1")) == "B");
    Features.Set(d, fb, true);
    Check("restore_takes_newest_copy", File.ReadAllText(P("full-body")) == "B" && File.ReadAllText(P("full-body.off-by-launcher")) == "A");
    File.WriteAllText(P("full-body"), "C"); Features.Set(d, fb, true);
    Check("restore_never_clobbers_existing_file", File.ReadAllText(P("full-body")) == "C" && Has(d, "full-body.off-by-launcher"));
}
{
    var fb = F("full-body"); var d = NewBuild(); string P(string n) => Path.Combine(d, n);
    File.WriteAllText(P("full-body"), "0.5 0.2"); Features.Apply(d, list); Features.Set(d, fb, false);
    // the user edits the state: removes full-body (enable) and no-hand-blink (disable) from off
    Features.Set(d, F("hand-blink"), false);
    var doc = System.Text.Json.Nodes.JsonNode.Parse(StateText(d))!;
    var off = doc["off"]!.AsArray(); for (int i = off.Count - 1; i >= 0; i--) off.RemoveAt(i);
    File.WriteAllText(P("launcher-features.json"), doc.ToJsonString());
    Features.Apply(d, list);
    Check("leaving_off_turns_feature_back_on", File.ReadAllText(P("full-body")) == "0.5 0.2" && !Has(d, "full-body.off-by-launcher") && !Has(d, "no-hand-blink") && Features.State(d, list)["hand-blink"].On);
}
{
    var d = NewBuild(); string P(string n) => Path.Combine(d, n);
    Features.Apply(d, new[] { Row("y", "y-old", "enable") });
    File.WriteAllText(P("y-old"), "T"); Features.Set(d, Row("y", "y-old", "enable"), false);
    Features.Apply(d, new[] { Row("y", "y-new", "enable") });
    Check("renamed_row_migrates_saved_copies", Has(d, "y-old.off-by-launcher") == false && File.ReadAllText(P("y-new.off-by-launcher")) == "T" && !Has(d, "y-new"));
    Features.Set(d, Row("y", "y-new", "enable"), true);
    Check("renamed_row_restores_migrated_tuning", File.ReadAllText(P("y-new")) == "T");
}
{
    var d = NewBuild(); File.WriteAllText(Path.Combine(d, "launcher-features.json"), "{\"version\":7,\"off\":[],\"seeded\":[]}");
    bool aborted = false; try { Features.Apply(d, list); } catch (Features.FeaturesStateException) { aborted = true; }
    Check("unknown_version_aborts_untouched", aborted && !Has(d, "full-body") && !Has(d, "launcher-features.json.bad"));
}

{   // round 3
    var fb = F("full-body");
    {   // 1. round-1 state, off absent: Set another feature, Apply -> no archive, off intact
        var d = NewBuild(); string P(string n) => Path.Combine(d, n);
        WriteState(d, "{\"version\":2,\"off\":[\"full-body\"],\"seeded\":[\"full-body\"],\"applied\":{\"full-body\":{\"file\":\"full-body\",\"kind\":\"enable\"}}}");
        Features.Set(d, F("hand-blink"), false);
        var written = StateText(d);
        Features.Apply(d, list);
        Check("null_off_never_written_round_trip", !written.Contains("null") && !Has(d, "launcher-features.json.bad") && Ids(d, "off").Contains("full-body") && !Has(d, "full-body"));
        Features.Apply(d, list.Where(f => f.Id != "full-body").ToList());  // row skipped: its applied entry is carried
        Features.Set(d, F("hand-clamp"), false);
        Features.Apply(d, list);
        Check("carried_applied_entry_round_trips", !Has(d, "launcher-features.json.bad") && Ids(d, "off").Contains("full-body") && !Has(d, "full-body"));
    }
    {   // 2. .bad never overwritten
        var d = NewBuild(); WriteState(d, "{g1"); Features.Apply(d, list);
        WriteState(d, "{g2"); Features.Apply(d, list);
        Check("bad_archives_not_overwritten", File.ReadAllText(Path.Combine(d, "launcher-features.json.bad")) == "{g1" && File.ReadAllText(Path.Combine(d, "launcher-features.json.bad.1")) == "{g2");
    }
    {   // 3. newest by mtime with a numbering gap
        var d = NewBuild(); string P(string n) => Path.Combine(d, n);
        File.WriteAllText(P("full-body.off-by-launcher"), "C"); File.WriteAllText(P("full-body.off-by-launcher.3"), "B");
        File.SetLastWriteTimeUtc(P("full-body.off-by-launcher"), DateTime.UtcNow);
        File.SetLastWriteTimeUtc(P("full-body.off-by-launcher.3"), DateTime.UtcNow.AddHours(-2));
        Features.Set(d, fb, true);
        Check("restore_picks_newest_by_mtime_with_gap", File.ReadAllText(P("full-body")) == "C" && Has(d, "full-body.off-by-launcher.3"));
    }
    {   // 4. slot collision is typed
        var d = NewBuild(); string P(string n) => Path.Combine(d, n);
        File.WriteAllText(P("full-body"), "T");
        Features.BeforeMove = dst => File.WriteAllText(dst, "X");
        bool typed = false; try { Features.Set(d, fb, false); } catch (Features.FeaturesStateException) { typed = true; }
        Features.BeforeMove = null;
        Check("slot_collision_is_typed_error", typed && File.ReadAllText(P("full-body")) == "T" && File.ReadAllText(P("full-body.off-by-launcher")) == "X");
    }
}
{   // round 4: launcher Features page
    {   // set_off_then_on_roundtrip
        var d = NewBuild(); Features.Apply(d, list); var before = Listing(d);
        Features.Set(d, list, "full-body", false); Features.Set(d, list, "hand-blink", false);
        bool off = !Has(d, "full-body") && Has(d, "no-hand-blink") && Ids(d, "off").OrderBy(x => x).SequenceEqual(new[] { "full-body", "hand-blink" });
        Features.Set(d, list, "full-body", true); Features.Set(d, list, "hand-blink", true);
        Check("set_off_then_on_roundtrip", off && Listing(d) == before && Ids(d, "off").Length == 0);
    }
    {   // set_rejects_unknown_id
        var d = NewBuild(); Features.Apply(d, list); var before = Listing(d); var st = StateText(d);
        bool threw = false; try { Features.Set(d, list, "no-such-feature", false); } catch (ArgumentException) { threw = true; }
        Check("set_rejects_unknown_id", threw && Listing(d) == before && StateText(d) == st);
    }
    {   // state_reports_on_for_untouched_defaults
        var d = NewBuild(); Features.Apply(d, list); var all = Features.State(d, list);
        Check("state_reports_on_for_untouched_defaults", list.All(f => all[f.Id].On == f.Default && !all[f.Id].Adopted));
        var d2 = NewBuild();  // before any Apply: knob files are absent, so enable rows read off and disable rows read on, no exception
        var none = Features.State(d2, list);
        Check("state_before_apply_does_not_throw", none["hand-blink"].On && !none["full-body"].On);
    }
    {   // restore_defaults_keeps_user_data
        var d = NewBuild(); string P(string n) => Path.Combine(d, n);
        Features.Apply(d, list);
        File.WriteAllText(P("full-body"), "0.5 0.2");
        Features.Set(d, list, "full-body", false);       // tuning set aside as a saved copy
        Features.Set(d, list, "hand-blink", false);
        Features.Set(d, list, "power-gestures", true);   // default-off row switched on by the user
        File.WriteAllText(P("hud-scale"), "0.75");
        bool setAside = File.ReadAllText(P("full-body.off-by-launcher")) == "0.5 0.2" && !Has(d, "full-body");
        Features.RestoreDefaults(d, list);
        var st = Features.State(d, list);
        Check("restore_defaults_restores_saved_tuning", setAside && File.ReadAllText(P("full-body")) == "0.5 0.2" && !Has(d, "full-body.off-by-launcher"));
        Check("restore_defaults_matches_defaults", list.All(f => st[f.Id].On == f.Default) && !Has(d, "no-hand-blink") && !Has(d, "power-gestures") && Ids(d, "off").SequenceEqual(new[] { "power-gestures" }));
        Check("restore_defaults_leaves_unknown_files", File.ReadAllText(P("hud-scale")) == "0.75");
        var again = Listing(d); Features.RestoreDefaults(d, list);
        Check("restore_defaults_is_idempotent", Listing(d) == again);
        Features.Set(d, list, "power-gestures", true); Features.RestoreDefaults(d, list);
        Check("restore_defaults_switches_default_off_row_off", !Has(d, "power-gestures") && !Features.State(d, list)["power-gestures"].On);
    }
    {   // restore_defaults_aborts_on_unreadable_state
        var d = NewBuild(); Directory.CreateDirectory(Path.Combine(d, "launcher-features.json"));
        bool typed = false; try { Features.RestoreDefaults(d, list); } catch (Features.FeaturesStateException) { typed = true; }
        Check("restore_defaults_aborts_on_unreadable_state", typed && !Has(d, "full-body"));
    }
}
Console.WriteLine(failures == 0 ? "ALL PASS" : failures + " FAILED");
return failures == 0 ? 0 : 1;
