using System.Buffers.Binary;
using System.Drawing.Imaging;
using System.IO.Compression;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;

namespace DishonoredVR;

/// <summary>Port of tools/extract_game_art.py: reads the game's UI art from the user's own install (read-only) into
/// artifacts\game-art\ as &lt;key&gt;.png (launcher), &lt;key&gt;.rgba (u32 width, u32 height, straight RGBA8; the DLL's
/// src/game_art.hpp) and manifest.json. Never commit or redistribute the output. The contact sheet is not written.</summary>
public static class ArtImport
{
    const string Gui = "generated/image/models/guis/";
    // key, resource, Iggy texture id (null = bimage7), expected size, note; same table as extract_game_art.py PICKS
    static readonly (string Key, string Res, int? Id, int W, int H, string Note)[] Picks =
    {
        ("key_art", Gui + "loading_screens/loading_mainmenu_is.bimage7", null, 1920, 1080, "Main-menu loading screen: split Corvo mask / Emily key art"),
        ("logo", "ui/main_menu.iggy", 54, 1472, 240, "DISHONORED 2 wordmark from the title screen"),
        ("portrait_corvo", "ui/corvo_pause_menu.iggy", 101, 572, 940, "Pause-menu portrait (Corvo mask)"),
        ("portrait_emily", "ui/emily_pause_menu.iggy", 110, 764, 844, "Pause-menu portrait (Emily profile)"),
        ("menu_background", "ui/lib_common.iggy", 139, 552, 700, "Dark ink panel behind menu lists"),
        ("highlight_brush", "ui/lib_widgets.iggy", 99, 164, 44, "White brush-stroke list highlight. The pause-menu blade highlight was not found as a bitmap (likely vector art in the movie)"),
        ("arrow_right", "ui/lib_widgets.iggy", 122, 24, 36, "Selector arrow"),
        ("arrow_left", "ui/lib_widgets.iggy", 122, 24, 36, "Horizontal mirror of arrow_right; no separate left bitmap"),
        ("slider_track", "ui/lib_widgets.iggy", 54, 364, 16, "Rounded track bar; picked by inspection"),
        ("slider_knob", "ui/lib_widgets.iggy", 61, 60, 48, "Lens-shaped handle; picked by inspection"),
        ("divider", "ui/global.iggy", 60, 592, 32, "Ornamental divider with centre diamond"),
        ("frame", "ui/lib_widgets.iggy", 85, 380, 44, "Outlined option-row frame"),
        ("portrait_corvo_red", "ui/corvo_pause_menu.iggy", 45, 572, 944, "Red variant (game-over state)"),
        ("portrait_emily_red", "ui/emily_pause_menu.iggy", 52, 764, 844, "Red variant (game-over state)"),
        ("character_corvo", "ui/lib_journal_charms.iggy", 122, 468, 636, "Painted unmasked Corvo"),
        ("character_emily", "ui/lib_journal_charms.iggy", 124, 468, 636, "Painted masked Emily"),
        ("select_corvo", "ui/main_menu.iggy", 88, 288, 596, "Main-menu character art (Corvo mask)"),
        ("select_emily", "ui/main_menu.iggy", 91, 520, 568, "Main-menu character art (Emily profile)"),
        ("key_art_banner", "ui/global.iggy", 57, 860, 352, "Emily and Corvo banner"),
        ("story_art", "ui/main_menu.iggy", 160, 528, 216, "Painted fight scene"),
        ("menu_vignette", "ui/main_menu.iggy", 19, 1920, 1080, "Full-screen dark vignette frame"),
        ("highlight_glow_bar", "ui/lib_common.iggy", 22, 384, 180, "Glowing white selection bar"),
        ("outsider_mark", "ui/lib_journal_powers.iggy", 159, 616, 584, "The Outsider's mark"),
        ("parchment", "ui/lib_diary.iggy", 21, 456, 684, "Folded parchment page"),
        ("sketch_corvo", "ui/lib_diary.iggy", 24, 616, 824, "Parchment with Corvo sketch"),
        ("sketch_emily", "ui/lib_diary.iggy", 25, 616, 824, "Parchment with Emily sketch"),
        ("dark_page", "ui/lib_diary.iggy", 8, 612, 844, "Dark leather book page"),
        ("ink_flourish", "ui/lib_common.iggy", 16, 1000, 408, "Ink flourish ornament"),
        ("ink_splatter", "ui/lib_particles.iggy", 54, 800, 800, "Blood-ink splatter"),
        ("paper_texture", "ui/lib_particles.iggy", 5, 512, 512, "Dark paper grain overlay"),
        ("glass_shard", "ui/lib_particles.iggy", 100, 160, 160, "Pause-menu glass shard"),
        ("bonecharm", "ui/lib_journal_charms.iggy", 144, 148, 148, "Bonecharm bone fragments"),
        ("power_glyph", "ui/lib_journal_charms.iggy", 42, 136, 136, "Blue power/rune glyph"),
        ("crosshair_dot", "ui/hud.iggy", 926, 8, 8, "HUD crosshair dot (neutral state, glowing pale dot); the mod's in-eye reticle"),
        ("crosshair_halo", "ui/hud.iggy", 922, 12, 12, "HUD crosshair dark contrast halo drawn under the dot"),
    };
    public static int Count => Picks.Length;

