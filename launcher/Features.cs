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

    static readonly System.Text.RegularExpressions.Regex NameRe = new(@"^[a-z0-9-]+\z");

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
        if (list.Select(f => f.File).Distinct().Count() != list.Count) throw new InvalidDataException("Duplicate feature file");
        return list;
    }

    /// <summary>What Apply/Set last did for an id: its file and kind, and whether it left the feature off (null = unknown, v1/early v2 states).</summary>
    readonly record struct Applied(string File, string Kind, bool? Off);

    sealed class StateFile
    {
        public SortedSet<string> Off = new(StringComparer.Ordinal), Seeded = new(StringComparer.Ordinal);
        public SortedDictionary<string, Applied> Applied = new(StringComparer.Ordinal);
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
            var vEl = root.GetProperty("version");
            if (vEl.ValueKind != JsonValueKind.Number || !System.Text.RegularExpressions.Regex.IsMatch(vEl.GetRawText(), @"^-?[0-9]+\z")) throw new InvalidDataException("version is not an integer");
            // A well-formed state of another version (any integer outside 1 and 2) is not corruption: abort rather than archive and re-seed.
            var version = vEl.TryGetInt64(out var v64) ? v64 : -1;
            if (version is not (1 or 2)) throw new FeaturesStateException($"{p} has unknown version {vEl.GetRawText()}");
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
                    bool? off = null;
                    if (kv.Value.TryGetProperty("off", out var of) && of.ValueKind != JsonValueKind.Null)  // null = unknown, as absent
                        off = of.ValueKind is JsonValueKind.True or JsonValueKind.False ? of.GetBoolean() : throw new InvalidDataException("bad applied off");
                    s.Applied[kv.Name] = new Applied(fl.GetString()!, kd.GetString()!, off);
                }
            }
            return s;
        }
        catch (Exception ex) when (ex is JsonException or InvalidOperationException or KeyNotFoundException or FormatException or InvalidDataException)
        {
            try { File.Move(p, FreeSlot(p + ".bad"), false); }
            catch (Exception mv) when (mv is IOException or UnauthorizedAccessException) { throw new FeaturesStateException("Cannot archive corrupt " + p + ": " + mv.Message, mv); }
            log?.Invoke("Feature state was corrupt and was moved to " + StateName + ".bad");
            return new StateFile();
        }
    }

    static void WriteState(string buildDir, StateFile s)
    {
        var applied = s.Applied.ToDictionary(kv => kv.Key, kv =>
        {
            var row = new Dictionary<string, object> { ["file"] = kv.Value.File, ["kind"] = kv.Value.Kind };
            if (kv.Value.Off is bool b) row["off"] = b;  // never write null
            return row;
        });
        var text = JsonSerializer.Serialize(new { version = 2, off = s.Off.ToArray(), seeded = s.Seeded.ToArray(), applied }, new JsonSerializerOptions { WriteIndented = true }) + "\n";
        var bytes = Encoding.UTF8.GetBytes(text);
        var p = StatePath(buildDir);
        if (File.Exists(p))
        {
            byte[] old;
            try { old = File.ReadAllBytes(p); }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException) { throw new FeaturesStateException("Cannot read " + p + ": " + ex.Message, ex); }
            if (old.AsSpan().SequenceEqual(bytes)) return;
        }
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

    static readonly System.Text.RegularExpressions.Regex SavedRe = new(@"^\.off-by-launcher(\.([0-9]+))?\z");

    /// <summary>Saved copies of a file in order: `.off-by-launcher` (0), `.off-by-launcher.1`, ...</summary>
    static List<(int N, string Path)> SavedCopies(string path)
    {
        var dir = Path.GetDirectoryName(path)!; var name = Path.GetFileName(path);
        var list = new List<(int, string)>();
        if (!Directory.Exists(dir)) return list;
        foreach (var f in Directory.GetFiles(dir, name + ".off-by-launcher*"))
        {
            var m = SavedRe.Match(Path.GetFileName(f).Substring(name.Length));
            if (m.Success) list.Add((m.Groups[2].Success ? int.Parse(m.Groups[2].Value) : 0, f));
        }
        return list.OrderBy(x => x.Item1).ToList();
    }

    static string FreeSlot(string baseName)
    {
        var c = baseName;
        for (int i = 1; File.Exists(c); i++) c = baseName + "." + i;
        return c;
    }

    static string FreeSavedSlot(string path) => FreeSlot(path + Saved);

    /// <summary>Test hook: runs just before a no-clobber move so a test can create the destination (a race).</summary>
    internal static Action<string>? BeforeMove;

    /// <summary>Moves without replacing; a destination that appears first is a typed error (nothing is lost, a retry converges).</summary>
    static void MoveNoClobber(string src, string dst)
    {
        BeforeMove?.Invoke(dst);
        try { File.Move(src, dst, false); }
        catch (IOException) when (File.Exists(dst)) { throw new FeaturesStateException($"{dst} already exists; {src} was left in place"); }
    }

    /// <summary>Turns an enable knob off without losing a valued file (full-body holds tuning): empty files are deleted, others are set
    /// aside in the first free `.off-by-launcher[.N]` slot (an earlier saved copy is never overwritten).</summary>
    static void Retire(string path)
    {
        if (!File.Exists(path)) return;
        if (new FileInfo(path).Length == 0) File.Delete(path);
        else MoveNoClobber(path, FreeSavedSlot(path));
    }

    /// <summary>Brings the newest saved copy back (or creates an empty file); an existing file is never replaced.</summary>
    static void Restore(string path)
    {
        if (File.Exists(path)) return;
        var saved = SavedCopies(path);
        // newest by modification time; ties go to the highest slot number
        if (saved.Count > 0) MoveNoClobber(saved.OrderBy(x => File.GetLastWriteTimeUtc(x.Path)).ThenBy(x => x.N).Last().Path, path);
        else Touch(path);
    }

    /// <summary>A renamed row: carry the old name's saved copies over to free slots of the new name.</summary>
    static void MigrateSaved(string oldPath, string newPath)
    {
        foreach (var (_, p) in SavedCopies(oldPath)) MoveNoClobber(p, FreeSavedSlot(newPath));
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
            Applied ap = default;
            bool changed = seeded && s.Applied.TryGetValue(f.Id, out ap) && (ap.File != f.File || ap.Kind != f.Kind);
            bool wasOff = s.Applied.TryGetValue(f.Id, out var prev) && prev.Off is bool po ? po : s.Off.Contains(f.Id);
            if (changed)
            {
                log?.Invoke($"Feature {f.Id}: row changed ({ap.Kind} {ap.File} -> {f.Kind} {f.File}); the old file is left in place");
                if (ap.Kind == "enable" && f.Kind == "enable" && ap.File != f.File) MigrateSaved(Path.Combine(buildDir, ap.File), path);
            }
            if (s.Off.Contains(f.Id)) ForceOff(f, path);
            else if (!seeded || changed)
            {
                if (File.Exists(path)) { if (!f.IsEnable) s.Off.Add(f.Id); }  // user-made knob: keep; a no-knob means off
                else if (f.Default) { if (f.IsEnable) Touch(path); }
                else if (!f.IsEnable) { Touch(path); s.Off.Add(f.Id); }  // disable row that defaults off: the no-file is the switch
            }
            else if (wasOff)  // the id left `off` since the last run: turn the feature back on
            {
                if (f.IsEnable) Restore(path); else if (File.Exists(path)) File.Delete(path);
            }
            s.Seeded.Add(f.Id);
            s.Applied[f.Id] = new Applied(f.File, f.Kind, s.Off.Contains(f.Id));
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
        s.Applied[f.Id] = new Applied(f.File, f.Kind, !on);
        WriteState(buildDir, s);
    }

    /// <summary>Set by id; an id not in the list is an ArgumentException (nothing is touched).</summary>
    public static void Set(string buildDir, IReadOnlyList<Feature> list, string id, bool on)
    {
        var f = list.FirstOrDefault(x => x.Id == id) ?? throw new ArgumentException("Unknown feature id: " + id, nameof(id));
        Set(buildDir, f, on);
    }

    /// <summary>The launcher's "Restore defaults": every feature goes to its Default (a default-off row such as power-gestures is switched off, not on).
    /// Nothing is deleted: a valued knob file turned off is set aside as a saved copy and comes back when the feature is switched on again.
    /// Then Apply seeds anything not yet recorded. Throws FeaturesStateException like Apply.</summary>
    public static void RestoreDefaults(string buildDir, IReadOnlyList<Feature> list, IReadOnlySet<string>? knownKnobs = null, Action<string>? log = null)
    {
        Directory.CreateDirectory(buildDir);
        ReadState(buildDir, log);  // surface an unreadable state before touching any knob
        foreach (var f in list)
            if (f.RequiresDllKnob == null || knownKnobs == null || knownKnobs.Contains(f.RequiresDllKnob)) Set(buildDir, f, f.Default);
        Apply(buildDir, list, knownKnobs, log);
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
