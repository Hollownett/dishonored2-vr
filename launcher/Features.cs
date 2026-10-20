using System.Text;
using System.Text.Json;

namespace DishonoredVR;

/// <summary>One default feature knob from tools/default_features.json. Kind "enable": file present = on. Kind "disable": file present = off.
/// Default says whether Apply turns it on for a fresh home. RequiresDllKnob: skipped while the DLL does not contain that knob name.</summary>
public sealed record Feature(string Id, string File, string Kind, string Label, string Help, bool Default, string? RequiresDllKnob = null)
{
    public bool IsEnable => Kind == "enable";
}

public readonly record struct FeatureState(bool On, bool Adopted);

/// <summary>Seeds and tracks the feature knob files in the mod home's build\ folder. State lives in build\launcher-features.json
/// ({"version":1,"off":[ids],"seeded":[ids]}). Never deletes a knob file it did not write except through Set (an explicit user action).</summary>
public static class Features
{
    public const string StateName = "launcher-features.json";

    /// <summary>The state file could not be read or archived (I/O). Apply aborts without touching knobs or state.</summary>
    public sealed class FeaturesStateException(string message, Exception? inner = null) : IOException(message, inner);

    static readonly System.Text.RegularExpressions.Regex NameRe = new("^[a-z0-9-]+$");

    static string Name(JsonElement e, string key)
    {
        var v = e.GetProperty(key).GetString();
        if (v == null || !NameRe.IsMatch(v)) throw new InvalidDataException($"Bad feature {key}: {v}");
        return v;
    }

    /// <summary>Parses and validates the whole list; any bad row rejects it (InvalidDataException / JsonException / InvalidOperationException).</summary>
    public static IReadOnlyList<Feature> Load(Stream json)
    {
        using var doc = JsonDocument.Parse(json);
        var list = new List<Feature>();
        foreach (var e in doc.RootElement.GetProperty("features").EnumerateArray())
        {
            string S(string n) => e.GetProperty(n).GetString() ?? throw new InvalidDataException("Missing " + n);
            var kind = S("kind");
            if (kind != "enable" && kind != "disable") throw new InvalidDataException("Bad feature kind: " + kind);
            var def = e.GetProperty("default");
            if (def.ValueKind is not (JsonValueKind.True or JsonValueKind.False)) throw new InvalidDataException("default must be a boolean");
            string? req = null;
            if (e.TryGetProperty("requires_dll_knob", out _)) req = Name(e, "requires_dll_knob");
            list.Add(new Feature(Name(e, "id"), Name(e, "file"), kind, S("label"), S("help"), def.GetBoolean(), req));
        }
        if (list.Select(f => f.Id).Distinct().Count() != list.Count) throw new InvalidDataException("Duplicate feature id");
        return list;
    }

    sealed class StateFile
    {
        public SortedSet<string> Off = new(StringComparer.Ordinal), Seeded = new(StringComparer.Ordinal);
        public SortedDictionary<string, (string File, string Kind)> Applied = new(StringComparer.Ordinal);
    }

    static string StatePath(string buildDir) => Path.Combine(buildDir, StateName);

    static IEnumerable<string> Strings(JsonElement e)
    {
        if (e.ValueKind != JsonValueKind.Array) throw new InvalidDataException("not a list");
        foreach (var x in e.EnumerateArray())
            yield return x.ValueKind == JsonValueKind.String ? x.GetString()! : throw new InvalidDataException("not a string");
    }