    /// <summary>Extracts every key; progress(done, total). Returns the number written; failed keys go to the manifest's "missing".</summary>
    public static int Run(string game, string outDir, Action<string> log, Action<int, int>? progress = null)
    {
        game = Path.GetFullPath(game); outDir = Path.GetFullPath(outDir);
        if ((outDir + "\\").StartsWith(game.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase)) throw new IOException("Refusing to write inside the game folder");
        var wanted = new HashSet<string>();
        foreach (var p in Picks) { wanted.Add(p.Res); if (p.Id != null) wanted.Add(RawName(p.Res)); }
        var containers = new Containers(game, wanted);
        var cache = new Dictionary<string, byte[]>();
        byte[] Load(string n) => cache.TryGetValue(n, out var b) ? b : cache[n] = containers.Read(n);
        Directory.CreateDirectory(outDir);
        var keys = new List<(string, Dictionary<string, object>)>(); var missing = new List<(string, string)>();
        for (int i = 0; i < Picks.Length; i++)
        {
            var (key, res, id, w, h, note) = Picks[i];
            progress?.Invoke(i, Picks.Length);
            byte[] rgba; string source;
            try
            {
                int iw, ih;
                if (id == null) { (rgba, iw, ih) = Bimage(Load(res)); source = res; }
                else { (rgba, iw, ih) = IggyTexture(Load(res), Load(RawName(res)), id.Value); source = $"{RawName(res)}#{id}"; }
                if (iw != w || ih != h) throw new InvalidDataException($"expected ({w}, {h}), decoded ({iw}, {ih})");
                bool opaque = false; for (int k = 3; k < rgba.Length && !opaque; k += 4) opaque = rgba[k] != 0;
                if (!opaque) throw new InvalidDataException("fully transparent");
            }
            catch (Exception ex) when (ex is InvalidDataException or IOException or ArgumentException)
            {
                missing.Add((key, $"{res}: {ex.Message}")); log($"{key,-20} MISSING {res}: {ex.Message}"); continue;
            }
            if (key == "arrow_left") FlipX(rgba, w, h);  // extract_game_art.py MIRRORED; no picked image exceeds MAX_SIDE 2048
            var png = Png(rgba, w, h);
            var raw = new byte[8 + rgba.Length];
            BinaryPrimitives.WriteUInt32LittleEndian(raw, (uint)w); BinaryPrimitives.WriteUInt32LittleEndian(raw.AsSpan(4), (uint)h);
            rgba.CopyTo(raw, 8);
            WriteIfChanged(Path.Combine(outDir, key + ".png"), png); WriteIfChanged(Path.Combine(outDir, key + ".rgba"), raw);
            keys.Add((key, new() { ["file"] = key + ".png", ["raw"] = key + ".rgba", ["source"] = source, ["width"] = w, ["height"] = h,
                ["sha256"] = Hex(SHA256.HashData(png)), ["raw_sha256"] = Hex(SHA256.HashData(raw)), ["note"] = note }));
            log($"{key,-20} {w,5}x{h,-5} {source}");
        }
        progress?.Invoke(Picks.Length, Picks.Length);
        using var ms = new MemoryStream();
        using (var j = new Utf8JsonWriter(ms, new JsonWriterOptions { Indented = true, Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping }))
        {
            j.WriteStartObject();
            j.WriteString("game", game);
            j.WriteString("notice", "Extracted from the local game install. Do not commit or redistribute.");
            j.WriteStartObject("keys");
            foreach (var (key, e) in keys)
            {
                j.WriteStartObject(key);
                foreach (var (k, v) in e) { if (v is int n) j.WriteNumber(k, n); else j.WriteString(k, (string)v); }
                j.WriteEndObject();
            }
            j.WriteEndObject();
            j.WriteStartObject("missing"); foreach (var (k, v) in missing) j.WriteString(k, v); j.WriteEndObject();
            j.WriteEndObject();
        }
        WriteIfChanged(Path.Combine(outDir, "manifest.json"), ms.ToArray().Concat("\n"u8.ToArray()).ToArray());
        log($"{keys.Count} keys written to {outDir} (manifest.json); do not commit or redistribute.");
        return keys.Count;
    }

