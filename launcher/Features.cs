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

    public static IReadOnlyList<Feature> Load(Stream json)
    {
        using var doc = JsonDocument.Parse(json);
        var list = new List<Feature>();
        foreach (var e in doc.RootElement.GetProperty("features").EnumerateArray())
        {
            string S(string n) => e.GetProperty(n).GetString() ?? "";
            var kind = S("kind");
            if (kind != "enable" && kind != "disable") throw new InvalidDataException("Bad feature kind: " + kind);
            list.Add(new Feature(S("id"), S("file"), kind, S("label"), S("help"), e.GetProperty("default").GetBoolean(),
                e.TryGetProperty("requires_dll_knob", out var r) ? r.GetString() : null));
        }
        return list;
    }

    sealed class StateFile
    {
        public SortedSet<string> Off = new(StringComparer.Ordinal), Seeded = new(StringComparer.Ordinal);
    }

    static string StatePath(string buildDir) => Path.Combine(buildDir, StateName);

    static StateFile ReadState(string buildDir)
    {
        var p = StatePath(buildDir);
        if (!File.Exists(p)) return new StateFile();
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllBytes(p));
            var s = new StateFile();
            foreach (var x in doc.RootElement.GetProperty("off").EnumerateArray()) s.Off.Add(x.GetString()!);
            foreach (var x in doc.RootElement.GetProperty("seeded").EnumerateArray()) s.Seeded.Add(x.GetString()!);
            return s;
        }
        catch (Exception ex) when (ex is JsonException or InvalidOperationException or KeyNotFoundException or IOException or NullReferenceException)
        {
            try { File.Move(p, p + ".bad", true); } catch (IOException) { }
            return new StateFile();
        }
    }

    static void WriteState(string buildDir, StateFile s)
    {
        var text = JsonSerializer.Serialize(new { version = 1, off = s.Off.ToArray(), seeded = s.Seeded.ToArray() }, new JsonSerializerOptions { WriteIndented = true }) + "\n";
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

    /// <summary>On: the feature is in effect. Adopted: the user switched it off (or owned a no-knob before first seeding); the launcher keeps it off.</summary>
    public static FeatureState State(string buildDir, IReadOnlyList<Feature> list, string id)
    {
        var f = list.First(x => x.Id == id);
        var present = File.Exists(Path.Combine(buildDir, f.File));
        return new FeatureState(f.IsEnable ? present : !present, ReadState(buildDir).Off.Contains(id));
    }

    public static IReadOnlyDictionary<string, FeatureState> State(string buildDir, IReadOnlyList<Feature> list)
    {
        var s = ReadState(buildDir);
        return list.ToDictionary(f => f.Id, f =>
        {
            var present = File.Exists(Path.Combine(buildDir, f.File));
            return new FeatureState(f.IsEnable ? present : !present, s.Off.Contains(f.Id));
        });
    }

    /// <summary>Seeds features not seen before (default-true rows only) and adopts knob files the user already made. Returns how many
    /// features it newly recorded. knownKnobs (names found in the DLL) filters rows with RequiresDllKnob; null = no filtering.</summary>
    public static int Apply(string buildDir, IReadOnlyList<Feature> list, IReadOnlySet<string>? knownKnobs = null)
    {
        Directory.CreateDirectory(buildDir);
        var s = ReadState(buildDir);
        var before = s.Seeded.Count;
        foreach (var f in list)
        {
            if (f.RequiresDllKnob != null && knownKnobs != null && !knownKnobs.Contains(f.RequiresDllKnob)) continue;
            if (s.Seeded.Contains(f.Id)) continue;
            var path = Path.Combine(buildDir, f.File);
            if (File.Exists(path))
            {
                if (!f.IsEnable) s.Off.Add(f.Id);  // user-made no-knob: keep it, remember the feature is off
            }
            else if (f.Default)
            {
                if (f.IsEnable) Touch(path);
            }
            else if (!f.IsEnable) { Touch(path); s.Off.Add(f.Id); }  // disable row that defaults off: the no-file is the off switch
            else s.Off.Add(f.Id);
            s.Seeded.Add(f.Id);
        }
        // A feature the user turned off stays off: if its enable file reappears nothing here removes it (only Set does).
        WriteState(buildDir, s);
        return s.Seeded.Count - before;
    }

    /// <summary>The user's explicit switch from the launcher.</summary>
    public static void Set(string buildDir, Feature f, bool on)
    {
        Directory.CreateDirectory(buildDir);
        var s = ReadState(buildDir);
        var path = Path.Combine(buildDir, f.File);
        var wantFile = f.IsEnable ? on : !on;
        if (wantFile) Touch(path);
        else if (File.Exists(path)) File.Delete(path);
        if (on) s.Off.Remove(f.Id); else s.Off.Add(f.Id);
        s.Seeded.Add(f.Id);
        WriteState(buildDir, s);
    }

    /// <summary>Knob names are UTF-16 strings in the DLL (L"no-hand-blink"); returns every [a-z0-9-] run of 3+ characters.</summary>
    public static IReadOnlySet<string> ScanKnobs(byte[] dll)
    {
        var set = new HashSet<string>(StringComparer.Ordinal);
        var sb = new StringBuilder();
        void Flush() { if (sb.Length >= 3) set.Add(sb.ToString()); sb.Clear(); }
        for (int i = 0; i + 1 < dll.Length; i += 2)
        {
            char c = (char)(dll[i] | dll[i + 1] << 8);
            if (c is >= 'a' and <= 'z' or >= '0' and <= '9' or '-') sb.Append(c); else Flush();
        }
        Flush();
        return set;
    }
}
