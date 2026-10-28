using System.Buffers.Binary;
using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;
using System.Text.RegularExpressions;

namespace DishonoredVR;

/// <summary>C# port of tools/render_calibration_poses.py with the parts of tools/build_player_asset_pack.py (MD6 mesh and
/// skeleton parsing), tools/extract_head_assets.py (masked NPC heads) and tools/render_body_ik.py (skinning) it needs.
/// Renders the calibration avatar images pose_{shoulders,tpose}_{corvo,emily}.png/.rgba (u32 width, u32 height, straight
/// RGBA8; shown on the in-game VR settings page) from the user's own install, read-only. The poses replace the C++ solver
/// frames (tests/body_ik_check.cpp scenarios "shoulders" and "tpose") with the bind body plus src/body_ik.hpp's arm step
/// (clavicle shrug, Boneworks elbow pole, soft two-bone IK, forearm twist, fist curl) for the same hand targets.</summary>
public static partial class PoseRender
{
    /// <summary>Teaching targets use metres, Z up, +X forward and +Y anatomical left. Curls are thumb to little finger.</summary>
    public sealed record TeachingHand(Vector3 Position, Vector3 Direction, Vector3 Normal, float[] Curls);
    public sealed record TeachingPose(TeachingHand? Left, TeachingHand? Right, float Azimuth, float Elevation, Vector3 Centre, float Span);

    public sealed class GuideFrame
    {
        public byte[] Rgba { get; }
        readonly Func<Vector3, PointF> project;
        readonly PointF[] handPoints, palmPoints, wristPoints;
        internal GuideFrame(byte[] rgba, Func<Vector3, PointF> project, PointF[] handPoints, PointF[] palmPoints, PointF[] wristPoints)
        { Rgba = rgba; this.project = project; this.handPoints = handPoints; this.palmPoints = palmPoints; this.wristPoints = wristPoints; }
        public PointF Project(Vector3 world) => project(world);
        /// <summary>Actual solved attachment, palm centre approximation and wrist; 0 is anatomical left, 1 is right.</summary>
        public PointF HandPoint(int side) => handPoints[side];
        public PointF PalmPoint(int side) => palmPoints[side];
        public PointF WristPoint(int side) => wristPoints[side];
    }

    public static GuideRenderer CreateGuideRenderer(string game) => new(game);
    public static void RunGuideFigures(string game, string outDir, Action<string> log, Action<int, int>? progress = null) =>
        GuideFigures.Run(game, outDir, log, progress);

    /// <summary>Loads the installed player models once for a complete guide generation. Calibration rendering is independent.</summary>
    public sealed class GuideRenderer : IDisposable
    {
        readonly Avatar[] avatars = new Avatar[2];
        internal GuideRenderer(string game)
        {
            var c = new ArtImport.Containers(Path.GetFullPath(game), n => n.StartsWith("generated/") && n.Contains("models/characters/"));
            Parallel.For(0, 2, i => avatars[i] = new Avatar(c, i == 0 ? "corvo" : "emily"));
        }

        public GuideFrame Render(string character, TeachingPose pose, int w, int h, bool silhouette = false, bool crouch = false)
        {
            if (character is not ("corvo" or "emily")) throw new ArgumentException("Unknown guide character", nameof(character));
            if (w <= 0 || h <= 0 || !float.IsFinite(pose.Span) || pose.Span <= 0) throw new ArgumentOutOfRangeException(nameof(w));
            var av = avatars[character == "corvo" ? 0 : 1];
            TeachingHand Rest(int i) => new(new(.18f, i == 0 ? .22f : -.22f, 1.05f), Vector3.UnitZ, new(0, i == 0 ? -1 : 1, 0), new float[5]);
            var hands = new[] { pose.Left ?? Rest(0), pose.Right ?? Rest(1) };
            var parts = new List<Part> { av.BodyMesh, av.Head };
            var solvedHands = new Vector3[2]; var solvedWrists = new Vector3[2];
            for (int i = 0; i < 2; i++)
            {
                var hand = hands[i];
                if (hand.Curls.Length != 5 || hand.Curls.Any(c => !float.IsFinite(c)) || hand.Direction.LengthSquared() < 1e-8f || Vector3.Cross(hand.Direction, hand.Normal).LengthSquared() < 1e-8f)
                    throw new ArgumentException("Invalid teaching hand frame or curls", nameof(pose));
                var arm = av.PoseArm(i, av.TeachingTarget(i, hand), hand.Curls);
                solvedHands[i] = av.HandPosition(i, arm); solvedWrists[i] = av.WristPosition(i, arm);
                parts.Add(av.SkinArm(i, arm));
            }
            Vector3 Bend(Vector3 bind)
            {
                if (!crouch) return bind;
                var origin = av.FromWorld(Vector3.Zero);
                var p = (bind - origin) * av.Scale;
                float z = p.Z;
                // Guide-only squat: planted feet, forward knees, lowered hips and inclined torso.
                p.X += z < .5f ? .30f * z / .5f : z < 1 ? .30f - .48f * (z - .5f) / .5f : -.18f + .30f * (z - 1);
                p.Z = z < .5f ? z * .60f : z < 1 ? .30f + (z - .5f) * .64f : z - .38f;
                return origin + p / av.Scale;
            }
            if (crouch) parts = parts.Select(p => p.Posed(p.P.Select(Bend).ToArray(), p.N)).ToList();
            float az = pose.Azimuth * MathF.PI / 180, el = pose.Elevation * MathF.PI / 180;
            var fwd = -new Vector3(MathF.Cos(el) * MathF.Cos(az), MathF.Cos(el) * MathF.Sin(az), MathF.Sin(el));
            var right = Unit(Vector3.Cross(fwd, Vector3.UnitZ)); var up = Vector3.Cross(right, fwd);
            var view = new View(w, h, av.FromWorld(pose.Centre) - fwd * 5, fwd, right, up, pose.Span / av.Scale);
            PointF Project(Vector3 world) { var p = view.Project(Bend(av.FromWorld(world))); return new(p.X, p.Y); }
            PointF BindProject(Vector3 bind) { var p = view.Project(Bend(bind)); return new(p.X, p.Y); }
            var g = Rasterize(view, parts, true);
            var mask = g.Depth.Select(float.IsFinite).ToArray(); KeepLargest(mask, w, h);
            var rgba = new byte[w * h * 4];
            var edge = new bool[w * h];
            var light = view.Dir(Key);
            var handPoints = solvedHands.Select(BindProject).ToArray();
            var palmPoints = Enumerable.Range(0, 2).Select(i => BindProject(solvedHands[i] + Unit(hands[i].Direction) * (.055f / av.Scale))).ToArray();
            var wristPoints = solvedWrists.Select(BindProject).ToArray();
            float handRadius = .15f * h / pose.Span;
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
            {
                int o = y * w + x; if (!mask[o]) continue;
                var n = Unit(g.Normal![o]); if (Vector3.Dot(n, -fwd) < 0) n = -n;
                float shade = MathF.Max(0, Vector3.Dot(n, light));
                // Three sepia washes; retain only a trace of the material's value, rather than photographic colour.
                var a = g.Albedo![o]; shade = shade * .9f + (a.X + a.Y + a.Z) / 3 * .1f;
                bool dark = shade < .28f;
                var c = shade > .64f ? Color.FromArgb(202, 185, 151) : dark ? Color.FromArgb(102, 81, 58) : Color.FromArgb(155, 130, 94);
                bool nearHand = handPoints.Any(p => (p.X - x) * (p.X - x) + (p.Y - y) * (p.Y - y) < handRadius * handRadius);
                if (dark && !nearHand && (x + y) % 22 < 2 && (x / 48 + y / 48) % 3 != 0) c = Color.FromArgb(66, 55, 42);
                rgba[o * 4] = c.R; rgba[o * 4 + 1] = c.G; rgba[o * 4 + 2] = c.B; rgba[o * 4 + 3] = 255;
                void Compare(int q)
                {
                    if (!mask[q]) { edge[o] = true; return; }
                    var other = Unit(g.Normal[q]); if (Vector3.Dot(other, -fwd) < 0) other = -other;
                    if (Math.Abs(g.Depth[o] - g.Depth[q]) * av.Scale > .018f || Vector3.Dot(n, other) < .55f) edge[o] = true;
                }
                if (x == 0 || x == w - 1 || y == 0 || y == h - 1) edge[o] = true;
                else { Compare(o - 1); Compare(o + 1); Compare(o - w); Compare(o + w); }
            }
            // At the guide's 2x working resolution this becomes a 2-3 px final charcoal contour.
            const int radius = 2;
            for (int y = 0; y < h; y++) for (int x = 0; x < w; x++)
            {
                if (!edge[y * w + x]) continue;
                for (int dy = -radius; dy <= radius; dy++) for (int dx = -radius; dx <= radius; dx++)
                {
                    if (dx * dx + dy * dy > radius * radius || x + dx < 0 || x + dx >= w || y + dy < 0 || y + dy >= h) continue;
                    int o = ((y + dy) * w + x + dx) * 4;
                    rgba[o] = 41; rgba[o + 1] = 39; rgba[o + 2] = 35; rgba[o + 3] = 255;
                }
            }
            if (silhouette)
            {
                for (int o = 0; o < mask.Length; o++)
                {
                    if (rgba[o * 4 + 3] == 0) continue;
                    // Keep the same mesh contour, with a quiet paper-lit rim around solid ink.
                    bool rim = mask[o] && (o % w < 2 || o % w >= w - 2 || o < w * 2 || o >= mask.Length - w * 2 ||
                        !mask[o - 2] || !mask[o + 2] || !mask[o - w * 2] || !mask[o + w * 2]);
                    rgba[o * 4] = (byte)(rim ? 101 : 41);
                    rgba[o * 4 + 1] = (byte)(rim ? 95 : 39);
                    rgba[o * 4 + 2] = (byte)(rim ? 81 : 35);
                    rgba[o * 4 + 3] = 217;
                }
            }
            return new GuideFrame(rgba, Project, handPoints, palmPoints, wristPoints);
        }