    /// <summary>Headless check: extracts into a temporary folder and compares every .rgba with a reference folder written by
    /// tools/extract_game_art.py. Returns 0 when every key matches within 1 LSB per channel.</summary>
    public static int Check(string reference, string? scratch, TextWriter o)
    {
        var game = Env.FindGameDir(); if (game == null) { o.WriteLine("Dishonored 2 not found"); return 2; }
        var tmp = Path.Combine(scratch ?? Path.GetTempPath(), "d2vr-art-check-" + Environment.ProcessId);
        try
        {
            var sw = System.Diagnostics.Stopwatch.StartNew();
            Run(game, tmp, _ => { });
            o.WriteLine($"extracted in {sw.ElapsedMilliseconds} ms");
            int bad = 0, compared = 0;
            foreach (var p in Picks)
            {
                var mine = Path.Combine(tmp, p.Key + ".rgba"); var theirs = Path.Combine(reference, p.Key + ".rgba");
                if (!File.Exists(theirs)) { o.WriteLine($"{p.Key,-20} no reference"); continue; }
                if (!File.Exists(mine)) { o.WriteLine($"{p.Key,-20} FAIL not extracted"); bad++; continue; }
                byte[] a = File.ReadAllBytes(mine), b = File.ReadAllBytes(theirs);
                int maxDiff = a.Length != b.Length ? 999 : 0, diffs = 0;
                if (a.Length == b.Length) for (int i = 0; i < a.Length; i++) { int d = Math.Abs(a[i] - b[i]); if (d > 0) diffs++; maxDiff = Math.Max(maxDiff, d); }
                compared++; if (maxDiff > 1) bad++;
                o.WriteLine($"{p.Key,-20} {(maxDiff == 0 ? "exact" : maxDiff <= 1 ? "within 1 LSB" : "FAIL")} max diff {maxDiff}, {diffs} bytes differ");
            }
            o.WriteLine($"{compared} compared, {bad} failed");
            return bad == 0 && compared > 0 ? 0 : 1;
        }
        finally { try { Directory.Delete(tmp, true); } catch (IOException) { } }
    }

    static string RawName(string iggy) => iggy[..^".iggy".Length] + ".raw.iggytex";
    internal static string Hex(byte[] b) => Convert.ToHexString(b).ToLowerInvariant();
    internal static void WriteIfChanged(string path, byte[] data) { if (!File.Exists(path) || !File.ReadAllBytes(path).AsSpan().SequenceEqual(data)) File.WriteAllBytes(path, data); }

