using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Microsoft.Win32;

namespace DishonoredVR;

/// <summary>Saved settings, byte-compatible with src/vr_preferences.hpp (version 7). Measurement tokens are preserved.</summary>
public sealed class Prefs
{
    public double Resolution = 1, Sharpness = .5, SnapDeg = 45, SmoothDps = 90;
    public int Aa = 1, Mode = 1, Mirror = 2, TemporalAa = 2, Hud = 65, Hint = 1;
    public string[] Measurements = { "0", "0", "0", "0", "0" };

    public static Prefs Load(string file)
    {
        var p = new Prefs();
        if (!File.Exists(file)) return p;
        var t = File.ReadAllText(file).Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        var inv = CultureInfo.InvariantCulture;
        if (t.Length < 6 || !int.TryParse(t[0], out var v) || v < 1 || v > 7)
            throw new InvalidDataException("Saved settings file is not a recognised version (1-7). It was left untouched.");
        double D(int i) => double.Parse(t[i], inv);
        int I(int i) => int.Parse(t[i], inv);
        p.Resolution = D(1); p.Aa = I(2); p.Mode = I(3); p.SnapDeg = D(4); p.SmoothDps = D(5);
        if (v >= 2) { p.Mirror = I(6); p.Sharpness = D(7); }
        p.TemporalAa = v >= 5 && t.Length > 8 ? I(8) : 2;
        if (v >= 4 && t.Length > 10) { p.Hud = v >= 6 ? I(9) : 65; p.Hint = I(10); }
        if (v >= 7 && t.Length >= 16) p.Measurements = t[11..16];
        return p;
    }

    public string Validate()
    {
        if (Resolution < .5 || Resolution > 2) return "Resolution must be 0.5 to 2.0.";
        if (Mirror is < 0 or > 2) return "Monitor view must be right, left or both eyes.";
        if (Sharpness < 0 || Sharpness > 1) return "Sharpening must be 0 to 1.";
        if (Hud < 50 || Hud > 100) return "HUD size must be 50 to 100 percent.";
        if (TemporalAa is < 0 or > 6) return "Anti-aliasing mode must be 0 to 6.";
        if (Mode is < 0 or > 2) return "Turning mode must be native, snap or smooth.";
        if (SmoothDps < 15 || SmoothDps > 360) return "Smooth turn speed must be 15 to 360 degrees/s.";
        return "";
    }

    public void Save(string file)
    {
        var e = Validate(); if (e != "") throw new InvalidDataException(e);
        var inv = CultureInfo.InvariantCulture;
        string F(double d) => d.ToString("R", inv);
        var head = new[] { "7", F(Resolution), Aa.ToString(), Mode.ToString(), F(SnapDeg), F(SmoothDps), Mirror.ToString(), F(Sharpness),
            TemporalAa.ToString(), Hud.ToString(), Hint.ToString() };
        var line = string.Join(' ', head.Concat(Measurements)) + "\n";
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        var tmp = file + ".tmp";
        File.WriteAllText(tmp, line, new UTF8Encoding(false));
        File.Move(tmp, file, true);
    }
}

public sealed record Check(string Name, bool Ok, string Detail, string Fix);
public sealed record Candidate(string Manifest, string Dll, string DllPath);

public sealed class Env
{
    public string Root;
    /// <summary>Standalone: Root is the per-user mod home (ModHome) and the mod is the one embedded in the exe (Pinned).</summary>
    public bool Standalone;
    public Candidate? Pinned; public string InstallError = "not installed yet";
    public Env(string root) { Root = root; }
    public string P(params string[] s) => Path.Combine(new[] { Root }.Concat(s).ToArray());
    public string Art => P("artifacts");
    public string SettingsFile => P("build", "vr-settings.cfg");

    /// <summary>Developer mode: the repo passed as the first argument, or the repo the exe was built in (exe under &lt;repo&gt;\launcher\).
    /// A copy of the exe anywhere else runs standalone.</summary>
    public static string? FindRoot(string[] args)
    {
        static bool Repo(string d) => File.Exists(Path.Combine(d, "tools", "run_demo.ps1"));
        if (args.Length > 0 && Repo(args[0])) return Path.GetFullPath(args[0]);
        for (var d = new DirectoryInfo(AppContext.BaseDirectory); d?.Parent != null; d = d.Parent)
            if (d.Name.Equals("launcher", StringComparison.OrdinalIgnoreCase) && Repo(d.Parent.FullName)) return d.Parent.FullName;
        return null;
    }