    static StateFile ReadState(string buildDir, Action<string>? log)
    {
        var p = StatePath(buildDir);
        if (Directory.Exists(p)) throw new FeaturesStateException(p + " is a folder");
        if (!File.Exists(p)) return new StateFile();
        byte[] bytes;
        try { bytes = File.ReadAllBytes(p); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { throw new FeaturesStateException("Cannot read " + p + ": " + ex.Message, ex); }
        try
        {
            using var doc = JsonDocument.Parse(bytes);
            var root = doc.RootElement;
            var version = root.GetProperty("version").GetInt32();
            if (version is not (1 or 2)) throw new InvalidDataException("unknown version");
            var s = new StateFile();
            foreach (var x in Strings(root.GetProperty("off"))) s.Off.Add(x);
            foreach (var x in Strings(root.GetProperty("seeded"))) s.Seeded.Add(x);
            if (version == 2 && root.TryGetProperty("applied", out var ap))
            {
                if (ap.ValueKind != JsonValueKind.Object) throw new InvalidDataException("applied is not an object");
                foreach (var kv in ap.EnumerateObject())
                {
                    if (kv.Value.ValueKind != JsonValueKind.Object) throw new InvalidDataException("bad applied row");
                    var fl = kv.Value.GetProperty("file"); var kd = kv.Value.GetProperty("kind");
                    if (fl.ValueKind != JsonValueKind.String || kd.ValueKind != JsonValueKind.String) throw new InvalidDataException("bad applied row");
                    s.Applied[kv.Name] = (fl.GetString()!, kd.GetString()!);
                }
            }
            return s;
        }
        catch (Exception ex) when (ex is JsonException or InvalidOperationException or KeyNotFoundException or FormatException or InvalidDataException)
        {
            try { File.Move(p, p + ".bad", true); }
            catch (Exception mv) when (mv is IOException or UnauthorizedAccessException) { throw new FeaturesStateException("Cannot archive corrupt " + p + ": " + mv.Message, mv); }
            log?.Invoke("Feature state was corrupt and was moved to " + StateName + ".bad");
            return new StateFile();
        }
    }

    static void WriteState(string buildDir, StateFile s)
    {
        var applied = s.Applied.ToDictionary(kv => kv.Key, kv => new { file = kv.Value.File, kind = kv.Value.Kind });
        var text = JsonSerializer.Serialize(new { version = 2, off = s.Off.ToArray(), seeded = s.Seeded.ToArray(), applied }, new JsonSerializerOptions { WriteIndented = true }) + "\n";
        var bytes = Encoding.UTF8.GetBytes(text);
        var p = StatePath(buildDir);
        if (File.Exists(p) && File.ReadAllBytes(p).AsSpan().SequenceEqual(bytes)) return;
        var tmp = p + ".tmp";
        File.WriteAllBytes(tmp, bytes);
        File.Move(tmp, p, true);
    }

    static void Touch(string path)
    {
        if (File.Exists(path)) return;
        var tmp = path + ".tmp";
        File.WriteAllBytes(tmp, Array.Empty<byte>());
        File.Move(tmp, path, true);
    }

    const string Saved = ".off-by-launcher";

    /// <summary>Turns an enable knob off without losing a valued file (full-body holds tuning): empty files are deleted, others are set aside.</summary>
    static void Retire(string path)
    {
        if (!File.Exists(path)) return;
        if (new FileInfo(path).Length == 0) File.Delete(path);
        else File.Move(path, path + Saved, true);
    }

    static void Restore(string path)
    {
        if (File.Exists(path)) return;
        if (File.Exists(path + Saved)) File.Move(path + Saved, path);
        else Touch(path);
    }

    static void ForceOff(Feature f, string path) { if (f.IsEnable) Retire(path); else Touch(path); }

    /// <summary>On: the feature is in effect. Adopted: the id is in the state's off list (user switch or a hand-made no-knob).</summary>
    public static FeatureState State(string buildDir, IReadOnlyList<Feature> list, string id) => State(buildDir, list)[id];

    public static IReadOnlyDictionary<string, FeatureState> State(string buildDir, IReadOnlyList<Feature> list)
    {
        var s = ReadState(buildDir, null);
        return list.ToDictionary(f => f.Id, f =>
        {
            var present = File.Exists(Path.Combine(buildDir, f.File));
            return new FeatureState(f.IsEnable ? present : !present, s.Off.Contains(f.Id));
        });
    }

    /// <summary>`off` is authoritative: every listed id in `off` is made off (enable: file absent; disable: file present) whether or not
    /// it was seeded. Otherwise features not seen before (or whose file/kind changed) are seeded (default-true rows only), adopting
    /// knob files the user made. Old files of changed rows and rows removed from the list are never deleted. Returns how many features
    /// were newly recorded. knownKnobs (names in the DLL) filters rows with RequiresDllKnob; null = no filtering.
    /// Throws FeaturesStateException when the state file cannot be read; nothing is touched then.</summary>
    public static int Apply(string buildDir, IReadOnlyList<Feature> list, IReadOnlySet<string>? knownKnobs = null, Action<string>? log = null)
    {
        Directory.CreateDirectory(buildDir);
        var s = ReadState(buildDir, log);
        var before = s.Seeded.Count;
        foreach (var f in list)
        {
            if (f.RequiresDllKnob != null && knownKnobs != null && !knownKnobs.Contains(f.RequiresDllKnob)) continue;
            var path = Path.Combine(buildDir, f.File);
            var seeded = s.Seeded.Contains(f.Id);
            bool changed = seeded && s.Applied.TryGetValue(f.Id, out var ap) && (ap.File != f.File || ap.Kind != f.Kind);
            if (changed) log?.Invoke($"Feature {f.Id}: row changed ({s.Applied[f.Id].Kind} {s.Applied[f.Id].File} -> {f.Kind} {f.File}); the old file is left in place");
            if (s.Off.Contains(f.Id)) ForceOff(f, path);
            else if (!seeded || changed)
            {
                if (File.Exists(path)) { if (!f.IsEnable) s.Off.Add(f.Id); }  // user-made knob: keep; a no-knob means off
                else if (f.Default) { if (f.IsEnable) Touch(path); }
                else if (!f.IsEnable) { Touch(path); s.Off.Add(f.Id); }  // disable row that defaults off: the no-file is the switch
            }
            s.Seeded.Add(f.Id);
            s.Applied[f.Id] = (f.File, f.Kind);
        }
        WriteState(buildDir, s);
        return s.Seeded.Count - before;
    }

    /// <summary>The user's explicit switch from the launcher.</summary>
    public static void Set(string buildDir, Feature f, bool on)
    {
        Directory.CreateDirectory(buildDir);
        var s = ReadState(buildDir, null);
        var path = Path.Combine(buildDir, f.File);
        if (f.IsEnable) { if (on) Restore(path); else Retire(path); }
        else { if (on) { if (File.Exists(path)) File.Delete(path); } else Touch(path); }
        if (on) s.Off.Remove(f.Id); else s.Off.Add(f.Id);
        s.Seeded.Add(f.Id);
        s.Applied[f.Id] = (f.File, f.Kind);
        WriteState(buildDir, s);
    }

    /// <summary>Knob names are UTF-16 strings in the DLL (L"no-hand-blink"); returns every [a-z0-9-] run of 3+ characters.</summary>
    public static IReadOnlySet<string> ScanKnobs(byte[] dll)
    {
        var set = new HashSet<string>(StringComparer.Ordinal);
        var sb = new StringBuilder();
        void Flush() { if (sb.Length >= 3) set.Add(sb.ToString()); sb.Clear(); }
        for (int start = 0; start < 2; start++)  // the string table may sit at an odd offset
        {
            for (int i = start; i + 1 < dll.Length; i += 2)
            {
                char c = (char)(dll[i] | dll[i + 1] << 8);
                if (c is >= 'a' and <= 'z' or >= '0' and <= '9' or '-') sb.Append(c); else Flush();
            }
            Flush();
        }
        return set;
    }
}