        public void Dispose() { } // Assets are managed arrays; no game files remain open.
    }

    public static readonly string[] Keys = { "pose_shoulders_corvo", "pose_tpose_corvo", "pose_shoulders_emily", "pose_tpose_emily" };
    public static bool Missing(string dir) => Keys.Any(k => !File.Exists(Path.Combine(dir, k + ".rgba")));

    /// <summary>Renders the four images into outDir and adds them to its manifest.json when ArtImport wrote one.</summary>
    public static void Run(string game, string outDir, Action<string> log, Action<int, int>? progress = null)
    {
        game = Path.GetFullPath(game); outDir = Path.GetFullPath(outDir);
        if ((outDir + "\\").StartsWith(game.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase)) throw new IOException("Refusing to write inside the game folder");
        var sw = Stopwatch.StartNew();
        int done = 0, total = 2 + Keys.Length;
        progress?.Invoke(0, total);
        var c = new ArtImport.Containers(game, n => n.StartsWith("generated/") && n.Contains("models/characters/"));
        progress?.Invoke(++done, total);
        var avatars = new Avatar[2];
        Parallel.For(0, 2, i => avatars[i] = new Avatar(c, i == 0 ? "corvo" : "emily"));
        progress?.Invoke(++done, total);
        Directory.CreateDirectory(outDir);
        var entries = new List<(string Key, JsonObject Entry)>();
        for (int k = 0; k < Keys.Length; k++)
        {
            var (rgba, w, h) = Render(avatars[k / 2], tpose: k % 2 == 1);
            var png = ArtImport.Png(rgba, w, h);
            var raw = new byte[8 + rgba.Length];
            BinaryPrimitives.WriteUInt32LittleEndian(raw, (uint)w); BinaryPrimitives.WriteUInt32LittleEndian(raw.AsSpan(4), (uint)h);
            rgba.CopyTo(raw, 8);
            ArtImport.WriteIfChanged(Path.Combine(outDir, Keys[k] + ".png"), png); ArtImport.WriteIfChanged(Path.Combine(outDir, Keys[k] + ".rgba"), raw);
            entries.Add((Keys[k], new JsonObject { ["file"] = Keys[k] + ".png", ["raw"] = Keys[k] + ".rgba", ["source"] = "rendered: player body, masked NPC head",
                ["width"] = w, ["height"] = h, ["sha256"] = ArtImport.Hex(SHA256.HashData(png)), ["raw_sha256"] = ArtImport.Hex(SHA256.HashData(raw)),
                ["note"] = "Calibration avatar pose for the VR settings page" }));
            log($"{Keys[k],-20} {w,5}x{h,-5} rendered");
            progress?.Invoke(++done, total);
        }
        var manifest = Path.Combine(outDir, "manifest.json");
        if (File.Exists(manifest))
        {
            var doc = JsonNode.Parse(File.ReadAllText(manifest))!.AsObject();
            var keys = doc["keys"] as JsonObject ?? new JsonObject(); doc["keys"] = keys;
            foreach (var (key, e) in entries) { keys[key] = e; (doc["missing"] as JsonObject)?.Remove(key); }
            var json = doc.ToJsonString(new System.Text.Json.JsonSerializerOptions { WriteIndented = true, Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping });
            ArtImport.WriteIfChanged(manifest, Encoding.UTF8.GetBytes(json + "\n"));
        }
        log($"{Keys.Length} calibration poses rendered in {sw.ElapsedMilliseconds} ms; do not commit or redistribute.");
    }

    /// <summary>Headless review (--render-poses): renders into outDir and writes poses_sheet.png (the four images side by side
    /// as in build/tmpcheck/poses_sheet.png).</summary>
    public static int Check(string outDir, TextWriter o)
    {
        var game = Env.FindGameDir(); if (game == null) { o.WriteLine("Dishonored 2 not found"); return 2; }
        var sw = Stopwatch.StartNew();
        Run(game, outDir, o.WriteLine);
        o.WriteLine($"total {sw.ElapsedMilliseconds} ms");
        var images = Keys.Select(k => new Bitmap(Path.Combine(outDir, k + ".png"))).ToList();
        float s = 420f / images.Max(b => b.Height);
        int width = 10 + images.Sum(b => (int)(b.Width * s) + 10);
        using var sheet = new Bitmap(width, 440);
        using (var g = Graphics.FromImage(sheet))
        {
            g.Clear(Color.FromArgb(28, 28, 40)); g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            int x = 10;
            foreach (var b in images) { int w = (int)(b.Width * s), h = (int)(b.Height * s); g.DrawImage(b, x, 430 - h, w, h); x += w + 10; b.Dispose(); }
        }
        sheet.Save(Path.Combine(outDir, "poses_sheet.png"), ImageFormat.Png);
        o.WriteLine("sheet " + Path.Combine(outDir, "poses_sheet.png"));
        return 0;
    }

    // ---------------------------------------------------------------- math (src/body_ik.hpp conventions: Z up, +X forward, +Y left)
    readonly record struct Xf(Quaternion Q, Vector3 P)
    {
        public static Xf operator *(Xf a, Xf b) => new(Quaternion.Normalize(a.Q * b.Q), a.P + Vector3.Transform(b.P, a.Q));
        public Xf Inverse() { var c = Quaternion.Conjugate(Q); return new(c, -Vector3.Transform(P, c)); }
        public Vector3 Apply(Vector3 v) => P + Vector3.Transform(v, Q);
    }
    static Vector3 Rot(Quaternion q, Vector3 v) => Vector3.Transform(v, q);
    static Vector3 Unit(Vector3 v) => Vector3.Normalize(v);
    static float Smooth01(float t) { t = Math.Clamp(t, 0, 1); return t * t * (3 - 2 * t); }
    static Quaternion AxisAngle(Vector3 axis, float angle) => axis.Length() > 1e-12f ? Quaternion.CreateFromAxisAngle(Unit(axis), angle) : Quaternion.Identity;
    static Quaternion FromAxes(Vector3 x, Vector3 y, Vector3 z) =>  // rotation whose columns are x, y, z
        Quaternion.Normalize(Quaternion.CreateFromRotationMatrix(new Matrix4x4(x.X, x.Y, x.Z, 0, y.X, y.Y, y.Z, 0, z.X, z.Y, z.Z, 0, 0, 0, 0, 1)));
    /// <summary>Rotation taking direction a to b and hinge ha to hb (body_ik.hpp frame_map).</summary>
    static Quaternion FrameMap(Vector3 a, Vector3 ha, Vector3 b, Vector3 hb)
    {
        static Quaternion Basis(Vector3 d, Vector3 h) { d = Unit(d); h = Unit(h - d * Vector3.Dot(h, d)); return FromAxes(d, h, Vector3.Cross(d, h)); }
        return Quaternion.Normalize(Basis(b, hb) * Quaternion.Conjugate(Basis(a, ha)));
    }

    // ---------------------------------------------------------------- assets
    sealed class Skel
    {
        public string[] Names = Array.Empty<string>(); public int[] Parent = Array.Empty<int>(); public Xf[] Local = Array.Empty<Xf>(), Bind = Array.Empty<Xf>();
        public int Find(string n) { int i = Array.IndexOf(Names, n); return i >= 0 ? i : throw new InvalidDataException("Missing joint " + n); }
        public void Finish() { Bind = new Xf[Local.Length]; for (int j = 0; j < Local.Length; j++) Bind[j] = Parent[j] < 0 ? Local[j] : Bind[Parent[j]] * Local[j]; }
        public Skel Clone() => new() { Names = Names, Parent = Parent, Local = (Xf[])Local.Clone(), Bind = (Xf[])Bind.Clone() };
    }

