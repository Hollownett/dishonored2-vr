using System.Buffers.Binary;
using System.Numerics;
using System.Text.RegularExpressions;

namespace DishonoredVR;

/// <summary>prop-shapes.bin: runtime Havok convex hull (key) -> decimated real render-mesh vertices, built from the user's own install.
/// Key = FNV-1a 64 over uint32 vertex count then x,y,z of each hull vertex as int32 round(coord*10000) (0.1 mm), all little endian.
/// A compound (several hulls in one file: a bottle's neck and body) is keyed by CompoundKey: its children's keys, each over the hull without
/// Havok's padding, sorted (the runtime reads them from hknpCompoundShape's instance array, prop_hull.hpp).
/// File: "D2PS", u32 version 2, u32 entries, per entry u64 key, u32 n, n x float3. Every .lwo.{hkt,cvx,box}._SX_SY_SZ.10.hkx (mirrored scales too;
/// v1 read .hkt and positive scales only, which left the game's box and convex files and every mirrored placement without a shape).</summary>
public static class PropShapes
{
    public sealed record Entry(string Hkx, string Mesh, ulong Key, int HullCount, Vector3[] Hull, Vector3[] Verts, Vector3 HullCentre, Vector3 MeshCentre, Vector3 MeshSize, int Children = 1);

    static readonly Regex HkxName = new(@"^generated/models/(.+)\.lwo\.(?:hkt|cvx|box)\._(-?\d+)_(-?\d+)_(-?\d+)\.10\.hkx$", RegexOptions.Compiled);
    public const uint Version = 2;

    public static string DefaultPath => Path.Combine(ModHome.Dir, "prop-shapes.bin");

    public static bool Stale(string game, string path)
    {
        if (!File.Exists(path)) return true;
        try { using var f = File.OpenRead(path); var h = new byte[8]; if (f.Read(h, 0, 8) != 8 || BitConverter.ToUInt32(h, 4) != Version) return true; }
        catch (IOException) { return true; }
        var t = File.GetLastWriteTimeUtc(path);
        foreach (var c in new[] { "game1", "game2", "game3", "game4" })
            if (File.GetLastWriteTimeUtc(Path.Combine(game, "base", c + "_patch.index")) > t) return true;
        return false;
    }

    /// <summary>Writes the database when missing or older than the game's resource indexes. Returns a log line, or null when current.</summary>
    public static string? EnsureCurrent(string game, string path)
    {
        if (!Stale(game, path)) return null;
        var sw = System.Diagnostics.Stopwatch.StartNew();
        var entries = Build(game, out int hkx);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var tmp = path + ".tmp"; Write(entries, tmp); File.Move(tmp, path, true);
        return $"Prop shapes: {entries.Count} hulls from {hkx} physics files written in {sw.Elapsed.TotalSeconds:F1} s ({new FileInfo(path).Length / 1024} KB)";
    }

    public static List<Entry> Build(string game, out int hkxCount)
    {
        var c = new ArtImport.Containers(game, n => n.StartsWith("generated/model/models/") && n.EndsWith(".bmodel") || HkxName.IsMatch(n));
        var byKey = new Dictionary<ulong, Entry>(); var list = new List<Entry>(); hkxCount = 0;
        foreach (var name in c.Names.OrderBy(n => n, StringComparer.Ordinal))
        {
            var m = HkxName.Match(name); if (!m.Success) continue;
            var mesh = "generated/model/models/" + m.Groups[1].Value + ".bmodel"; if (!c.Has(mesh)) continue;
            hkxCount++;
            try
            {
                var runs = HullRuns(c.Read(name)); if (runs.Count == 0 || runs.Count > 16) continue;
                var hull = runs.Count == 1 ? runs[0] : runs.SelectMany(r => r).ToList();
                var scale = new Vector3(int.Parse(m.Groups[2].Value), int.Parse(m.Groups[3].Value), int.Parse(m.Groups[4].Value)) / 100f;
                var (pts, tris) = MeshTriangles(c.Read(mesh)); if (pts.Count == 0) continue;
                for (int i = 0; i < pts.Count; i++) pts[i] *= scale;
                double area = Area(pts, tris); pts = SampleSurface(pts, tris, area);
                var (hmn, hmx) = Bounds(hull); var (mmn, mmx) = Bounds(pts);
                var verts = Decimate(pts, 512, area);
                // Havok pads the vertex array to a multiple of 4 by repeating the last vertex; the runtime count may be either, so key both.
                // A compound has one key (its children's, unpadded and sorted).
                foreach (var h in runs.Count == 1 ? PaddedVariants(hull) : new[] { hull })
                {
                    ulong key = runs.Count == 1 ? Key(h) : CompoundKey(runs);
                    if (byKey.ContainsKey(key)) continue;
                    var e = new Entry(name, mesh, key, h.Count, h.ToArray(), verts, (hmn + hmx) / 2, (mmn + mmx) / 2, mmx - mmn, runs.Count);
                    byKey[key] = e; list.Add(e);
                }
            }
            catch (Exception) { /* a malformed resource only drops its own entry */ }
        }
        return list;
    }

