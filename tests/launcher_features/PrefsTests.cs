using DishonoredVR;

public static class PrefsTests
{
    public static void Run(Action<string, bool> check)
    {
        string Tmp() => Path.Combine(Path.GetTempPath(), "d2vrprefs-" + Guid.NewGuid().ToString("N"), "vr-settings.cfg");
        Prefs LoadText(string text) { var f = Tmp(); Directory.CreateDirectory(Path.GetDirectoryName(f)!); File.WriteAllText(f, text); return Prefs.Load(f); }
        bool Throws(string text) { try { LoadText(text); return false; } catch (InvalidDataException) { return true; } }

        var rt = new Prefs { Resolution = 1.25, TemporalAa = 5, Hud = 150, Hint = 0, AaChosen = 1, Crosshair = 0, Measurements = new[] { "1", "2", "3", "4", "5" } };
        var f1 = Tmp(); rt.Save(f1); var back = Prefs.Load(f1);
        check("prefs_v9_roundtrip", File.ReadAllText(f1).StartsWith("9 ") && back.Resolution == 1.25 && back.TemporalAa == 5 && back.Hud == 150 && back.Hint == 0
            && back.AaChosen == 1 && back.Crosshair == 0 && string.Join(",", back.Measurements) == "1,2,3,4,5");

        var v8 = LoadText("8 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0\n");
        check("prefs_v8_loads_marker_off_crosshair_on", v8.TemporalAa == 7 && v8.Hud == 150 && v8.AaChosen == 0 && v8.Crosshair == 1);
        check("prefs_rejects_v10", Throws("10 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0 1 0\n"));
        check("prefs_rejects_short_or_bad_v9", Throws("9 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0 1\n") && Throws("9 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0 2 0\n"));

        var fx = Path.Combine(AppContext.BaseDirectory, "vr-settings-v9.cfg");
        var p = Prefs.Load(fx);
        check("prefs_reads_shared_v9_fixture", p.TemporalAa == 7 && p.Hud == 150 && p.AaChosen == 1 && p.Crosshair == 0);

        check("hud_accepts_50_to_200", new Prefs { Hud = 200 }.Validate() == "" && new Prefs { Hud = 50 }.Validate() == "" && new Prefs { Hud = 201 }.Validate() != "" && new Prefs { Hud = 49 }.Validate() != "");
        check("validate_rejects_bad_flags", new Prefs { AaChosen = 2 }.Validate() != "" && new Prefs { Crosshair = -1 }.Validate() != "");

        var baseline = new Prefs { TemporalAa = 2, Hud = 65 };
        var form = new Prefs { TemporalAa = 2, Hud = 65, Mirror = 2 };
        var disk = new Prefs { TemporalAa = 7, Hud = 150, Crosshair = 0, AaChosen = 1 };
        var m = Prefs.Merge(baseline, form, disk);
        check("merge_keeps_game_changes_on_stale_form", m.TemporalAa == 7 && m.Hud == 150 && m.Crosshair == 0 && m.AaChosen == 1 && m.Mirror == 2);

        var m2 = Prefs.Merge(new Prefs { TemporalAa = 2 }, new Prefs { TemporalAa = 4 }, new Prefs { TemporalAa = 7, Measurements = new[] { "9", "9", "9", "9", "9" } });
        check("merge_takes_user_aa_edit_and_marks_chosen", m2.TemporalAa == 4 && m2.AaChosen == 1 && string.Join(",", m2.Measurements) == "9,9,9,9,9");

        var f2 = Tmp(); Prefs.Merge(p, Prefs.Load(fx), Prefs.Load(fx)).Save(f2);
        check("merge_without_edits_equals_disk", File.ReadAllBytes(f2).SequenceEqual(File.ReadAllBytes(fx)));
    }
}