    sealed class Tex { public int W, H; public byte[] Rgba = Array.Empty<byte>(); }
    sealed class Surface { public string Material = ""; public int Start, Count, VertexStart, VertexCount; public uint Kind; public Tex? Tex; public bool AlphaTest; }
    sealed class Part
    {
        public Vector3[] P = Array.Empty<Vector3>(), N = Array.Empty<Vector3>(); public Vector2[] Uv = Array.Empty<Vector2>();
        public byte[] W = Array.Empty<byte>(), J = Array.Empty<byte>(); public int[] Idx = Array.Empty<int>();
        public List<Surface> Surfaces = new(); public string Skeleton = "";
        public Part Posed(Vector3[] p, Vector3[] n) => new() { P = p, N = n, Uv = Uv, W = W, J = J, Idx = Idx, Surfaces = Surfaces, Skeleton = Skeleton };
    }

    /// <summary>MD6SKL / edgeskl (build_player_asset_pack.py parse_skeleton): header, padded (joint, parent) u16 pairs, 48-byte
    /// transforms (quaternion xyzw, translation, scale), length-prefixed joint names starting at "origin".</summary>
    static Skel ParseSkeleton(byte[] d)
    {
        if (d.Length < 0x50 || !d.AsSpan(0, 4).SequenceEqual("60SE"u8)) throw new InvalidDataException("Not an MD6 skeleton");
        int n = BinaryPrimitives.ReadInt32LittleEndian(d.AsSpan(0x10)), start = 0x50, t0 = -1;
        if (n <= 0 || n > 1024) throw new InvalidDataException("Bad skeleton joint count");
        float F(int o) => BinaryPrimitives.ReadSingleLittleEndian(d.AsSpan(o));
        for (int off = start; off <= d.Length - 48 * n && t0 < 0; off += 4)
        {
            bool ok = true;
            for (int j = 0; j < n && ok; j++)
            {
                int o = off + 48 * j;
                float qq = F(o) * F(o) + F(o + 4) * F(o + 4) + F(o + 8) * F(o + 8) + F(o + 12) * F(o + 12);
                float sc = Math.Max(Math.Abs(F(o + 32) - 1), Math.Max(Math.Abs(F(o + 36) - 1), Math.Abs(F(o + 40) - 1)));
                if (Math.Abs(qq - 1) > 1e-3 || sc > 1e-4) ok = false;
            }
            if (ok) t0 = off;
        }
        if (t0 < 0) throw new InvalidDataException("Skeleton transforms not found");
        var parent = Enumerable.Repeat(-1, n).ToArray();
        for (int o = start; o < t0; o += 4)
        {
            int child = BinaryPrimitives.ReadUInt16LittleEndian(d.AsSpan(o)), p = BinaryPrimitives.ReadUInt16LittleEndian(d.AsSpan(o + 2));
            if (child == 0 || child >= n || p >= n) continue;
            parent[child] = p;
        }
        int at = d.AsSpan().IndexOf("\x06\0\0\0origin"u8);
        if (at < 0) throw new InvalidDataException("Joint name table not found");
        var names = new string[n];
        for (int j = 0; j < n; j++)
        {
            int len = BinaryPrimitives.ReadInt32LittleEndian(d.AsSpan(at)); at += 4;
            if (len < 0 || at + len > d.Length) throw new InvalidDataException("Truncated joint names");
            names[j] = Encoding.UTF8.GetString(d, at, len); at += len;
        }
        var local = new Xf[n];
        for (int j = 0; j < n; j++) { int o = t0 + 48 * j; local[j] = new(new Quaternion(F(o), F(o + 4), F(o + 8), F(o + 12)), new Vector3(F(o + 16), F(o + 20), F(o + 24))); }
        for (int j = 0; j < n; j++) if (parent[j] >= j) throw new InvalidDataException("Skeleton parents out of order");
        var s = new Skel { Names = names, Parent = parent, Local = local }; s.Finish(); return s;
    }

    /// <summary>MD6MESH / bmd6model (parse_mesh + merge_surfaces): 44-byte vertices = positions f32x3 | half2 uv0, uv1 |
    /// int8 normal | int8 tangent | 8 weight + 8 joint bytes; u16 indices. Surfaces whose material fails keep are dropped.</summary>
    static Part ParseMesh(byte[] d, Func<string, bool>? keep = null)
    {
        if (d.Length < 8 || !d.AsSpan(0, 4).SequenceEqual("MM6B"u8) || !d.AsSpan(d.Length - 4).SequenceEqual("MM6B"u8)) throw new InvalidDataException("Not an MD6 mesh");
        int o = 4;
        int U32() { if (o + 4 > d.Length) throw new InvalidDataException("Truncated mesh"); int v = BinaryPrimitives.ReadInt32LittleEndian(d.AsSpan(o)); o += 4; return v; }
        string Name() { int n = U32(); if (n < 0 || o + n > d.Length) throw new InvalidDataException("Truncated mesh"); var s = Encoding.UTF8.GetString(d, o, n); o += n; return s; }
        var part = new Part { Skeleton = Name() };
        o += 2 + 24;
        var p = new List<Vector3>(); var nr = new List<Vector3>(); var uv = new List<Vector2>(); var w = new List<byte>(); var j = new List<byte>(); var idx = new List<int>();
        int surfaces = U32();
        for (int s = 0; s < surfaces; s++)
        {
            Name(); string material = Name(); uint kind = checked((uint)U32()); o += 5;
            if (!d.AsSpan(o, 4).SequenceEqual("BTRI"u8)) throw new InvalidDataException("Unexpected mesh surface");
            o += 4; U32(); int cpu = U32(), gpu = U32(); o += 40;
            int nv = U32(); long streamed = BinaryPrimitives.ReadInt64LittleEndian(d.AsSpan(o)); o += 8;
            if (cpu != 10 || gpu != 10 || streamed != 0 || nv < 0 || o + (long)nv * 44 > d.Length) throw new InvalidDataException("Unverified vertex format");
            int v0 = o; o += nv * 44;
            int ni = U32(); int i0 = o; o += ni * 2;
            o += 24; U32(); int ranges = U32(); o += ranges * 12 + 48;
            if (ni < 0 || ni % 3 != 0 || o > d.Length) throw new InvalidDataException("Truncated mesh");
            if (keep != null && !keep(material)) continue;
            int b = p.Count;
            part.Surfaces.Add(new Surface { Material = material, Start = idx.Count, Count = ni, VertexStart = b, VertexCount = nv, Kind = kind });
            for (int k = 0; k < ni; k++) { int v = BinaryPrimitives.ReadUInt16LittleEndian(d.AsSpan(i0 + 2 * k)); if (v >= nv) throw new InvalidDataException("Mesh index out of range"); idx.Add(b + v); }
            for (int v = 0; v < nv; v++)
            {
                int q = v0 + 12 * v, a = v0 + nv * 12 + 8 * v, n = v0 + nv * 20 + 4 * v, sk = v0 + nv * 28 + 16 * v;
                p.Add(new(BinaryPrimitives.ReadSingleLittleEndian(d.AsSpan(q)), BinaryPrimitives.ReadSingleLittleEndian(d.AsSpan(q + 4)), BinaryPrimitives.ReadSingleLittleEndian(d.AsSpan(q + 8))));
                uv.Add(new((float)BitConverter.UInt16BitsToHalf(BinaryPrimitives.ReadUInt16LittleEndian(d.AsSpan(a))), (float)BitConverter.UInt16BitsToHalf(BinaryPrimitives.ReadUInt16LittleEndian(d.AsSpan(a + 2)))));
                nr.Add(new Vector3((sbyte)d[n], (sbyte)d[n + 1], (sbyte)d[n + 2]) / 127f);
                for (int k = 0; k < 8; k++) { w.Add(d[sk + k]); j.Add(d[sk + 8 + k]); }
            }
        }
        part.P = p.ToArray(); part.N = nr.ToArray(); part.Uv = uv.ToArray(); part.W = w.ToArray(); part.J = j.ToArray(); part.Idx = idx.ToArray();
        return part;
    }

    /// <summary>Diffuse map from its bimage7 header and the mip closest to 1024 px (render_calibration_poses.py loads every
    /// texture at most 1024 px).</summary>
    static Tex LoadTex(ArtImport.Containers c, string map)
    {
        var name = "generated/image/" + map + ".bimage7";
        var hdr = c.Read(name);
        if (hdr.Length < 0x20 || !hdr.AsSpan(0, 4).SequenceEqual("BIM\x18"u8)) throw new InvalidDataException("Not a bimage7: " + map);
        uint fmt = BinaryPrimitives.ReadUInt32LittleEndian(hdr.AsSpan(0x0c));
        // Separate mip resources (20-byte header: level, 0, width, height, size); the smallest one of at least 1024 px.
        int level = 0; while (c.Has($"{name}_mip{level + 1}")) level++;
        byte[] mip; int lv, w, h;
        for (; ; level--)
        {
            mip = c.Read($"{name}_mip{level}");
            if (mip.Length < 20) throw new InvalidDataException("Truncated bimage7 mip " + map);
            lv = BinaryPrimitives.ReadInt32LittleEndian(mip); w = BinaryPrimitives.ReadInt32LittleEndian(mip.AsSpan(8)); h = BinaryPrimitives.ReadInt32LittleEndian(mip.AsSpan(12));
            if (level == 0 || Math.Max(w, h) >= 1024) break;
        }
        uint bytes = BinaryPrimitives.ReadUInt32LittleEndian(mip.AsSpan(16));
        if (fmt is not (16 or 20) || lv != level || mip.Length != 20 + (long)bytes) throw new InvalidDataException($"Unsupported bimage7 mip {map} (format {fmt})");
        return new Tex { W = w, H = h, Rgba = Bc.Decode(mip.AsSpan(20), w, h, fmt == 20) };
    }

