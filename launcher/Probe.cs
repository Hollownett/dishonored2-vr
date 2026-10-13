using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace DishonoredVR;

/// <summary>C# port of the headset path of tools/run_demo.ps1 -Launch -SkipPreflightWait and tools/inject_probe.py (session and --stop),
/// plus tools/game_keys.py. Same probe configuration, launch record, run archives and safety checks; keys are posted to the game window only.</summary>
public static class Probe
{
    public const string GameHash = "b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180";
    const uint GameImageSize = 68988928;
    static readonly string[] ProbeOutputs = {
        "synchronized-diagnostics.jsonl", "native-grab-decisions.jsonl",
        "probe-launch-config.json", "dxgi-probe-result.json", "dxgi-probe-failure.txt", "dxgi-camera-probe.csv", "xr-live-status.json",
        "dxgi-gpu-trace.csv", "dxgi-native-hand-draw-candidates.csv", "native-view-setup.csv", "native-eye-boundary.csv", "native-gameplay-camera.csv", "native-turning-feedback.csv", "native-menu-input.csv", "paired-bind-counts.csv",
        "rendered-head-anchor.csv", "xr-frame-timing.csv", "xr-frame-timing-previous.csv", "present-boundaries.csv", "present-boundaries-previous.csv", "xr-frame-timing.json", "native-interaction-rays.csv", "native-interaction-targets.csv", "native-physical-grab.csv", "native-physical-grab-loss.txt",
        "dxgi-mono.rgba", "dxgi-left.rgba", "dxgi-stereo.rgba", "dxgi-mono.png", "dxgi-left.png", "dxgi-stereo.png",
        "dxgi-mono-views.json", "dxgi-left-views.json", "dxgi-right-views.json", "dxgi-stereo-views.json",
        "xr-source-left.rgba", "xr-source-right.rgba", "xr-destination-left.rgba", "xr-destination-right.rgba",
        "xr-transfer-left.json", "xr-transfer-right.json",
        "xr-native-source-left.rgba", "xr-native-source-right.rgba",
        "xr-settings.rgba", "xr-settings-transfer.json",
        "native-prompt-probe.jsonl", "native-wrist-diagnostics.json", "wrist-hud-left.rgba", "wrist-hud-left.json", "wrist-hud-right.rgba", "wrist-hud-right.json",
        "native-marker-probe.csv", "native-marker-probe-summary.txt", "native-crosshair.csv",
        "xr-settings-composed.rgba", "xr-settings-composed.json",
        "world-ui-1.rgba", "world-ui-1.json", "world-ui-2.rgba", "world-ui-2.json", "world-ui-3.rgba", "world-ui-3.json",
        "world-ui-target-context.jsonl",
        "xr-source-left.png", "xr-source-right.png", "xr-destination-left.png", "xr-destination-right.png",
        "native-arm-upload.csv", "native-arm-palettes.bin", "avatar-fit-events.csv", "stereo-geometry.json", "native-grab-acquisition.csv", "native-gameplay-aim.csv", "native-left-aim.csv",
        "xr-menu-source-left.rgba", "xr-menu-source-right.rgba", "xr-menu-destination-left.rgba", "xr-menu-destination-right.rgba",
        "xr-menu-transfer-left.json", "xr-menu-transfer-right.json", "xr-lifecycle.csv",
        "dxgi-settled-views.json", "settled-render-state.json",
        "headset-preflight.txt",
        "scene-resolution.json",
        "native-exception-context.txt",
        "simulated-gameplay-script.json", "simulated-gameplay-commands.csv",
        "simulated-cleanup-exception.json",
        "authored-native-left.rgba", "authored-native-right.rgba", "authored-native-views.json", "authored-native-pair.json",
        "native-context-dispatch-change.json",
        "native-context-dispatch-change.jsonl",
        "native-weapon-attachment.csv",
        "native-lean-query-trace.csv",
        "native-lean-lifecycle.csv",
        "native-weapon-model-base.csv",
        "native-weapon-dry-lease.csv",
    };

    // ---------------- run_demo.ps1 -Launch -SkipPreflightWait (headset), injecting at the title screen

    /// <summary>Starts the game if needed, waits for its title screen, then runs the session until StopProbe, game exit or failure.
    /// When this call started the game it posts Enter three times (title, Campaign, Continue) to the game window once the mod is in.</summary>
    public static async Task Launch(Candidate c, string root, Action<string> log, Func<bool> inGameplay)
    {
        using (var doc = JsonDocument.Parse(File.ReadAllText(c.Manifest)))
        {
            var m = doc.RootElement;
            if (m.TryGetProperty("launch_blocked", out var lb) && lb.ValueKind == JsonValueKind.True)
                throw new InvalidOperationException(m.TryGetProperty("launch_blocked_reason", out var why) ? why.ToString() : "Candidate is launch-blocked.");
            if (!Regex.IsMatch(m.GetProperty("dll_name").GetString() ?? "", @"^d2vr_render_probe_arm_v[0-9]+\.dll$")) throw new InvalidOperationException("Invalid candidate filename.");
            if (ModHome.Sha256(c.DllPath) != m.GetProperty("sha256").GetString())
                throw new InvalidOperationException("Candidate DLL changed since validation. Build and validate a fresh version before updating the candidate.");
            if (m.GetProperty("game_sha256").GetString() != GameHash) throw new InvalidOperationException("The candidate was validated for a different game build.");
        }
        var games = Process.GetProcessesByName("Dishonored2");
        bool startedGame = false;
        if (games.Length == 0)
        {
            GameKeys.PrepareConfig(log);
            Process.Start(new ProcessStartInfo("steam://rungameid/403640") { UseShellExecute = true });  // Steam app 403640 is Dishonored 2
            log("Starting Dishonored 2 through Steam...");
            var deadline = DateTime.UtcNow.AddMinutes(3);
            while (games.Length == 0 && DateTime.UtcNow < deadline) { await Task.Delay(2000); games = Process.GetProcessesByName("Dishonored2"); }
            if (games.Length > 0)
            {
                log("Game started; waiting 45 s for the title screen.");
                await Task.Delay(45000);
                games = Process.GetProcessesByName("Dishonored2"); startedGame = true;
            }
        }
        if (games.Length != 1) throw new InvalidOperationException("Start Dishonored 2 and enter gameplay or its pause menu; exactly one process is required.");
        int pid = games[0].Id;
        await Task.Run(() => Session(root, c.Dll, pid, log, () =>
        {
            if (startedGame) _ = Task.Run(async () =>
            {
                await Task.Delay(10000);  // let the session come up first, as tools/vr_selftest.py --inject-at-title does
                foreach (var _ in Enumerable.Range(0, 3))
                {
                    if (inGameplay()) { log("Gameplay reached; no more keys sent."); return; }
                    if (!GameKeys.Post(pid, 0x0D)) { log("Game window not found; load your save on the desktop."); return; }
                    log("Posted Enter to the game window (title -> Campaign -> Continue).");
                    await Task.Delay(1500);
                }
            });
        }));
    }

