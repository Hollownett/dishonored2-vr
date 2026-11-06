using DishonoredVR;

public static class LogCleanupTests
{
    public static void Run(Action<string, bool> check)
    {
        var art = Path.Combine(Path.GetTempPath(), "d2vrlogs-" + Guid.NewGuid().ToString("N"), "artifacts");
        void Put(string rel, int bytes) { var p = Path.Combine(art, rel); Directory.CreateDirectory(Path.GetDirectoryName(p)!); File.WriteAllBytes(p, new byte[bytes]); }
        bool Kept(params string[] rel) => File.Exists(Path.Combine(new[] { art }.Concat(rel).ToArray()));
        foreach (var keep in new[] { "repair-v1047-test-candidate.json", "aa-gpu-caps.txt", "xr-live-status.json", "vr-command.txt", "vr-command-result.txt",
                     "linux-proxy.log", "probe-launch-config.json", "dxgi-probe-result.json", @"game-art\pistol.rgba", @"installed-backup\old.dll", @"ngx\cache.bin" }) Put(keep, 10);
        foreach (var log in new[] { "dual-wield-events.jsonl", "xr-final-0-left.rgba", "native-exception-context-fatal.dmp", "fsr-depth-left.r32",
                     @"runs\20261102-pid1\dxgi-probe-result.json", @"ui-dump\hud-0.rgba" }) Put(log, 1000);
        var plan = LogCleanup.Plan(art);
        check("log_cleanup_plans_only_logs", plan.Count == 6 && plan.Sum(i => i.Bytes) == 6000);
        var (deleted, bytes, failed) = LogCleanup.Run(art, plan);
        check("log_cleanup_deletes_logs_and_archives", deleted == 6 && bytes == 6000 && failed == 0 &&
            !Directory.Exists(Path.Combine(art, "runs")) && !Directory.Exists(Path.Combine(art, "ui-dump")));
        check("log_cleanup_keeps_launcher_and_mod_files", Kept("repair-v1047-test-candidate.json") && Kept("aa-gpu-caps.txt") && Kept("linux-proxy.log") &&
            Kept("game-art", "pistol.rgba") && Kept("installed-backup", "old.dll") && Kept("ngx", "cache.bin") && LogCleanup.Plan(art).Count == 0);
        check("log_cleanup_missing_folder_plans_nothing", LogCleanup.Plan(Path.Combine(art, "missing")).Count == 0);
        check("log_cleanup_size_text", LogCleanup.Size(3L << 30) == "3.0 GB" && LogCleanup.Size(5L << 20) == "5 MB");
        try { Directory.Delete(Path.GetDirectoryName(art)!, true); } catch (IOException) { }
    }
}
