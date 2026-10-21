using System.Text;

namespace DishonoredVR;

/// <summary>Installs and removes the dinput8 proxy (linux/proxy/dinput8_proxy.cpp, built for Windows as build\dinput8.dll) beside
/// Dishonored2.exe. Same rules as linux/d2vr_linux/modhome.py: only our own proxy (it contains <see cref="Marker"/>) is ever written over or
/// removed, a foreign dinput8.dll is refused and left alone, and d2vr-linux.ini carries the mod home. Pure file logic, no Windows types.</summary>
public static class ProxyInstall
{
    public const string Marker = "D2VR-LINUX-PROXY-1";   // modhome.PROXY_MARKER
    public const string DllName = "dinput8.dll";
    public const string IniName = "d2vr-linux.ini";      // proxy_core.hpp kIniName
    /// <summary>The proxy starts VR this long after the game window has a client area (proxy_core.hpp kDefaultSettleMs; docs/VR-START-INVESTIGATION.md).</summary>
    public const int DefaultStartSeconds = 20;
    /// <summary>The old wait of the injection path, kept as the fallback when the proxy cannot be installed (and proxy_core.hpp kLegacySettleSeconds).</summary>
    public const int FallbackDelaySeconds = 45;

    static readonly byte[] MarkerBytes = Encoding.ASCII.GetBytes(Marker);

    public static bool IsOurs(ReadOnlySpan<byte> image) => image.IndexOf(MarkerBytes) >= 0;

    public static bool IsOursFile(string path)
    {
        try { return File.Exists(path) && IsOurs(File.ReadAllBytes(path)); }
        catch (IOException) { return false; }
        catch (UnauthorizedAccessException) { return false; }
    }

    public static bool Installed(string gameDir) => IsOursFile(Path.Combine(gameDir, DllName));

    /// <summary>Checks everything, then writes the proxy and the ini. Returns the files that changed. Throws InvalidOperationException with the reason
    /// (nothing is written in that case); an IOException from a locked file (game running) propagates.</summary>
    public static IReadOnlyList<string> Install(string gameDir, string modHome, byte[] proxy)
    {
        if (!Directory.Exists(gameDir)) throw new InvalidOperationException($"game folder {gameDir} does not exist");
        if (!IsOurs(proxy)) throw new InvalidOperationException("the embedded dinput8.dll is not the d2vr proxy (no identity marker)");
        var target = Path.Combine(gameDir, DllName);
        if (File.Exists(target) && !IsOursFile(target))
            throw new InvalidOperationException($"{target} is another mod's dinput8.dll; refusing to replace it. Move it away to use this launcher's fast start");
        var changed = new List<string>();
        if (Write(target, proxy)) changed.Add(DllName);
        if (Write(Path.Combine(gameDir, IniName), Encoding.UTF8.GetBytes(modHome.TrimEnd('\\') + "\r\n"))) changed.Add(IniName);  // UTF-8, no BOM: the proxy decodes it as UTF-8
        return changed;
    }

    /// <summary>Removes our proxy and the ini; a foreign dinput8.dll is left alone (modhome.uninstall).</summary>
    public static IReadOnlyList<string> Uninstall(string gameDir)
    {
        var removed = new List<string>();
        var dll = Path.Combine(gameDir, DllName);
        if (IsOursFile(dll)) { File.Delete(dll); removed.Add(DllName); }
        var ini = Path.Combine(gameDir, IniName);
        if (File.Exists(ini)) { File.Delete(ini); removed.Add(IniName); }
        return removed;
    }

    static bool Write(string path, byte[] data)
    {
        if (File.Exists(path) && new FileInfo(path).Length == data.Length && File.ReadAllBytes(path).AsSpan().SequenceEqual(data)) return false;
        var tmp = path + ".tmp";
        File.WriteAllBytes(tmp, data);
        try { File.Move(tmp, path, true); }
        catch { try { File.Delete(tmp); } catch (IOException) { } throw; }
        return true;
    }
}