    /// <summary>Highest-numbered artifacts/repair-vNNN-test-candidate.json that is not launch_blocked and whose DLL exists.</summary>
    public Candidate? LatestCandidate(out string why)
    {
        if (Standalone) { why = InstallError; return Pinned; }
        why = "no artifacts\\repair-vNNN-test-candidate.json found";
        if (!Directory.Exists(Art)) return null;
        var files = Directory.GetFiles(Art, "repair-v*-test-candidate.json")
            .Select(f => (f, n: Regex.Match(Path.GetFileName(f), @"repair-v(\d+)-").Groups[1].Value))
            .Where(x => x.n != "").OrderByDescending(x => int.Parse(x.n));
        foreach (var (f, n) in files)
        {
            try
            {
                using var doc = JsonDocument.Parse(File.ReadAllText(f));
                var r = doc.RootElement;
                if (r.TryGetProperty("launch_blocked", out var lb) && lb.ValueKind == JsonValueKind.True) { why = $"v{n} is launch-blocked"; continue; }
                var dll = r.GetProperty("dll_name").GetString()!;
                var path = P("build", dll);
                if (!File.Exists(path)) { why = $"{dll} missing from build\\"; continue; }
                return new Candidate(f, dll, path);
            }
            catch (Exception ex) { why = $"{Path.GetFileName(f)}: {ex.Message}"; }
        }
        return null;
    }

