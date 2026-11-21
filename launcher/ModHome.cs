using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace DishonoredVR;

/// <summary>The per-user mod home the standalone launcher works in, laid out like the repo the DLL expects
/// (src/dxgi_probe.cpp StartProbe: root = the DLL folder's parent, which must contain .git):
/// build\&lt;dll&gt; + knob files + dxgi-probe.cfg + vr-settings.cfg, artifacts\ (outputs, runs\, game-art\, the candidate manifest),
/// third_party\fsr4\bin\ (optional FSR 4 provider). Everything comes from resources named "home/&lt;relative path&gt;".</summary>
public static class ModHome
{
    /// <summary>Bump to make every existing install take tools/default_features.json's defaults once (Features.RestoreDefaults) on its next start.
    /// "1": dual wield and intro skip on; only power gestures, performance mode and logging stay off.
    /// "2" (v1439, user 2026-11-20: spacewarp "should turn by default for all players"): spacewarp on (Auto).</summary>
    const string DefaultsRevision = "2";

    public static readonly string Dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DishonoredVR");

    /// <summary>The Death of the Outsider mod home (separate from <see cref="Dir"/>: settings, knobs and logs never collide).</summary>
    public static readonly string DotoDir = GameProfile.Doto.DefaultModHome;

    static bool IsManifest(string rel) => Regex.IsMatch(rel, @"^artifacts/repair-v\d+-test-candidate\.json$");
    static bool IsCandidateFile(string rel) => IsManifest(rel) || Regex.IsMatch(rel, @"^build/d2vr_render_probe_arm_v\d+\.dll(\.source\.json)?$");

    /// <summary>The embedded files for a game as (resource name, relative path in the mod home). Dishonored 2: every "home/" resource. Another game: the
    /// shared "home/" files (proxy, FSR/OpenXR loaders, licenses) without the D2 candidate, plus its own payload prefix.</summary>
    static List<(string Res, string Rel)> Payload(Assembly asm, GameProfile profile)
    {
        var all = asm.GetManifestResourceNames();
        var files = all.Where(n => n.StartsWith("home/")).Select(n => (Res: n, Rel: n["home/".Length..]));
        if (profile.IsD2) return files.ToList();
        return files.Where(f => !IsCandidateFile(f.Rel))
            .Concat(all.Where(n => n.StartsWith(profile.PayloadPrefix)).Select(n => (Res: n, Rel: n[profile.PayloadPrefix.Length..]))).ToList();
    }

    /// <summary>True when this launcher carries the profile's own mod (its candidate manifest).</summary>
    public static bool HasPayload(GameProfile profile) => Payload(Assembly.GetExecutingAssembly(), profile).Any(f => IsManifest(f.Rel));