    static void FlipX(byte[] p, int w, int h)
    {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w / 2; x++)
                for (int c = 0; c < 4; c++) (p[(y * w + x) * 4 + c], p[(y * w + w - 1 - x) * 4 + c]) = (p[(y * w + w - 1 - x) * 4 + c], p[(y * w + x) * 4 + c]);
    }

    internal static byte[] Png(byte[] rgba, int w, int h)
    {
        using var bmp = new Bitmap(w, h, PixelFormat.Format32bppArgb);
        var d = bmp.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        var row = new byte[w * 4];
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w * 4; x += 4) { int s = y * w * 4 + x; row[x] = rgba[s + 2]; row[x + 1] = rgba[s + 1]; row[x + 2] = rgba[s]; row[x + 3] = rgba[s + 3]; }
            Marshal.Copy(row, 0, d.Scan0 + y * d.Stride, row.Length);
        }
        bmp.UnlockBits(d);
        using var ms = new MemoryStream(); bmp.Save(ms, ImageFormat.Png); return ms.ToArray();
    }

    // ---- containers (tools/extract_arm_assets.py; reference https://github.com/chipolux/disrev/blob/main/resources.py)
    internal sealed class Containers
    {
        readonly string baseDir;
        readonly Dictionary<string, HashSet<(string C, int Res, long Off, int Packed, int Size)>> entries = new();

        public Containers(string game, HashSet<string> wanted) : this(game, wanted.Contains) { }
        /// <summary>Indexes every destination name (normalised: lower case, forward slashes) that wanted accepts.</summary>
        public Containers(string game, Func<string, bool> wanted)
        {
            baseDir = Path.Combine(game, "base");
            foreach (var c in new[] { "game1", "game2", "game3", "game4" })
            {
                var index = Path.Combine(baseDir, c + "_patch.index");
                if (!File.Exists(index)) throw new IOException($"Missing final patch index {index}");
                var d = File.ReadAllBytes(index); int p = 0;
                ReadOnlySpan<byte> Take(byte[] d, ref int p, int n) { if (n < 0 || p + n > d.Length) throw new InvalidDataException("Truncated resource data"); var s = d.AsSpan(p, n); p += n; return s; }
                if (!Take(d, ref p, 32)[..4].SequenceEqual("\x05SER"u8)) throw new InvalidDataException("Unexpected index magic");
                uint count = BinaryPrimitives.ReadUInt32BigEndian(Take(d, ref p, 4));
                if (count > 250000) throw new InvalidDataException("Excessive resource entry count");
                for (uint i = 0; i < count; i++)
                {
                    p += 4;  // id
                    string dest = "";
                    for (int k = 0; k < 3; k++)
                    {
                        int size = (int)BinaryPrimitives.ReadUInt32LittleEndian(Take(d, ref p, 4));
                        if (size > 4096) throw new InvalidDataException("Excessive resource name");
                        dest = Encoding.UTF8.GetString(Take(d, ref p, size));  // type, source, destination: the last one counts
                    }
                    var f = Take(d, ref p, 26);
                    long off = (long)BinaryPrimitives.ReadUInt64BigEndian(f);
                    int sz = (int)BinaryPrimitives.ReadUInt32BigEndian(f[8..]), packed = (int)BinaryPrimitives.ReadUInt32BigEndian(f[12..]);
                    int res = BinaryPrimitives.ReadUInt16BigEndian(f[24..]) >> 2;
                    var name = dest.Replace('\\', '/').ToLowerInvariant();
                    if (sz != 0 && wanted(name))
                    {
                        if (!entries.TryGetValue(name, out var set)) entries[name] = set = new();
                        set.Add((c, res, off, packed, sz));
                    }
                }
            }
        }

        public bool Has(string name) => entries.ContainsKey(name);

        public byte[] Read(string name)
        {
            if (!entries.TryGetValue(name, out var set)) throw new InvalidDataException($"{name} is not in the final patch indexes");
            byte[]? result = null;
            foreach (var e in set)
            {
                var file = ResourcePath(e.C, e.Res);
                using var s = File.OpenRead(file);
                if (e.Off < 4 || e.Off + e.Packed > s.Length) throw new InvalidDataException($"{name} lies outside its container");
                var magic = new byte[4]; s.ReadExactly(magic);
                if (!magic.AsSpan().SequenceEqual("\x04SER"u8)) throw new InvalidDataException("Unexpected resource magic");
                if (e.Packed <= 0 || e.Packed > 32 << 20 || e.Size <= 0 || e.Size > 32 << 20) throw new InvalidDataException("Excessive or empty asset");
                var packed = new byte[e.Packed]; s.Position = e.Off; s.ReadExactly(packed);
                var data = packed;
                if (e.Packed != e.Size)
                {
                    data = new byte[e.Size];
                    using var z = new ZLibStream(new MemoryStream(packed), CompressionMode.Decompress);
                    try { z.ReadExactly(data); } catch (EndOfStreamException) { throw new InvalidDataException("Resource decompression does not match the declared bounded size"); }
                    if (z.ReadByte() != -1) throw new InvalidDataException("Resource decompression does not match the declared bounded size");
                }
                if (result != null && !result.AsSpan().SequenceEqual(data)) throw new InvalidDataException($"Conflicting copies of {name} across containers");
                result = data;
            }
            return result!;
        }

        string ResourcePath(string container, int id)
        {
            if (id == 8192) return Path.Combine(baseDir, "shared_2_3.sharedrsc");
            if (id is < 0 or > 6) throw new InvalidDataException("Unknown resource file selector");
            return Path.Combine(baseDir, container + (id == 0 ? "" : id == 6 ? "_patch" : $"_{id:000}") + ".resources");
        }
    }

    // ---- Iggy movie external textures and bimage7 (formats documented in extract_game_art.py)
    static (byte[], int, int) IggyTexture(byte[] movie, byte[] raw, int textureId)
    {
        var m = movie.AsSpan();
        if (m.Length < 0x20 || !m[..4].SequenceEqual(new byte[] { 0x49, 0x67, 0x0a, 0xed })) throw new InvalidDataException("Not an Iggy movie");  // "Ig\n\xed"
        uint count = BinaryPrimitives.ReadUInt32LittleEndian(m[0x1c..]);
        if (count > 64 || 0x20 + 16 * count > m.Length) throw new InvalidDataException("Bad Iggy subfile table");
        (int off, int size)? table = null;
        for (int i = 0; i < count; i++)
        {
            var e = m[(0x20 + 16 * i)..];
            if (BinaryPrimitives.ReadUInt32LittleEndian(e) == 8) table = ((int)BinaryPrimitives.ReadUInt32LittleEndian(e[12..]), (int)BinaryPrimitives.ReadUInt32LittleEndian(e[4..]));
        }
        if (table == null) throw new InvalidDataException("Movie has no external texture table");
        var (offset, tsize) = table.Value;
        if (offset < 0 || tsize < 16 || (long)offset + tsize > m.Length || (tsize - 16) % 12 != 0) throw new InvalidDataException("Bad Iggy texture table");
        uint total = BinaryPrimitives.ReadUInt32LittleEndian(m[(offset + 4)..]);
        if (total != raw.Length) throw new InvalidDataException("Iggy texture table does not match raw.iggytex size");
        var recs = new List<(uint o, int fmt, int w, int h, int id)>();
        for (int p = offset + 16; p < offset + tsize; p += 12)
            recs.Add((BinaryPrimitives.ReadUInt32LittleEndian(m[p..]), BinaryPrimitives.ReadUInt16LittleEndian(m[(p + 4)..]),
                BinaryPrimitives.ReadUInt16LittleEndian(m[(p + 6)..]), BinaryPrimitives.ReadUInt16LittleEndian(m[(p + 8)..]), BinaryPrimitives.ReadUInt16LittleEndian(m[(p + 10)..])));
        recs.Sort();
        for (int i = 0; i < recs.Count; i++)
        {
            var r = recs[i]; long next = i + 1 < recs.Count ? recs[i + 1].o : total;
            int bpp = r.fmt == 0x10d ? 1 : r.fmt == 0x100 ? 4 : 0;
            if (bpp == 0 || r.o + (long)r.w * r.h * bpp != next) throw new InvalidDataException("Iggy texture records are not contiguous");
        }
        var match = recs.Where(r => r.id == textureId).ToList();
        if (match.Count != 1) throw new InvalidDataException($"Iggy texture {textureId} not found");
        var (o, fmt, w, h, _) = match[0];
        var px = fmt == 0x10d ? Bc.Decode(raw.AsSpan((int)o, w * h), w, h, true) : raw.AsSpan((int)o, w * h * 4).ToArray();
        // Iggy alpha is premultiplied: PIL 'RGBa' -> 'RGBA' (c * 255 / a, truncated and clipped; a of 0 or 255 unchanged)
        for (int i = 0; i < px.Length; i += 4)
        {
            int a = px[i + 3];
            if (a is 0 or 255) continue;
            for (int c = 0; c < 3; c++) px[i + c] = (byte)Math.Min(255, 255 * px[i + c] / a);
        }
        return (px, w, h);
    }

    static (byte[], int, int) Bimage(byte[] data)
    {
        var d = data.AsSpan();
        if (d.Length < 0x52 || !d[..4].SequenceEqual("BIM\x18"u8)) throw new InvalidDataException("Not a bimage7");
        uint fmt = BinaryPrimitives.ReadUInt32LittleEndian(d[0x0c..]);
        int width = (int)BinaryPrimitives.ReadUInt32LittleEndian(d[0x18..]), height = (int)BinaryPrimitives.ReadUInt32LittleEndian(d[0x1c..]);
        uint mips = BinaryPrimitives.ReadUInt32LittleEndian(d[0x2c..]);
        uint level = BinaryPrimitives.ReadUInt32LittleEndian(d[0x3e..]);
        int mw = (int)BinaryPrimitives.ReadUInt32LittleEndian(d[0x46..]), mh = (int)BinaryPrimitives.ReadUInt32LittleEndian(d[0x4a..]);
        uint size = BinaryPrimitives.ReadUInt32LittleEndian(d[0x4e..]);
        if (fmt is not (16 or 20) || mips != 1 || level != 0 || mw < width || mh < height || d.Length != 0x52 + (long)size)
            throw new InvalidDataException($"Unsupported bimage7 layout (format {fmt}, mips {mips})");
        var full = Bc.Decode(d[0x52..], mw, mh, fmt == 20);
        if (mw == width && mh == height) return (full, width, height);
        var crop = new byte[width * height * 4];
        for (int y = 0; y < height; y++) Array.Copy(full, y * mw * 4, crop, y * width * 4, width * 4);
        return (crop, width, height);
    }
}