    static IEnumerable<List<Vector3>> PaddedVariants(List<Vector3> h)
    {
        yield return h;
        if (h.Count % 4 != 0) { var p = new List<Vector3>(h); while (p.Count % 4 != 0) p.Add(h[^1]); yield return p; }
    }

    /// <summary>A hull without Havok's padding (trailing repeats of the last vertex).</summary>
    public static List<Vector3> Unpadded(IReadOnlyList<Vector3> h)
    {
        int n = h.Count; while (n > 1 && h[n - 1] == h[n - 2]) n--;
        return h.Take(n).ToList();
    }

    /// <summary>A compound's key: FNV-1a 64 over u32 child count, then the children's keys (each over its unpadded hull) sorted, as u64.</summary>
    public static ulong CompoundKey(IReadOnlyList<List<Vector3>> children)
    {
        var keys = children.Select(c => Key(Unpadded(c))).OrderBy(k => k).ToList();
        ulong h = 1469598103934665603UL;
        void Byte(byte b) { h ^= b; h *= 1099511628211UL; }
        for (int i = 0; i < 4; i++) Byte((byte)((uint)keys.Count >> (8 * i)));
        foreach (var k in keys) for (int i = 0; i < 8; i++) Byte((byte)(k >> (8 * i)));
        return h;
    }

    public static ulong Key(IReadOnlyList<Vector3> hull)
    {
        ulong h = 1469598103934665603UL;
        void Byte(byte b) { h ^= b; h *= 1099511628211UL; }
        void U32(uint v) { for (int i = 0; i < 4; i++) Byte((byte)(v >> (8 * i))); }
        U32((uint)hull.Count);
        foreach (var v in hull)
            foreach (var f in new[] { v.X, v.Y, v.Z }) U32((uint)(int)Math.Round(f * 10000.0, MidpointRounding.AwayFromZero));
        return h;
    }

    public static void Write(List<Entry> entries, string path)
    {
        var tmp = path + ".tmp";  // written aside and renamed: a game starting meanwhile never reads half a file
        using (var w = new BinaryWriter(File.Create(tmp)))
        {
            w.Write("D2PS"u8); w.Write(Version); w.Write((uint)entries.Count);
            foreach (var e in entries) { w.Write(e.Key); w.Write((uint)e.Verts.Length); foreach (var v in e.Verts) { w.Write(v.X); w.Write(v.Y); w.Write(v.Z); } }
        }
        File.Move(tmp, path, true);
    }

    static (Vector3, Vector3) Bounds(IEnumerable<Vector3> p)
    {
        var mn = new Vector3(float.MaxValue); var mx = new Vector3(float.MinValue);
        foreach (var v in p) { mn = Vector3.Min(mn, v); mx = Vector3.Max(mx, v); }
        return (mn, mx);
    }

    /// <summary>Voxel grid from 4 mm, growing x1.25 until at most 512 vertices; the first vertex of each voxel is kept.</summary>
    public static Vector3[] Decimate(List<Vector3> pts, int max = 512, double area = 0)
    {
        // Skip the 4 mm x1.25^k steps that cannot fit: a surface of this area covers at least area/cell^2 voxels, so a cell under half
        // sqrt(area/max) always leaves more than max points (same cell sequence, fewer passes over big meshes).
        float cell = 0.004f, skipBelow = (float)(0.5 * Math.Sqrt(area / max));
        while (cell * 1.25f <= skipBelow) cell *= 1.25f;
        for (; ; cell *= 1.25f)
        {
            var seen = new HashSet<(int, int, int)>(); var keep = new List<Vector3>();
            foreach (var p in pts)
                if (seen.Add(((int)MathF.Floor(p.X / cell), (int)MathF.Floor(p.Y / cell), (int)MathF.Floor(p.Z / cell)))) keep.Add(p);
            if (keep.Count <= max) return keep.ToArray();
        }
    }