    // ---------------------------------------------------------------- avatar: player body + arms, masked NPC head, posed arms
    sealed class Avatar
    {
        const string Player = "models/characters/player/";
        static readonly Dictionary<string, string> Heads = new()
        {
            ["corvo"] = "models/characters/tall/str/corvo/t_str_corvo_head_mask.md6",       // skull mask, hair, face
            ["emily"] = "models/characters/female/emily/head01/emily_head01.md6",           // face, hair, cloth face mask
        };
        public readonly Skel Body; public readonly Skel[] Arm = new Skel[2];
        public readonly Part BodyMesh, Head; public readonly Part[] ArmMesh = new Part[2];
        readonly int spine3, camera, headJoint;
        readonly int[] upper = new int[2], elbow = new int[2], wrist = new int[2], attach = new int[2], clavicle = new int[2];
        readonly int[,,] finger = new int[2, 5, 4]; readonly Vector3[,,] fingerAxis = new Vector3[2, 5, 4];
        readonly Skel[] fit = new Skel[2];  // arms with the span-fitted upper/forearm links (body_ik.hpp hand_span)

        public Avatar(ArtImport.Containers c, string ch)
        {
            string Mesh(string part) => $"generated/basemodel/{Player}{ch}/player_{part}_{ch}.bmd6model";
            string Skl(string part) => $"generated/skeleton/{Player}emily/player_{part}_emily.edgeskl";  // Corvo reuses Emily's skeletons
            Body = ParseSkeleton(c.Read(Skl("body")));
            BodyMesh = ParseMesh(c.Read(Mesh("body")));
            for (int i = 0; i < 2; i++)
            {
                var part = i == 0 ? "leftarm" : "rightarm";
                Arm[i] = ParseSkeleton(c.Read(Skl(part))); ArmMesh[i] = ParseMesh(c.Read(Mesh(part)));
                if (!ArmMesh[i].Skeleton.EndsWith($"/player_{part}_emily.md6skl")) throw new InvalidDataException("Unexpected arm skeleton " + ArmMesh[i].Skeleton);
            }
            if (!BodyMesh.Skeleton.EndsWith("/player_body_emily.md6skl")) throw new InvalidDataException("Unexpected body skeleton " + BodyMesh.Skeleton);
            spine3 = Body.Find("center_spine03_jnt"); camera = Body.Find("camera_jnt"); headJoint = Body.Find("center_head_jnt");

            // Textures as render_body_ik.py _texture_path picks them; the ring and anything unknown is flat grey.
            var bodyMap = ch == "corvo" ? Player + "corvo/textures/player_corvo_body_d" : "models/characters/female/emily/textures/emily_body_d";
            var maps = new Dictionary<Surface, string>();
            foreach (var s in BodyMesh.Surfaces) maps[s] = bodyMap;
            for (int i = 0; i < 2; i++)
                foreach (var s in ArmMesh[i].Surfaces)
                {
                    var m = s.Material.ToLowerInvariant(); var t = $"{Player}{ch}/textures/player_{ch}_";
                    if (m.Contains("sleeves")) maps[s] = t + "sleeves_d"; else if (m.Contains("gloves")) maps[s] = t + "hands_d";
                    else if (m.Contains("hands")) maps[s] = t + "hands_tattoo_d"; else if (m.Contains("body")) maps[s] = bodyMap;
                }
            // The ring is skinned to a thumb helper; build_player_asset_pack.py binds it rigidly to the middle finger.
            for (int i = 0; i < 2; i++)
            {
                var a = ArmMesh[i]; int mid = Arm[i].Find((i == 0 ? "left" : "right") + "_medium_02_jnt");
                var used = new HashSet<int>();
                foreach (var s in a.Surfaces.Where(s => s.Material.Split('/')[^1].Contains("ring")))
                    for (int k = s.Start; k < s.Start + s.Count; k++) used.Add(a.Idx[k]);
                foreach (int v in used) for (int k = 0; k < 8; k++) { a.W[v * 8 + k] = (byte)(k == 0 ? 255 : 0); a.J[v * 8 + k] = (byte)(k == 0 ? mid : 0); }
            }

            // Masked head (extract_head_assets.py): decl -> lod 0 mesh -> skeleton -> per-surface material -> diffuse map.
            string Decl(string kind, string path) => $"generated/decls/{kind}/{path}.decl";
            var decl = Encoding.UTF8.GetString(c.Read(Decl("md6def", Heads[ch])));
            var meshPath = Regex.Match(decl, @"lod 0\s*\{\s*mesh\s+""([^""]+)""").Groups[1].Value;
            if (!meshPath.EndsWith(".md6mesh")) throw new InvalidDataException("Head decl has no lod 0 mesh");
            Head = ParseMesh(c.Read("generated/basemodel/" + meshPath[..^".md6mesh".Length] + ".bmd6model"),
                m => !(m.Contains("teeth") || m.Contains("lashes") || m.Contains("glass")));  // hidden or sub-pixel at this size
            if (!Head.Skeleton.EndsWith(".md6skl")) throw new InvalidDataException("Head mesh has no skeleton");
            var npc = ParseSkeleton(c.Read("generated/skeleton/" + Head.Skeleton[..^".md6skl".Length] + ".edgeskl"));
            foreach (var s in Head.Surfaces)
            {
                var mat = Encoding.UTF8.GetString(c.Read(Decl("material", s.Material)));
                var diffuse = Regex.Match(mat, @"^\s*diffusemap\s+(\S+)", RegexOptions.Multiline | RegexOptions.IgnoreCase);
                if (!diffuse.Success) throw new InvalidDataException("No diffuse map in " + s.Material);
                var map = diffuse.Groups[1].Value.ToLowerInvariant(); if (map.EndsWith(".tga")) map = map[..^4];
                maps[s] = map;
                s.AlphaTest = Regex.IsMatch(mat, @"^\s*alphatest\s+1", RegexOptions.Multiline | RegexOptions.IgnoreCase);
            }
            var tex = new Dictionary<string, Tex>();
            Parallel.ForEach(maps.Values.Distinct().ToList(), m => { var t = LoadTex(c, m); lock (tex) tex[m] = t; });
            foreach (var (s, m) in maps) s.Tex = tex[m];
            // Carry the NPC head rigidly: its center_head_jnt onto the player's bind head joint (pose_head; the body is at bind).
            var shift = Body.Bind[headJoint].P - npc.Bind[npc.Find("center_head_jnt")].P;
            for (int v = 0; v < Head.P.Length; v++) Head.P[v] += shift;

            // Arm joints, finger flexion axes (body_ik.hpp init_finger_axes) and the span fit.
            var palm = Rot(Quaternion.Conjugate(Body.Bind[spine3].Q), -Vector3.UnitZ);  // T-pose palms face down; arm-model space
            string[] fingers = { "thumb", "index", "medium", "ring", "pinky" };
            for (int i = 0; i < 2; i++)
            {
                var a = Arm[i]; var side = i == 0 ? "left" : "right";
                clavicle[i] = a.Find(side + "_shoulder_jnt"); upper[i] = a.Find(side + "_arm_jnt"); elbow[i] = a.Find(side + "_elbow_jnt");
                wrist[i] = a.Find(side + "_wrist_jnt"); attach[i] = a.Find(side + "_hand_attachment_jnt");
                if (a.Parent[attach[i]] != wrist[i]) throw new InvalidDataException("Hand attachment is not under the wrist");
                for (int f = 0; f < 5; f++) for (int k = 0; k < 4; k++) finger[i, f, k] = f == 0 && k == 3 ? -1 : a.Find($"{side}_{fingers[f]}_0{k + 1}_jnt");
                var across = a.Bind[finger[i, 2, 1]].P;
                for (int f = 0; f < 5; f++) for (int k = 0; k < 4; k++)
                {
                    int j = finger[i, f, k]; if (j < 0) continue;
                    int child = Enumerable.Range(0, a.Parent.Length).FirstOrDefault(cj => a.Parent[cj] == j && !a.Names[cj].Contains("helper"), -1);
                    if (child < 0) continue;
                    var d = Unit(a.Bind[child].P - a.Bind[j].P);
                    var axis = f == 0 ? Vector3.Cross(d, Unit(across - a.Bind[j].P)) : Vector3.Cross(d, palm);
                    if (axis.Length() < 1e-6f) continue;
                    fingerAxis[i, f, k] = Rot(Quaternion.Conjugate(a.Bind[j].Q), Unit(axis));
                }
            }
            // Arm links grow so the T-pose hand_attachment distance is the solver test's span (1.55 m at eye height 1.75 m).
            var chest = Body.Bind[spine3];
            Vector3 At(int i, int j) => (chest * Arm[i].Bind[j]).P;
            var span = At(0, attach[0]) - At(1, attach[1]);
            var limbs = (At(0, wrist[0]) - At(0, upper[0])) - (At(1, wrist[1]) - At(1, upper[1]));
            float want = HandSpan / Scale, qa = limbs.LengthSquared(), lin = 2 * Vector3.Dot(span, limbs), cst = span.LengthSquared() - want * want;
            float armScale = 1 + (-lin + MathF.Sqrt(Math.Max(0, lin * lin - 4 * qa * cst))) / (2 * qa);
            if (!(armScale is > .7f and < 1.45f)) throw new InvalidDataException("Unsupported fitted arm length");
            for (int i = 0; i < 2; i++)
            {
                fit[i] = Arm[i].Clone();
                for (int j = 0; j < fit[i].Local.Length; j++)
                    if (fit[i].Parent[j] == upper[i] || fit[i].Parent[j] == elbow[i]) fit[i].Local[j] = fit[i].Local[j] with { P = fit[i].Local[j].P * armScale };
                fit[i].Finish();
            }
        }

