using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace DishonoredVR;

public sealed record Release(string Tag, string Name, string Notes, string Url, int Version, string ExeUrl, string ExeSha256);

/// <summary>Launcher self-update from the latest GitHub release: the release's DishonoredVRLauncher.exe, checked against the
/// sha256 its release.json lists, replaces the running exe (renamed to .old, deleted at the next start).</summary>
public static class Updates
{
    public const string Repo = "Hollownett/dishonored2-vr";
    public const string ExeName = "DishonoredVRLauncher.exe";
    public static readonly string LatestPage = $"https://github.com/{Repo}/releases/latest";
    static readonly HttpClient Http = new() { Timeout = TimeSpan.FromMinutes(10), DefaultRequestHeaders = { { "User-Agent", "DishonoredVRLauncher" } } };

    /// <summary>The mod version this exe carries (its embedded repair-vNNN manifest), 0 when none.</summary>
    public static int LocalVersion => Assembly.GetExecutingAssembly().GetManifestResourceNames()
        .Select(n => Regex.Match(n, @"^home/artifacts/repair-v(\d+)-test-candidate\.json$")).Where(m => m.Success)
        .Select(m => int.Parse(m.Groups[1].Value)).DefaultIfEmpty(0).Max();

    /// <summary>The release tag stamped at publish (dotnet publish -p:LauncherVersion=vNNNN.N), "dev" when unstamped.</summary>
    public static string LauncherVersion => Assembly.GetExecutingAssembly().GetCustomAttributes<AssemblyMetadataAttribute>()
        .FirstOrDefault(a => a.Key == "LauncherVersion")?.Value ?? "dev";

    public static string OwnSha256() => ModHome.Sha256(Environment.ProcessPath!);

    public static async Task<Release> Latest()
    {
        using var rel = JsonDocument.Parse(await Http.GetStringAsync($"https://api.github.com/repos/{Repo}/releases/latest"));
        var r = rel.RootElement;
        string Asset(string name) => r.GetProperty("assets").EnumerateArray().FirstOrDefault(a => a.GetProperty("name").GetString() == name) is { ValueKind: JsonValueKind.Object } a
            ? a.GetProperty("browser_download_url").GetString()! : throw new InvalidDataException($"The latest release has no {name}.");
        using var info = JsonDocument.Parse(await Http.GetStringAsync(Asset("release.json")));
        var i = info.RootElement;
        var tag = r.GetProperty("tag_name").GetString()!;
        return new Release(tag, r.GetProperty("name").GetString() ?? tag, PlainNotes(r.GetProperty("body").GetString() ?? ""),
            r.GetProperty("html_url").GetString()!, i.GetProperty("version").GetInt32(), Asset(ExeName),
            i.GetProperty("assets").GetProperty(ExeName).GetString()!.ToLowerInvariant());
    }

    /// <summary>Release markdown as readable plain text: links keep their text, emphasis and code marks go.</summary>
    public static string PlainNotes(string md)
    {
        md = Regex.Replace(md, @"\[([^\]]+)\]\([^)]+\)", "$1");
        md = Regex.Replace(md, @"(?m)^#+\s*(.*)$", m => m.Groups[1].Value.ToUpperInvariant());
        md = Regex.Replace(md, @"(?m)^(\s*)[-*]\s+", "$1•  ");
        return md.Replace("**", "").Replace("`", "").Replace("\r\n", "\n").Trim();
    }

    /// <summary>Downloads and verifies the release exe, then swaps it in for the running one. The caller restarts.</summary>
    public static async Task Install(Release r, IProgress<int> progress)
    {
        var exe = Environment.ProcessPath!; var tmp = exe + ".download"; var old = exe + ".old";
        using (var resp = await Http.GetAsync(r.ExeUrl, HttpCompletionOption.ResponseHeadersRead))
        {
            resp.EnsureSuccessStatusCode();
            long total = resp.Content.Headers.ContentLength ?? 0, done = 0;
            await using var src = await resp.Content.ReadAsStreamAsync();
            await using var dst = File.Create(tmp);
            var buf = new byte[1 << 16]; int n, last = -1;
            while ((n = await src.ReadAsync(buf)) > 0)
            {
                await dst.WriteAsync(buf.AsMemory(0, n)); done += n;
                int pct = total > 0 ? (int)(done * 100 / total) : 0;
                if (pct != last) progress.Report(last = pct);
            }
        }
        if (ModHome.Sha256(tmp) != r.ExeSha256) { File.Delete(tmp); throw new InvalidDataException("The download does not match the release's sha256; nothing was changed."); }
        try { File.Delete(old); } catch (IOException) { } catch (UnauthorizedAccessException) { }
        File.Move(exe, old, true);  // a running exe can be renamed, not overwritten
        try { File.Move(tmp, exe); }
        catch { File.Move(old, exe); throw; }
    }
}