    public static string? FindGameDir()
    {
        var steams = new List<string>();
        if (Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam")?.GetValue("SteamPath") is string s1) steams.Add(s1.Replace('/', '\\'));
        if (Registry.LocalMachine.OpenSubKey(@"SOFTWARE\WOW6432Node\Valve\Steam")?.GetValue("InstallPath") is string s2) steams.Add(s2);
        steams.Add(@"C:\Program Files (x86)\Steam");
        foreach (var steam in steams.Distinct())
        {
            var libs = new List<string> { steam };
            var vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
            if (File.Exists(vdf))
                foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\"path\"\\s+\"([^\"]+)\"")) libs.Add(m.Groups[1].Value.Replace(@"\\", @"\"));
            foreach (var l in libs)
            {
                var d = Path.Combine(l, "steamapps", "common", "Dishonored2");
                if (File.Exists(Path.Combine(d, "Dishonored2.exe"))) return d;
            }
        }
        return null;
    }

    public static string? OpenXrRuntime()
    {
        using var k = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Khronos\OpenXR\1");
        return k?.GetValue("ActiveRuntime") as string;
    }

    /// <summary>The mod loads the bundled Khronos OpenXR loader (third_party\openxr\bin under the mod root), which finds any active
    /// runtime (Virtual Desktop, Oculus, SteamVR); SteamVR's copy is only a fallback (src/dxgi_probe.cpp openxr_loader_path).</summary>
    public const string SteamVrLoader = @"C:\Program Files (x86)\Steam\steamapps\common\SteamVR\bin\win64\openxr_loader.dll";
    public string BundledLoader => Path.Combine(Root, "third_party", "openxr", "bin", "openxr_loader.dll");
    /// <summary>The mod DLL imports the Visual C++ 2015-2022 x64 runtime.</summary>
    public static bool VcRuntime() => new[] { "msvcp140.dll", "msvcp140_2.dll", "vcruntime140.dll", "vcruntime140_1.dll" }
        .All(f => File.Exists(Path.Combine(Environment.SystemDirectory, f)));

    /// <summary>artState: null when the art is present, otherwise what to show (importing progress or why it is missing).</summary>
    public List<Check> RunChecks(string? artState)
    {
        var l = new List<Check>();
        var gd = FindGameDir();
        l.Add(new("Dishonored 2", gd != null, gd ?? "not found in any Steam library",
            "Install Dishonored 2 through Steam (app 403640). If it is installed, start Steam once so libraryfolders.vdf is written."));
        var rt = OpenXrRuntime();
        var vd = rt != null && (rt.Contains("vdxr", StringComparison.OrdinalIgnoreCase) || rt.Contains("VirtualDesktop", StringComparison.OrdinalIgnoreCase));
        l.Add(new("OpenXR runtime", rt != null, rt == null ? "no active runtime" : (vd ? "Virtual Desktop (VDXR)" : rt),
            rt == null ? "Set an active OpenXR runtime (Virtual Desktop Streamer > Settings > OpenXR, or the Oculus / SteamVR app)."
                       : (vd ? "" : "Usable, but Virtual Desktop's VDXR is the recommended runtime for this mod.")));
        var bundledLoader = File.Exists(BundledLoader); var steamLoader = File.Exists(SteamVrLoader);
        l.Add(new("OpenXR loader", bundledLoader || steamLoader, bundledLoader ? "bundled Khronos loader" : steamLoader ? "SteamVR's loader" : "missing",
            Standalone ? "The launcher installs the Khronos OpenXR loader into " + Root + "; check that the folder is writable, then Re-check."
                       : "Copy the verified loader to third_party\\openxr\\bin (see third_party\\openxr\\bin\\source.json), or install SteamVR."));
        var c = LatestCandidate(out var why);
        l.Add(new("Mod", c != null, c != null ? (Standalone ? $"{c.Dll} (verified)" : c.Dll) : why, Standalone
            ? "The launcher could not write the mod to " + Root + ". Check that the folder is writable and not locked, then Re-check."
            : "Build the mod (tools\\build.ps1) so artifacts\\repair-vNNN-test-candidate.json and build\\<dll> exist, and make sure the newest manifest is not launch_blocked."));
        var vc = VcRuntime();
        l.Add(new("VC++ runtime", vc, vc ? "Visual C++ 2015-2022 x64" : "missing", "Install the Microsoft Visual C++ 2015-2022 Redistributable (x64) from Microsoft."));
        l.Add(new("Game art", true, artState ?? "imported from your install", artState == null ? "" : "Usable without it: the launcher and VR menus use plain styling until the import finishes."));
        return l;
    }

    public bool Ready(Candidate? c) => c != null && FindGameDir() != null && (File.Exists(BundledLoader) || File.Exists(SteamVrLoader)) && VcRuntime();

    public static Process? Game() => Process.GetProcessesByName("Dishonored2").FirstOrDefault();

    /// <summary>Live status from artifacts/xr-live-status.json (written about once a second by the injected DLL).</summary>
    public (bool fresh, bool gameplay, long logic) LiveStatus()
    {
        var f = P("artifacts", "xr-live-status.json");
        try
        {
            if (!File.Exists(f) || (DateTime.UtcNow - File.GetLastWriteTimeUtc(f)).TotalSeconds > 6) return (false, false, 0);
            using var s = new FileStream(f, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            using var d = JsonDocument.Parse(s);
            var r = d.RootElement;
            bool gp = r.TryGetProperty("gameplay", out var g) && (g.ValueKind == JsonValueKind.True || (g.ValueKind == JsonValueKind.Number && g.GetDouble() != 0));
            long lm = r.TryGetProperty("logic_mask", out var m) && m.ValueKind == JsonValueKind.Number ? m.GetInt64() : 0;
            return (true, gp, lm);
        }
        catch { return (false, false, 0); }
    }
}

public static class Proc
{
    /// <summary>Runs a hidden child process (no window, no focus change), streaming stdout+stderr lines.</summary>
    public static async Task<int> Run(string file, string args, string? cwd, Action<string> line, Action<Process>? started = null)
    {
        var psi = new ProcessStartInfo(file, args)
        {
            RedirectStandardOutput = true, RedirectStandardError = true, RedirectStandardInput = true, UseShellExecute = false, CreateNoWindow = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8,
        };
        if (cwd != null) psi.WorkingDirectory = cwd;
        psi.Environment["PYTHONUNBUFFERED"] = "1"; psi.Environment["PYTHONIOENCODING"] = "utf-8";
        using var p = new Process { StartInfo = psi };
        p.OutputDataReceived += (_, e) => { if (e.Data != null) line(e.Data); };
        p.ErrorDataReceived += (_, e) => { if (e.Data != null) line(e.Data); };
        p.Start(); p.StandardInput.Close();
        started?.Invoke(p);
        p.BeginOutputReadLine(); p.BeginErrorReadLine();
        await p.WaitForExitAsync();
        return p.ExitCode;
    }
}