        // tests/body_ik_check.cpp: eye 1.75 m, hand span 1.55 m; the avatar is scaled from its bind eye height.
        const float EyeHeight = 1.75f, HandSpan = 1.55f;
        public float Scale => EyeHeight / Body.Bind[camera].P.Z;
        /// <summary>A test-world point (eye at (0, 0, 1.75)) in unscaled bind space (bind camera_jnt at the eye).</summary>
        public Vector3 FromWorld(Vector3 p) => Body.Bind[camera].P + (p - new Vector3(0, 0, EyeHeight)) / Scale;

        public Xf TeachingTarget(int i, TeachingHand hand)
        {
            var bindHand = (Body.Bind[spine3] * Arm[i].Bind[attach[i]]).Q;
            float side = i == 0 ? 1 : -1;
            var q = FrameMap(new(0, side, 0), new(0, 0, -1), hand.Direction, hand.Normal) * bindHand;
            return new(Quaternion.Normalize(q), FromWorld(hand.Position));
        }

        public Vector3 HandPosition(int i, Xf[] posed) => posed[attach[i]].P;
        public Vector3 WristPosition(int i, Xf[] posed) => posed[wrist[i]].P;

        /// <summary>Hand targets of the solver test: T-pose hands at {.02, +-span/2, eye-.27} with the bind hand rotation;
        /// shoulders hands at {.1, +-.2, eye-.3}, fingers up and palms in. Both with grip and trigger fully pressed (fists).</summary>
        public Xf HandTarget(int i, bool tpose)
        {
            var bindHand = (Body.Bind[spine3] * Arm[i].Bind[attach[i]]).Q;
            float side = i == 0 ? 1 : -1;
            if (tpose) return new(bindHand, FromWorld(new(.02f, side * HandSpan / 2, EyeHeight - .27f)));
            var q = FrameMap(new(0, side, 0), new(0, 0, -1), new(0, 0, 1), new(0, -side, 0)) * bindHand;
            return new(Quaternion.Normalize(q), FromWorld(new(.1f, side * .2f, EyeHeight - .3f)));
        }

        // body_ik.hpp finger_local: degrees per joint (01 metacarpal, 02 MCP, 03 PIP, 04 DIP), relaxed to closed.
        static readonly float[,] Relaxed = { { 8, 10, 12, 0 }, { 0, 12, 18, 8 }, { 0, 18, 24, 12 }, { 2, 22, 28, 14 }, { 4, 26, 32, 16 } };
        static readonly float[,] Closed = { { 25, 30, 45, 0 }, { 0, 80, 95, 55 }, { 0, 85, 100, 60 }, { 6, 88, 100, 60 }, { 12, 90, 100, 62 } };
        Xf HandLocal(int i, int j, float[]? curls = null)
        {
            var a = fit[i];
            for (int f = 0; f < 5; f++) for (int k = 0; k < 4; k++) if (finger[i, f, k] == j)
                    {
                        float amount = curls == null ? (f == 1 ? .85f : f == 0 ? .8f : 1f) : Math.Clamp(curls[f], 0, 1);  // calibration: grip = trigger = 1
                        float deg = Relaxed[f, k] + (Closed[f, k] - Relaxed[f, k]) * amount;
                        var axis = fingerAxis[i, f, k];
                        return axis.Length() < .5f ? a.Local[j] : new(Quaternion.Normalize(a.Local[j].Q * AxisAngle(axis, deg * MathF.PI / 180)), a.Local[j].P);
                    }
            return a.Local[j];
        }

        /// <summary>body_ik.hpp pose_arm for a still body (chest at bind, no previous pole or roll, procedural hand): world
        /// transforms of the arm's joints.</summary>
        public Xf[] PoseArm(int i, Xf hand, float[]? curls = null)
        {
            var a = fit[i]; int n = a.Local.Length; var chest = Body.Bind[spine3];
            var carried = new Xf[n]; for (int j = 0; j < n; j++) carried[j] = chest * a.Bind[j];
            var w = (Xf[])carried.Clone();
            Vector3 up = Vector3.UnitZ, fwd = Vector3.UnitX, outward = new(0, i == 0 ? 1 : -1, 0);
            var wt = hand * HandLocal(i, attach[i], curls).Inverse();
            float la = (a.Bind[elbow[i]].P - a.Bind[upper[i]].P).Length(), lb = (a.Bind[wrist[i]].P - a.Bind[elbow[i]].P).Length();
            {   // clavicle: shrug toward far or high reaches (bounded)
                Vector3 c = carried[clavicle[i]].P, u = carried[upper[i]].P, reach = wt.P - u; float r = reach.Length();
                float reaching = Smooth01((r / (la + lb) - .75f) / .3f), high = Smooth01((Vector3.Dot(Unit(reach), up) - .2f) / .7f);
                float amount = MathF.Max(reaching * .6f, high) * 22 * MathF.PI / 180;
                var from = u - c; var axis = Vector3.Cross(from, reach);
                if (axis.Length() > 1e-9f && r > 1e-6f)
                {
                    float gap = MathF.Acos(Math.Clamp(Vector3.Dot(Unit(from), Unit(reach)), -1, 1));
                    var q = AxisAngle(axis, MathF.Min(amount, gap));
                    for (int j = 1; j < n; j++) w[j] = new(Quaternion.Normalize(q * carried[j].Q), c + Rot(q, carried[j].P - c));
                }
            }
            var shoulder = w[upper[i]].P;
            var palm = Rot(wt.Q * Quaternion.Conjugate(carried[wrist[i]].Q), -Vector3.UnitZ);
            var reach2 = wt.P - shoulder;
            var pole = ElbowPole(reach2.Length() > 1e-4f ? reach2 : fwd, fwd, outward, up, la + lb, Vector3.Dot(palm, up));
            // Soft IK near full reach: the elbow straightens along an exponential; bones stretch at most 6 %.
            float full = la + lb, soft = .08f * full, d = reach2.Length(), extension = 1, distance = d;
            if (d > full - soft)
            {
                float softened = full - soft * MathF.Exp(-(d - (full - soft)) / soft);
                extension = MathF.Min(1.06f, d / softened); distance = softened * extension;
            }
            var toward = d > 1e-9f ? shoulder + reach2 * (distance / d) : wt.P;
            var (elbowP, wristP) = SolveArm(shoulder, toward, pole, la * extension, lb * extension);
            var hinge = HingeOf(shoulder, elbowP, wristP, pole);
            var bindHinge = Unit(Vector3.Cross(-Vector3.UnitX, Unit(w[wrist[i]].P - w[upper[i]].P)));  // T-pose arm flexes forward
            Xf bu = w[upper[i]], be = w[elbow[i]], bw = w[wrist[i]];
            var qa = FrameMap(be.P - bu.P, bindHinge, elbowP - shoulder, hinge);
            var qb = FrameMap(bw.P - be.P, bindHinge, wristP - elbowP, hinge);
            for (int j = 0; j < n; j++) if (a.Parent[j] == upper[i] && j != elbow[i]) w[j] = new(Quaternion.Normalize(qa * w[j].Q), shoulder + Rot(qa, w[j].P - bu.P));
            w[upper[i]] = new(Quaternion.Normalize(qa * bu.Q), shoulder);
            // Forearm twist toward the wrist roll, spread along the helpers.
            var axis2 = Unit(wristP - elbowP);
            var dq = wt.Q * Quaternion.Conjugate(qb * bw.Q);
            float raw = 2 * MathF.Atan2(dq.X * axis2.X + dq.Y * axis2.Y + dq.Z * axis2.Z, dq.W);
            raw = MathF.IEEERemainder(raw, 2 * MathF.PI);
            float roll = Math.Clamp(raw, -2.3f, 2.3f);
            for (int j = 0; j < n; j++) if (a.Parent[j] == elbow[i] && j != wrist[i])
                {
                    float along = Math.Clamp(Vector3.Dot(w[j].P - be.P, bw.P - be.P) / (bw.P - be.P).LengthSquared(), 0, 1);
                    w[j] = new(Quaternion.Normalize(AxisAngle(axis2, roll * along) * qb * w[j].Q), elbowP + Rot(qb, w[j].P - be.P) * extension);
                }
            w[elbow[i]] = new(Quaternion.Normalize(qb * be.Q), elbowP);
            w[wrist[i]] = new(wt.Q, wristP);
            for (int j = 0; j < n; j++)  // hand subtree follows the wrist with curled fingers
            {
                bool under = false;
                for (int p = a.Parent[j]; p >= 0 && !under; p = a.Parent[p]) under = p == wrist[i];
                if (under) w[j] = w[a.Parent[j]] * HandLocal(i, j, curls);
            }
            return w;
        }

