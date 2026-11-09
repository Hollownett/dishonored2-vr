using System.IO.Compression;

namespace DishonoredVR;

/// <summary>The Tools page's "Delete logs" (v1047, user: "don't waste any disk space on their device to collect VR logs"). The mod's
/// diagnostic output in artifacts\ is every top-level file except the few the launcher and the mod read (candidate manifests, GPU caps,
/// live status, command files, proxy log, probe launch and result files), plus the runs\ and ui-dump\ archives. Every other folder
/// (game-art, installed-backup, ngx) is kept whole.</summary>
public static class LogCleanup
{
    static readonly string[] Keep = { "repair-v", "aa-gpu-caps", "xr-live-status", "vr-command", "linux-proxy", "probe-launch-config",
        "loaded-render-probe", "dxgi-probe-result", "dxgi-probe-failure", "package-check" };
    static readonly string[] LogFolders = { "runs", "ui-dump" };

    public readonly record struct Item(string Path, long Bytes);

    public static List<Item> Plan(string artifacts)
    {
        var items = new List<Item>();
        if (!Directory.Exists(artifacts)) return items;
        foreach (var f in new DirectoryInfo(artifacts).EnumerateFiles())
            if (!Keep.Any(k => f.Name.StartsWith(k, StringComparison.OrdinalIgnoreCase))) items.Add(new(f.FullName, f.Length));
        foreach (var name in LogFolders)
        {
            var d = new DirectoryInfo(Path.Combine(artifacts, name));
            if (d.Exists) items.AddRange(d.EnumerateFiles("*", SearchOption.AllDirectories).Select(f => new Item(f.FullName, f.Length)));
        }
        return items;
    }

    /// <summary>Deletes the planned files (one in use is skipped and counted), then each log folder once no file is left in it.</summary>
    public static (int Deleted, long Bytes, int Failed) Run(string artifacts, IEnumerable<Item> items)
    {
        int deleted = 0, failed = 0; long bytes = 0;
        foreach (var i in items)
        {
            try { File.Delete(i.Path); deleted++; bytes += i.Bytes; }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { failed++; }
        }
        foreach (var name in LogFolders)
        {
            var d = Path.Combine(artifacts, name);
            try { if (Directory.Exists(d) && !Directory.EnumerateFiles(d, "*", SearchOption.AllDirectories).Any()) Directory.Delete(d, true); }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        }
        return (deleted, bytes, failed);
    }

    /// <summary>Zips artifacts\ for a bug report: everything except the imported game art and backups (large, and not the player's to share).
    /// Files the mod is writing are read shared; one that cannot be opened is skipped. Returns the file count.</summary>
    public static int Zip(string artifacts, string dest)
    {
        string[] skip = { "game-art", "installed-backup", "ngx" };
        int n = 0;
        using var zip = ZipFile.Open(dest, ZipArchiveMode.Create);
        foreach (var f in new DirectoryInfo(artifacts).EnumerateFiles("*", SearchOption.AllDirectories))
        {
            var rel = Path.GetRelativePath(artifacts, f.FullName);
            if (skip.Any(s => rel.StartsWith(s + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))) continue;
            try
            {
                using var src = new FileStream(f.FullName, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
                using var e = zip.CreateEntry(rel.Replace('\\', '/'), CompressionLevel.Optimal).Open();
                src.CopyTo(e); n++;
            }
            catch (Exception e) when (e is IOException or UnauthorizedAccessException) { }
        }
        return n;
    }

    public static string Size(long bytes) => bytes >= 1L << 30 ? $"{bytes / (double)(1L << 30):0.0} GB" : $"{bytes / (double)(1L << 20):0} MB";
}
