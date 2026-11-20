using DishonoredVR;

/// <summary>launcher/FrameGen.cs: one frame generation at a time (SteamVR's per-app Motion Smoothing override, the manual list).</summary>
public static class FrameGenTests
{
    public static void Run(Action<string, bool> check)
    {
        var dir = Path.Combine(Path.GetTempPath(), "d2vrframegen-" + Guid.NewGuid().ToString("N")); Directory.CreateDirectory(dir);
        var settings = Path.Combine(dir, "steamvr.vrsettings"); var state = Path.Combine(dir, FrameGen.StateFileName);
        // the user's file shape (2026-11-20): a steam.app.403640 section without the override, other sections around it
        const string userFile = "{\n   \"steam.app.403640\" : {\n      \"resolutionScale\" : 100\n   },\n   \"steamvr\" : {\n      \"preferredRefreshRate\" : 72\n   }\n}\n";
        string Override() => FrameGen.ReadOverride(File.ReadAllText(settings));
        int Rate() => System.Text.Json.JsonDocument.Parse(File.ReadAllText(settings)).RootElement.GetProperty("steamvr").GetProperty("preferredRefreshRate").GetInt32();

        File.WriteAllText(settings, userFile);
        var on = FrameGen.SyncSteamVr(true, settings, state, steamVrRunning: false);
        check("framegen_steamvr_spacewarp_on_forces_smoothing_off", on != null && Override() == "2" && File.ReadAllText(state) == FrameGen.Absent && Rate() == 72
            && System.Text.Json.JsonDocument.Parse(File.ReadAllText(settings)).RootElement.GetProperty("steam.app.403640").GetProperty("resolutionScale").GetInt32() == 100);
        check("framegen_steamvr_backup_kept", File.Exists(settings + ".d2vr-backup") && File.ReadAllText(settings + ".d2vr-backup") == userFile);
        check("framegen_steamvr_second_on_is_quiet", FrameGen.SyncSteamVr(true, settings, state, false) == null && File.ReadAllText(state) == FrameGen.Absent);
        var off = FrameGen.SyncSteamVr(false, settings, state, false);
        check("framegen_steamvr_spacewarp_off_restores_absent", off != null && Override() == FrameGen.Absent && !File.Exists(state) && Rate() == 72);
        check("framegen_steamvr_off_without_record_is_quiet", FrameGen.SyncSteamVr(false, settings, state, false) == null);

        // a user value before (Always on = 3) comes back; SteamVR running: nothing is written
        File.WriteAllText(settings, FrameGen.WriteOverride(userFile, "3"));
        var running = FrameGen.SyncSteamVr(true, settings, state, steamVrRunning: true);
        check("framegen_steamvr_running_writes_nothing", running != null && running.Contains("SteamVR is running") && Override() == "3" && !File.Exists(state));
        FrameGen.SyncSteamVr(true, settings, state, false);
        check("framegen_steamvr_records_user_value", Override() == "2" && File.ReadAllText(state) == "3");
        FrameGen.SyncSteamVr(false, settings, state, false);
        check("framegen_steamvr_restores_user_value", Override() == "3" && !File.Exists(state));

        // the user changed the override while spacewarp was on: theirs, not restored over
        FrameGen.SyncSteamVr(true, settings, state, false);
        File.WriteAllText(settings, FrameGen.WriteOverride(File.ReadAllText(settings), "1"));
        check("framegen_steamvr_user_change_wins", FrameGen.SyncSteamVr(false, settings, state, false) == null && Override() == "1" && !File.Exists(state));

        // already off by the user: nothing recorded, so Spacewarp off leaves it off
        File.WriteAllText(settings, FrameGen.WriteOverride(userFile, "2"));
        check("framegen_steamvr_already_off_untouched", FrameGen.SyncSteamVr(true, settings, state, false) == null && !File.Exists(state)
            && FrameGen.SyncSteamVr(false, settings, state, false) == null && Override() == "2");
        // no section yet; no file at all
        File.WriteAllText(settings, "{ \"steamvr\" : { \"preferredRefreshRate\" : 90 } }");
        FrameGen.SyncSteamVr(true, settings, state, false);
        check("framegen_steamvr_creates_app_section", Override() == "2" && Rate() == 90);
        check("framegen_steamvr_no_file_is_quiet", FrameGen.SyncSteamVr(true, Path.Combine(dir, "missing.vrsettings"), state, false) == null);

        var none = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var vd = FrameGen.ManualConflicts(@"C:\Program Files\Virtual Desktop Streamer\OpenXR\virtualdesktop-openxr.json", none, false);
        check("framegen_manual_vd_ssw", vd.Count == 1 && vd[0].Contains("Synchronous Spacewarp"));
        var steamvrWithVd = FrameGen.ManualConflicts(@"D:\steam\steamapps\common\SteamVR\steamxr_win64.json", new HashSet<string>(new[] { "VirtualDesktop.Streamer" }, StringComparer.OrdinalIgnoreCase), null);
        check("framegen_manual_vd_streamer_under_steamvr", steamvrWithVd.Count == 1 && steamvrWithVd[0].Contains("Virtual Desktop"));
        check("framegen_manual_oculus_asw", FrameGen.ManualConflicts(@"C:\Program Files\Oculus\Support\oculus-runtime\oculus_openxr_64.json", none, null).Single().Contains("Asynchronous Spacewarp"));
        check("framegen_manual_wmr", FrameGen.ManualConflicts(@"C:\WINDOWS\system32\MixedRealityRuntime.json", none, null).Single().Contains("Motion reprojection"));
        check("framegen_manual_smooth_motion_and_lossless", FrameGen.ManualConflicts(null, new HashSet<string>(new[] { "LosslessScaling" }, StringComparer.OrdinalIgnoreCase), true).Count == 2);
        check("framegen_manual_steamvr_alone_is_empty", FrameGen.ManualConflicts(@"D:\steam\steamapps\common\SteamVR\steamxr_win64.json", none, false).Count == 0);

        // NVIDIA Smooth Motion: a read of the real driver profile where there is one (no driver: null); it must never throw
        bool threw = false; (bool? on, string detail) nv = (null, "");
        try { nv = FrameGen.NvidiaSmoothMotion("Dishonored2.exe"); } catch (Exception) { threw = true; }
        Console.WriteLine($"INFO nvidia smooth motion: {(nv.on?.ToString() ?? "unknown")} ({nv.detail})");
        check("framegen_nvidia_read_does_not_throw", !threw);
        try { Directory.Delete(dir, true); } catch (Exception) { }
    }
}