        /// <summary>Boneworks elbows (arm_body.hpp boneworks_elbow_pole without a previous pole): down and slightly out,
        /// rising for high or rear reaches; palm-up turns the elbow in.</summary>
        static Vector3 ElbowPole(Vector3 reach, Vector3 fwd, Vector3 outward, Vector3 up, float length, float palmUp)
        {
            float raise = MathF.Max(Smooth01((Vector3.Dot(reach, up) / length - .25f) / .6f), Smooth01(-Vector3.Dot(reach, fwd) / length / .4f));
            palmUp = Math.Clamp(palmUp, -1, 1);
            var hint = outward * (.45f + .5f * raise - .6f * palmUp) - up * (1 - 1.3f * raise) - fwd * .15f;
            var dir = Unit(reach);
            Vector3 Project(Vector3 v) => v - dir * Vector3.Dot(v, dir);
            var bend = Project(Unit(hint));
            if (bend.Length() < 1e-6f) bend = Project(outward);
            if (bend.Length() < 1e-6f) bend = Project(fwd);
            return Unit(bend);
        }

        /// <summary>arm_ik.hpp solve_arm: elbow and wrist positions of a two-bone chain.</summary>
        static (Vector3 Elbow, Vector3 Wrist) SolveArm(Vector3 shoulder, Vector3 target, Vector3 pole, float ua, float fa)
        {
            var delta = target - shoulder; float requested = delta.Length();
            var dir = requested > 1e-9f ? delta / requested : -Vector3.UnitZ;
            float distance = Math.Clamp(requested, Math.Max(1e-6f, Math.Abs(ua - fa)), ua + fa);
            pole = pole.Length() > 1e-9f ? Unit(pole) : Vector3.Zero;
            var bend = pole - dir * Vector3.Dot(pole, dir);
            if (bend.Length() < 1e-6f)
            {
                var ax = Math.Abs(dir.X) <= Math.Abs(dir.Y) && Math.Abs(dir.X) <= Math.Abs(dir.Z) ? Vector3.UnitX : Math.Abs(dir.Y) <= Math.Abs(dir.Z) ? Vector3.UnitY : Vector3.UnitZ;
                bend = ax - dir * Vector3.Dot(ax, dir);
            }
            bend = Unit(bend);
            float along = (ua * ua - fa * fa + distance * distance) / (2 * distance), height = MathF.Sqrt(MathF.Max(0, ua * ua - along * along));
            return (shoulder + dir * along + bend * height, shoulder + dir * distance);
        }

        static Vector3 HingeOf(Vector3 a, Vector3 b, Vector3 c, Vector3 pole)
        {
            var dir = Unit(c - a); var bend = b - (a + dir * Vector3.Dot(b - a, dir));
            if (bend.Length() < 1e-6f) bend = pole - dir * Vector3.Dot(pole, dir);
            return Unit(Vector3.Cross(Unit(bend), dir));
        }

        /// <summary>Linear blend skinning of an arm (palette = posed joint * inverse bind; mesh in arm-model space).</summary>
        public Part SkinArm(int i, Xf[] world)
        {
            var m = ArmMesh[i]; var pal = new Xf[world.Length];
            for (int j = 0; j < pal.Length; j++) pal[j] = world[j] * Arm[i].Bind[j].Inverse();
            var p = new Vector3[m.P.Length]; var nr = new Vector3[m.P.Length];
            for (int v = 0; v < p.Length; v++)
            {
                Vector3 sp = default, sn = default;
                for (int k = 0; k < 8; k++)
                {
                    float wk = m.W[v * 8 + k] / 255f; if (wk <= 0) continue;
                    var x = pal[m.J[v * 8 + k]]; sp += wk * x.Apply(m.P[v]); sn += wk * Rot(x.Q, m.N[v]);
                }
                p[v] = sp; nr[v] = sn.Length() > 1e-9f ? Unit(sn) : sn;
            }
            return m.Posed(p, nr);
        }
    }

    // ---------------------------------------------------------------- renderer (render_calibration_poses.py render)
    static readonly Vector3 Grey = new(.35f, .35f, .37f);
    // Lights: direction in the camera basis (right, up, towards camera) and linear RGB intensity.
    static readonly (Vector3 Dir, Vector3 Rgb) Key = (new(-.65f, .65f, .45f), new(2.5f, 2.38f, 2.2f)), Fill = (new(.85f, -.05f, .5f), new(.3f, .34f, .42f)),
        Rim = (new(.55f, .3f, -.8f), new(.55f, .95f, 1.4f));
    static readonly Vector3 Ambient = new(.05f, .055f, .07f);
    const float Black = .03f, GrainSigma = .012f;

    sealed class View
    {
        public readonly int W, H; public Vector3 Origin; public readonly Vector3 Fwd, Right, Up; public readonly float Span, Scale;
        public View(int w, int h, Vector3 origin, Vector3 fwd, Vector3 right, Vector3 up, float span)
        { W = w; H = h; Origin = origin; Fwd = Unit(fwd); Right = Unit(right); Up = Unit(up); Span = span; Scale = h / span; }
        public Vector3 Project(Vector3 p) { var r = p - Origin; return new(W * .5f + Vector3.Dot(r, Right) * Scale, H * .5f - Vector3.Dot(r, Up) * Scale, Vector3.Dot(r, Fwd)); }
        public Vector3 Dir((Vector3 Dir, Vector3 Rgb) l) => Unit(l.Dir.X * Right + l.Dir.Y * Up - l.Dir.Z * Fwd);
    }

    sealed class GBuffer
    {
        public readonly float[] Depth; public readonly Vector3[]? Albedo, Normal;
        public GBuffer(int n, bool color) { Depth = new float[n]; Array.Fill(Depth, float.PositiveInfinity); if (color) { Albedo = new Vector3[n]; Normal = new Vector3[n]; } }
    }

    /// <summary>Z-buffered, alpha-tested triangles with nearest texels (raster_triangle); orthographic views only.</summary>
    static GBuffer Rasterize(View view, IEnumerable<Part> parts, bool color)
    {
        var g = new GBuffer(view.W * view.H, color);
        foreach (var part in parts)
        {
            var pt = part.P.Select(view.Project).ToArray();
            foreach (var s in part.Surfaces)
                for (int t = s.Start; t < s.Start + s.Count; t += 3)
                {
                    int i0 = part.Idx[t], i1 = part.Idx[t + 1], i2 = part.Idx[t + 2];
                    Vector3 a = pt[i0], b = pt[i1], c = pt[i2];
                    int xlo = (int)MathF.Max(MathF.Min(a.X, MathF.Min(b.X, c.X)), 0), xhi = (int)MathF.Min(MathF.Max(a.X, MathF.Max(b.X, c.X)) + 1, view.W);
                    int ylo = (int)MathF.Max(MathF.Min(a.Y, MathF.Min(b.Y, c.Y)), 0), yhi = (int)MathF.Min(MathF.Max(a.Y, MathF.Max(b.Y, c.Y)) + 1, view.H);
                    float den = (b.Y - c.Y) * (a.X - c.X) + (c.X - b.X) * (a.Y - c.Y);
                    if (xhi <= xlo || yhi <= ylo || MathF.Abs(den) < .2f) continue;
                    for (int y = ylo; y < yhi; y++)
                    {
                        float sy = y + .5f;
                        for (int x = xlo; x < xhi; x++)
                        {
                            float sx = x + .5f;
                            float l0 = ((b.Y - c.Y) * (sx - c.X) + (c.X - b.X) * (sy - c.Y)) / den, l1 = ((c.Y - a.Y) * (sx - c.X) + (a.X - c.X) * (sy - c.Y)) / den, l2 = 1 - l0 - l1;
                            if (l0 < 0 || l1 < 0 || l2 < 0) continue;
                            float z = l0 * a.Z + l1 * b.Z + l2 * c.Z; int o = y * view.W + x;
                            if (!(z < g.Depth[o])) continue;
                            Vector3 albedo = Grey;
                            if (s.Tex is Tex tx)
                            {
                                var uv = l0 * part.Uv[i0] + l1 * part.Uv[i1] + l2 * part.Uv[i2];
                                int u = Math.Min((int)((uv.X - MathF.Floor(uv.X)) * tx.W), tx.W - 1), v = Math.Min((int)((uv.Y - MathF.Floor(uv.Y)) * tx.H), tx.H - 1);
                                int k = (v * tx.W + u) * 4;
                                if (s.AlphaTest && tx.Rgba[k + 3] < 128) continue;
                                albedo = new Vector3(tx.Rgba[k], tx.Rgba[k + 1], tx.Rgba[k + 2]) / 255f;
                            }
                            g.Depth[o] = z;
                            if (g.Albedo != null) { g.Albedo[o] = albedo; g.Normal![o] = l0 * part.N[i0] + l1 * part.N[i1] + l2 * part.N[i2]; }
                        }
                    }
                }
        }
        return g;
    }

