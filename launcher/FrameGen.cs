using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Text.Json.Nodes;
using Microsoft.Win32;

namespace DishonoredVR;

/// <summary>One frame generation at a time (user 2026-11-20: "when spacewarp is enabled in launcher then other framegens are ignored like virtual
/// desktop framegen and steam smooth motion and so on. Only one framegen is active").
/// With Spacewarp on, the mod's own performance mode does not start (src/dxgi_probe.cpp, the session renderer). Outside the mod:
/// - SteamVR Motion Smoothing: switched off for Dishonored 2 only (the per-app override in steamvr.vrsettings), and put back when Spacewarp is
///   turned off. SteamVR rewrites that file while it runs, so the change is made only while SteamVR is closed.
/// - NVIDIA Smooth Motion: read from the game's driver profile (NVAPI DRS). Changing a driver profile needs administrator rights, so the
///   launcher only reports it.
/// - Virtual Desktop SSW, Meta ASW, WMR motion reprojection, Pimax Smart Smoothing, Lossless Scaling: no PC-side switch for one app exists
///   (research 2026-11-20: the open-source VDXR reads no SSW setting; SSW runs on the headset; ASW is the Oculus Debug Tool's), so the
///   launcher says what to switch off.</summary>
public static class FrameGen
{
    public const string SteamVrAppSection = "steam.app.403640";  // SteamVR already keys this game by its Steam app id (the user's file, 2026-11-20)
    public const string SmoothingKey = "motionSmoothingOverride";
    /// <summary>SteamVR's per-application Motion Smoothing choice, as its own dashboard defines it (resources/webinterface/dashboard:
    /// UseGlobal=0, ForceOn=1, ForceOff=2, ForceAlwaysOn=3).</summary>
    public const int SmoothingForceOff = 2;
    public const string Absent = "absent";

    static readonly JsonDocumentOptions ReadOptions = new() { AllowTrailingCommas = true, CommentHandling = JsonCommentHandling.Skip };
    static readonly JsonSerializerOptions WriteOptions = new() { WriteIndented = true };

    /// <summary>The app's Motion Smoothing override in a steamvr.vrsettings text: its raw JSON value, or <see cref="Absent"/>.</summary>
    public static string ReadOverride(string json)
    {
        var root = JsonNode.Parse(json, null, ReadOptions) as JsonObject ?? throw new InvalidDataException("steamvr.vrsettings is not a JSON object");
        return root[SteamVrAppSection] is JsonObject app && app[SmoothingKey] is JsonNode v ? v.ToJsonString() : Absent;
    }

    /// <summary>The text with the app's override set to a raw JSON value, or removed for <see cref="Absent"/>. Every other key is kept.</summary>
    public static string WriteOverride(string json, string raw)
    {
        var root = JsonNode.Parse(json, null, ReadOptions) as JsonObject ?? throw new InvalidDataException("steamvr.vrsettings is not a JSON object");
        if (raw == Absent) { if (root[SteamVrAppSection] is JsonObject old) old.Remove(SmoothingKey); }
        else
        {
            if (root[SteamVrAppSection] is not JsonObject app) root[SteamVrAppSection] = app = new JsonObject();
            app[SmoothingKey] = JsonNode.Parse(raw);
        }
        return root.ToJsonString(WriteOptions);
    }

#pragma warning disable CA1416  // the launcher is Windows-only; its test project builds this file for net8.0
    public static string? SteamVrSettingsPath()
    {
        try
        {
            using var k = Registry.CurrentUser.OpenSubKey(@"Software\Valve\Steam");
            if (k?.GetValue("SteamPath") is string steam && steam != "") return Path.Combine(steam.Replace('/', '\\'), "config", "steamvr.vrsettings");
        }
        catch (Exception) { }
        return null;
    }
#pragma warning restore CA1416

    public static bool SteamVrRunning() => new[] { "vrserver", "vrmonitor", "vrcompositor" }.Any(n => Process.GetProcessesByName(n).Length > 0);

