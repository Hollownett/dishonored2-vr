using System.Diagnostics;
using System.Text.RegularExpressions;

namespace DishonoredVR;

/// <summary>Finds the user's OWN nvngx_dlss.dll in their installed Steam games (never bundled, copied or downloaded) and records its path
/// through DlssProvider.Configure. Pure and never throws from FindAndConfigure; linux/d2vr_linux/dlss.py does the same search for Proton.</summary>
public static class DlssFinder
{
    static readonly Regex PathLine = new("\"path\"\\s+\"((?:[^\"\\\\]|\\\\.)*)\"", RegexOptions.IgnoreCase | RegexOptions.Compiled);

    /// <summary>The Steam root plus every "path" in steamapps/libraryfolders.vdf (tolerant parse; missing or garbled gives just the root).</summary>
    public static IReadOnlyList<string> SteamLibraries(string steamRoot)
    {
        var list = new List<string> { steamRoot };
        try
        {
            var vdf = Path.Combine(steamRoot, "steamapps", "libraryfolders.vdf");
            if (File.Exists(vdf))
                foreach (Match m in PathLine.Matches(File.ReadAllText(vdf)))
                {
                    var p = Regex.Replace(m.Groups[1].Value, @"\\(.)", "$1");
                    if (p.Length > 0 && !list.Contains(p, StringComparer.OrdinalIgnoreCase)) list.Add(p);
                }
        }
        catch (Exception) { }
        return list;
    }

    /// <summary>steamapps/common/**/nvngx_dlss.dll, at most maxDepth folders below common, never through reparse points, unreadable folders skipped, at most maxFiles results.</summary>
    public static IReadOnlyList<string> Candidates(IEnumerable<string> libraries, int maxDepth = 5, int maxFiles = 50, CancellationToken ct = default)
    {
        var found = new List<string>();
        void Walk(string dir, int depth)
        {
            if (found.Count >= maxFiles || ct.IsCancellationRequested) return;
            try
            {
                var hit = Path.Combine(dir, DlssProvider.DllName);
                if (File.Exists(hit) && (File.GetAttributes(hit) & FileAttributes.ReparsePoint) == 0 && !found.Contains(hit, StringComparer.OrdinalIgnoreCase)) found.Add(hit);
                if (depth >= maxDepth) return;
                var subs = Directory.GetDirectories(dir); Array.Sort(subs, StringComparer.Ordinal);
                foreach (var sub in subs)
                {
                    if (found.Count >= maxFiles || ct.IsCancellationRequested) return;
                    try { if ((File.GetAttributes(sub) & FileAttributes.ReparsePoint) != 0) continue; } catch (Exception) { continue; }
                    Walk(sub, depth + 1);
                }
            }
            catch (Exception) { }  // unreadable folder: skip
        }
        foreach (var lib in libraries)
        {
            var common = Path.Combine(lib, "steamapps", "common");
            if (Directory.Exists(common)) Walk(common, 0);
        }
        return found;
    }

    static (int, int, int, int) Version(string path)
    {
        try { var v = FileVersionInfo.GetVersionInfo(path); return (v.FileMajorPart, v.FileMinorPart, v.FileBuildPart, v.FilePrivatePart); }
        catch (Exception) { return (0, 0, 0, 0); }
    }
    static DateTime Mtime(string path) { try { return File.GetLastWriteTimeUtc(path); } catch (Exception) { return DateTime.MinValue; } }

    /// <summary>Highest PE FileVersion when readable, else newest LastWriteTimeUtc; ties go to the shortest path.</summary>
    public static string? Best(IEnumerable<string> candidates) =>
        candidates.OrderByDescending(Version).ThenByDescending(Mtime).ThenBy(p => p.Length).ThenBy(p => p, StringComparer.Ordinal).FirstOrDefault();

    /// <summary>Windows: HKCU\Software\Valve\Steam\SteamPath, else %ProgramFiles(x86)%\Steam. Null when neither exists; never throws.</summary>
    public static string? SteamRoot()
    {
        try
        {
            string? p = null;
            if (OperatingSystem.IsWindows()) p = Microsoft.Win32.Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) as string;
            if (string.IsNullOrWhiteSpace(p) || !Directory.Exists(p))
            {
                var pf = Environment.GetEnvironmentVariable("ProgramFiles(x86)");
                p = string.IsNullOrEmpty(pf) ? null : Path.Combine(pf, "Steam");
            }
            return p != null && Directory.Exists(p) ? Path.GetFullPath(p) : null;
        }
        catch (Exception) { return null; }
    }

    static string GameName(string dll)
    {
        for (var d = Path.GetDirectoryName(dll); d != null; d = Path.GetDirectoryName(d))
        {
            var parent = Path.GetDirectoryName(d);
            if (parent != null && Path.GetFileName(parent).Equals("common", StringComparison.OrdinalIgnoreCase)) return Path.GetFileName(d);
        }
        return Path.GetFileName(Path.GetDirectoryName(dll)) ?? "a game";
    }

    /// <summary>Only when no provider resolves: Configure()s the best candidate from the user's Steam libraries and returns it as "auto: &lt;game&gt;"; otherwise null.</summary>
    public static DlssProvider.Location? FindAndConfigure(string root, string? steamRoot, CancellationToken ct = default)
    {
        try
        {
            if (steamRoot == null || DlssProvider.Resolve(root) != null) return null;
            var best = Best(Candidates(SteamLibraries(steamRoot), ct: ct));
            if (best == null || ct.IsCancellationRequested) return null;
            var loc = DlssProvider.Configure(root, best);
            return loc with { Source = "auto: " + GameName(best) };
        }
        catch (Exception) { return null; }
    }
}