    static (byte[] Rgba, int W, int H) Render(Avatar av, bool tpose, int size = 900, int supersample = 2)
    {
        var parts = new List<Part> { av.BodyMesh, av.Head };
        for (int i = 0; i < 2; i++) parts.Add(av.SkinArm(i, av.PoseArm(i, av.HandTarget(i, tpose))));
        int W = size * supersample, N = W * W;
        // Slightly turned (20 deg) and raised so the arms read in depth, like a character-select pose.
        float az = 20 * MathF.PI / 180, el = 6 * MathF.PI / 180;
        var fwd = -new Vector3(MathF.Cos(el) * MathF.Cos(az), MathF.Cos(el) * MathF.Sin(az), MathF.Sin(el));
        var right = Unit(Vector3.Cross(fwd, Vector3.UnitZ)); var up = Vector3.Cross(right, fwd);
        Vector3 lo = new(float.MaxValue), hi = new(float.MinValue);
        foreach (var p in parts) foreach (var v in p.P) { lo = Vector3.Min(lo, v); hi = Vector3.Max(hi, v); }
        var ext = hi - lo; var centre = (lo + hi) / 2;
        var view = new View(W, W, centre - fwd * 5, fwd, right, up, MathF.Max(ext.X, MathF.Max(ext.Y, ext.Z)) * 1.08f);
        var g = Rasterize(view, parts, true);
        var mask = new bool[N]; for (int o = 0; o < N; o++) mask[o] = float.IsFinite(g.Depth[o]);
        KeepLargest(mask, W, W);  // Emily's arm mesh has two shoulder-piece triangles skinned to the thumb that float free
        var nearest = Nearest(mask, W, W);
        var color = Shade(g, mask, nearest, view, KeyShadow(view, g.Depth, mask, parts));
        // Spread figure colours into the background so smoothing and resampling never pull in black at the silhouette.
        var spread = new Vector3[N]; Parallel.For(0, N, o => spread[o] = color[nearest[o]]);
        color = Kuwahara(spread, W, W);
        var rgba = new byte[N * 4];
        Parallel.For(0, N, o =>
        {
            var c = color[o] * 255 + new Vector3(.5f);
            rgba[o * 4] = (byte)c.X; rgba[o * 4 + 1] = (byte)c.Y; rgba[o * 4 + 2] = (byte)c.Z; rgba[o * 4 + 3] = (byte)(mask[o] ? 255 : 0);
        });
        var small = Downsample(rgba, W, W, size, size);
        var rng = new Random(7);  // film grain, the same for all three channels
        int minX = size, minY = size, maxX = -1, maxY = -1;
        for (int y = 0; y < size; y++)
            for (int x = 0; x < size; x++)
            {
                int o = (y * size + x) * 4;
                float n = MathF.Sqrt(-2 * MathF.Log(1 - rng.NextSingle())) * MathF.Cos(2 * MathF.PI * rng.NextSingle()) * GrainSigma * 255;
                for (int k = 0; k < 3; k++) small[o + k] = (byte)Math.Clamp(small[o + k] + n + .5f, 0, 255);
                if (small[o + 3] > 8) { minX = Math.Min(minX, x); maxX = Math.Max(maxX, x); minY = Math.Min(minY, y); maxY = Math.Max(maxY, y); }
            }
        if (maxX < 0) throw new InvalidDataException("Empty calibration render");
        int cw = maxX - minX + 1, ch = maxY - minY + 1; var crop = new byte[cw * ch * 4];
        for (int y = 0; y < ch; y++) Array.Copy(small, ((minY + y) * size + minX) * 4, crop, y * cw * 4, cw * 4);
        return (crop, cw, ch);
    }

    /// <summary>Shadow map for the key light: 1 where lit, 0 where another part (head, arms, collar) blocks it; softened.</summary>
    static float[] KeyShadow(View view, float[] depth, bool[] mask, List<Part> parts, int size = 1400)
    {
        var key = view.Dir(Key); var lf = -key;
        var right = Unit(Vector3.Cross(lf, Vector3.UnitZ));
        var light = new View(size, size, Vector3.Zero, lf, right, Vector3.Cross(right, lf), view.Span * 1.2f);
        light.Origin = view.Origin + view.Fwd * 5 - light.Fwd * 5;  // look at the same centre
        var occ = Rasterize(light, parts, false).Depth;
        var lit = new float[mask.Length];
        Parallel.For(0, view.H, y =>
        {
            for (int x = 0; x < view.W; x++)
            {
                int o = y * view.W + x; lit[o] = 1;
                if (!mask[o]) continue;
                var world = view.Origin + (x + .5f - view.W * .5f) / view.Scale * view.Right + (view.H * .5f - y - .5f) / view.Scale * view.Up + depth[o] * view.Fwd;
                var l = light.Project(world);
                int lx = Math.Clamp((int)l.X, 0, size - 1), ly = Math.Clamp((int)l.Y, 0, size - 1);
                lit[o] = l.Z <= occ[ly * size + lx] + .02f ? 1 : 0;
            }
        });
        return Gaussian(lit, view.W, view.H, 2.5f);
    }

    static Vector3[] Shade(GBuffer g, bool[] mask, int[] nearest, View view, float[] shadow)
    {
        int N = mask.Length;
        var toward = -view.Fwd;
        Vector3 keyDir = view.Dir(Key), fillDir = view.Dir(Fill), rimDir = view.Dir(Rim), half = Unit(keyDir + toward);
        // Cheap ambient occlusion: darken pixels lying behind their surroundings (crevices, under collars and arms).
        var filled = new float[N]; Parallel.For(0, N, o => filled[o] = g.Depth[nearest[o]]);
        var blurred = Gaussian(filled, view.W, view.H, 14);
        var outc = new Vector3[N];
        Parallel.For(0, N, o =>
        {
            var nn = g.Normal![o]; float len = nn.Length(); var n = nn / MathF.Max(len, 1e-9f);
            float facing = Vector3.Dot(n, toward);
            if (facing < 0) n = -n;
            facing = MathF.Abs(facing);
            float occlusion = 1 - Math.Clamp((filled[o] - blurred[o]) / .05f, 0, .55f);
            float nk = Vector3.Dot(n, keyDir);
            var diffuse = (Key.Rgb * (MathF.Max(nk, 0) * shadow[o]) + Fill.Rgb * MathF.Max((Vector3.Dot(n, fillDir) + .3f) / 1.3f, 0) + Ambient) * occlusion;
            float spec = .06f * MathF.Pow(MathF.Max(Vector3.Dot(n, half), 0), 32) * (nk > 0 ? 1 : 0) * occlusion * shadow[o];
            var rim = Rim.Rgb * (MathF.Pow(1 - facing, 2.5f) * MathF.Max(Vector3.Dot(n, rimDir) + .2f, 0));
            var a = g.Albedo![o];
            var hdr = new Vector3(MathF.Pow(a.X, 2.2f), MathF.Pow(a.Y, 2.2f), MathF.Pow(a.Z, 2.2f)) * diffuse + new Vector3(spec) + rim;
            float Tone(float x) { float v = Math.Clamp(x * (2.51f * x + .03f) / (x * (2.43f * x + .59f) + .14f), 0, 1); v = MathF.Pow(v, 1 / 2.2f); return MathF.Pow(Math.Clamp((v - Black) / (1 - Black), 0, 1), 1.08f); }
            outc[o] = new(Tone(hdr.X), Tone(hdr.Y), Tone(hdr.Z));
        });
        return outc;
    }