    /// <summary>Spacewarp on: the override is Off for this game (the value before is kept in <paramref name="stateFile"/>, once). Spacewarp off:
    /// the value before is put back, unless the user changed the override since. Returns a line for the log, or null when nothing changed.</summary>
    public static string? SyncSteamVr(bool spacewarpOn, string? settingsPath, string stateFile, bool steamVrRunning)
    {
        if (settingsPath == null || !File.Exists(settingsPath)) return null;  // SteamVR never ran here: nothing can smooth
        var recorded = File.Exists(stateFile);
        if (!spacewarpOn && !recorded) return null;
        var json = File.ReadAllText(settingsPath);
        var current = ReadOverride(json);
        var off = SmoothingForceOff.ToString();
        if (spacewarpOn)
        {
            if (current == off) return null;  // already off (ours, or the user's own choice)
            if (steamVrRunning) return "SteamVR is running, so its Motion Smoothing for Dishonored 2 is switched off at the next launch with SteamVR closed. " +
                "Or set it Off now: SteamVR > Settings > Video > Per-application video settings > Dishonored 2.";
            Replace(settingsPath, WriteOverride(json, off));
            if (!recorded) File.WriteAllText(stateFile, current);
            return "SteamVR Motion Smoothing switched off for Dishonored 2: spacewarp generates the in-between frames (put back when Spacewarp is Off).";
        }
        if (current != off) { File.Delete(stateFile); return null; }  // the user changed it since: theirs now
        if (steamVrRunning) return "SteamVR is running, so its Motion Smoothing for Dishonored 2 is put back at the next launch with SteamVR closed.";
        Replace(settingsPath, WriteOverride(json, File.ReadAllText(stateFile).Trim()));
        File.Delete(stateFile);
        return "SteamVR Motion Smoothing for Dishonored 2 put back as it was (Spacewarp is Off).";
    }

    static void Replace(string path, string text)
    {
        var tmp = path + ".d2vr-tmp";
        File.WriteAllText(tmp, text);
        File.Replace(tmp, path, path + ".d2vr-backup", ignoreMetadataErrors: true);  // the previous file stays as steamvr.vrsettings.d2vr-backup
    }

    /// <summary>What the user must switch off by hand while Spacewarp is on. <paramref name="runtime"/>: the active OpenXR runtime's manifest
    /// path; <paramref name="running"/>: process names; <paramref name="smoothMotion"/>: NVIDIA Smooth Motion for the game (null = unknown).</summary>
    public static List<string> ManualConflicts(string? runtime, ISet<string> running, bool? smoothMotion)
    {
        var l = new List<string>();
        var rt = runtime ?? "";
        bool Has(string s) => rt.Contains(s, StringComparison.OrdinalIgnoreCase);
        if (Has("vdxr") || Has("VirtualDesktop") || Has("Virtual Desktop") || running.Contains("VirtualDesktop.Streamer"))
            l.Add("Virtual Desktop: set Synchronous Spacewarp to Disabled in the headset app (Settings > Streaming). The PC cannot switch it.");
        if (Has("oculus"))
            l.Add("Meta Link: set Asynchronous Spacewarp to Disabled in the Oculus Debug Tool (it resets when the Oculus app restarts).");
        if (Has("MixedReality"))
            l.Add("Windows Mixed Reality: set Motion reprojection to Off in OpenXR Tools for Windows Mixed Reality.");
        if (Has("pimax"))
            l.Add("Pimax: turn Smart Smoothing off in Pimax Play.");
        if (running.Contains("LosslessScaling"))
            l.Add("Lossless Scaling is running: do not use its frame generation on Dishonored 2.");
        if (smoothMotion == true)
            l.Add("NVIDIA Smooth Motion is on for Dishonored 2: turn it off in the NVIDIA App (Graphics > Program settings > Dishonored 2).");
        return l;
    }