/// <summary>BC1 colour / BC3 / BC7 block decoding to straight RGBA8, matching Pillow's BcnDecode (which extract_game_art.py
/// uses). BC7 follows the D3D11 BC7 format specification; the partition tables are from bcdec (MIT, see THIRD_PARTY_NOTICES.md).</summary>
public static class Bc
{
    /// <summary>Decodes w x h pixels of BC7 (bc7 = true) or BC3 blocks (16 bytes per 4x4 block, row-major).</summary>
    public static byte[] Decode(ReadOnlySpan<byte> blocks, int w, int h, bool bc7)
    {
        if (w % 4 != 0 || h % 4 != 0 || blocks.Length != w * h) throw new InvalidDataException("BC block size mismatch");
        var rgba = new byte[w * h * 4];
        var src = blocks.ToArray();
        Parallel.For(0, h / 4, by =>  // block rows are independent
        {
            for (int x = 0, b = by * w * 4; x < w; x += 4, b += 16)
            {
                var blk = src.AsSpan(b, 16); int o = (by * 4 * w + x) * 4;
                if (bc7) Bc7(blk, rgba, o, w * 4);
                else { Bc1Color(blk[8..], rgba, o, w * 4, true); Bc4(blk, rgba, o + 3, w * 4); }
            }
        });
        return rgba;
    }

