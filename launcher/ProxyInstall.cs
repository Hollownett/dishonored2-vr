using System.Security.Cryptography;
using System.Text.RegularExpressions;
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

    static readonly byte[] ExportBytes = Encoding.ASCII.GetBytes("DirectInput8Create");

    /// <summary>Identity of a dinput8.dll on disk. With the embedded proxy given: same size and SHA-256 is ours; otherwise (an older build of ours) it needs the
    /// marker and the one DirectInput8Create export name as a second check. A foreign DLL, or one that cannot be read, is not ours.</summary>
    public static bool IsOursFile(string path, byte[]? embedded = null)
    {
        try
        {
            if (!File.Exists(path)) return false;
            var bytes = File.ReadAllBytes(path);
            if (embedded != null && bytes.Length == embedded.Length && SHA256.HashData(bytes).AsSpan().SequenceEqual(SHA256.HashData(embedded))) return true;
            return IsOurs(bytes) && bytes.AsSpan().IndexOf(ExportBytes) >= 0;
        }
        catch (IOException) { return false; }
        catch (UnauthorizedAccessException) { return false; }
    }

    public static bool Installed(string gameDir) => IsOursFile(Path.Combine(gameDir, DllName));

    /// <summary>True when one of the game's loaded modules is the dinput8.dll we installed in gameDir (same path, our identity). A foreign dinput8.dll,
    /// even one in the same place, is not ours.</summary>
    public static bool OurProxyLoaded(string gameDir, IEnumerable<string> loadedModulePaths)
    {
        var want = Path.GetFullPath(Path.Combine(gameDir, DllName));
        return loadedModulePaths.Any(m => string.Equals(Path.GetFullPath(m), want, StringComparison.OrdinalIgnoreCase)) && IsOursFile(want);
    }

    /// <summary>Checks everything, then writes the ini and the proxy (ini first, so a partial state is still meaningful). Returns the files that changed.
    /// Throws InvalidOperationException with the reason (nothing is written in that case); an IOException from a locked file (game running) propagates.</summary>
    public static IReadOnlyList<string> Install(string gameDir, string modHome, byte[] proxy)
    {
        if (!Directory.Exists(gameDir)) throw new InvalidOperationException($"game folder {gameDir} does not exist");
        if (!IsOurs(proxy)) throw new InvalidOperationException("the embedded dinput8.dll is not the d2vr proxy (no identity marker)");
        var target = Path.Combine(gameDir, DllName);
        if (File.Exists(target) && !IsOursFile(target, proxy))
            throw new InvalidOperationException($"{target} is another mod's dinput8.dll; refusing to replace it. Move it away to use this launcher's fast start");
        var changed = new List<string>();
        if (Write(Path.Combine(gameDir, IniName), Encoding.UTF8.GetBytes(modHome.TrimEnd('\\') + "\r\n"))) changed.Add(IniName);  // UTF-8, no BOM: the proxy decodes it as UTF-8
        if (Write(target, proxy)) changed.Add(DllName);
        changed.Sort((a, b) => string.CompareOrdinal(b, a));  // dinput8.dll first, as reported
        return changed;
    }

    /// <summary>Removes our proxy, the ini and our leftover .tmp files; a foreign dinput8.dll is left alone (modhome.uninstall).</summary>
    public static IReadOnlyList<string> Uninstall(string gameDir)
    {
        var removed = new List<string>();
        var dll = Path.Combine(gameDir, DllName);
        if (IsOursFile(dll)) { File.Delete(dll); removed.Add(DllName); }
        var ini = Path.Combine(gameDir, IniName);
        if (File.Exists(ini)) { File.Delete(ini); removed.Add(IniName); }
        var dllTmp = dll + ".tmp";
        if (IsOursFile(dllTmp)) { File.Delete(dllTmp); removed.Add(DllName + ".tmp"); }
        var iniTmp = ini + ".tmp";
        if (File.Exists(iniTmp)) { File.Delete(iniTmp); removed.Add(IniName + ".tmp"); }
        return removed;
    }

    public enum StartPath { None, Proxy, Injected }

    /// <summary>Stop VR goes through linux-stop only when our proxy runs the session and the launcher did not inject it itself.</summary>
    public static bool StopViaProxy(bool proxyLoaded, StartPath path) => proxyLoaded && path != StartPath.Injected;

    /// <summary>Later proxy sessions require a fresh linux-start file. Validate the
    /// installed home before requesting one; never queue a restart behind an active session.</summary>
    public static bool RequestStart(string gameDir, string modHome, string logText)
    {
        if (!Installed(gameDir)) throw new InvalidOperationException("The game's dinput8.dll is not our proxy.");
        var configured = File.ReadAllText(Path.Combine(gameDir, IniName)).Trim();
        static string Full(string p) => Path.GetFullPath(p).TrimEnd('\\', '/', Path.DirectorySeparatorChar);
        if (!string.Equals(Full(configured), Full(modHome), StringComparison.OrdinalIgnoreCase))
            throw new InvalidOperationException("The running proxy is configured for another mod home. Restart the game from this launcher to apply its settings.");
        if (ReadStart(logText, out _, out _) == StartState.Started) return false;
        var build = Path.Combine(modHome, "build");
        if (File.Exists(Path.Combine(build, "linux-stop")))
            throw new InvalidOperationException("VR is still stopping. Wait for restoration, then launch again.");
        Directory.CreateDirectory(build);
        Write(Path.Combine(build, "linux-start"), Encoding.ASCII.GetBytes(Guid.NewGuid().ToString("N") + "\n"));
        return true;
    }

    public enum StartState { Pending, Started, Failed, Ended }

    /// <summary>Reads the proxy log of the current game start. "StartProbe begin" means the session is up; "StartProbe returned N" comes when
    /// StartProbe returns: 0 after the session ended, anything else a failed start (code is then set, reason says what it means).</summary>
    public static StartState ReadStart(string logText, out int code, out string reason)
    {
        code = 0; reason = "";
        // The proxy appends every session to one log. Only the last lifecycle
        // event describes whether a new start is safe to request.
        var m = Regex.Matches(logText, @"StartProbe (?:begin\b|returned (\d+)\b)").Cast<Match>().LastOrDefault();
        if (m == null) return StartState.Pending;
        if (m.Groups[1].Success)
        {
            code = int.Parse(m.Groups[1].Value);
            if (code == 0) return StartState.Ended;
            reason = code switch
            {
                1 => "the probe failed (see artifacts\\dxgi-probe-failure.txt)",
                2 => "a previous VR session is still shutting down or restoring the game; wait a few seconds and launch again",
                3 => "the mod home is not installed (its .git folder is missing)",
                _ => "unknown start failure",
            };
            return StartState.Failed;
        }
        return StartState.Started;
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