    /// <summary>The .hkx is a Havok tagfile (not a packfile). Hull vertices are float4 whose w is 0x3f000000|index; every run of
    /// at least two with w counting up from 0 is one convex hull.</summary>
    public static List<List<Vector3>> HullRuns(byte[] b)
    {
        var res = new List<List<Vector3>>();
        int di = b.AsSpan().IndexOf("DATA"u8); if (di < 4) return res;
        int d0 = di + 4, end = Math.Min(b.Length, d0 + (int)(BinaryPrimitives.ReadUInt32BigEndian(b.AsSpan(di - 4)) & 0x3fffffff) - 8);
        uint W(int p) => BitConverter.ToUInt32(b, p + 12);
        for (int x = d0; x + 16 <= end; x += 4)
        {
            if (W(x) != 0x3f000000) continue;
            var vv = new List<Vector3>();
            for (int p = x; p + 16 <= end && W(p) == (0x3f000000u | (uint)vv.Count); p += 16)
            {
                var v = new Vector3(BitConverter.ToSingle(b, p), BitConverter.ToSingle(b, p + 4), BitConverter.ToSingle(b, p + 8));
                if (!float.IsFinite(v.X + v.Y + v.Z) || MathF.Abs(v.X) > 5 || MathF.Abs(v.Y) > 5 || MathF.Abs(v.Z) > 5) break;
                vv.Add(v);
            }
            if (vv.Count < 2) continue;
            res.Add(vv); x += 16 * vv.Count - 4;
        }
        return res;
    }

    /// <summary>Static .bmodel ("1LMB"): every surface of LOD 0. Vertex block cpu format 1 = 32-byte interleaved, else planar 44 bytes.</summary>
    public static (List<Vector3>, List<int>) MeshTriangles(byte[] d)
    {
        var res = new List<Vector3>(); var tris = new List<int>();
        if (!d.AsSpan(0, 4).SequenceEqual("1LMB"u8)) return (res, tris);
        int o = 8;
        int U32() { int v = BinaryPrimitives.ReadInt32LittleEndian(d.AsSpan(o)); o += 4; return v; }
        void Name() { int n = U32(); if (n < 0 || o + n > d.Length) throw new InvalidDataException("bmodel"); o += n; }
        Name(); int surfaces = U32();
        for (int s = 0; s < surfaces; s++)
        {
            if (s > 0) { int next = d.AsSpan(o).IndexOf("BTRI"u8); if (next < 0) break; o += next; }
            else { Name(); U32(); }
            if (!d.AsSpan(o, 4).SequenceEqual("BTRI"u8)) throw new InvalidDataException("surface");
            o += 4; U32(); int cpu = U32(); U32(); o += 40;
            int nv = U32(); o += 8; int v0 = o; if (nv < 0 || o + (long)nv * (cpu == 1 ? 32 : 44) > d.Length) throw new InvalidDataException("vertices");
            o += nv * (cpu == 1 ? 32 : 44); int ni = U32(); int i0 = o; o += ni * 2; int b0 = res.Count;
            for (int k = 0; k + 2 < ni; k++) { int ix = BinaryPrimitives.ReadUInt16LittleEndian(d.AsSpan(i0 + 2 * k)); if (ix >= nv) throw new InvalidDataException("index"); tris.Add(b0 + ix); }
            for (int v = 0; v < nv; v++) { int q = cpu == 1 ? v0 + 32 * v : v0 + 12 * v; res.Add(new(BitConverter.ToSingle(d, q), BitConverter.ToSingle(d, q + 4), BitConverter.ToSingle(d, q + 8))); }
        }
        return (res, tris);
    }

    public static double Area(List<Vector3> v, List<int> tris)
    {
        double a = 0; for (int t = 0; t + 2 < tris.Count; t += 3) a += Vector3.Cross(v[tris[t + 1]] - v[tris[t]], v[tris[t + 2]] - v[tris[t]]).Length() / 2;
        return a;
    }

    /// <summary>Every vertex, then points along each triangle edge at most 8 mm apart, then its interior on a barycentric grid at most
    /// 10 mm apart, so long low-poly faces still have points for a slab cut. Vertices come first so decimation keeps them.</summary>
    public static List<Vector3> SampleSurface(List<Vector3> v, List<int> tris, double area)
    {
        var res = new List<Vector3>(v);
        // ponytail: spacing widens only for room-sized meshes (surface area > ~30 m2 at 1 cm) to bound generation time; props held by hand are unaffected
        float f = (float)Math.Max(1, Math.Sqrt(area * 2 / 1e-4 / 60000));
        for (int t = 0; t + 2 < tris.Count; t += 3)
        {
            Vector3 a = v[tris[t]], b = v[tris[t + 1]], c = v[tris[t + 2]];
            foreach (var (p, q) in new[] { (a, b), (b, c), (c, a) })
            {
                int n = (int)MathF.Ceiling((q - p).Length() / (0.008f * f));
                for (int k = 1; k < n; k++) res.Add(Vector3.Lerp(p, q, k / (float)n));
            }
            int g = (int)MathF.Ceiling(MathF.Max((b - a).Length(), MathF.Max((c - b).Length(), (a - c).Length())) / (0.010f * f));
            for (int i = 1; i < g; i++)
                for (int j = 1; i + j < g; j++) res.Add(a + (b - a) * (i / (float)g) + (c - a) * (j / (float)g));
        }
        return res;
    }
}