    /// <summary>BC1 colour block; fourColor forces the 4-colour palette as BC2/BC3 do.</summary>
    public static void Bc1Color(ReadOnlySpan<byte> s, byte[] dst, int o, int stride, bool fourColor)
    {
        int c0 = BinaryPrimitives.ReadUInt16LittleEndian(s), c1 = BinaryPrimitives.ReadUInt16LittleEndian(s[2..]);
        uint lut = BinaryPrimitives.ReadUInt32LittleEndian(s[4..]);
        Span<int> p = stackalloc int[16];
        Rgb565(c0, p); Rgb565(c1, p[4..]);
        for (int k = 0; k < 3; k++)
            if (c0 > c1 || fourColor) { p[8 + k] = (2 * p[k] + p[4 + k]) / 3; p[12 + k] = (p[k] + 2 * p[4 + k]) / 3; }
            else { p[8 + k] = (p[k] + p[4 + k]) / 2; p[12 + k] = 0; }
        p[11] = 255; p[15] = c0 > c1 || fourColor ? 255 : 0;
        for (int i = 0; i < 16; i++)
        {
            int c = (int)(lut >> (2 * i)) & 3, d = o + (i >> 2) * stride + (i & 3) * 4;
            for (int k = 0; k < 4; k++) dst[d + k] = (byte)p[c * 4 + k];
        }
    }

    static void Rgb565(int v, Span<int> p)
    {
        int r = (v & 0xf800) >> 8, g = (v & 0x7e0) >> 3, b = (v & 0x1f) << 3;
        p[0] = r | r >> 5; p[1] = g | g >> 6; p[2] = b | b >> 5; p[3] = 255;
    }

