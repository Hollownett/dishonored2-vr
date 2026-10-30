using System.Numerics;
using System.Text;
using System.Text.RegularExpressions;

namespace DishonoredVR;

public static partial class PoseRender
{
    /// <summary>The Karnaca Grand Guard from the owner's install: the cloth-free body, its LOD 0 cloth pieces (shirt,
    /// trousers, sash, chain) and the default head, all skinned to gs_guard_01's skeleton by joint name. The bind pose
    /// holds the arms out; the guide lowers them at the shoulder and bends the elbows slightly.</summary>
    sealed class Guard
    {
        const string Dir = "models/characters/small/guards/gs_guards/gs_guard_01/";
        public static bool Wanted(string n) => n.StartsWith("nocloth_gs_guard_01_body_") && !n.Contains("_lod") ||
            n.StartsWith("voidresourcecache/md6model/cloth28/0/gs_guard_01_body_");
        public readonly List<Part> Parts = new();
        public readonly Vector3[] Hands = new Vector3[2];
        readonly Skel skel;
        public readonly float Scale;  // metres per model unit

        public Guard(ArtImport.Containers c)
        {
            skel = ParseSkeleton(c.Read($"generated/skeleton/{Dir}gs_guard_01.edgeskl"));
            var meshes = c.Names.Where(Wanted).OrderBy(n => n).Select(c.Read).ToList();
            if (meshes.Count < 2) throw new InvalidDataException("Guard body or cloth missing");
            meshes.Add(c.Read($"generated/basemodel/{Dir}gs_guard_01_head.bmd6model"));
            var skeletons = new Dictionary<string, Skel>();
            var maps = new Dictionary<Surface, string>();
            foreach (var bytes in meshes)
            {
                var part = ParseMesh(bytes, m => !(m.Contains("teeth") || m.Contains("lashes") || m.Contains("eye") || m.Contains("gore")));
                // Joint indices follow the mesh's own skeleton (none named: the body's); carry them over by name.
                if (part.Skeleton == "") part.Skeleton = $"{Dir}gs_guard_01.md6skl";
                if (!skeletons.TryGetValue(part.Skeleton, out var own))
                    skeletons[part.Skeleton] = own = ParseSkeleton(c.Read("generated/skeleton/" + part.Skeleton[..^".md6skl".Length] + ".edgeskl"));
                var remap = own.Names.Select(n => Array.IndexOf(skel.Names, n)).ToArray();
                for (int k = 0; k < part.J.Length; k++)
                {
                    int to = part.J[k] < remap.Length ? remap[part.J[k]] : -1;
                    if (to < 0 || to > 255) { if (part.W[k] != 0) to = skel.Find("center_head_jnt"); else to = 0; }
                    part.J[k] = (byte)to;
                }
                foreach (var s in part.Surfaces)
                {
                    try
                    {
                        var mat = Encoding.UTF8.GetString(c.Read($"generated/decls/material/{s.Material}.decl"));
                        s.AlphaTest = Regex.IsMatch(mat, @"^\s*alphatest\s+1", RegexOptions.Multiline | RegexOptions.IgnoreCase);
                        var diffuse = Regex.Match(mat, @"^\s*diffusemap\s+(\S+)", RegexOptions.Multiline | RegexOptions.IgnoreCase);
                        if (diffuse.Success) maps[s] = diffuse.Groups[1].Value.ToLowerInvariant().Replace(".tga", "");
                    }
                    catch (InvalidDataException) { }  // shared material decls elsewhere: flat grey is fine in ink
                }
                Parts.Add(part);
            }
            var tex = new Dictionary<string, Tex?>();
            Parallel.ForEach(maps.Values.Distinct().ToList(), m => { Tex? t = null; try { t = LoadTex(c, m); } catch (InvalidDataException) { } lock (tex) tex[m] = t; });
            foreach (var (s, m) in maps) s.Tex = tex[m];
            Scale = 1.62f / skel.Bind[skel.Find("center_head_jnt")].P.Z;

            // Lower each arm (bind: straight out to the side) by turning the upper arm about the forward axis.
            var pose = (Xf[])skel.Local.Clone();
            var world = new Xf[pose.Length];
            void Solve() { for (int j = 0; j < pose.Length; j++) world[j] = skel.Parent[j] < 0 ? pose[j] : world[skel.Parent[j]] * pose[j]; }
            Solve();
            foreach (var (side, sign) in new[] { ("left", 1f), ("right", -1f) })
            {
                Bend(skel.Find(side + "_arm_jnt"), Vector3.UnitX, -sign * 72 * MathF.PI / 180);
                Bend(skel.Find(side + "_elbow_jnt"), Vector3.UnitZ, sign * 15 * MathF.PI / 180);
            }
            void Bend(int j, Vector3 axis, float angle)
            {
                var local = Rot(Quaternion.Conjugate(world[j].Q), axis);
                pose[j] = pose[j] with { Q = Quaternion.Normalize(pose[j].Q * AxisAngle(local, angle)) };
                Solve();
            }
            Hands[0] = world[skel.Find("left_wrist_jnt")].P; Hands[1] = world[skel.Find("right_wrist_jnt")].P;
            var palette = new Xf[world.Length];
            for (int j = 0; j < palette.Length; j++) palette[j] = world[j] * skel.Bind[j].Inverse();
            for (int i = 0; i < Parts.Count; i++)
            {
                var m = Parts[i]; var p = new Vector3[m.P.Length]; var nr = new Vector3[m.P.Length];
                for (int v = 0; v < p.Length; v++)
                {
                    Vector3 sp = default, sn = default; float total = 0;
                    for (int k = 0; k < 8; k++)
                    {
                        float wk = m.W[v * 8 + k] / 255f; if (wk <= 0) continue;
                        var x = palette[m.J[v * 8 + k]]; sp += wk * x.Apply(m.P[v]); sn += wk * Rot(x.Q, m.N[v]); total += wk;
                    }
                    p[v] = total > 0 ? sp / total : m.P[v]; nr[v] = sn.Length() > 1e-9f ? Unit(sn) : m.N[v];
                }
                Parts[i] = m.Posed(p, nr);
            }
        }

        /// <summary>A guide-world point (metres, feet at the origin, Z up) in model units.</summary>
        public Vector3 FromWorld(Vector3 p) => p / Scale;
        public View ViewOf(float azimuth, float elevation, Vector3 centre, float span, int w, int h)
        {
            float az = azimuth * MathF.PI / 180, el = elevation * MathF.PI / 180;
            var fwd = -new Vector3(MathF.Cos(el) * MathF.Cos(az), MathF.Cos(el) * MathF.Sin(az), MathF.Sin(el));
            var right = Unit(Vector3.Cross(fwd, Vector3.UnitZ)); var up = Vector3.Cross(right, fwd);
            return new View(w, h, FromWorld(centre) - fwd * 500, fwd, right, up, span / Scale);
        }
    }
}
