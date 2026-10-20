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
    public static readonly string Dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DishonoredVR");

    /// <summary>Writes every embedded file that is missing or different, then verifies the DLL against its manifest sha256.</summary>
    public static Candidate Install(string dir, Action<string>? log = null)
    {
        var asm = Assembly.GetExecutingAssembly();
        var names = asm.GetManifestResourceNames().Where(n => n.StartsWith("home/")).ToArray();
        var manifestName = names.SingleOrDefault(n => Regex.IsMatch(n, @"^home/artifacts/repair-v\d+-test-candidate\.json$"))
            ?? throw new InvalidDataException("This launcher build carries no mod (no embedded candidate manifest).");
        Directory.CreateDirectory(Path.Combine(dir, ".git"));  // the DLL refuses to start without <root>\.git (StartProbe returns 3)
        foreach (var n in names)
        {
            var path = Path.Combine(dir, n["home/".Length..].Replace('/', '\\'));
            using var s = asm.GetManifestResourceStream(n)!;
            var bytes = new byte[s.Length]; s.ReadExactly(bytes);
            if (File.Exists(path) && new FileInfo(path).Length == bytes.Length && Sha256(path) == Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant()) continue;
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            var tmp = path + ".tmp";
            File.WriteAllBytes(tmp, bytes);
            File.Move(tmp, path, true);  // fails if a different file of this name is loaded in the game; rebuilt DLLs always get a new name
            log?.Invoke("Installed " + n["home/".Length..]);
        }
        var manifest = Path.Combine(dir, manifestName["home/".Length..].Replace('/', '\\'));
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
                log?.Invoke($"Features: {Features.Apply(Path.Combine(dir, "build"), list, known)} seeded");
            }
        }
        catch (Exception ex)  // a bad list or an unreadable state must not fail an otherwise valid install
        { log?.Invoke("Features not applied: " + ex.Message); }
        return new Candidate(manifest, dll, dllPath);
    }

    public static string Sha256(string file)
    {
        using var s = File.OpenRead(file);
        return Convert.ToHexString(SHA256.HashData(s)).ToLowerInvariant();
    }
}