    /// <summary>BC4 / BC3 alpha block (8 bytes) into one channel.</summary>
    static void Bc4(ReadOnlySpan<byte> s, byte[] dst, int o, int stride)
    {
        int a0 = s[0], a1 = s[1];
        Span<int> a = stackalloc int[8];
        a[0] = a0; a[1] = a1;
        if (a0 > a1) for (int k = 1; k < 7; k++) a[k + 1] = ((7 - k) * a0 + k * a1) / 7;
        else { for (int k = 1; k < 5; k++) a[k + 1] = ((5 - k) * a0 + k * a1) / 5; a[6] = 0; a[7] = 255; }
        ulong bits = 0; for (int k = 7; k >= 2; k--) bits = bits << 8 | s[k];
        for (int i = 0; i < 16; i++) dst[o + (i >> 2) * stride + (i & 3) * 4] = (byte)a[(int)(bits >> (3 * i)) & 7];
    }

    static readonly int[] ColorBits = { 4, 6, 5, 7, 5, 7, 7, 5 }, AlphaBits = { 0, 0, 0, 0, 6, 8, 7, 5 };
    static readonly int[] W2 = { 0, 21, 43, 64 }, W3 = { 0, 9, 18, 27, 37, 46, 55, 64 }, W4 = { 0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64 };
    // Partition shapes (row-major texels): digit = subset, +3 marks the subset's anchor (fix-up) texel.
    static readonly string[] P2 = {
        "3011001100110014", "3001000100010004", "3111011101110114", "3001001100110114",
        "3000000100010014", "3011011101111114", "3001001101111114", "3000000100110114",
        "3000000000010014", "3011011111111114", "3000000101111114", "3000000000010114",
        "3001011111111114", "3000000011111114", "3000111111111114", "3000000000001114",
        "3000100011101114", "3141000100000000", "3000000040001110", "3141001100010000",
        "3041000100000000", "3000100041001110", "3000000040001100", "3111001100110004",
        "3041000100010000", "3000100040001100", "3140011001100110", "3041011001101100",
        "3001011141101000", "3000111141110000", "3141000110001110", "3041100110011100",
        "3101010101010104", "3000111100001114", "3101104001011010", "3011001141001100",
        "3041110000111100", "3101010140101010", "3110100101101004", "3101101010100104",
        "3141001111001110", "3001001141001000", "3041001001001100", "3041101111011100",
        "3140100110010110", "3011110011000014", "3110011010011004", "3000014001100000",
        "3100114001000000", "3040011100100000", "3000004001110010", "3000010041100100",
        "3110110010010014", "3011011011001004", "3140001110011100", "3041100111000110",
        "3110110011001004", "3110001100111004", "3111111010000004", "3001100011100114",
        "3000111100110014", "3041001111110000", "3040001011101110", "3100010001110114",
    };
    static readonly string[] P3 = {
        "3014001102212225", "3004001152112221", "3000200152112214", "3225002200110114",
        "3000000041221125", "3014001100220025", "3025002211111114", "3011001152112214",
        "3000000041112225", "3000111141112225", "3000114122222225", "3012004200120015",
        "3112014201120115", "3122042201220125", "3014011211221225", "3014200152002220",
        "3004001101121125", "3114001150012200", "3000112241221125", "3025002200221114",
        "3114011102220225", "3004000152212221", "3000004101220125", "3000110052402210",
        "3125042200110000", "3012001241222225", "3110125142210110", "3000014012511221",
        "3022110241020025", "3110041020022225", "3011012201520014", "3000200052112224",
        "3000000241221225", "3225002200120014", "3014001200220225", "3120042001500120",
        "3000114122520000", "3120120150420120", "3120201245010120", "3011220011520014",
        "3011115222000014", "3104010122222225", "3000000051212124", "3022142200221125",
        "3025001100220014", "3220125102201224", "3101225222220104", "3000212151212124",
        "3104010101012225", "3225011102220114", "3002141200021115", "3000241221122115",
        "3222041101110225", "3002111241120005", "3110041001102225", "3000000021422115",
        "3110041022222225", "3022001100410025", "3022112241220025", "3000000000002415",
        "3005000100020004", "3222122202224225", "3104222222222225", "3114201152012220",
    };

    ref struct Bits
    {
        readonly ulong lo, hi; int pos;
        public Bits(ReadOnlySpan<byte> s) { lo = BinaryPrimitives.ReadUInt64LittleEndian(s); hi = BinaryPrimitives.ReadUInt64LittleEndian(s[8..]); pos = 0; }
        public int Read(int n)
        {
            int v = 0;
            for (int i = 0; i < n; i++, pos++) v |= (int)((pos < 64 ? lo >> pos : hi >> (pos - 64)) & 1) << i;
            return v;
        }
    }

