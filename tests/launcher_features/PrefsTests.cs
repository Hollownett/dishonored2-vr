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
        check("prefs_v10_roundtrip", File.ReadAllText(f1).StartsWith("10 ") && back.Resolution == 1.25 && back.TemporalAa == 5 && back.Hud == 150 && back.Hint == 0
            && back.AaChosen == 1 && back.Crosshair == 0 && back.ScriptedRecenter == 1 && back.CutsceneMode == "vr_comfort" && string.Join(",", back.Measurements) == "1,2,3,4,5");

        var v8 = LoadText("8 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0\n");
        check("prefs_v8_loads_marker_off_crosshair_on", v8.TemporalAa == 7 && v8.Hud == 150 && v8.AaChosen == 0 && v8.Crosshair == 1);
        check("prefs_rejects_v11", Throws("11 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0 1 0 1 vr_full\n"));
        check("prefs_rejects_short_or_bad_v9", Throws("9 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0 1\n") && Throws("9 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0 2 0\n"));

        var v9 = LoadText("9 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0 1 0\n");
        check("prefs_v9_migrates_keeping_keys_with_new_defaults", v9.TemporalAa == 7 && v9.Hud == 150 && v9.AaChosen == 1 && v9.Crosshair == 0 && v9.ScriptedRecenter == 1 && v9.CutsceneMode == "vr_comfort");
        var f10 = Tmp(); v9.Save(f10);
        check("prefs_v9_resave_is_v10_and_keeps_keys", File.ReadAllText(f10).StartsWith("10 ") && Prefs.Load(f10).Crosshair == 0 && Prefs.Load(f10).Hud == 150);
        foreach (var mode in Prefs.CutsceneModes)
        {
            var f = Tmp(); new Prefs { ScriptedRecenter = 0, CutsceneMode = mode }.Save(f); var r = Prefs.Load(f);
            check("prefs_v10_cutscene_roundtrip_" + mode, r.ScriptedRecenter == 0 && r.CutsceneMode == mode);
        }
        check("prefs_v10_unknown_mode_is_default", LoadText("10 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0 1 0 0 bogus\n").CutsceneMode == "vr_comfort");
        check("prefs_rejects_short_v10", Throws("10 1.3 1 2 45 120 1 0.25 7 150 1 0 0 0 0 0 1 0\n"));
        var fx10 = Prefs.Load(Path.Combine(AppContext.BaseDirectory, "vr-settings-v10.cfg"));
        check("prefs_reads_shared_v10_fixture", fx10.TemporalAa == 7 && fx10.Crosshair == 0 && fx10.ScriptedRecenter == 0 && fx10.CutsceneMode == "theater");
        check("validate_rejects_bad_cutscene_settings", new Prefs { CutsceneMode = "imax" }.Validate() != "" && new Prefs { ScriptedRecenter = 2 }.Validate() != "");
        var mc = Prefs.Merge(new Prefs(), new Prefs { Hud = 100 }, new Prefs { CutsceneMode = "theater", ScriptedRecenter = 0 });
        check("merge_keeps_game_cutscene_mode", mc.CutsceneMode == "theater" && mc.ScriptedRecenter == 0 && mc.Hud == 100);
        var mu = Prefs.Merge(new Prefs(), new Prefs { CutsceneMode = "vr_full" }, new Prefs { CutsceneMode = "theater" });
        check("merge_takes_user_cutscene_edit", mu.CutsceneMode == "vr_full");

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

        // v613: picking the value that is already saved/default (FSR 3 native on a fresh install) is still a choice; without it the game applies its one-time DLAA default over the pick.
        var same = Prefs.Merge(new Prefs { TemporalAa = 2 }, new Prefs { TemporalAa = 2 }, new Prefs { TemporalAa = 2 });
        check("merge_marks_chosen_on_any_save_even_with_no_aa_edit", same.AaChosen == 1 && same.TemporalAa == 2);
        var other = Prefs.Merge(new Prefs { TemporalAa = 2 }, new Prefs { TemporalAa = 2, Hud = 90 }, new Prefs { TemporalAa = 7, AaChosen = 1 });
        check("merge_save_keeps_in_game_aa_and_chosen", other.AaChosen == 1 && other.TemporalAa == 7 && other.Hud == 90);

        // v613: the default AA follows the GPU (mirrors src/aa_first_start.hpp first_start_aa / default_aa_mode).
        check("default_aa_nvidia_rtx_dlss_is_dlaa", Prefs.DefaultAa(false, 2, dlss: true, fsr4: false) == 7 && Prefs.DefaultAa(true, 0, dlss: true, fsr4: true) == 7);
        check("default_aa_fsr4_gpu_is_fsr4_native", Prefs.DefaultAa(false, 2, dlss: false, fsr4: true) == 5 && Prefs.DefaultAa(true, 0, dlss: false, fsr4: true) == 5);
        check("default_aa_other_gpu_is_fsr3_native", Prefs.DefaultAa(false, 2, dlss: false, fsr4: false) == 2);
        check("default_aa_unchosen_old_file_keeps_mode_without_better_upscaler", Prefs.DefaultAa(true, 0, dlss: false, fsr4: false) == 0);
        check("default_aa_never_overrides_a_choice", Prefs.WithDefaultAa(new Prefs { TemporalAa = 0, AaChosen = 1 }, true, true, true).TemporalAa == 0
            && Prefs.WithDefaultAa(new Prefs(), false, true, false).TemporalAa == 7);
        var shown = Prefs.WithDefaultAa(new Prefs(), false, false, true);
        var savedDefault = Prefs.Merge(shown, shown, new Prefs());
        check("merge_saves_the_shown_gpu_default_when_nothing_chosen", savedDefault.TemporalAa == 5 && savedDefault.AaChosen == 1);
        check("aa_help_notes_performance_mode_only_when_on", Prefs.AaHelpFor("base", false) == "base" && Prefs.AaHelpFor("base", true).Contains("selected AA") && Prefs.AaHelpFor("base", true).EndsWith("base"));
        var caps = Tmp(); Directory.CreateDirectory(Path.GetDirectoryName(caps)!);
        File.WriteAllText(caps, "1 4098 0 1\n"); var capsYes = Prefs.Fsr4Supported(caps);
        File.WriteAllText(caps, "1 4318 1 0\n"); var capsNo = Prefs.Fsr4Supported(caps);
        File.WriteAllText(caps, "2 4098 0 1\n"); var capsFuture = Prefs.Fsr4Supported(caps);
        check("fsr4_caps_file_read_from_dll", capsYes && !capsNo && !capsFuture && !Prefs.Fsr4Supported(caps + ".missing"));

        var fx10p = Path.Combine(AppContext.BaseDirectory, "vr-settings-v10.cfg");
        var f2 = Tmp(); Prefs.Merge(fx10, Prefs.Load(fx10p), Prefs.Load(fx10p)).Save(f2);
        // Git may check the shared fixture out with CRLF; the writer always emits LF.
        check("merge_without_edits_equals_disk", File.ReadAllText(f2).Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)
            .SequenceEqual(File.ReadAllText(fx10p).Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)));
    }
}
