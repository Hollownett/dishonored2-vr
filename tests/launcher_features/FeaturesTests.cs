using DishonoredVR;

if (args.Length == 2 && args[0] == "--write-settings-pipeline")
{
    var file = Path.Combine(args[1], "build", "vr-settings.cfg");
    Prefs.Merge(new Prefs(), new Prefs { TemporalAa = 7, Hud = 150, Mirror = 1, Sharpness = .25 }, new Prefs()).Save(file);
    Console.WriteLine(file);
    return 0;
}

int failures = 0;
void Check(string name, bool ok) { Console.WriteLine((ok ? "PASS " : "FAIL ") + name); if (!ok) failures++; }

var list = Features.Load(File.OpenRead(Path.Combine(AppContext.BaseDirectory, "default_features.json")));
Feature F(string id) => list.First(f => f.Id == id);

string NewBuild() { var d = Path.Combine(Path.GetTempPath(), "d2vrfeat-" + Guid.NewGuid().ToString("N"), "build"); Directory.CreateDirectory(d); return d; }
string Listing(string d) => string.Join("|", Directory.GetFiles(d).OrderBy(x => x, StringComparer.Ordinal).Select(f => Path.GetFileName(f) + "=" + Convert.ToHexString(File.ReadAllBytes(f))));
bool Has(string d, string f) => File.Exists(Path.Combine(d, f));
string[] Ids(string d, string key) => System.Text.Json.JsonDocument.Parse(File.ReadAllText(Path.Combine(d, "launcher-features.json"))).RootElement.GetProperty(key).EnumerateArray().Select(x => x.GetString()!).ToArray();