    static int Lerp(int e0, int e1, int w) => ((64 - w) * e0 + w * e1 + 32) >> 6;

    public static void Bc7(ReadOnlySpan<byte> blk, byte[] dst, int o, int stride)
    {
        var s = new Bits(blk);
        int mode = 0;
        while (mode < 8 && s.Read(1) == 0) mode++;
        if (mode == 8) { for (int i = 0; i < 16; i++) dst.AsSpan(o + (i >> 2) * stride + (i & 3) * 4, 4).Clear(); return; }  // reserved: transparent black
        int ns = mode is 0 or 2 ? 3 : mode is 1 or 3 or 7 ? 2 : 1;
        int part = ns > 1 ? s.Read(mode == 0 ? 4 : 6) : 0;
        int rot = mode is 4 or 5 ? s.Read(2) : 0;
        int isb = mode == 4 ? s.Read(1) : 0;
        int ne = ns * 2, cb = ColorBits[mode], ab = AlphaBits[mode];
        Span<int> ep = stackalloc int[24];  // endpoint e, channel c at e * 4 + c
        for (int c = 0; c < 3; c++) for (int e = 0; e < ne; e++) ep[e * 4 + c] = s.Read(cb);
        if (ab > 0) for (int e = 0; e < ne; e++) ep[e * 4 + 3] = s.Read(ab);
        int pb = mode is 0 or 1 or 3 or 6 or 7 ? 1 : 0;
        if (pb == 1)
        {
            for (int i = 0; i < ne * 4; i++) ep[i] <<= 1;
            if (mode == 1) { int p0 = s.Read(1), p1 = s.Read(1); for (int c = 0; c < 3; c++) { ep[c] |= p0; ep[4 + c] |= p0; ep[8 + c] |= p1; ep[12 + c] |= p1; } }
            else for (int e = 0; e < ne; e++) { int p = s.Read(1); for (int c = 0; c < 4; c++) ep[e * 4 + c] |= p; }
        }
        for (int e = 0; e < ne; e++)
        {
            for (int c = 0; c < 4; c++)
            {
                int bits = (c < 3 ? cb : ab) + pb;
                if (c == 3 && ab == 0) { ep[e * 4 + 3] = 255; continue; }
                int v = ep[e * 4 + c] << (8 - bits); ep[e * 4 + c] = v | v >> bits;
            }
        }
        int ib = mode is 0 or 1 ? 3 : mode == 6 ? 4 : 2, ib2 = mode == 4 ? 3 : mode == 5 ? 2 : 0;
        int[] w1 = ib == 2 ? W2 : ib == 3 ? W3 : W4, w2 = ib2 == 2 ? W2 : W3;
        var shape = ns == 2 ? P2[part] : ns == 3 ? P3[part] : "3000000000000000";
        Span<int> idx = stackalloc int[16];
        for (int i = 0; i < 16; i++) idx[i] = s.Read(shape[i] >= '3' ? ib - 1 : ib);
        for (int i = 0; i < 16; i++)
        {
            int e0 = (shape[i] - '0') % 3 * 8, e1 = e0 + 4;  // ep offset of the subset's two endpoints
            int ci = idx[i], ai = idx[i]; int[] cw = w1, aw = w1;
            if (ib2 > 0)
            {
                int i2 = s.Read(i == 0 ? ib2 - 1 : ib2);
                if (isb == 0) { ai = i2; aw = w2; } else { ci = i2; cw = w2; }
            }
            int r = Lerp(ep[e0], ep[e1], cw[ci]), g = Lerp(ep[e0 + 1], ep[e1 + 1], cw[ci]), b = Lerp(ep[e0 + 2], ep[e1 + 2], cw[ci]), a = Lerp(ep[e0 + 3], ep[e1 + 3], aw[ai]);
            if (rot == 1) (a, r) = (r, a); else if (rot == 2) (a, g) = (g, a); else if (rot == 3) (a, b) = (b, a);
            int d = o + (i >> 2) * stride + (i & 3) * 4;
            dst[d] = (byte)r; dst[d + 1] = (byte)g; dst[d + 2] = (byte)b; dst[d + 3] = (byte)a;
        }
    }
}