    // ---------------------------------------------------------------- image helpers (scipy.ndimage / Pillow equivalents)
    /// <summary>Keeps the mask pixels of the largest 4-connected component of the mask dilated by 8 px (city block).</summary>
    static void KeepLargest(bool[] mask, int w, int h)
    {
        var d = new int[mask.Length];
        for (int o = 0; o < d.Length; o++) d[o] = mask[o] ? 0 : 1 << 20;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) { int o = y * w + x; if (y > 0) d[o] = Math.Min(d[o], d[o - w] + 1); if (x > 0) d[o] = Math.Min(d[o], d[o - 1] + 1); }
        for (int y = h - 1; y >= 0; y--) for (int x = w - 1; x >= 0; x--) { int o = y * w + x; if (y < h - 1) d[o] = Math.Min(d[o], d[o + w] + 1); if (x < w - 1) d[o] = Math.Min(d[o], d[o + 1] + 1); }
        var label = new int[mask.Length]; var count = new List<int> { 0 }; var stack = new Stack<int>();
        for (int s = 0; s < d.Length; s++)
        {
            if (d[s] > 8 || label[s] != 0) continue;
            int id = count.Count, n = 0; label[s] = id; stack.Push(s);
            while (stack.Count > 0)
            {
                int o = stack.Pop(), x = o % w; if (mask[o]) n++;
                void Visit(int q) { if (d[q] <= 8 && label[q] == 0) { label[q] = id; stack.Push(q); } }
                if (x > 0) Visit(o - 1); if (x < w - 1) Visit(o + 1); if (o >= w) Visit(o - w); if (o + w < d.Length) Visit(o + w);
            }
            count.Add(n);
        }
        int best = count.IndexOf(count.Max());
        for (int o = 0; o < mask.Length; o++) mask[o] &= label[o] == best;
    }

    /// <summary>Index of the nearest mask pixel for every pixel (exact Euclidean feature transform, Felzenszwalb-Huttenlocher),
    /// as ndimage.distance_transform_edt(~mask, return_indices=True).</summary>
    static int[] Nearest(bool[] mask, int w, int h)
    {
        var row = new int[w * h];  // nearest mask row within the column, -1 if the column has none
        Parallel.For(0, w, x =>
        {
            int last = -1;
            for (int y = 0; y < h; y++) { if (mask[y * w + x]) last = y; row[y * w + x] = last; }
            int next = -1;
            for (int y = h - 1; y >= 0; y--)
            {
                if (mask[y * w + x]) next = y;
                int prev = row[y * w + x];
                if (next >= 0 && (prev < 0 || next - y < y - prev)) row[y * w + x] = next;
            }
        });
        if (!mask.Contains(true)) throw new InvalidDataException("Empty calibration render");
        var result = new int[w * h];
        Parallel.For(0, h, y =>
        {
            var f = new double[w]; var v = new int[w]; var z = new double[w + 1];
            for (int x = 0; x < w; x++) { int r = row[y * w + x]; f[x] = r < 0 ? double.PositiveInfinity : (double)(r - y) * (r - y); }
            int k = -1;
            for (int q = 0; q < w; q++)
            {
                if (double.IsInfinity(f[q])) continue;
                double s = 0;
                while (k >= 0 && (s = (f[q] + (double)q * q - f[v[k]] - (double)v[k] * v[k]) / (2.0 * q - 2.0 * v[k])) <= z[k]) k--;
                k++; v[k] = q; z[k] = k == 0 ? double.NegativeInfinity : s; z[k + 1] = double.PositiveInfinity;
            }
            for (int x = 0, j = 0; x < w; x++)
            {
                while (z[j + 1] < x) j++;
                result[y * w + x] = row[y * w + v[j]] * w + v[j];
            }
        });
        return result;
    }

    /// <summary>ndimage.gaussian_filter (mode reflect, truncate 4).</summary>
    static float[] Gaussian(float[] src, int w, int h, float sigma)
    {
        int r = (int)(4 * sigma + .5f); var k = new float[2 * r + 1]; float sum = 0;
        for (int i = -r; i <= r; i++) sum += k[i + r] = MathF.Exp(-.5f * i * i / (sigma * sigma));
        for (int i = 0; i < k.Length; i++) k[i] /= sum;
        static int Reflect(int i, int n) => i < 0 ? -i - 1 : i >= n ? 2 * n - i - 1 : i;
        var tmp = new float[src.Length]; var dst = new float[src.Length];
        Parallel.For(0, h, y =>
        {
            for (int x = 0; x < w; x++) { float s = 0; for (int i = -r; i <= r; i++) s += k[i + r] * src[y * w + Reflect(x + i, w)]; tmp[y * w + x] = s; }
        });
        Parallel.For(0, h, y =>
        {
            for (int i = -r; i <= r; i++) { int yy = Reflect(y + i, h) * w; float kk = k[i + r]; for (int x = 0; x < w; x++) dst[y * w + x] += kk * tmp[yy + x]; }
        });
        return dst;
    }

    /// <summary>Painterly finish: each pixel takes the 3x3 mean of its least-varied diagonal neighbour window (Kuwahara, radius 2).</summary>
    static readonly (int, int)[] Diagonals = { (1, 1), (1, -1), (-1, 1), (-1, -1) };  // np.roll order of the Python
    static Vector3[] Kuwahara(Vector3[] img, int w, int h)
    {
        var mean = new Vector3[img.Length]; var vari = new float[img.Length];
        Parallel.For(0, h, y =>
        {
            for (int x = 0; x < w; x++)
            {
                Vector3 s = default, s2 = default;
                for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { var c = img[Math.Clamp(y + dy, 0, h - 1) * w + Math.Clamp(x + dx, 0, w - 1)]; s += c; s2 += c * c; }
                s /= 9; s2 /= 9; var v = s2 - s * s;
                mean[y * w + x] = s; vari[y * w + x] = v.X + v.Y + v.Z;
            }
        });
        var outp = new Vector3[img.Length];
        Parallel.For(0, h, y =>
        {
            for (int x = 0; x < w; x++)
            {
                int best = -1; float bv = float.MaxValue;
                foreach (var (dy, dx) in Diagonals)
                {
                    int o = Math.Clamp(y + dy, 0, h - 1) * w + Math.Clamp(x + dx, 0, w - 1);
                    if (vari[o] < bv) { bv = vari[o]; best = o; }
                }
                outp[y * w + x] = mean[best];
            }
        });
        return outp;
    }

    /// <summary>Pillow's LANCZOS resize of an RGBA8 image (premultiplied while filtering, separable).</summary>
    internal static byte[] Downsample(byte[] src, int sw, int sh, int dw, int dh)
    {
        static (int Min, float[] W)[] Taps(int inSize, int outSize)
        {
            float scale = (float)inSize / outSize, support = 3 * Math.Max(scale, 1), fs = 1 / Math.Max(scale, 1);
            static float Sinc(float x) => x == 0 ? 1 : MathF.Sin(MathF.PI * x) / (MathF.PI * x);
            var taps = new (int, float[])[outSize];
            for (int i = 0; i < outSize; i++)
            {
                float centre = (i + .5f) * scale; int min = Math.Max((int)(centre - support + .5f), 0), max = Math.Min((int)(centre + support + .5f), inSize);
                var w = new float[max - min]; float sum = 0;
                for (int j = 0; j < w.Length; j++) { float x = (j + min - centre + .5f) * fs; sum += w[j] = MathF.Abs(x) < 3 ? Sinc(x) * Sinc(x / 3) : 0; }
                for (int j = 0; j < w.Length; j++) w[j] /= sum;
                taps[i] = (min, w);
            }
            return taps;
        }
        var pre = new Vector4[sw * sh];
        for (int o = 0; o < pre.Length; o++) { float a = src[o * 4 + 3] / 255f; pre[o] = new(src[o * 4] * a, src[o * 4 + 1] * a, src[o * 4 + 2] * a, src[o * 4 + 3]); }
        var tx = Taps(sw, dw); var ty = Taps(sh, dh);
        var mid = new Vector4[dw * sh];
        Parallel.For(0, sh, y => { for (int x = 0; x < dw; x++) { var (m, w) = tx[x]; Vector4 s = default; for (int j = 0; j < w.Length; j++) s += w[j] * pre[y * sw + m + j]; mid[y * dw + x] = s; } });
        var dst = new byte[dw * dh * 4];
        Parallel.For(0, dh, y =>
        {
            var (m, w) = ty[y];
            for (int x = 0; x < dw; x++)
            {
                Vector4 s = default; for (int j = 0; j < w.Length; j++) s += w[j] * mid[(m + j) * dw + x];
                float a = Math.Clamp(s.W, 0, 255); int o = (y * dw + x) * 4;
                dst[o + 3] = (byte)(a + .5f);
                if (dst[o + 3] == 0) continue;
                float inv = 255 / Math.Max(a, 1e-3f);  // premultiplied by alpha / 255
                dst[o] = (byte)Math.Clamp(s.X * inv + .5f, 0, 255); dst[o + 1] = (byte)Math.Clamp(s.Y * inv + .5f, 0, 255); dst[o + 2] = (byte)Math.Clamp(s.Z * inv + .5f, 0, 255);
            }
        });
        return dst;
    }
}