    /// <summary>Writes every embedded file that is missing or different, then verifies the DLL against its manifest sha256.</summary>
    public static Candidate Install(string dir, Action<string>? log = null, GameProfile? profile = null)
    {
        var prof = profile ?? GameProfile.D2;
        var asm = Assembly.GetExecutingAssembly();
        var names = Payload(asm, prof);
        var manifestRel = names.SingleOrDefault(f => IsManifest(f.Rel)).Rel
            ?? throw new InvalidDataException(prof.NotIncludedMessage);
        Directory.CreateDirectory(Path.Combine(dir, ".git"));  // the DLL refuses to start without <root>\.git (StartProbe returns 3)
        foreach (var (n, rel) in names)
        {
            var path = Path.Combine(dir, rel.Replace('/', '\\'));
            using var s = asm.GetManifestResourceStream(n)!;
            var bytes = new byte[s.Length]; s.ReadExactly(bytes);
            if (File.Exists(path) && new FileInfo(path).Length == bytes.Length && Sha256(path) == Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant()) continue;
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            var tmp = path + ".tmp";
            File.WriteAllBytes(tmp, bytes);
            File.Move(tmp, path, true);  // fails if a different file of this name is loaded in the game; rebuilt DLLs always get a new name
            log?.Invoke("Installed " + rel);
        }
        var manifest = Path.Combine(dir, manifestRel.Replace('/', '\\'));
        using var doc = JsonDocument.Parse(File.ReadAllText(manifest));
        var r = doc.RootElement;
        var dll = r.GetProperty("dll_name").GetString() ?? "";
        if (!Regex.IsMatch(dll, @"^d2vr_render_probe_arm_v[0-9]+\.dll$")) throw new InvalidDataException("Invalid candidate filename.");
        var dllPath = Path.Combine(dir, "build", dll);
        if (!File.Exists(dllPath) || Sha256(dllPath) != r.GetProperty("sha256").GetString())
            throw new InvalidDataException($"{dll} in the mod home does not match its manifest sha256.");
        // Older mod versions left by earlier launchers; a version the running game still has loaded is locked and stays.
        foreach (var old in Directory.GetFiles(Path.Combine(dir, "build"), "d2vr_render_probe_arm_v*.dll"))
            if (!Path.GetFileName(old).Equals(dll, StringComparison.OrdinalIgnoreCase))
                try { File.Delete(old); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        // Default-on feature knobs (tools/default_features.json): seeded once, then the user's choices and hand-made knobs are kept.
        try
        {
            using var fs = asm.GetManifestResourceStream("features.json");
            if (fs != null)
            {
                var list = Features.Load(fs);
                var known = list.Any(f => f.RequiresDllKnob != null) ? Features.ScanKnobs(File.ReadAllBytes(dllPath)) : null;
                var build = Path.Combine(dir, "build");
                var mark = Path.Combine(build, "launcher-defaults-revision");
                if (File.Exists(mark) && File.ReadAllText(mark).Trim() == DefaultsRevision)
                    log?.Invoke($"Features: {Features.Apply(build, list, known)} seeded");
                else
                {   // a launcher with new defaults resets every feature once; the player's later switches are kept until the next bump
                    Features.RestoreDefaults(build, list, known, log);
                    File.WriteAllText(mark, DefaultsRevision);
                    log?.Invoke("Features: reset to this version's defaults");
                }
            }
        }
        catch (Exception ex)  // a bad list or an unreadable state must not fail an otherwise valid install
        { log?.Invoke("Features not applied: " + ex.Message); }
        return new Candidate(manifest, dll, dllPath);
    }

    /// <summary>The DOTO page's "Use my Dishonored 2 VR settings" option: a file in the DOTO home's build\ folder (present = on; absent = off, as a
    /// file written by the toggle). Default on until the player switches it off.</summary>
    public const string ShareSettingsFile = "launcher-share-d2-settings";

    public static bool ShareSettingsOn(string dotoRoot)
    {
        try { var f = Path.Combine(dotoRoot, "build", ShareSettingsFile); return !File.Exists(f) || File.ReadAllText(f).Trim() != "off"; }
        catch (IOException) { return true; }
    }

    public static void SetShareSettings(string dotoRoot, bool on)
    {
        var f = Path.Combine(dotoRoot, "build", ShareSettingsFile);
        Directory.CreateDirectory(Path.GetDirectoryName(f)!);
        File.WriteAllText(f, on ? "on\n" : "off\n");
    }

    /// <summary>Before a DOTO launch with the option on: copies the Dishonored 2 home's build\vr-settings.cfg over the DOTO home's, and the D2 home's
    /// artifacts\game-art\ files (menu art) that are missing or older in the DOTO home. Missing sources are skipped and logged; throws nothing for them.</summary>
    public static void ShareD2Settings(string d2Root, string dotoRoot, Action<string> log)
    {
        var settings = Path.Combine(d2Root, "build", "vr-settings.cfg");
        if (File.Exists(settings))
        {
            var to = Path.Combine(dotoRoot, "build", "vr-settings.cfg");
            Directory.CreateDirectory(Path.GetDirectoryName(to)!);
            File.Copy(settings, to, true);
            log("Copied your Dishonored 2 VR settings (build\\vr-settings.cfg) into the Death of the Outsider mod home.");
        }
        else log("No Dishonored 2 VR settings file yet (build\\vr-settings.cfg); Death of the Outsider starts with the defaults.");
        var art = Path.Combine(d2Root, "artifacts", "game-art");
        if (!Directory.Exists(art)) { log("No Dishonored 2 menu art to share (artifacts\\game-art not found)."); return; }
        int n = 0;
        foreach (var src in Directory.EnumerateFiles(art, "*", SearchOption.AllDirectories))
        {
            var to = Path.Combine(dotoRoot, "artifacts", "game-art", Path.GetRelativePath(art, src));
            if (File.Exists(to) && File.GetLastWriteTimeUtc(to) >= File.GetLastWriteTimeUtc(src)) continue;
            Directory.CreateDirectory(Path.GetDirectoryName(to)!);
            File.Copy(src, to, true); n++;
        }
        if (n > 0) log($"Copied {n} menu art files from the Dishonored 2 mod home.");
    }

    public static string Sha256(string file)
    {
        using var s = File.OpenRead(file);
        return Convert.ToHexString(SHA256.HashData(s)).ToLowerInvariant();
    }
}