    public static HashSet<string> RunningProcesses()
    {
        var s = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var p in Process.GetProcesses()) { try { s.Add(p.ProcessName); } catch (Exception) { } finally { p.Dispose(); } }
        return s;
    }

    // NVIDIA Smooth Motion (driver frame generation, RTX 50, DX11/DX12). Setting ids from NVIDIA Profile Inspector's CustomSettingNames.xml
    // (no NVIDIA document lists them): "Smooth Motion - Enable" 0xB0D384C0 (0 off, 1 on) and "Smooth Motion - Enabled APIs" 0xB0CC0875
    // (bit 2 = DX11; absent = every API). Struct sizes and offsets measured with the SDK header (third_party/nvapi, 2026-11-20).
    public const uint SmoothMotionEnable = 0xB0D384C0, SmoothMotionApis = 0xB0CC0875;
    const int SettingSize = 12320, SettingCurrentU32 = 8220, ApplicationSize = 20492, UnicodeBytes = 4096;
    const int NvOk = 0, NvSettingNotFound = -160;

    [DllImport("nvapi64.dll", EntryPoint = "nvapi_QueryInterface", CallingConvention = CallingConvention.Cdecl)] static extern IntPtr NvQuery(uint id);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int NvInit();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int NvCreate(out IntPtr session);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int NvSession(IntPtr session);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int NvFindApp(IntPtr session, IntPtr appName, out IntPtr profile, IntPtr application);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int NvGlobal(IntPtr session, out IntPtr profile);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int NvGetSetting(IntPtr session, IntPtr profile, uint id, IntPtr setting);
    static T Nv<T>(uint id) where T : Delegate
    {
        var p = NvQuery(id);
        if (p == IntPtr.Zero) throw new EntryPointNotFoundException($"nvapi function 0x{id:x8}");
        return Marshal.GetDelegateForFunctionPointer<T>(p);
    }

    /// <summary>Whether NVIDIA Smooth Motion applies to <paramref name="exe"/> (the game's full path, or its file name): true/false, or null when
    /// it cannot be read (no NVIDIA driver, an older driver). Read only; nothing in the driver profile changes.</summary>
    public static (bool? on, string detail) NvidiaSmoothMotion(string exe)
    {
        if (!File.Exists(Path.Combine(Environment.SystemDirectory, "nvapi64.dll"))) return (null, "no NVIDIA driver");
        IntPtr session = IntPtr.Zero, name = IntPtr.Zero, app = IntPtr.Zero, setting = IntPtr.Zero;
        try
        {
            if (Nv<NvInit>(0x0150E828)() != NvOk) return (null, "NvAPI_Initialize failed");
            if (Nv<NvCreate>(0x0694D52E)(out session) != NvOk) return (null, "no driver settings session");
            if (Nv<NvSession>(0x375DBD6B)(session) is var load && load != NvOk) return (null, $"driver settings not loaded ({load})");
            name = Marshal.AllocHGlobal(UnicodeBytes); app = Marshal.AllocHGlobal(ApplicationSize); setting = Marshal.AllocHGlobal(SettingSize);
            Zero(name, UnicodeBytes); Zero(app, ApplicationSize);
            var chars = exe.Length > 2047 ? exe[..2047] : exe;
            Marshal.Copy(chars.ToCharArray(), 0, name, chars.Length);
            Marshal.WriteInt32(app, ApplicationSize | 4 << 16);  // NVDRS_APPLICATION_VER_V4
            var getGlobal = Nv<NvGlobal>(0x617BFF9F);
            var found = Nv<NvFindApp>(0xEEE566B2)(session, name, out var profile, app) == NvOk;
            if (!found && getGlobal(session, out profile) != NvOk) return (null, "no profile");
            var get = Nv<NvGetSetting>(0x73BF8338);
            uint? Read(IntPtr prof, uint id)
            {
                Zero(setting, SettingSize); Marshal.WriteInt32(setting, SettingSize | 1 << 16);  // NVDRS_SETTING_VER1
                var r = get(session, prof, id, setting);
                return r == NvOk ? (uint)Marshal.ReadInt32(setting, SettingCurrentU32) : r == NvSettingNotFound ? null : throw new InvalidOperationException($"NvAPI_DRS_GetSetting {r}");
            }
            var enable = Read(profile, SmoothMotionEnable); var apis = Read(profile, SmoothMotionApis);
            if (found && enable == null && getGlobal(session, out var global) == NvOk) { enable = Read(global, SmoothMotionEnable); apis ??= Read(global, SmoothMotionApis); }
            var on = enable == 1 && (apis == null || (apis & 2) != 0);
            return (on, $"{(found ? "game profile" : "global profile")}: enable {(enable?.ToString() ?? "unset")}, APIs {(apis?.ToString() ?? "unset")}");
        }
        catch (Exception ex) when (ex is DllNotFoundException or EntryPointNotFoundException or InvalidOperationException or BadImageFormatException)
        {
            return (null, ex.Message);
        }
        finally
        {
            if (session != IntPtr.Zero) try { Nv<NvSession>(0xDAD9CFF8)(session); } catch (Exception) { }
            foreach (var p in new[] { name, app, setting }) if (p != IntPtr.Zero) Marshal.FreeHGlobal(p);
        }
    }

    static void Zero(IntPtr p, int n) { var z = new byte[n]; Marshal.Copy(z, 0, p, n); }

    /// <summary>Everything above for one launch or Save: what the launcher changed (the SteamVR override), and what the user must switch off
    /// by hand (only while Spacewarp is on). Never throws.</summary>
    public static (List<string> changed, List<string> manual) Prepare(bool spacewarpOn, string stateFile, string? runtime, string? gameDir)
    {
        var changed = new List<string>(); var manual = new List<string>();
        try { if (SyncSteamVr(spacewarpOn, SteamVrSettingsPath(), stateFile, SteamVrRunning()) is string s) changed.Add(s); }
        catch (Exception ex) { changed.Add("SteamVR Motion Smoothing not changed: " + ex.Message); }
        if (!spacewarpOn) return (changed, manual);
        bool? smooth = null;
        try { smooth = NvidiaSmoothMotion(gameDir != null ? Path.Combine(gameDir, "Dishonored2.exe") : "Dishonored2.exe").on; } catch (Exception) { }
        try { manual.AddRange(ManualConflicts(runtime, RunningProcesses(), smooth)); } catch (Exception) { }
        return (changed, manual);
    }

    public const string StateFileName = "steamvr-smoothing-before";  // build\: SteamVR's override before Spacewarp switched it off
}
