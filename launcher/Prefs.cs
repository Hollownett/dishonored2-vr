using System.Globalization;
using System.Text;

namespace DishonoredVR;

/// <summary>Saved settings, byte-compatible with src/vr_preferences.hpp (version 9). Measurement tokens are preserved.</summary>
public sealed class Prefs
{
    public double Resolution = 1, Sharpness = .5, SnapDeg = 45, SmoothDps = 90;
    public int Aa = 1, Mode = 1, Mirror = 0, TemporalAa = 2, Hud = 65, Hint = 1;
    /// <summary>1 once the player (or the game menu) has picked an AA mode; Crosshair 0 hides the game crosshair.</summary>
    public int AaChosen = 0, Crosshair = 1;
    public string[] Measurements = { "0", "0", "0", "0", "0" };

    public static Prefs Load(string file)
    {
        var p = new Prefs();
        if (!File.Exists(file)) return p;
        var t = File.ReadAllText(file).Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries);
        var inv = CultureInfo.InvariantCulture;
        if (t.Length < 6 || !int.TryParse(t[0], out var v) || v < 1 || v > 9)
            throw new InvalidDataException("Saved settings file is not a recognised version (1-9). It was left untouched.");
        double D(int i) => double.Parse(t[i], inv);
        int I(int i) => int.Parse(t[i], inv);
        p.Resolution = D(1); p.Aa = I(2); p.Mode = I(3); p.SnapDeg = D(4); p.SmoothDps = D(5);
        if (v >= 2) { p.Mirror = I(6); p.Sharpness = D(7); }
        p.TemporalAa = v >= 5 && t.Length > 8 ? I(8) : 2;
        if (v >= 4 && t.Length > 10) { p.Hud = v >= 6 ? I(9) : 65; p.Hint = I(10); }
        if (v >= 7 && t.Length >= 16) p.Measurements = t[11..16];
        if (v >= 9)
        {
            if (t.Length < 18 || !int.TryParse(t[16], out var ac) || !int.TryParse(t[17], out var ch) || ac is < 0 or > 1 || ch is < 0 or > 1)
                throw new InvalidDataException("Saved settings file is damaged (version 9 needs the anti-aliasing and crosshair flags). It was left untouched.");
            p.AaChosen = ac; p.Crosshair = ch;
        }
        return p;
    }

    /// <summary>Disk copy with every field the user changed on the form (edited vs baseline) laid over it, so game-side changes survive a save.</summary>
    public static Prefs Merge(Prefs baseline, Prefs edited, Prefs disk)
    {
        var m = new Prefs
        {
            Resolution = disk.Resolution, Sharpness = disk.Sharpness, SnapDeg = disk.SnapDeg, SmoothDps = disk.SmoothDps,
            Aa = disk.Aa, Mode = disk.Mode, Mirror = disk.Mirror, TemporalAa = disk.TemporalAa, Hud = disk.Hud, Hint = disk.Hint,
            AaChosen = disk.AaChosen, Crosshair = disk.Crosshair, Measurements = (string[])disk.Measurements.Clone(),
        };
        if (edited.Resolution != baseline.Resolution) m.Resolution = edited.Resolution;
        if (edited.Sharpness != baseline.Sharpness) m.Sharpness = edited.Sharpness;
        if (edited.SnapDeg != baseline.SnapDeg) m.SnapDeg = edited.SnapDeg;
        if (edited.SmoothDps != baseline.SmoothDps) m.SmoothDps = edited.SmoothDps;
        if (edited.Aa != baseline.Aa) m.Aa = edited.Aa;
        if (edited.Mode != baseline.Mode) m.Mode = edited.Mode;
        if (edited.Mirror != baseline.Mirror) m.Mirror = edited.Mirror;
        if (edited.Hud != baseline.Hud) m.Hud = edited.Hud;
        if (edited.Hint != baseline.Hint) m.Hint = edited.Hint;
        if (edited.Crosshair != baseline.Crosshair) m.Crosshair = edited.Crosshair;
        if (edited.TemporalAa != baseline.TemporalAa) { m.TemporalAa = edited.TemporalAa; m.AaChosen = 1; }
        return m;
    }

    public string Validate()
    {
        if (Resolution < .5 || Resolution > 2) return "Resolution must be 0.5 to 2.0.";
        if (Mirror is < 0 or > 3) return "Monitor view must be right, left, both eyes or off.";
        if (Sharpness < 0 || Sharpness > 1) return "Sharpening must be 0 to 1.";
        if (Hud < 50 || Hud > 200) return "HUD size must be 50 to 200 percent.";
        if (TemporalAa is < 0 or > 8) return "Anti-aliasing mode must be 0 to 8.";
        if (Mode is < 0 or > 2) return "Turning mode must be native, snap or smooth.";
        if (SmoothDps < 15 || SmoothDps > 360) return "Smooth turn speed must be 15 to 360 degrees/s.";
        if (AaChosen is < 0 or > 1) return "Anti-aliasing chosen flag must be 0 or 1.";
        if (Crosshair is < 0 or > 1) return "Crosshair must be on or off.";
        return "";
    }

    public void Save(string file)
    {
        var e = Validate(); if (e != "") throw new InvalidDataException(e);
        var inv = CultureInfo.InvariantCulture;
        string F(double d) => d.ToString("R", inv);
        var head = new[] { "9", F(Resolution), Aa.ToString(), Mode.ToString(), F(SnapDeg), F(SmoothDps), Mirror.ToString(), F(Sharpness),
            TemporalAa.ToString(), Hud.ToString(), Hint.ToString() };
        var line = string.Join(' ', head.Concat(Measurements).Concat(new[] { AaChosen.ToString(), Crosshair.ToString() })) + "\n";
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        var tmp = file + ".tmp";
        File.WriteAllText(tmp, line, new UTF8Encoding(false));
        File.Move(tmp, file, true);
    }
}
