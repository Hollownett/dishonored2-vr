using DishonoredVR;
using System.Buffers.Binary;
using System.Numerics;
using System.Text;
using System.Text.Json;
// args: <gameDir> <outDir> [kind=resource ...]; no kinds => write names.txt only
var game = args[0]; var outDir = args[1]; Directory.CreateDirectory(outDir);
if (Environment.GetEnvironmentVariable("DB") != null)
{
    // DB=1 ./run.sh : same generator the launcher runs; also prints the hull/render frame check and writes keys.txt
    var sw = System.Diagnostics.Stopwatch.StartNew();
    var entries = PropShapes.Build(game, out int hkxCount);
    Console.WriteLine($"{entries.Count} entries from {hkxCount} hkx with a bmodel, build {sw.Elapsed.TotalSeconds:F1} s");
    PropShapes.Write(entries, Path.Combine(outDir, "prop-shapes.bin"));
    Console.WriteLine($"total {sw.Elapsed.TotalSeconds:F1} s, {new FileInfo(Path.Combine(outDir, "prop-shapes.bin")).Length} bytes");
    var want = new Dictionary<string, string> { ["book"] = "bookstack_01/book_01.lwo", ["spoon"] = "teapot_set_spoon_01.lwo", ["screwdriver"] = "screwdriver_01.lwo", ["plate"] = "teapot_set_smallplate_01.lwo", ["box"] = "shelf_assets_box_01.lwo" };
    var kt = new StringBuilder();
    foreach (var (k, frag) in want)
        foreach (var e in entries.Where(e => e.Hkx.Contains(frag) && e.Hkx.Contains("._100_100_100.")))
        {
            var dc = (e.HullCentre - e.MeshCentre) * 1000;
            var line = $"{k}: key=0x{e.Key:x16} hull_verts={e.HullCount} decimated_verts={e.Verts.Length} hull_centre_minus_mesh_centre_mm=({dc.X:F1},{dc.Y:F1},{dc.Z:F1}) mesh_size_cm=({e.MeshSize.X * 100:F1},{e.MeshSize.Y * 100:F1},{e.MeshSize.Z * 100:F1})";
            Console.WriteLine(line); kt.AppendLine(line);
        }
    kt.AppendLine("bottle: skipped (compound, 2 hulls: neck + body) -> generated/models/environment/props/generic/bottle_set_01/bottle_01.lwo.hkt._100_100_100.10.hkx");
    File.WriteAllText(Path.Combine(outDir, "keys.txt"), kt.ToString());
    return;
}
var c = new ArtImport.Containers(game, n => true);
var names = c.Names.OrderBy(n => n).ToList();
var sb = new StringBuilder($"total resources: {names.Count}\n");
void Pat(string title, string[] pats, Func<string,bool>? extra = null)
{
    var hits = names.Where(n => pats.Any(p => n.Contains(p)) && (extra?.Invoke(n) ?? true)).ToList();
    sb.AppendLine($"\n## {title} [{string.Join(",", pats)}]: {hits.Count}");
    foreach (var h in hits.Take(15)) sb.AppendLine("  " + h);
}
foreach (var p in new[] { "spoon", "screwdriver", "book", "bottle", "plate", "crate", "box", "severed", "head", "arm", "leg" }) Pat(p, new[] { p });
foreach (var p in new[] { ".hk", "hkx", "physics", "collision", "havok", "bhk", "phys" }) Pat("phys " + p, new[] { p });
Pat("bmd6model with spoon/screwdriver/book/bottle/plate/crate/box/severed", new[] { "spoon", "screwdriver", "book", "bottle", "plate", "crate", "box", "severed" }, n => n.EndsWith(".bmd6model"));
File.WriteAllText(Path.Combine(outDir, "names.txt"), sb.ToString());
Console.WriteLine(sb.ToString().Split('\n').Take(1).First());
var q = Environment.GetEnvironmentVariable("Q");
if (q != null) { foreach (var n in names.Where(n => System.Text.RegularExpressions.Regex.IsMatch(n, q))) Console.WriteLine(n); return; }
if (Environment.GetEnvironmentVariable("HULL") != null)
{
    // HULL=1 ./run.sh kind=resource.hkx ...  appends hulls.jsonl (scan for hknp convex shapes, see coordinator's layout)
    var path = Path.Combine(outDir, "hulls.jsonl"); var lines = new List<string>();
    foreach (var a in args.Skip(2))
    {
        var kind = a[..a.IndexOf('=')]; var res = a[(a.IndexOf('=') + 1)..]; var b = c.Read(res);
        string magic = BitConverter.ToString(b, 0, 8);
        Console.WriteLine($"{kind}: {res} len={b.Length} magic={magic} ({(b[0] == 0x57 ? "packfile" : Encoding.ASCII.GetString(b, 4, 4) == "TAG0" || Encoding.ASCII.GetString(b, 0, 4) == "TAG0" ? "tagfile" : "other")})");
        // These .hkx are Havok TAGFILES (wrapper 00 00 00 00 00 00 00 08, "TAG0", "SDKV", "DATA", "TYPE", ...), not packfiles, and the
        // DATA chunk is NOT the in-memory layout (no shape header before the radius). Vertices are still float4 whose w holds
        // 0x3f000000|index, so scan the DATA chunk for such runs; radius = float at DATA+24 (verified on book: 0.01 = render bbox - hull bbox).
        int di = IndexOf(b, "DATA"u8); if (di < 4) { Console.WriteLine("  not a tagfile (no DATA chunk): skipped"); continue; } int dlen = (int)(BinaryPrimitives.ReadUInt32BigEndian(b.AsSpan(di - 4)) & 0x3fffffff) - 8;
        var tstr = Encoding.ASCII.GetString(b, 0, Math.Min(b.Length, 1400)); var cls = System.Text.RegularExpressions.Regex.Matches(tstr, "hknpw*Shape").Select(m => m.Value).Distinct();
        Console.WriteLine($"  DATA at {di + 4} len {dlen}; classes: {string.Join(",", cls)}");
        int d0 = di + 4, n = 0; float radius = BitConverter.ToSingle(b, d0 + 24);
        uint W(int pp) => BitConverter.ToUInt32(b, pp + 12);
        for (int x = d0; x + 16 <= d0 + dlen; x += 4)
        {
            if (W(x) != 0x3f000000) continue;
            var vv = new List<Vector3>();
            for (int pp = x; pp + 16 <= d0 + dlen && W(pp) == (0x3f000000u | (uint)vv.Count); pp += 16)
            {
                var v = new Vector3(BitConverter.ToSingle(b, pp), BitConverter.ToSingle(b, pp + 4), BitConverter.ToSingle(b, pp + 8));
                if (!float.IsFinite(v.X + v.Y + v.Z) || Math.Abs(v.X) > 5 || Math.Abs(v.Y) > 5 || Math.Abs(v.Z) > 5) break; vv.Add(v);
            }
            if (vv.Count < 2) continue; // ponytail: drops lone-vertex false positives and single-vertex spheres
            var mn = new Vector3(float.MaxValue); var mx = new Vector3(float.MinValue); foreach (var v in vv) { mn = Vector3.Min(mn, v); mx = Vector3.Max(mx, v); }
            var z = mx - mn; n++;
            Console.WriteLine($"  X=data+{x - d0} count={vv.Count} radius={radius:F4} hull bbox(cm) {z.X * 100:F1} x {z.Y * 100:F1} x {z.Z * 100:F1}  centre=({(mn.X + mx.X) / 2:F3},{(mn.Y + mx.Y) / 2:F3},{(mn.Z + mx.Z) / 2:F3})");
            lines.Add(JsonSerializer.Serialize(new { kind, resource = res, offset = x, type = -1, shapeClasses = cls, radius, vertices = vv.Select(v => new[] { v.X, v.Y, v.Z }) }));
            x += 16 * vv.Count - 4;
        }
        if (n == 0) Console.WriteLine("  no shapes found");
    }
    File.AppendAllLines(path, lines); return;
}
var dm = Environment.GetEnvironmentVariable("DUMP");
if (dm != null) { File.WriteAllBytes(args[2], c.Read(dm)); return; }
var hx = Environment.GetEnvironmentVariable("HEX");
if (hx != null) { var b = c.Read(hx); Console.WriteLine(b.Length); Console.WriteLine(BitConverter.ToString(b, 0, Math.Min(Environment.GetEnvironmentVariable("N") is string ns ? int.Parse(ns) : 256, b.Length))); Console.WriteLine(Encoding.ASCII.GetString(b, 0, Math.Min(400, b.Length)).Replace("\0", ".")); return; }
var rx = Environment.GetEnvironmentVariable("CAT");
if (rx != null) { c.Names.Where(n => n.Contains(rx)); Console.WriteLine(System.Text.Encoding.UTF8.GetString(c.Read(rx))); return; }
foreach (var a in args.Skip(2))
{
    var kind = a[..a.IndexOf('=')]; var res = a[(a.IndexOf('=') + 1)..];
    var (v, t) = Parse(c.Read(res));
    var min = new Vector3(float.MaxValue); var max = new Vector3(float.MinValue);
    foreach (var p in v) { min = Vector3.Min(min, p); max = Vector3.Max(max, p); }
    var s = max - min;
    Console.WriteLine($"{kind}: {res} v={v.Count} t={t.Count} size(model units) x={s.X:F4} y={s.Y:F4} z={s.Z:F4}");
    File.WriteAllText(Path.Combine(outDir, kind + ".json"), JsonSerializer.Serialize(new { kind, resource = res,
        vertices = v.Select(p => new[] { p.X, p.Y, p.Z }), triangles = t.Select(x => new[] { x.Item1, x.Item2, x.Item3 }) }));
}
static int IndexOf(byte[] b, ReadOnlySpan<byte> m) => b.AsSpan().IndexOf(m);
// first surface only. MM6B (skinned basemodel, as launcher PoseRender.ParseMesh) or 1LMB (static .bmodel).
// Vertex block: cpu format 1 = 32-byte interleaved (pos f32x3 first); otherwise planar 44-byte (positions f32x3 first).
static (List<Vector3>, List<(int, int, int)>) Parse(byte[] d)
{
    int o = 4;
    int U32() { int v = BinaryPrimitives.ReadInt32LittleEndian(d.AsSpan(o)); o += 4; return v; }
    string Name() { int n = U32(); var s = Encoding.UTF8.GetString(d, o, n); o += n; return s; }
    if (d.AsSpan(0, 4).SequenceEqual("MM6B"u8)) { var skel = Name(); o += skel == "" ? 24 : 26; U32(); Name(); Name(); U32(); o += 5; }
    else if (d.AsSpan(0, 4).SequenceEqual("1LMB"u8)) { o += 4; Name(); U32(); Name(); U32(); }
    else throw new InvalidDataException("unknown mesh magic");
    if (!d.AsSpan(o, 4).SequenceEqual("BTRI"u8)) throw new InvalidDataException("surface");
    o += 4; U32(); int cpu = U32(); U32(); o += 40;
    int nv = U32(); o += 8; int v0 = o;
    int stride = cpu == 1 ? 32 : 44; o += nv * stride;
    int ni = U32(); int i0 = o;
    var vs = new List<Vector3>();
    for (int v = 0; v < nv; v++) { int q = cpu == 1 ? v0 + 32 * v : v0 + 12 * v; vs.Add(new(BitConverter.ToSingle(d, q), BitConverter.ToSingle(d, q + 4), BitConverter.ToSingle(d, q + 8))); }
    var ts = new List<(int, int, int)>();
    for (int k = 0; k + 2 < ni; k += 3) ts.Add((BinaryPrimitives.ReadUInt16LittleEndian(d.AsSpan(i0 + 2 * k)), BinaryPrimitives.ReadUInt16LittleEndian(d.AsSpan(i0 + 2 * k + 2)), BinaryPrimitives.ReadUInt16LittleEndian(d.AsSpan(i0 + 2 * k + 4))));
    Console.WriteLine($"  cpu={cpu} nv={nv} ni={ni}");
    return (vs, ts);
}