Check("performance_mode_row_is_default_off_alternate_eye", list.Count(f => f.Id == "performance-mode" && f.File == "alternate-eye" && f.Kind == "enable" && !f.Default && f.Help.Contains("selected anti-aliasing") && f.Label.Contains("double")) == 1);   // v637: off by default (headset v636: one eye doubled on motion)
Check("list_has_no_unwanted_rows", !list.Any(f => f.Id is "corpse-grab") && list.Count(f => f.Id == "hold-y-menu" && f.File == "no-hold-y-menu" && f.Kind == "disable" && f.Default) == 1 && list.Count == list.Select(f => f.Id).Distinct().Count());
LogCleanupTests.Run(Check); // v1047: the Tools page's "Delete logs" (no build folder: it runs before the feature cases)

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
    Check("default_false_row_not_created", !F("native-temporal-census").Default && !Has(d, "native-temporal-census"));
    Check("default_false_row_reports_off_and_set_on_creates", !Features.State(d, list)["native-temporal-census"].On && Features.State(d, list)["full-body"].On);
    Features.Set(d, F("native-temporal-census"), true);
    Check("set_on_creates_default_false_row", Has(d, "native-temporal-census") && Features.State(d, list)["native-temporal-census"].On);
    Features.Apply(d, list);
    Check("default_false_row_survives_apply_after_set", Has(d, "native-temporal-census"));
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
{   // Fresh installs retain the deliberately opt-in performance and diagnostic paths.
    var d = NewBuild(); Features.Apply(d, list);
    var st = Features.State(d, list);
    var offIds = list.Where(f => !st[f.Id].On).Select(f => f.Id).ToArray();
    var newIds = new[] { "grip-alone-grab", "palm-select", "corpse-slip-release", "living-hold", "finger-surface", "finger-ease", "hand-grenade-throw", "hand-mine-aim", "bink-theater", "cutscene-skip-hold", "letterbox-hide" };
    Check("fresh_install_enables_sprint_and_selected_aa", offIds.OrderBy(x => x).SequenceEqual(new[] { "performance-mode", "power-gestures", "native-temporal-census", "dual-wield", "diagnostics", "diagnostic-capture", "skip-intro" }.OrderBy(x => x)) && newIds.All(i => st[i].On) && list.Where(f => !f.IsEnable).All(f => !Has(d, f.File)) && !list.Any(f => f.File == "finger-probe-3"));
}
{   // v1046: the old frame-diagnostics row (no-diagnostics) is gone; the one master "diagnostics" switch (build\diagnostics) is off by default.
    var old = new Feature("frame-diagnostics", "no-diagnostics", "disable", "x", "x", true);
    var d = NewBuild(); Features.Apply(d, new[] { old });                        // a home that seeded the old row
    Features.Apply(d, list);                                                      // must not throw or choke on the dropped id
    Check("diagnostics_master_switch_is_off_after_the_old_row_was_seeded", !Has(d, "diagnostics") && !Features.State(d, list)["diagnostics"].On && Ids(d, "seeded").Contains("diagnostics"));
    Features.Apply(d, list);
    Check("diagnostics_stays_off_on_every_apply", !Has(d, "diagnostics"));
    var o = NewBuild(); Features.Apply(o, new[] { old }); Features.Set(o, old, false);   // the user had switched the old diagnostics off: the file no-diagnostics exists
    Features.Apply(o, list);
    Check("old_no_diagnostics_file_is_left_alone_and_diagnostics_stays_off", Has(o, "no-diagnostics") && !Has(o, "diagnostics") && !Features.State(o, list)["diagnostics"].On);
    Features.Set(d, F("diagnostics"), true);
    Check("diagnostics_switch_on_writes_the_file", Has(d, "diagnostics") && Features.State(d, list)["diagnostics"].On);
    Features.Apply(d, list); Features.Set(d, F("diagnostics"), false);
    Check("diagnostics_switch_off_removes_the_file", !Has(d, "diagnostics") && !Features.State(d, list)["diagnostics"].On);
    var h = NewBuild(); File.WriteAllText(Path.Combine(h, "diagnostics"), ""); Features.Apply(h, list); Features.Apply(h, list);
    Check("handmade_diagnostics_file_is_kept", Has(h, "diagnostics") && Features.State(h, list)["diagnostics"].On);
}
{   // Upgrade v620 experiments to defaults, preserving an explicit user opt-out.
    foreach (var id in new[] { "run-root-gate", "alternate-upscalers" })
    {
        var old = new Feature(id, id, "enable", id, id, false);
        var d = NewBuild(); Features.Apply(d, new[] { old });
        Features.Apply(d, list);
        Check("repair_default_upgrades_" + id, Features.State(d, list)[id].On && !Has(d, "no-" + id));
        Features.Set(d, F(id), false); Features.Apply(d, list);
        Check("repair_optout_persists_" + id, !Features.State(d, list)[id].On && Has(d, "no-" + id));
        var explicitOff = NewBuild(); Features.Apply(explicitOff, new[] { old }); Features.Set(explicitOff, old, false);
        Features.Apply(explicitOff, list);
        Check("old_repair_optout_migrates_" + id, !Features.State(explicitOff, list)[id].On && Has(explicitOff, "no-" + id));
    }
}
{   // handmade_negative_knob_is_adopted
    var d = NewBuild(); File.WriteAllText(Path.Combine(d, "no-stealth-contact"), "");
    Features.Apply(d, list);
    var st = Features.State(d, list)["stealth-contact"];
    Check("handmade_negative_knob_is_adopted", Has(d, "no-stealth-contact") && !st.On && st.Adopted && Ids(d, "off").Contains("stealth-contact"));
}
{   // handmade_positive_knob_is_kept
    var d = NewBuild(); File.WriteAllText(Path.Combine(d, "native-temporal-census"), "");
    Features.Apply(d, list);
    Check("handmade_positive_knob_is_kept", Has(d, "native-temporal-census") && Features.State(d, list)["native-temporal-census"].On);
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
        Features.Set(d, list, "native-temporal-census", true);   // default-off row switched on by the user
        File.WriteAllText(P("hud-scale"), "0.75");
        bool setAside = File.ReadAllText(P("full-body.off-by-launcher")) == "0.5 0.2" && !Has(d, "full-body");
        Features.RestoreDefaults(d, list);
        var st = Features.State(d, list);
        Check("restore_defaults_restores_saved_tuning", setAside && File.ReadAllText(P("full-body")) == "0.5 0.2" && !Has(d, "full-body.off-by-launcher"));
        Check("restore_defaults_matches_defaults", list.All(f => st[f.Id].On == f.Default) && !Has(d, "no-hand-blink") && !Has(d, "native-temporal-census") && Ids(d, "off").OrderBy(x => x, StringComparer.Ordinal).SequenceEqual(list.Where(f => !f.Default).Select(f => f.Id).OrderBy(x => x, StringComparer.Ordinal)));
        Check("restore_defaults_leaves_unknown_files", File.ReadAllText(P("hud-scale")) == "0.75");
        var again = Listing(d); Features.RestoreDefaults(d, list);
        Check("restore_defaults_is_idempotent", Listing(d) == again);
        Features.Set(d, list, "native-temporal-census", true); Features.RestoreDefaults(d, list);
        Check("restore_defaults_switches_default_off_row_off", !Has(d, "native-temporal-census") && !Features.State(d, list)["native-temporal-census"].On);
    }
    {   // restore_defaults_aborts_on_unreadable_state
        var d = NewBuild(); Directory.CreateDirectory(Path.Combine(d, "launcher-features.json"));
        bool typed = false; try { Features.RestoreDefaults(d, list); } catch (Features.FeaturesStateException) { typed = true; }
        Check("restore_defaults_aborts_on_unreadable_state", typed && !Has(d, "full-body"));
    }
}
{   // proxy install (launcher/ProxyInstall.cs), on temp folders only
    string NewGame() { var d = Path.Combine(Path.GetTempPath(), "d2vrproxy-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(d); return d; }
    var ours = System.Text.Encoding.ASCII.GetBytes("MZ-fake-proxy DirectInput8Create " + ProxyInstall.Marker + " tail");
    var theirs = System.Text.Encoding.ASCII.GetBytes("MZ-another-mods-dinput8");
    const string home = @"C:\Users\me\AppData\Local\DishonoredVR";
    string Dll(string g) => Path.Combine(g, "dinput8.dll"); string Ini(string g) => Path.Combine(g, "d2vr-linux.ini");

    {   var g = NewGame(); File.WriteAllBytes(Dll(g), theirs);
        string? why = null; try { ProxyInstall.Install(g, home, ours); } catch (InvalidOperationException e) { why = e.Message; }
        Check("launcher_proxy_install_refuses_foreign_dinput8", why != null && why.Contains("another mod") && File.ReadAllBytes(Dll(g)).SequenceEqual(theirs) && !File.Exists(Ini(g)));
        Check("uninstall_leaves_foreign_dinput8", ProxyInstall.Uninstall(g).Count == 0 && File.ReadAllBytes(Dll(g)).SequenceEqual(theirs) && !ProxyInstall.Installed(g)); }
    {   var g = NewGame(); string? why = null;
        try { ProxyInstall.Install(g, home, theirs); } catch (InvalidOperationException e) { why = e.Message; }
        Check("install_refuses_a_payload_without_the_marker", why != null && why.Contains("marker") && !File.Exists(Dll(g)) && !File.Exists(Ini(g)));
        string? why2 = null; try { ProxyInstall.Install(Path.Combine(g, "missing"), home, ours); } catch (InvalidOperationException e) { why2 = e.Message; }
        Check("install_refuses_a_missing_game_folder", why2 != null); }
    {   var g = NewGame();
        var changed = ProxyInstall.Install(g, home, ours);
        var ini = File.ReadAllBytes(Ini(g));
        Check("install_writes_proxy_and_ini", changed.SequenceEqual(new[] { "dinput8.dll", "d2vr-linux.ini" }) && File.ReadAllBytes(Dll(g)).SequenceEqual(ours) && ProxyInstall.Installed(g)
            && System.Text.Encoding.UTF8.GetString(ini) == home + "\r\n" && ini[0] != 0xEF && !Directory.GetFiles(g, "*.tmp").Any());
        Check("install_is_idempotent", ProxyInstall.Install(g, home, ours).Count == 0);
        var ours2 = System.Text.Encoding.ASCII.GetBytes("MZ-newer DirectInput8Create " + ProxyInstall.Marker);
        Check("install_replaces_our_older_proxy", ProxyInstall.Install(g, home + "\\", ours2).SequenceEqual(new[] { "dinput8.dll" }) && File.ReadAllBytes(Dll(g)).SequenceEqual(ours2));
        var removed = ProxyInstall.Uninstall(g);
        Check("install_uninstall_round_trip", removed.SequenceEqual(new[] { "dinput8.dll", "d2vr-linux.ini" }) && !File.Exists(Dll(g)) && !File.Exists(Ini(g)) && !ProxyInstall.Installed(g) && Directory.GetFileSystemEntries(g).Length == 0);
        Check("uninstall_twice_is_a_no_op", ProxyInstall.Uninstall(g).Count == 0); }
    {   var g = NewGame(); ProxyInstall.Install(g, home, ours); File.Delete(Dll(g)); File.WriteAllBytes(Dll(g), theirs);
        var removed = ProxyInstall.Uninstall(g);   // modhome.uninstall: ini goes, a foreign dll stays
        Check("uninstall_removes_ini_but_not_a_foreign_dll", removed.SequenceEqual(new[] { "d2vr-linux.ini" }) && File.Exists(Dll(g))); }
    {   // fix round 1: loaded-module identity, start state parsing, tmp cleanup, hash identity
        var g = NewGame(); var loaded = new[] { Path.Combine(g, "Dishonored2.exe"), Path.Combine(g, "dinput8.dll"), @"C:\Windows\System32\kernel32.dll" };
        File.WriteAllBytes(Dll(g), theirs);
        Check("foreign_loaded_dinput8_is_not_our_proxy", !ProxyInstall.OurProxyLoaded(g, loaded));
        Check("stop_uses_remote_stop_for_a_foreign_dinput8", !ProxyInstall.StopViaProxy(ProxyInstall.OurProxyLoaded(g, loaded), ProxyInstall.StartPath.None));
        File.Delete(Dll(g)); ProxyInstall.Install(g, home, ours);
        Check("our_loaded_dinput8_is_our_proxy", ProxyInstall.OurProxyLoaded(g, loaded) && !ProxyInstall.OurProxyLoaded(g, new[] { @"C:\Windows\System32\dinput8.dll" }));
        Check("stop_via_proxy_only_for_proxy_sessions", ProxyInstall.StopViaProxy(true, ProxyInstall.StartPath.Proxy) && ProxyInstall.StopViaProxy(true, ProxyInstall.StartPath.None) && !ProxyInstall.StopViaProxy(true, ProxyInstall.StartPath.Injected) && !ProxyInstall.StopViaProxy(false, ProxyInstall.StartPath.Proxy));
        File.WriteAllBytes(Dll(g), System.Text.Encoding.ASCII.GetBytes("MZ " + ProxyInstall.Marker));  // marker but no export: not ours
        Check("marker_alone_is_not_identity", !ProxyInstall.IsOursFile(Dll(g)));
        string? w = null; try { ProxyInstall.Install(g, home, ours); } catch (InvalidOperationException e) { w = e.Message; }
        Check("truncated_copy_is_refused", w != null);
        File.WriteAllBytes(Dll(g) + ".tmp", ours); File.WriteAllText(Ini(g) + ".tmp", "x"); File.Delete(Dll(g));
        Check("uninstall_removes_our_tmp_files", ProxyInstall.Uninstall(g).Count == 3 && Directory.GetFileSystemEntries(g).Length == 0); }
    {   var st = ProxyInstall.ReadStart("proxy ...\nStartProbe begin x:\\mod\\build\\d.dll\n", out var c0, out _);
        Check("start_state_begin_is_started", st == ProxyInstall.StartState.Started && c0 == 0);
        Check("start_state_empty_log_is_pending", ProxyInstall.ReadStart("", out _, out _) == ProxyInstall.StartState.Pending && ProxyInstall.ReadStart("proxy up\ngame window found\n", out _, out _) == ProxyInstall.StartState.Pending);
        var f3 = ProxyInstall.ReadStart("StartProbe begin d\nStartProbe returned 3 (x\\.git missing)\n", out var c3, out var r3);
        Check("start_state_nonzero_return_is_failed_with_reason", f3 == ProxyInstall.StartState.Failed && c3 == 3 && r3.Contains(".git"));
        var f1 = ProxyInstall.ReadStart("StartProbe begin d\nStartProbe returned 1 (see artifacts)\n", out var c1, out var r1);
        var f2 = ProxyInstall.ReadStart("StartProbe returned 2\n", out var c2, out var r2);
        Check("start_state_codes_1_and_2_have_reasons", f1 == ProxyInstall.StartState.Failed && c1 == 1 && r1.Contains("dxgi-probe-failure") && f2 == ProxyInstall.StartState.Failed && c2 == 2 && r2.Contains("shutting down"));
        Check("start_state_zero_return_is_ended_not_failure", ProxyInstall.ReadStart("StartProbe begin d\nStartProbe returned 0\n", out _, out _) == ProxyInstall.StartState.Ended);
        Check("start_state_uses_latest_session", ProxyInstall.ReadStart("StartProbe begin d\nStartProbe returned 1\nStartProbe begin d\n", out _, out _) == ProxyInstall.StartState.Started
            && ProxyInstall.ReadStart("StartProbe returned 1\nStartProbe begin d\nStartProbe returned 0\n", out _, out _) == ProxyInstall.StartState.Ended); }
    {   // Real files: a stopped proxy needs a fresh request in the configured home's build directory.
        var g = NewGame(); var h = Path.GetDirectoryName(NewBuild())!;
        ProxyInstall.Install(g, h, ours);
        var start = Path.Combine(h, "build", "linux-start"); var stop = Path.Combine(h, "build", "linux-stop");
        Check("restart_requests_configured_home", ProxyInstall.RequestStart(g, h, "StartProbe begin d\nStartProbe returned 0\n") && File.Exists(start));
        File.Delete(start);
        Check("restart_never_queues_behind_running_session", !ProxyInstall.RequestStart(g, h, "StartProbe returned 0\nStartProbe begin d\n") && !File.Exists(start));
        File.WriteAllText(stop, "stop"); bool refusedStop = false;
        try { ProxyInstall.RequestStart(g, h, "StartProbe returned 0\n"); } catch (InvalidOperationException) { refusedStop = true; }
        Check("restart_waits_for_pending_stop", refusedStop && !File.Exists(start)); File.Delete(stop);
        File.WriteAllText(Ini(g), h + "-other"); bool refusedHome = false;
        try { ProxyInstall.RequestStart(g, h, "StartProbe returned 0\n"); } catch (InvalidOperationException) { refusedHome = true; }
        Check("restart_refuses_another_mod_home", refusedHome && !File.Exists(start));
        Directory.Delete(g, true); Directory.Delete(h, true);
    }
    Check("fallback_and_default_seconds", ProxyInstall.DefaultStartSeconds == 0 && ProxyInstall.FallbackDelaySeconds == 45);
}
{   // DLSS provider: the user's own nvngx_dlss.dll, mod home first, then build\dlss-dll-path; nothing is copied or bundled.
    var root = Path.Combine(Path.GetTempPath(), "d2vrdlss-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(Path.Combine(root, "build"));
    Check("dlss_nothing_found", DlssProvider.Resolve(root) == null && DlssProvider.Configured(root) == null && DlssProvider.Status(root).Contains("hidden"));
    var game = Path.Combine(root, "games", "bin"); Directory.CreateDirectory(game);
    var dll = Path.Combine(game, DlssProvider.DllName);
    bool threw = false; try { DlssProvider.Configure(root, dll); } catch (FileNotFoundException) { threw = true; }
    Check("dlss_configure_requires_existing_dll", threw && !File.Exists(DlssProvider.KnobPath(root)));
    File.WriteAllBytes(dll, new byte[] { 1, 2, 3 });
    threw = false; try { DlssProvider.Configure(root, Path.Combine(game, "other.dll")); } catch (InvalidDataException) { threw = true; }
    Check("dlss_configure_rejects_other_names", threw);
    var located = DlssProvider.Configure(root, dll);
    Check("dlss_configure_writes_knob", located.Source == "configured" && File.ReadAllText(DlssProvider.KnobPath(root)) == Path.GetFullPath(dll) + "\n" && !Directory.GetFiles(Path.Combine(root, "build"), "*.tmp").Any());
    Check("dlss_resolve_configured", DlssProvider.Resolve(root) is { Source: "configured" } r1 && r1.Path == Path.GetFullPath(dll));
    File.WriteAllText(DlssProvider.KnobPath(root), "  \"" + game + "\"\r\n");
    Check("dlss_knob_folder_and_quotes", DlssProvider.Resolve(root) is { } r2 && r2.Path == dll);
    File.WriteAllText(DlssProvider.KnobPath(root), "relative\\" + DlssProvider.DllName + "\n");
    Check("dlss_knob_relative_refused", DlssProvider.Configured(root) == null && DlssProvider.Resolve(root) == null);
    File.WriteAllText(DlssProvider.KnobPath(root), "\n");
    Check("dlss_knob_blank_ignored", DlssProvider.Resolve(root) == null);
    File.WriteAllText(DlssProvider.KnobPath(root), dll + "\n");
    File.WriteAllBytes(Path.Combine(root, DlssProvider.DllName), new byte[] { 4 });
    Check("dlss_mod_home_wins", DlssProvider.Resolve(root) is { Source: "mod home" } r3 && r3.Path == Path.Combine(root, DlssProvider.DllName));
    Check("dlss_status_names_provider", DlssProvider.Status(root).Contains(DlssProvider.DllName) && DlssProvider.Status(root).Contains("mod home"));
    Check("dlss_attribution_present", DlssProvider.Attribution.Contains("NVIDIA DLSS") && DlssProvider.Prompt.Contains(DlssProvider.DllName));
    Check("dlss_available_needs_driver", DlssProvider.Available(root) == DlssProvider.NvidiaDriverPresent());
}
{   // DlssFinder: auto-find the user's own DLL in installed Steam games (temp trees only).
    string Tmp() { var d = Path.Combine(Path.GetTempPath(), "d2vrfind-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(d); return d; }
    string Dll(string lib, string rel, byte b = 4) { var f = Path.GetFullPath(Path.Combine(lib, "steamapps", "common", rel, DlssProvider.DllName)); Directory.CreateDirectory(Path.GetDirectoryName(f)!); File.WriteAllBytes(f, new[] { b }); return f; }
    var steam = Tmp(); Directory.CreateDirectory(Path.Combine(steam, "steamapps"));
    var lib2 = Tmp(); var lib3 = Tmp();
    File.WriteAllText(Path.Combine(steam, "steamapps", "libraryfolders.vdf"), "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + steam.Replace("\\", "\\\\") + "\"\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + lib2.Replace("\\", "\\\\") + "\"\n\t\t\"label\"\t\t\"\"\n\t}\n\t\"2\"\n\t{\n\t\t\"path\"\t\t\"" + lib3.Replace("\\", "\\\\") + "\"\n\t}\n}\n");
    var libs = DlssFinder.SteamLibraries(steam);
    Check("vdf_parses_library_paths", libs.SequenceEqual(new[] { steam, lib2, lib3 }));
    var winVdf = Tmp(); Directory.CreateDirectory(Path.Combine(winVdf, "steamapps"));
    File.WriteAllText(Path.Combine(winVdf, "steamapps", "libraryfolders.vdf"), "\"libraryfolders\" { \"1\" { \"path\" \"D:\\\\SteamLibrary\" } \"2\" { \"path\" \"E:\\\\Games\\\\Steam Lib\" } }");
    Check("vdf_escaped_backslashes", DlssFinder.SteamLibraries(winVdf).Skip(1).SequenceEqual(new[] { @"D:\SteamLibrary", @"E:\Games\Steam Lib" }));
    File.WriteAllText(Path.Combine(winVdf, "steamapps", "libraryfolders.vdf"), "\"libraryfolders\" { \"path\" \"unterminated \\\\ ");
    Check("vdf_garbled_returns_root_only", DlssFinder.SteamLibraries(winVdf).SequenceEqual(new[] { winVdf }) && DlssFinder.SteamLibraries(Path.Combine(winVdf, "nope")).Count == 1);
    var deep = Tmp(); var near = Dll(deep, "A"); var edge = Dll(deep, "B/1/2/3/4"); Dll(deep, "C/1/2/3/4/5");
    var cands = DlssFinder.Candidates(new[] { deep });
    Check("candidates_depth_limited", cands.Count == 2 && cands.Contains(near) && cands.Contains(edge));
    var many = Tmp(); for (int i = 0; i < 8; i++) Dll(many, "g" + i);
    Check("candidates_capped", DlssFinder.Candidates(new[] { many }, 5, 3).Count == 3 && DlssFinder.Candidates(new[] { many }).Count == 8 && DlssFinder.Candidates(new[] { Path.Combine(many, "missing") }).Count == 0);
    var link = Tmp(); var real = Tmp(); Dll(real, "Linked");
    try { Directory.CreateSymbolicLink(Path.Combine(link, "steamapps-x"), Path.Combine(real, "steamapps", "common")); Directory.CreateDirectory(Path.Combine(link, "steamapps", "common")); Directory.CreateSymbolicLink(Path.Combine(link, "steamapps", "common", "Linked"), Path.Combine(real, "steamapps", "common", "Linked"));
        Check("candidates_skip_reparse_points", DlssFinder.Candidates(new[] { link }).Count == 0); } catch (Exception) { Check("candidates_skip_reparse_points", true); }
    var pick = Tmp(); var old = Dll(pick, "Old"); var fresh = Dll(pick, "New/deeper"); File.SetLastWriteTimeUtc(old, new DateTime(2022, 1, 1)); File.SetLastWriteTimeUtc(fresh, new DateTime(2024, 1, 1));
    Check("best_prefers_higher_version_else_newer", DlssFinder.Best(new[] { old, fresh }) == fresh && DlssFinder.Best(new[] { fresh, old }) == fresh && DlssFinder.Best(Array.Empty<string>()) == null);
    var tie = Tmp(); var s1 = Dll(tie, "Z"); var s2 = Dll(tie, "A/long/er"); var when = new DateTime(2023, 5, 5); File.SetLastWriteTimeUtc(s1, when); File.SetLastWriteTimeUtc(s2, when);
    Check("best_tie_shortest_path", DlssFinder.Best(new[] { s2, s1 }) == s1);
    var rootOk = Tmp(); Directory.CreateDirectory(Path.Combine(rootOk, "build"));
    var gotit = DlssFinder.FindAndConfigure(rootOk, tie);
    Check("find_configures_best_with_game_source", gotit is { } g && g.Path == s1 && g.Source == "auto: Z" && File.ReadAllText(DlssProvider.KnobPath(rootOk)) == s1 + "\n" && DlssProvider.Resolve(rootOk)?.Path == s1);
    var rootHome = Tmp(); Directory.CreateDirectory(Path.Combine(rootHome, "build")); File.WriteAllBytes(Path.Combine(rootHome, DlssProvider.DllName), new byte[] { 1 });
    Check("find_skips_when_provider_exists", DlssFinder.FindAndConfigure(rootHome, tie) == null && !File.Exists(DlssProvider.KnobPath(rootHome)));
    var rootKnob = Tmp(); Directory.CreateDirectory(Path.Combine(rootKnob, "build")); DlssProvider.Configure(rootKnob, old);
    Check("find_does_not_override_manual_knob", DlssFinder.FindAndConfigure(rootKnob, tie) == null && File.ReadAllText(DlssProvider.KnobPath(rootKnob)) == old + "\n");
    var rootNone = Tmp(); Directory.CreateDirectory(Path.Combine(rootNone, "build"));
    Check("find_nothing_is_null_and_writes_nothing", DlssFinder.FindAndConfigure(rootNone, Tmp()) == null && DlssFinder.FindAndConfigure(rootNone, null) == null && !File.Exists(DlssProvider.KnobPath(rootNone)));
}
Check("crosshair_row_moved_to_settings", !list.Any(x => x.Id == "hand-crosshair" || x.File == "no-hand-crosshair"));
{
    var dc = NewBuild(); File.WriteAllText(Path.Combine(dc, "no-hand-crosshair"), "");
    Features.Apply(dc, list);
    Check("leftover_no_hand_crosshair_untouched", Has(dc, "no-hand-crosshair"));
}
PrefsTests.Run(Check);
Console.WriteLine(failures == 0 ? "ALL PASS" : failures + " FAILED");
return failures == 0 ? 0 : 1;