    // ---------------- inject_probe.py: the fixed headset configuration run_demo.ps1 passes

    // --pid P --paired --native-eye-capture --dll-name D --vr-preview --head-tracking --controllers --symmetric-frustum --eye-antialiasing
    // --native-menu-pointer --stable-camera --vr-turning snap --physical-grab --native-left-aim-experiment --controller-aim --tracked-arms
    // --headset-size --saved-settings --until-stopped
    static readonly string[] HeadsetFlags = { "--paired", "--native-eye-capture", "--dll-name", "", "--vr-preview", "--head-tracking", "--controllers",
        "--symmetric-frustum", "--eye-antialiasing", "--native-menu-pointer", "--stable-camera", "--vr-turning", "snap", "--physical-grab",
        "--native-left-aim-experiment", "--controller-aim", "--tracked-arms", "--headset-size", "--saved-settings", "--until-stopped" };
    // ProbeCapabilities bits: arm upload observer 1, tracked arms 8, stable camera 16, eye antialiasing 64, turning 512, saved settings 1024,
    // menu 2048, controller aim 8192, physical grab 16384, native left aim 32768.
    const uint RequiredCapabilities = 1 | 8 | 16 | 64 | 512 | 1024 | 2048 | 8192 | 16384 | 32768;

    /// <summary>inject_probe.py probe_config() for that profile: pid, camera 0, mode 6 (head tracking), paired, skip TAA 0, full render scale
    /// (implied by headset size), symmetric frustum, controllers, headset size, eye boundary (implied), native eye capture, seconds 0
    /// (until stopped), arm upload observer (implied by tracked arms), optical density 0, palette copy 0, tracked arms, stable camera,
    /// resolution 1.0, eye AA, camera trace 0, snap turning, 45 deg, 90 deg/s, saved settings, menu pointer 2, simulated 0, controller aim,
    /// physical grab, left aim, cleanup exception 0, script 0, weapon attachment trace 0.</summary>
    public static string ProbeConfig(int pid) => $"{pid} 0 6 1 0 1 1 1 1 1 1 0 1 0 0 1 1 1.0 1 0 1 45 90 1 2 0 1 1 1 0 0 0\n";

    /// <summary>The parsed argparse namespace as inject_probe.py records it (before implied flags), in argument order.</summary>
    static (string, object?)[] RequestedConfig(int pid, string dll) => new (string, object?)[] {
        ("pid", pid), ("camera_report", null), ("stereo", false), ("single_eye", false), ("candidate", false), ("pipeline", false), ("paired", true),
        ("view_setup", false), ("wait_for_game", false), ("wait_for_headset", false), ("dll_name", dll), ("vr_preview", true), ("head_tracking", true),
        ("skip_temporal_aa", false), ("full_render_scale", false), ("symmetric_frustum", true), ("controllers", true), ("headset_size", true),
        ("eye_boundary", false), ("native_eye_capture", true), ("seconds", null), ("until_stopped", true), ("stop", false), ("arm_upload_observer", false),
        ("optical_density", false), ("arm_palette_copy", false), ("tracked_arms", true), ("stable_camera", true), ("eye_antialiasing", true),
        ("native_camera_trace", false), ("vr_turning", "snap"), ("snap_degrees", 45), ("smooth_degrees_per_second", 90), ("resolution_multiplier", 1.0),
        ("saved_settings", true), ("native_menu_observer", false), ("native_menu_pointer", true), ("simulated_vr", false), ("simulated_input", false),
        ("controller_aim", true), ("physical_grab", true), ("simulated_gameplay_script", null), ("native_weapon_attachment_trace", false),
        ("native_left_aim_experiment", true), ("simulated_cleanup_exception", false) };

    sealed record Mod(string Path, ulong Base, uint Size);

    sealed class Attached : IDisposable
    {
        public required Pe Pe; public required string Dll, DllHash, Variant, IdentityFile; public required List<Mod> Loaded, Prior; public required Mod Game;
        public required ulong RemoteLoader, LocalLoader; public required IntPtr Process; public required List<(string, string)> Provenance;
        public void Dispose() => N.CloseHandle(Process);
    }

    /// <summary>inject_probe.py up to the mode branch: DLL/PE, supported game image and hash, loaded-identity checks, LoadLibraryW resolution.</summary>
    static Attached Attach(string root, string dllName, int pid, bool stop, Action<string> log)
    {
        if (!Environment.Is64BitProcess || pid <= 0) throw new InvalidOperationException("Use an x64 launcher on Windows and a positive game PID");
        if (!Regex.IsMatch(dllName, @"^d2vr_render_probe(?:_[a-z0-9_]+)?\.dll$")) throw new InvalidOperationException("DLL name must be a lowercase workspace diagnostic filename");
        var variant = dllName["d2vr_render_probe".Length..^4].Replace('_', '-');
        var dll = Path.GetFullPath(Path.Combine(root, "build", dllName));
        var bytes = File.ReadAllBytes(dll);
        var pe = new Pe(bytes);
        if (pe.Machine != 0x8664) throw new InvalidOperationException("The diagnostic must be x64");
        pe.CodeExport("StartProbe");
        var loaded = Modules(pid);
        var games = loaded.Where(m => Path.GetFileName(m.Path).Equals("dishonored2.exe", StringComparison.OrdinalIgnoreCase)).ToList();
        if (games.Count != 1 || games[0].Size != GameImageSize) throw new InvalidOperationException("PID is not the supported Dishonored 2 image");
        var game = games[0];
        if (ModHome.Sha256(game.Path) != GameHash) throw new InvalidOperationException("Unsupported executable hash; no DLL loaded");
        var prior = loaded.Where(m => Path.GetFileName(m.Path).Equals(dllName, StringComparison.OrdinalIgnoreCase)).ToList();
        var identityFile = Path.Combine(root, "artifacts", "loaded-render-probe" + variant + ".json");
        var dllHash = Hex(SHA256.HashData(bytes));
        var provenance = MatchingProvenance(root, dllName, dllHash, log);
        if (prior.Count > 0)
        {
            using var id = JsonDocument.Parse(File.Exists(identityFile) ? File.ReadAllText(identityFile) : "{}");
            var r = id.RootElement;
            bool Is(string k, Func<JsonElement, bool> eq) => r.TryGetProperty(k, out var v) && eq(v);
            if (prior.Count != 1 || !Same(prior[0].Path, dll) || prior[0].Size != pe.ImageSize ||
                !Is("pid", v => v.ValueKind == JsonValueKind.Number && v.GetInt64() == pid) ||
                !Is("base", v => v.ValueKind == JsonValueKind.Number && v.GetUInt64() == prior[0].Base) ||
                !Is("sha256", v => v.ValueKind == JsonValueKind.String && v.GetString() == dllHash))
                throw new InvalidOperationException("Loaded diagnostic identity differs; restart the game before testing a rebuilt DLL");
        }
        if (stop && prior.Count == 0) throw new InvalidOperationException("The specified diagnostic is not loaded in this game process");
        if (variant != "" && prior.Count == 0 && loaded.Any(m => Path.GetFileName(m.Path).Equals("d2vr_render_probe.dll", StringComparison.OrdinalIgnoreCase)))
        {
            using var old = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "artifacts", "loaded-render-probe.json")));
            using var res = JsonDocument.Parse(File.ReadAllText(Path.Combine(root, "artifacts", "dxgi-probe-result.json")));
            var r = res.RootElement;
            bool True(string k) => r.TryGetProperty(k, out var v) && v.ValueKind == JsonValueKind.True;
            if (!(old.RootElement.TryGetProperty("pid", out var op) && op.ValueKind == JsonValueKind.Number && op.GetInt64() == pid) ||
                !new[] { "hook_disabled", "backend_hook_disabled", "mode_restored", "values_restored" }.All(True))
                throw new InvalidOperationException("Previous diagnostic has not confirmed clean restoration; restart the game");
            if (r.TryGetProperty("gpu_trace_requested", out var g) && g.ValueKind == JsonValueKind.True && !True("gpu_hooks_disabled"))
                throw new InvalidOperationException("Previous GPU trace hooks have not confirmed clean restoration; restart the game");
            if (r.TryGetProperty("view_setup_requested", out var vs) && vs.ValueKind == JsonValueKind.True && !True("view_setup_hook_disabled"))
                throw new InvalidOperationException("Previous native view observer has not confirmed clean restoration; restart the game");
        }
        // Resolve forwarded LoadLibraryW by its actual owning local module, then use that module's base in the target. Steam hooks
        // kernel32's forwarding thunk in the game, so use the implementation KernelBase exports and require matching module identity.
        var loader = (ulong)N.GetProcAddress(N.GetModuleHandleW("kernelbase.dll"), "LoadLibraryW");
        if (loader == 0) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot resolve LoadLibraryW implementation");
        var owners = Modules(Environment.ProcessId).Where(m => m.Base <= loader && loader < m.Base + m.Size).ToList();
        var remoteOwners = loaded.Where(m => owners.Count == 1 && Same(m.Path, owners[0].Path)).ToList();
        if (owners.Count != 1 || remoteOwners.Count != 1 || owners[0].Size != remoteOwners[0].Size) throw new InvalidOperationException("Cannot establish the remote loader module identity");
        var process = N.OpenProcess(0x2 | 0x8 | 0x10 | 0x20 | 0x400, false, pid);
        if (process == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot open game for diagnostic injection");
        var path = new StringBuilder(32768); int size = path.Capacity;
        if (!N.QueryFullProcessImageNameW(process, 0, path, ref size)) { N.CloseHandle(process); throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot verify opened process"); }
        if (!Same(path.ToString(), game.Path)) { N.CloseHandle(process); throw new InvalidOperationException("Process identity changed"); }
        return new Attached { Pe = pe, Dll = dll, DllHash = dllHash, Variant = variant, IdentityFile = identityFile, Loaded = loaded, Prior = prior, Game = game,
            RemoteLoader = remoteOwners[0].Base + loader - owners[0].Base, LocalLoader = loader, Process = process, Provenance = provenance };
    }

    /// <summary>inject_probe.py --pid P --stop --dll-name D: StopProbe in a remote thread of the exact loaded DLL.</summary>
    public static void Stop(string root, string dllName, int pid, Action<string> log)
    {
        using var a = Attach(root, dllName, pid, true, log);
        var code = RunThread(a.Process, ExportAddress(a.Process, a.Prior[0].Base, a.Pe, "StopProbe"), 0, 5000);
        log($"{{\"pid\": {pid}, \"stop_requested\": {Js(code == 0)}, \"stop_exit\": {code}, \"restoration_pending\": {Js(code == 0)}}}");
        if (code != 0) throw new InvalidOperationException("Probe is inactive, starting or already stopping; no new session was started");
    }

    /// <summary>inject_probe.py session for the headset profile, until stopped. started runs just before StartProbe.</summary>
    public static void Session(string root, string dllName, int pid, Action<string> log, Action? started = null)
    {
        using var a = Attach(root, dllName, pid, false, log);
        var p = a.Process;
        ulong allocation = 0; bool pathThreadMayRun = false;
        try
        {
            // A running session must not lose its shared configuration or frame evidence. (inject_probe.py checks probes loaded from
            // its own build folder; any folder is checked here, so a repo build and the mod home cannot run side by side.)
            foreach (var item in a.Loaded)
            {
                if (!Regex.IsMatch(Path.GetFileName(item.Path), @"^d2vr_render_probe(?:_[a-z0-9_]+)?\.dll$")) continue;
                var existing = new Pe(File.ReadAllBytes(item.Path));
                if (!existing.HasExport("ProbeStatus")) continue;  // older timed diagnostics have no status export
                if (RunThread(p, ExportAddress(p, item.Base, existing, "ProbeStatus"), 0, 5000) != 0)
                    throw new InvalidOperationException($"A diagnostic is still active: {Path.GetFileName(item.Path)}; stop it before another test");
            }
            var remote = Read(p, a.RemoteLoader, 16); var local = new byte[16]; Marshal.Copy((IntPtr)(long)a.LocalLoader, local, 0, 16);
            if (!remote.AsSpan().SequenceEqual(local)) throw new InvalidOperationException($"Remote loader instructions differ: local={Hex(local)} remote={Hex(remote)}");
            var gamePe = new Pe(File.ReadAllBytes(a.Game.Path));
            if (!Read(p, a.Game.Base + 0x5bf00, 16).AsSpan().SequenceEqual(gamePe.Bytes(gamePe.Offset(0x5bf00, 16), 16)))
                throw new InvalidOperationException("Live stereo construction code differs from the supported executable");
            if (BitConverter.ToUInt64(Read(p, a.Game.Base + 0x2be6910, 8)) != a.Game.Base + 0x1aca0b8)
                throw new InvalidOperationException("The native renderer must start in single-camera mode");
            a.Pe.CodeExport("ProbeCapabilities");  // reject older DLLs before changing shared configuration
            var config = ProbeConfig(pid);
            Directory.CreateDirectory(Path.Combine(root, "build"));
            File.WriteAllText(Path.Combine(root, "build", "dxgi-probe.cfg"), config, Encoding.ASCII);
            var art = Path.Combine(root, "artifacts"); Directory.CreateDirectory(art);
            Preserve(root, "previous-artifacts");
            foreach (var n in ProbeOutputs) File.Delete(Path.Combine(art, n));
            log("Loading exact-build diagnostic; game-image XR preview until stopped...");
            if (a.Prior.Count == 0)
            {
                var payload = Encoding.Unicode.GetBytes(a.Dll + "\0");
                allocation = (ulong)N.VirtualAllocEx(p, IntPtr.Zero, (nuint)payload.Length, 0x3000, 0x4);
                if (allocation == 0) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot allocate DLL path");
                if (!N.WriteProcessMemory(p, (IntPtr)(long)allocation, payload, (nuint)payload.Length, out var written)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot write DLL path");
                if (written != (nuint)payload.Length) throw new InvalidOperationException("Incomplete DLL-path write");
                pathThreadMayRun = true;  // on any uncertain wait, keep the tiny path allocation until game exit
                RunThread(p, a.RemoteLoader, allocation, 10000);
                pathThreadMayRun = false;
            }
            var diagnostic = Modules(pid).Where(m => Same(m.Path, a.Dll)).ToList();
            if (diagnostic.Count != 1 || diagnostic[0].Size != a.Pe.ImageSize) throw new InvalidOperationException("DLL did not load with the expected identity");
            var entryRva = a.Pe.CodeExport("StartProbe");
            var start = diagnostic[0].Base + entryRva;
            if (!Read(p, start, 16).AsSpan().SequenceEqual(a.Pe.Bytes(a.Pe.Offset(entryRva, 16), 16))) throw new InvalidOperationException("Loaded diagnostic entry instructions differ from built DLL");
            ExportAddress(p, diagnostic[0].Base, a.Pe, "StopProbe");
            var caps = RunThread(p, ExportAddress(p, diagnostic[0].Base, a.Pe, "ProbeCapabilities"), 0, 5000);
            if ((caps & RequiredCapabilities) != RequiredCapabilities) throw new InvalidOperationException("The exact built DLL does not implement the requested features");
            File.WriteAllText(a.IdentityFile, $"{{\"pid\": {pid}, \"base\": {diagnostic[0].Base}, \"sha256\": \"{a.DllHash}\"}}");
            started?.Invoke();
            double runStarted = Unix();
            uint code;
            try { code = RunThread(p, start, 0, null); }
            catch (Exception error)
            {
                WriteLaunchConfig(root, pid, a, config, runStarted, null, null, error, Unix());
                var evidence = Preserve(root, $"pid{pid}{a.Variant}-failed", a.IdentityFile, a.Provenance);
                log($"{{\"pid\": {pid}, \"run_config_archived\": {Js(evidence)}, \"build_source_provenance\": {Js(a.Provenance.Count > 0)}, \"status\": \"remote_call_failed\"}}");
                throw;
            }
            WriteLaunchConfig(root, pid, a, config, runStarted, Unix(), code, null, null);
            var kept = Preserve(root, code == 0 ? $"pid{pid}{a.Variant}" : $"pid{pid}{a.Variant}-failed", a.IdentityFile, a.Provenance);
            log($"{{\"pid\": {pid}, \"diagnostic_exit\": {code}, \"preserved_artifacts\": {Js(kept)}, \"build_source_provenance\": {Js(a.Provenance.Count > 0)}, " +
                "\"dll_retained_until_game_exit\": true, \"camera_object_writes\": null, \"camera_object_writes_requested\": true, \"native_left_aim_experiment_requested\": true, " +
                "\"simulated_cleanup_exception_requested\": false, \"simulated_gameplay_script_requested\": false, \"expected_diagnostic_exit\": 0, " +
                "\"native_left_aim_backend\": \"xr\", \"native_stereo_settings_requested\": true}");
            if (code != 0) throw new InvalidOperationException($"Diagnostic failed ({code}); inspect artifacts/dxgi-probe-failure.txt or result JSON");
            if (!File.Exists(Path.Combine(art, "dxgi-probe-result.json"))) throw new InvalidOperationException("Game exited before the diagnostic wrote a restoration report; clean shutdown is unverified");
        }
        finally
        {
            if (allocation != 0 && !pathThreadMayRun) N.VirtualFreeEx(p, (IntPtr)(long)allocation, 0, 0x8000);
        }
    }

    // ---------------- launch record and run archives

    static void WriteLaunchConfig(string root, int pid, Attached a, string config, double started, double? ended, uint? exit, Exception? error, double? errorUnix) =>
        File.WriteAllBytes(Path.Combine(root, "artifacts", "probe-launch-config.json"),
            LaunchConfigJson(pid, a.Game.Path, Path.GetFileName(a.Dll), a.DllHash, config, started, ended, exit, error, errorUnix));

    /// <summary>inject_probe.py write_launch_config() for the headset profile (schema D2VR-probe-launch-config-v1).</summary>
    static byte[] LaunchConfigJson(int pid, string executable, string dllName, string dllHash, string config, double started, double? ended, uint? exit, Exception? error, double? errorUnix)
    {
        var requested = RequestedConfig(pid, dllName);
        using var ms = new MemoryStream();
        using (var j = new Utf8JsonWriter(ms, new JsonWriterOptions { Indented = true, Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping }))
        {
            j.WriteStartObject();
            j.WriteString("schema", "D2VR-probe-launch-config-v1");
            j.WriteNumber("pid", pid);
            j.WriteString("executable", executable);
            j.WriteString("executable_sha256", GameHash);
            j.WriteString("dll_name", dllName);
            j.WriteString("dll_sha256", dllHash);
            j.WriteNumber("started_unix", started);
            if (ended is double e) j.WriteNumber("ended_unix", e); else j.WriteNull("ended_unix");
            j.WriteBoolean("tracked_arms_requested", true);
            j.WriteBoolean("arm_palette_copy_requested", false);
            j.WriteStartObject("requested_flags");
            foreach (var (k, v) in requested) if (v is bool b) j.WriteBoolean(k + "_requested", b);
            j.WriteEndObject();
            j.WriteStartObject("requested_config");
            foreach (var (k, v) in requested)
            {
                j.WritePropertyName(k);
                switch (v)
                {
                    case null: j.WriteNullValue(); break;
                    case bool b: j.WriteBooleanValue(b); break;
                    case int n: j.WriteNumberValue(n); break;
                    case double d: j.WriteRawValue(d.ToString("0.0###############", System.Globalization.CultureInfo.InvariantCulture)); break;  // Python float repr: 1.0
                    default: j.WriteStringValue(v.ToString()); break;
                }
            }
            j.WriteEndObject();
            j.WriteStartArray("launcher_argv");
            j.WriteStringValue(Environment.ProcessPath); j.WriteStringValue("--pid"); j.WriteStringValue(pid.ToString());
            foreach (var f in HeadsetFlags) j.WriteStringValue(f == "" ? dllName : f);
            j.WriteEndArray();
            j.WriteString("probe_config", config);
            j.WriteString("probe_config_sha256", Hex(SHA256.HashData(Encoding.ASCII.GetBytes(config))));
            if (exit is uint x) j.WriteNumber("diagnostic_exit", x); else j.WriteNull("diagnostic_exit");
            j.WriteString("status", error != null ? "remote_call_failed" : exit is > 0 ? "diagnostic_failed" : "completed");
            if (error != null)
            {
                j.WriteStartObject("error"); j.WriteString("type", error.GetType().Name); j.WriteString("message", error.Message); j.WriteEndObject();
                j.WriteNumber("launcher_error_unix", errorUnix ?? 0);
            }
            j.WriteEndObject();
        }
        return ms.ToArray().Concat("\n"u8.ToArray()).ToArray();
    }

    /// <summary>Headless check (no game access): the probe configuration and launch record for a reference probe-launch-config.json
    /// written by inject_probe.py on the headset path, and the export table of a mod DLL. Returns 0 when everything matches.</summary>
    public static int Check(string reference, string? dll, TextWriter o)
    {
        using var doc = JsonDocument.Parse(File.ReadAllText(reference));
        var r = doc.RootElement;
        int pid = r.GetProperty("pid").GetInt32(), bad = 0;
        var config = ProbeConfig(pid);
        bool cfg = config == r.GetProperty("probe_config").GetString() && Hex(SHA256.HashData(Encoding.ASCII.GetBytes(config))) == r.GetProperty("probe_config_sha256").GetString();
        o.WriteLine($"probe_config {(cfg ? "matches" : "DIFFERS")}: {config.TrimEnd()}"); if (!cfg) bad++;
        double? ended = r.GetProperty("ended_unix").ValueKind == JsonValueKind.Null ? null : r.GetProperty("ended_unix").GetDouble();
        uint? exit = r.GetProperty("diagnostic_exit").ValueKind == JsonValueKind.Null ? null : r.GetProperty("diagnostic_exit").GetUInt32();
        var mine = JsonNode.Parse(LaunchConfigJson(pid, r.GetProperty("executable").GetString()!, r.GetProperty("dll_name").GetString()!,
            r.GetProperty("dll_sha256").GetString()!, config, r.GetProperty("started_unix").GetDouble(), ended, exit, null, null))!.AsObject();
        var theirs = JsonNode.Parse(r.GetRawText())!.AsObject();
        mine["launcher_argv"]![0] = theirs["launcher_argv"]![0]!.GetValue<string>();  // argv[0] is this exe, not tools\inject_probe.py
        foreach (var (k, v) in theirs)
        {
            bool same = JsonNode.DeepEquals(v, mine[k]);
            o.WriteLine($"{k,-28} {(same ? "matches" : "DIFFERS: " + mine[k]?.ToJsonString())}"); if (!same) bad++;
        }
        if (mine.Count != theirs.Count) { o.WriteLine($"key count {mine.Count} vs {theirs.Count}"); bad++; }
        if (dll != null)
        {
            var pe = new Pe(File.ReadAllBytes(dll));
            o.WriteLine($"{Path.GetFileName(dll)}: machine 0x{pe.Machine:x}, image size {pe.ImageSize}");
            foreach (var e in new[] { "StartProbe", "StopProbe", "ProbeStatus", "ProbeCapabilities" })
                o.WriteLine($"  {e} rva 0x{pe.CodeExport(e):x} file offset 0x{pe.Offset(pe.CodeExport(e), 16):x}");
        }
        o.WriteLine(bad == 0 ? "all match" : $"{bad} differences");
        return bad == 0 ? 0 : 1;
    }

    /// <summary>preserve_probe_outputs: copy the shared outputs (and timed eye captures, recovery log, identity, provenance) to artifacts\runs\&lt;UTC stamp&gt;-&lt;label&gt;.</summary>
    static string? Preserve(string root, string label, string? identity = null, List<(string, string)>? extra = null)
    {
        var art = Path.Combine(root, "artifacts");
        var sources = ProbeOutputs.Select(n => Path.Combine(art, n)).Where(File.Exists).ToList();
        if (sources.Count == 0) return null;
        var timed = Directory.GetFiles(art, "xr-t*-left.rgba").Where(f => Regex.IsMatch(Path.GetFileName(f), @"^xr-t.*-left\.rgba$")).Order().ToList();
        var recoveries = Path.Combine(art, "xr-recoveries.log");
        sources.AddRange(timed); if (File.Exists(recoveries)) sources.Add(recoveries);
        var dest = Path.Combine(art, "runs", DateTime.UtcNow.ToString("yyyyMMdd'T'HHmmss.ffffff'Z'") + "-" + label);
        if (Directory.Exists(dest)) throw new IOException("Run archive already exists: " + dest);
        Directory.CreateDirectory(dest);
        if (identity != null && File.Exists(identity)) sources.Add(identity);
        foreach (var s in sources) CopyShared(s, Path.Combine(dest, Path.GetFileName(s)));
        foreach (var (s, n) in extra ?? new()) CopyShared(s, Path.Combine(dest, n));
        foreach (var t in timed) File.Delete(t);  // never attribute stale timed frames to the next run
        return dest;
    }

    /// <summary>shutil.copy2 equivalent that tolerates the game still holding the file open for writing.</summary>
    static void CopyShared(string from, string to)
    {
        using (var src = new FileStream(from, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
        using (var dst = new FileStream(to, FileMode.CreateNew)) src.CopyTo(dst);
        File.SetLastWriteTimeUtc(to, File.GetLastWriteTimeUtc(from));
    }

    /// <summary>tools/archive_build_source.py matching_provenance: build\&lt;dll&gt;.source.json plus its verified source manifest and zip.</summary>
    static List<(string, string)> MatchingProvenance(string root, string dll, string hash, Action<string> log)
    {
        try
        {
            var pointer = Path.Combine(root, "build", dll + ".source.json");
            if (!File.Exists(pointer)) return new();
            using var doc = JsonDocument.Parse(File.ReadAllText(pointer));
            var r = doc.RootElement;
            string? S(string k) => r.TryGetProperty(k, out var v) && v.ValueKind == JsonValueKind.String ? v.GetString() : null;
            if (S("dll_name") != dll || S("dll_sha256") != hash) return new();
            if (S("state") != "complete" || !(r.TryGetProperty("source_unchanged_during_build", out var u) && u.ValueKind == JsonValueKind.True)) return new();
            var full = Path.GetFullPath(root).TrimEnd('\\') + "\\";
            var manifest = Path.GetFullPath(Path.Combine(root, S("source_manifest") ?? throw new KeyNotFoundException("source_manifest")));
            var zip = Path.GetFullPath(Path.Combine(root, S("source_zip") ?? throw new KeyNotFoundException("source_zip")));
            if (!manifest.StartsWith(full, StringComparison.OrdinalIgnoreCase) || !zip.StartsWith(full, StringComparison.OrdinalIgnoreCase)) throw new InvalidDataException("Provenance path escapes the workspace");
            if (!File.Exists(manifest) || !File.Exists(zip)) throw new InvalidDataException("Matching build provenance archive is incomplete");
            if (ModHome.Sha256(manifest) != S("source_manifest_sha256")) throw new InvalidDataException("Matching build source manifest hash changed");
            if (ModHome.Sha256(zip) != S("source_zip_sha256")) throw new InvalidDataException("Matching build source zip hash changed");
            return new() { (pointer, "build-source-provenance.json"), (manifest, "build-source-manifest.json"), (zip, "build-source.zip") };
        }
        catch (Exception ex) when (ex is IOException or InvalidDataException or JsonException or KeyNotFoundException)
        {
            log("No build-source provenance will be attached: " + ex.Message);
            return new();
        }
    }

    // ---------------- process helpers

    static List<Mod> Modules(int pid)
    {
        IntPtr snap = new(-1);
        for (int tries = 0; tries < 10 && snap == new IntPtr(-1); tries++)
        {
            snap = N.CreateToolhelp32Snapshot(0x8 | 0x10, (uint)pid);
            if (snap == new IntPtr(-1) && Marshal.GetLastWin32Error() != 24) break;  // ERROR_BAD_LENGTH: retry, as the API documents
        }
        if (snap == new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot enumerate process modules");
        try
        {
            var e = new N.ModuleEntry { dwSize = (uint)Marshal.SizeOf<N.ModuleEntry>() };
            if (!N.Module32FirstW(snap, ref e)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot read first module");
            var list = new List<Mod>();
            while (true)
            {
                list.Add(new Mod(Path.GetFullPath(e.szExePath), (ulong)e.modBaseAddr, e.modBaseSize));
                if (!N.Module32NextW(snap, ref e))
                {
                    if (Marshal.GetLastWin32Error() != 18) throw new Win32Exception(Marshal.GetLastWin32Error(), "Module enumeration interrupted");
                    return list;
                }
            }
        }
        finally { N.CloseHandle(snap); }
    }

    static byte[] Read(IntPtr process, ulong address, int size)
    {
        var buf = new byte[size];
        if (!N.ReadProcessMemory(process, (IntPtr)(long)address, buf, (nuint)size, out var n)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Process memory read failed");
        if (n != (nuint)size) throw new InvalidOperationException("Incomplete process read");
        return buf;
    }

    static ulong ExportAddress(IntPtr process, ulong moduleBase, Pe pe, string name)
    {
        var rva = pe.CodeExport(name);
        var address = moduleBase + rva;
        if (!Read(process, address, 16).AsSpan().SequenceEqual(pe.Bytes(pe.Offset(rva, 16), 16))) throw new InvalidOperationException($"Loaded {name} entry differs from the built DLL");
        return address;
    }

    /// <summary>Runs start(parameter) in a remote thread; null waits without limit (500 ms polls, as inject_probe.py).</summary>
    static uint RunThread(IntPtr process, ulong start, ulong parameter, int? milliseconds)
    {
        var h = N.CreateRemoteThread(process, IntPtr.Zero, 0, (IntPtr)(long)start, (IntPtr)(long)parameter, 0, IntPtr.Zero);
        if (h == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot create diagnostic thread");
        try
        {
            uint status;
            if (milliseconds == null) { while ((status = N.WaitForSingleObject(h, 500)) == 0x102) { } }
            else status = N.WaitForSingleObject(h, (uint)milliseconds.Value);
            if (status == 0x102) throw new TimeoutException("Diagnostic thread is still running; DLL/allocation retained, do not rerun");
            if (status != 0) throw new Win32Exception(Marshal.GetLastWin32Error(), "Diagnostic thread wait failed; do not rerun");
            if (!N.GetExitCodeThread(h, out var code)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot read diagnostic thread status");
            return code;
        }
        finally { N.CloseHandle(h); }
    }

    static bool Same(string a, string b) => string.Equals(Path.GetFullPath(a), Path.GetFullPath(b), StringComparison.OrdinalIgnoreCase);
    static string Hex(byte[] b) => Convert.ToHexString(b).ToLowerInvariant();
    static double Unix() => (DateTime.UtcNow - DateTime.UnixEpoch).TotalSeconds;
    static string Js(bool b) => b ? "true" : "false";
    static string Js(string? s) => s == null ? "null" : JsonSerializer.Serialize(s);

    static class N
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct ModuleEntry
        {
            public uint dwSize, th32ModuleID, th32ProcessID, GlblcntUsage, ProccntUsage;
            public IntPtr modBaseAddr; public uint modBaseSize; public IntPtr hModule;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string szModule;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string szExePath;
        }
        [DllImport("kernel32", SetLastError = true)] public static extern IntPtr CreateToolhelp32Snapshot(uint flags, uint pid);
        [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)] public static extern bool Module32FirstW(IntPtr snap, ref ModuleEntry e);
        [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)] public static extern bool Module32NextW(IntPtr snap, ref ModuleEntry e);
        [DllImport("kernel32", SetLastError = true)] public static extern bool CloseHandle(IntPtr h);
        [DllImport("kernel32", SetLastError = true)] public static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
        [DllImport("kernel32", SetLastError = true)] public static extern bool ReadProcessMemory(IntPtr p, IntPtr address, byte[] buffer, nuint size, out nuint read);
        [DllImport("kernel32", SetLastError = true)] public static extern bool WriteProcessMemory(IntPtr p, IntPtr address, byte[] buffer, nuint size, out nuint written);
        [DllImport("kernel32", SetLastError = true)] public static extern IntPtr VirtualAllocEx(IntPtr p, IntPtr address, nuint size, uint type, uint protect);
        [DllImport("kernel32", SetLastError = true)] public static extern bool VirtualFreeEx(IntPtr p, IntPtr address, nuint size, uint type);
        [DllImport("kernel32", SetLastError = true)] public static extern IntPtr CreateRemoteThread(IntPtr p, IntPtr attributes, nuint stack, IntPtr start, IntPtr parameter, uint flags, IntPtr id);
        [DllImport("kernel32", SetLastError = true)] public static extern uint WaitForSingleObject(IntPtr h, uint milliseconds);
        [DllImport("kernel32", SetLastError = true)] public static extern bool GetExitCodeThread(IntPtr h, out uint code);
        [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)] public static extern bool QueryFullProcessImageNameW(IntPtr p, uint flags, StringBuilder path, ref int size);
        [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Unicode)] public static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32", SetLastError = true, CharSet = CharSet.Ansi, BestFitMapping = false)] public static extern IntPtr GetProcAddress(IntPtr module, string name);
    }
}

/// <summary>Port of tools/game_keys.py: background-input game config and keys posted to the game window only (never global input, no focus change).</summary>
public static class GameKeys
{
    static readonly string Config = Environment.ExpandEnvironmentVariables(@"%USERPROFILE%\Saved Games\Arkane Studios\Dishonored2\base\dishonored2Config.cfg");

    /// <summary>--prepare-config (game closed): append win_pauseOnFocusLoss "0" and in_controlInactiveWindow "1" when absent.
    /// The game drops non-archived lines on exit, so this runs before every launch.</summary>
    public static void PrepareConfig(Action<string> log)
    {
        if (!File.Exists(Config)) { log("dishonored2Config.cfg not found yet (first start): the game may pause when it loses focus."); return; }
        var text = File.ReadAllText(Config);
        var present = text.Split('\n').Select(l => l.Trim()).Where(l => l != "").Select(l => l.Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries)[0]).ToHashSet();
        var add = new StringBuilder();
        foreach (var (name, value) in new[] { ("win_pauseOnFocusLoss", "\"0\""), ("in_controlInactiveWindow", "\"1\"") })
            if (!present.Contains(name)) add.Append($"{name} {value}\r\n");
        if (add.Length == 0) return;
        if (text.Length > 0 && !text.EndsWith('\n')) add.Insert(0, "\r\n");
        File.AppendAllText(Config, add.ToString());
        log("Prepared dishonored2Config.cfg so the game keeps running without focus.");
    }

    /// <summary>Posts one key (WM_KEYDOWN, 80 ms, WM_KEYUP) to the visible "Dishonored" window owned by the game process.</summary>
    public static bool Post(int pid, int vk)
    {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, _) =>
        {
            var title = new StringBuilder(256); GetWindowTextW(h, title, 256);
            GetWindowThreadProcessId(h, out var owner);
            if (owner == pid && title.ToString().Contains("Dishonored") && IsWindowVisible(h)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        if (found == IntPtr.Zero) return false;
        long scan = MapVirtualKeyW((uint)vk, 0);
        PostMessageW(found, 0x0100, vk, (IntPtr)(1 | scan << 16));                // WM_KEYDOWN
        Thread.Sleep(80);
        PostMessageW(found, 0x0101, vk, (IntPtr)(1 | scan << 16 | 3L << 30));     // WM_KEYUP
        return true;
    }

    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32")] static extern bool EnumWindows(EnumProc proc, IntPtr l);
    [DllImport("user32", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32")] static extern uint GetWindowThreadProcessId(IntPtr h, out int pid);
    [DllImport("user32")] static extern uint MapVirtualKeyW(uint code, uint type);
    [DllImport("user32")] static extern bool PostMessageW(IntPtr h, uint msg, nint w, IntPtr l);
}

/// <summary>Minimal PE32+ reader (tools/probe_game.py PE): machine, image size, file offsets of RVAs and direct code exports.</summary>
public sealed class Pe
{
    readonly byte[] d;
    public readonly int Machine;
    public readonly uint ImageSize;
    readonly uint headerSize, exportRva, exportSize;
    readonly List<(uint Rva, uint VSize, uint RawSize, uint RawOffset, bool Exec)> sections = new();

    public Pe(byte[] data)
    {
        d = data;
        if (U16(0) != 0x5a4d) throw new InvalidDataException("Not a Windows PE executable");
        int pe = (int)U32(0x3c);
        if (U32(pe) != 0x4550) throw new InvalidDataException("Invalid PE signature");
        Machine = U16(pe + 4); int count = U16(pe + 6), optionalSize = U16(pe + 20), opt = pe + 24;
        Bytes(opt, optionalSize);
        if (optionalSize < 112 || U16(opt) != 0x20B) throw new InvalidDataException("Expected a PE32+ executable");
        if (count is < 1 or > 96) throw new InvalidDataException("Invalid section count");
        ImageSize = U32(opt + 56); headerSize = U32(opt + 60);
        uint dirs = U32(opt + 108);
        if (headerSize > d.Length || ImageSize < headerSize) throw new InvalidDataException("Invalid PE image/header sizes");
        if (dirs * 8 > optionalSize - 112) throw new InvalidDataException("Truncated PE directory table");
        if (dirs > 0) { exportRva = U32(opt + 112); exportSize = U32(opt + 116); }
        for (int i = 0; i < count; i++)
        {
            int at = opt + optionalSize + i * 40;
            uint vsize = U32(at + 8), rva = U32(at + 12), raw = U32(at + 16), rawOff = U32(at + 20), flags = U32(at + 36);
            Bytes((int)rawOff, (int)raw);
            if ((ulong)rva + Math.Max(vsize, raw) > ImageSize) throw new InvalidDataException("Section exceeds PE image");
            sections.Add((rva, vsize, raw, rawOff, (flags & 0x20000000) != 0));
        }
    }

    public byte[] Bytes(int offset, int size)
    {
        if (offset < 0 || size < 0 || (long)offset + size > d.Length) throw new InvalidDataException("PE field lies outside file");
        return d.AsSpan(offset, size).ToArray();
    }
    ushort U16(int o) => BitConverter.ToUInt16(Bytes(o, 2));
    uint U32(int o) => BitConverter.ToUInt32(Bytes(o, 4));

    public int Offset(uint rva, int size = 1)
    {
        if (size <= 0) throw new InvalidDataException("Invalid requested RVA size");
        if ((ulong)rva + (ulong)size <= headerSize) return (int)rva;
        var m = sections.Where(s => s.Rva <= rva && (ulong)rva + (ulong)size <= (ulong)s.Rva + s.RawSize).Select(s => (int)(s.RawOffset + rva - s.Rva)).ToList();
        if (m.Count != 1) throw new InvalidDataException("Unmapped or ambiguous file-backed RVA");
        return m[0];
    }

    public bool HasExport(string name) { try { CodeExport(name); return true; } catch (InvalidDataException) { return false; } }

    /// <summary>One direct executable export RVA, rejecting forwarders.</summary>
    public uint CodeExport(string name)
    {
        if (exportRva == 0 || exportSize < 40) throw new InvalidDataException("Missing export directory");
        int dir = Offset(exportRva, 40);
        uint functions = U32(dir + 20), names = U32(dir + 24), functionTable = U32(dir + 28), nameTable = U32(dir + 32), ordinalTable = U32(dir + 36);
        if (functions is 0 or > 65536 || names is 0 or > 65536) throw new InvalidDataException("Invalid export table count");
        Offset(functionTable, (int)functions * 4); Offset(nameTable, (int)names * 4); Offset(ordinalTable, (int)names * 2);
        var found = new List<uint>();
        for (uint i = 0; i < names; i++)
        {
            uint nameRva = U32(Offset(nameTable + i * 4, 4));
            var sb = new StringBuilder(); int k = 0;
            for (; k < 512; k++) { byte c = d[Offset(nameRva + (uint)k)]; if (c == 0) break; sb.Append((char)c); }
            if (k == 512) throw new InvalidDataException("Unterminated export name");
            if (sb.ToString() != name) continue;
            uint ordinal = U16(Offset(ordinalTable + i * 2, 2));
            if (ordinal >= functions) throw new InvalidDataException("Export ordinal outside function table");
            uint target = U32(Offset(functionTable + ordinal * 4, 4));
            if ((target >= exportRva && target < exportRva + exportSize) || !sections.Any(s => s.Exec && s.Rva <= target && target < s.Rva + s.RawSize))
                throw new InvalidDataException("Export is forwarded or not executable");
            found.Add(target);
        }
        if (found.Count != 1) throw new InvalidDataException("Expected exactly one code export: " + name);
        return found[0];
    }
}
