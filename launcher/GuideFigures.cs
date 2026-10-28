using System.Buffers.Binary;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Numerics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json.Nodes;

namespace DishonoredVR;

/// <summary>Locally generated journal lessons. Player meshes come only from the owner's install;
/// props are original ink diagrams and opponents use the same player mesh, never extracted game weapons.</summary>
public static class GuideFigures
{
    public const int Version = 4;
    public static readonly string[] Ids = { "sword_throw", "enemy_grab", "sword_draw_block", "stealth", "move_jump_lean", "hands_push", "bodies_drag_carry", "left_hip", "wheel_watch", "temples", "menu_point" };
    public static int Height(string id) => id is "move_jump_lean" or "hands_push" or "menu_point" ? 804 : 232;
    static readonly Color Paper = Color.FromArgb(206, 192, 160), Ink = Color.FromArgb(41, 39, 35), Rust = Color.FromArgb(148, 89, 74);
    static readonly float[] Open = { 0, 0, 0, 0, 0 }, Holding = { .8f, .85f, 1, 1, 1 }, Grip = { .75f, .15f, 1, 1, 1 }, Pointing = { .65f, 0, 1, 1, 1 };
    static Vector3 V(float x, float y, float z) => new(x, y, z);
    static PoseRender.TeachingHand Hand(Vector3 p, Vector3 d, Vector3 n, float[] curls) => new(p, d, n, curls);
    static PoseRender.TeachingPose Pose(PoseRender.TeachingHand? r, float az, float el, Vector3 centre, float span, PoseRender.TeachingHand? l = null) => new(l, r, az, el, centre, span);

    public static bool Missing(string dir)
    {
        try
        {
            var doc = JsonNode.Parse(File.ReadAllText(Path.Combine(dir, "manifest.json")));
            var game = Env.FindGameDir();
            string? identity = game == null ? null : SourceIdentity(game);
            return Ids.Any(id => new[] { "corvo", "emily" }.Any(ch =>
                doc?["keys"]?[$"guide_{id}_{ch}"]?["generation_version"]?.GetValue<int>() != Version ||
                (identity != null && doc?["keys"]?[$"guide_{id}_{ch}"]?["source_identity"]?.GetValue<string>() != identity) ||
                !File.Exists(Path.Combine(dir, $"guide_{id}_{ch}.rgba")) || !File.Exists(Path.Combine(dir, $"guide_{id}_{ch}.png"))));
        }
        catch { return true; }
    }

    public static int Check(string outDir, TextWriter output)
    {
        var game = Env.FindGameDir();
        if (game == null) { output.WriteLine("Dishonored 2 not found"); return 2; }
        try
        {
            // Review output is self-contained, including the actual journal materials.
            ArtImport.Run(game, outDir, output.WriteLine);
            Run(game, outDir, output.WriteLine);
            return 0;
        }
        catch (Exception ex) { output.WriteLine(ex); return 1; }
    }

    public static void Run(string game, string outDir, Action<string> log, Action<int, int>? progress = null)
    {
        game = Path.GetFullPath(game); outDir = Path.GetFullPath(outDir);
        if ((outDir + "\\").StartsWith(game.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase)) throw new IOException("Refusing to write inside the game folder");
        Directory.CreateDirectory(outDir);
        using var renderer = PoseRender.CreateGuideRenderer(game);
        using var art = new GameArt(outDir);
        var manifest = Path.Combine(outDir, "manifest.json");
        var doc = File.Exists(manifest) ? JsonNode.Parse(File.ReadAllText(manifest))!.AsObject() : new JsonObject();
        var keys = doc["keys"] as JsonObject ?? new JsonObject(); doc["keys"] = keys;
        string sourceIdentity = SourceIdentity(game);
        int done = 0; progress?.Invoke(0, 22);
        // Signature lessons first, for both characters.
        foreach (string id in Ids) foreach (string ch in new[] { "corvo", "emily" })
        {
            int h = Height(id); string key = $"guide_{id}_{ch}";
            using var large = new Bitmap(1728, h * 2, PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage(large))
            {
                Ui.Quality(g); g.ScaleTransform(2, 2); g.Clear(Paper);
                Material(g, art["parchment"], new Rectangle(0, 0, 864, h), .22f);
                Material(g, art["paper_texture"], new Rectangle(0, 0, 864, h), .05f);
                Draw(g, renderer, ch, id);
                // Ornament lives below all instructional content.
                Ui.Image(g, art["ink_flourish"], new Rectangle(0, h - 16, 864, 16), .08f, Ink);
            }
            byte[] rgba = PoseRender.Downsample(Pixels(large), 1728, h * 2, 864, h);
            // A guide slot is opaque paper, including resampling at its outer edge.
            for (int i = 3; i < rgba.Length; i += 4) rgba[i] = 255;
            byte[] png = ArtImport.Png(rgba, 864, h), raw = new byte[8 + rgba.Length];
            BinaryPrimitives.WriteUInt32LittleEndian(raw, 864); BinaryPrimitives.WriteUInt32LittleEndian(raw.AsSpan(4), (uint)h); rgba.CopyTo(raw, 8);
            Atomic(Path.Combine(outDir, key + ".png"), png); Atomic(Path.Combine(outDir, key + ".rgba"), raw);
            keys[key] = new JsonObject { ["file"] = key + ".png", ["raw"] = key + ".rgba", ["width"] = 864, ["height"] = h,
                ["source"] = "rendered: local player model + original instructional diagrams", ["source_identity"] = sourceIdentity,
                ["generation_version"] = Version, ["sha256"] = ArtImport.Hex(SHA256.HashData(png)), ["raw_sha256"] = ArtImport.Hex(SHA256.HashData(raw)) };
            (doc["missing"] as JsonObject)?.Remove(key);
            log($"{key,-34} 864x{h} rendered"); progress?.Invoke(++done, 22);
        }
        doc["guide_generation_version"] = Version;
        doc["generation"] = DateTime.UtcNow.ToString("O");
        Atomic(manifest, Encoding.UTF8.GetBytes(doc.ToJsonString(new System.Text.Json.JsonSerializerOptions { WriteIndented = true }) + "\n"));
        Atomic(Path.Combine(outDir, "generation.txt"), Encoding.UTF8.GetBytes(doc["generation"]!.GetValue<string>() + "\n"));
        log("22 guide figures generated locally; do not commit or redistribute.");
    }

    static string SourceIdentity(string game)
    {
        var exe = new FileInfo(Path.Combine(game, "Dishonored2.exe"));
        return $"{Path.GetFullPath(game)}|{(exe.Exists ? exe.Length : 0)}|{(exe.Exists ? exe.LastWriteTimeUtc.Ticks : 0)}";
    }

    static void Atomic(string file, byte[] bytes)
    {
        string tmp = file + ".tmp";
        File.WriteAllBytes(tmp, bytes); File.Move(tmp, file, true);
    }
    static byte[] Pixels(Bitmap b)
    {
        var data = b.LockBits(new Rectangle(0, 0, b.Width, b.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        var bytes = new byte[b.Width * b.Height * 4];
        try { for (int y = 0; y < b.Height; y++) Marshal.Copy(data.Scan0 + y * data.Stride, bytes, y * b.Width * 4, b.Width * 4); }
        finally { b.UnlockBits(data); }
        for (int i = 0; i < bytes.Length; i += 4) (bytes[i], bytes[i + 2]) = (bytes[i + 2], bytes[i]);
        return bytes;
    }
    static void Material(Graphics g, Bitmap? b, Rectangle r, float opacity)
    {
        if (b == null) return;
        float s = Math.Max((float)r.Width / b.Width, (float)r.Height / b.Height);
        var state = g.Save(); g.SetClip(r);
        Ui.Image(g, b, new RectangleF(r.X + (r.Width - b.Width * s) / 2, r.Y + (r.Height - b.Height * s) / 2, b.Width * s, b.Height * s), opacity);
        g.Restore(state);
    }

    sealed class Panel
    {
        public readonly Graphics G; public readonly RectangleF Bounds;
        readonly PoseRender.GuideFrame frame;
        public Panel(Graphics g, PoseRender.GuideRenderer renderer, string ch, RectangleF bounds, PoseRender.TeachingPose pose, float alpha = 1)
        {
            G = g; Bounds = bounds;
            frame = renderer.Render(ch, pose, (int)bounds.Width * 2, (int)bounds.Height * 2);
            using var ms = new MemoryStream(ArtImport.Png(frame.Rgba, (int)bounds.Width * 2, (int)bounds.Height * 2));
            using var b = new Bitmap(ms); Ui.Image(g, b, bounds, alpha);
        }
        public PointF Hand(int side) { var p = frame.HandPoint(side); return new(Bounds.X + p.X / 2, Bounds.Y + p.Y / 2); }
        public PointF Wrist(int side) { var p = frame.WristPoint(side); return new(Bounds.X + p.X / 2, Bounds.Y + p.Y / 2); }
        public PointF P(Vector3 world) { var p = frame.Project(world); return new(Bounds.X + p.X / 2, Bounds.Y + p.Y / 2); }
    }
    static void Arrow(Graphics g, PointF a, PointF b, float alpha = 1)
    {
        using var p = new Pen(Color.FromArgb((int)(255 * alpha), Ink), 2.4f) { EndCap = LineCap.ArrowAnchor };
        g.DrawLine(p, a, b);
    }
    static void ThrowArc(Graphics g, PointF start, PointF end)
    {
        using var pen = new Pen(Ink, 2.4f) { EndCap = LineCap.ArrowAnchor };
        g.DrawBezier(pen, start, Add(start, 40, -100), Add(end, -40, -70), end);
    }
    static PointF Add(PointF p, float x, float y) => new(p.X + x, p.Y + y);
    static void Dot(Graphics g, PointF p) { using var b = new SolidBrush(Rust); g.FillEllipse(b, p.X - 4, p.Y - 4, 8, 8); }
    static void Step(Graphics g, int n, float x, float y)
    {
        using var p = new Pen(Color.FromArgb(110, Ink), 1); g.DrawEllipse(p, x, y, 20, 20);
        using var f = new Font("Segoe UI", 11, FontStyle.Bold, GraphicsUnit.Pixel); using var b = new SolidBrush(Ink);
        g.DrawString(n.ToString(), f, b, x + 6, y + 2);
    }
    static void Sword(Graphics g, PointF h, float angle, float length = 83, float alpha = 1, bool solid = false)
    {
        var state = g.Save(); g.TranslateTransform(h.X, h.Y); g.RotateTransform(angle);
        using var p = new Pen(Color.FromArgb((int)(255 * alpha), Ink), 2);
        var blade = new PointF[] { new(-3, -12), new(-4, -length + 12), new(0, -length), new(4, -length + 12), new(3, -12) };
        if (solid) { using var fill = new SolidBrush(Ink); g.FillPolygon(fill, blade); }
        g.DrawPolygon(p, blade);
        g.DrawLine(p, -13, -11, 13, -11); g.DrawLine(p, 0, -10, 0, 10); g.DrawEllipse(p, -3, 10, 6, 4); g.Restore(state);
    }
    // Opponents use the exact locally loaded player mesh, kept anonymous by ink.
    static void Guard(Graphics g, PoseRender.GuideRenderer renderer, string ch, PointF chest,
        float scale, bool facesLeft, float angle = 0, bool back = false, bool raisedWrist = false, bool crouch = false, PointF? wristContact = null)
    {
        var wrist = raisedWrist ? Hand(V(.12f, .50f, 1.85f), Vector3.UnitZ, -Vector3.UnitY, Open) : null;
        var pose = Pose(null, back ? -140 : facesLeft ? 40 : -40, 0, V(0, 0, .96f), 2.05f, wrist);
        var frame = renderer.Render(ch, pose, 220, 440, silhouette: true, crouch: crouch);
        using var ms = new MemoryStream(ArtImport.Png(frame.Rgba, 220, 440));
        using var bitmap = new Bitmap(ms);
        var anchor = frame.Project(V(0, 0, 1.25f));
        float factor = 190f / 440 * scale;
        var state = g.Save();
        if (wristContact is PointF contact)
        {
            anchor = frame.HandPoint(0);
            g.TranslateTransform(contact.X, contact.Y);
            g.RotateTransform(angle); g.ScaleTransform(factor, factor);
            g.DrawImage(bitmap, -anchor.X, -anchor.Y, 220, 440);
        }
        else
        {
            g.TranslateTransform(chest.X, chest.Y + 116 * scale);
            g.RotateTransform(angle); g.ScaleTransform(factor, factor);
            g.DrawImage(bitmap, -anchor.X, -anchor.Y - 116 * scale / factor, 220, 440);
        }
        g.Restore(state);
    }
    static void Pistol(Graphics g, PointF hand)
    {
        var state = g.Save(); g.TranslateTransform(hand.X, hand.Y); g.RotateTransform(-12);
        using var path = new GraphicsPath();
        path.AddPolygon(new PointF[] { new(-12, -9), new(1, -14), new(6, -23), new(12, -25), new(10, -15),
            new(61, -15), new(61, -9), new(17, -7), new(12, -2), new(7, 0), new(5, 19),
            new(-2, 24), new(-14, 20), new(-8, 3), new(-9, -1), new(-15, -3) });
        using var ink = new SolidBrush(Ink); g.FillPath(ink, path);
        using var rim = new Pen(Color.FromArgb(151, 133, 104), 1); g.DrawLine(rim, 14, -12, 43, -12);
        using var trigger = new Pen(Ink, 2); g.DrawArc(trigger, 4, -7, 16, 17, -10, 220);
        g.Restore(state);
    }

    static void DrawMotion(Graphics g, PoseRender.GuideRenderer renderer, string ch, string id)
    {
        var X = Vector3.UnitX; var Y = Vector3.UnitY; var Z = Vector3.UnitZ;
        Panel View(RectangleF box, PoseRender.TeachingPose pose, float alpha = 1) => new(g, renderer, ch, box, pose, alpha);
        var a = new RectangleF(12, 10, 404, 206); var b = new RectangleF(446, 10, 404, 206);
        using var rule = new Pen(Color.FromArgb(170, Ink), 1.4f);
        if (Height(id) == 232) g.DrawLine(rule, 432, 22, 432, 205);
        switch (id)
        {
            case "sword_throw":
            {
                var prep = Hand(V(.12f, -.45f, 1.62f), Z, Y, Holding);
                var release = Hand(V(.58f, -.18f, 1.38f), X, -Z, Open);
                var p = View(a, Pose(prep, -55, 8, V(.10f, -.08f, 1.43f), .92f));
                Sword(g, p.Hand(1), -28, 81); Step(g, 1, 20, 16);
                var q = View(b, Pose(release, -55, 8, V(.38f, -.08f, 1.43f), 1.05f));
                var h = q.Hand(1);
                Sword(g, Add(h, 17, -9), 65, 40, .18f); Sword(g, Add(h, 47, -20), 75, 40, .38f);
                Sword(g, Add(h, 80, -22), 85, 45, solid: true);
                using (var flight = new Pen(Ink, 2) { EndCap = LineCap.ArrowAnchor })
                    g.DrawBezier(flight, Add(h, 12, 20), Add(h, 46, -55), Add(h, 92, -55), Add(h, 129, -5)); Step(g, 2, 452, 16);
                break;
            }
            case "enemy_grab":
            {
                var contact = Hand(V(.58f, -.06f, 1.38f), Z, X, Grip);
                var pull = Hand(V(.28f, -.16f, 1.30f), Z, X, Grip);
                var pose = Pose(contact, -65, 6, V(.34f, -.10f, 1.42f), .65f);
                var p = View(a, pose); var h = p.Hand(1);
                Guard(g, renderer, ch, h, .90f, true); // chest meets the palm, target faces the player
                // Repaint the detailed hand over the silhouette chest, then mark contact.
                View(a, pose); Dot(g, h); Step(g, 1, 20, 16);
                View(b, Pose(contact, -65, 6, V(.34f, -.10f, 1.42f), .65f), .25f);
                var q = View(b, Pose(pull, -65, 6, V(.34f, -.10f, 1.42f), .65f));
                Guard(g, renderer, ch, new(816, 100), .90f, true, -60); using (var motion = new Pen(Ink, 2) { EndCap = LineCap.ArrowAnchor })
                    g.DrawBezier(motion, new PointF(808, 94), new PointF(780, 72), new PointF(740, 78), new PointF(708, 108)); Arrow(g, q.P(contact.Position), q.Hand(1)); Dot(g, q.Hand(1)); Step(g, 2, 452, 16);
                break;
            }
            case "sword_draw_block":
            {
                var draw = Hand(V(-.08f, -.27f, 1.60f), Z, Y, Holding); var block = Hand(V(.35f, -.08f, 1.32f), Z, Y, Holding);
                var p = View(a, Pose(draw, -140, 8, V(0, 0, 1.43f), .85f)); var h = p.Hand(1);
                Sword(g, h, -20, 85); Arrow(g, Add(h, -30, 45), Add(h, -5, -30)); Step(g, 1, 20, 16);
                var q = View(b, Pose(block, -15, 5, V(.1f, 0, 1.40f), .85f)); var k = q.Hand(1);
                Sword(g, k, 62, 115); Sword(g, k, 30, 115, .25f); Sword(g, k, 95, 115, .25f); Step(g, 2, 452, 16); break;
            }
            case "stealth":
            {
                var kill = Hand(V(.50f, -.05f, 1.23f), X, -Z, Holding);
                var p = View(a, Pose(kill, -90, 8, V(.28f, 0, 1.40f), .96f)); var h = p.Hand(1);
                Guard(g, renderer, ch, Add(h, 49, -21), .83f, false, back: true); Sword(g, h, 90, 63); Arrow(g, Add(h, -25, 14), Add(h, 39, 14));
                var choke = Hand(V(.52f, .08f, 1.56f), -Y, X, Holding);
                var q = View(b, Pose(null, -90, 8, V(.27f, 0, 1.42f), .96f, choke)); var k = q.Hand(0);
                Guard(g, renderer, ch, Add(k, 10, 39), .94f, false, back: true); View(b, Pose(null, -90, 8, V(.27f, 0, 1.42f), .96f, choke)); Dot(g, k); break;
            }
            case "move_jump_lean":
            {
                var l = Hand(V(.03f, .26f, 2.03f), Z, -Y, Open); var r = Hand(V(.03f, -.26f, 2.03f), Z, Y, Open);
                var p = View(new(100, 20, 664, 490), Pose(r, 0, 0, V(0, 0, 1.42f), 1.63f, l));
                foreach (int side in new[] { 0, 1 }) { var h = p.Hand(side); Arrow(g, Add(h, 0, 97), Add(h, 0, 25)); }
                g.DrawLine(rule, 40, 530, 824, 530);
                // The same mesh teaches standing, a planted squat, and whole-body leaning.
                Guard(g, renderer, ch, new(190, 624), .85f, true); Guard(g, renderer, ch, new(379, 667), .85f, true, crouch: true); Arrow(g, new(266, 611), new(324, 658));
                g.DrawLines(rule, new PointF[] { new(612, 572), new(612, 745), new(810, 745) });
                Guard(g, renderer, ch, new(635, 640), .74f, false); Guard(g, renderer, ch, new(715, 640), .74f, false, 18); Arrow(g, new(642, 570), new(709, 561)); break;
            }
            case "hands_push":
            {
                var hand = Hand(V(.55f, -.18f, 1.35f), Z, X, Open);
                var pose = Pose(hand, -60, 10, V(.42f, -.16f, 1.37f), .67f);
                var p = View(new(60, 26, 650, 454), pose); var h = p.Hand(1);
                using var pen = new Pen(Ink, 2.5f);
                g.DrawPolygon(pen, new PointF[] { Add(h, 0, -127), Add(h, 150, -171), Add(h, 150, 177), Add(h, 0, 120) });
                View(new(60, 26, 650, 454), pose); Dot(g, h);
                g.DrawArc(pen, h.X - 15, h.Y + 90, 210, 80, 15, 57); g.DrawArc(pen, h.X - 38, h.Y + 69, 255, 125, 15, 100);
                Arrow(g, Add(h, 45, 42), Add(h, 130, 65)); g.DrawLine(rule, 40, 525, 824, 525);
                Bottle(g, new(220, 632), 1); DrawGrip(g, new(210, 651));
                Bottle(g, new(558, 640), .8f); Bottle(g, new(682, 580), .65f); ThrowArc(g, new(574, 676), new(752, 625)); break;
            }
            case "bodies_drag_carry":
            {
                var drag = Hand(V(.35f, -.24f, .78f), X, -Z, Holding);
                var p = View(a, Pose(drag, -75, 10, V(.29f, -.18f, 1.04f), .98f)); var h = p.Hand(1);
                Guard(g, renderer, ch, Add(h, 77, -63), .83f, true, -90, raisedWrist: true, wristContact: h);
                Arrow(g, Add(h, 61, 50), Add(h, -20, 50)); Dot(g, h);
                var carry = Hand(V(.04f, -.28f, 1.50f), Z, Y, Open);
                View(b, Pose(carry, -75, 10, V(.02f, -.08f, 1.44f), .9f));
                Guard(g, renderer, ch, new(756, -46), 1.0f, true, -100);
                View(b, Pose(carry, -75, 10, V(.02f, -.08f, 1.44f), .9f)); break;
            }
            case "left_hip":
            {
                var hand = Hand(V(-.015f, .18f, .99f), -Z, -Y, Holding);
                var pose = Pose(null, 20, 5, V(0, .04f, 1.19f), 1.0f, hand);
                var box = new RectangleF(40, 8, 760, 210);
                var p = View(box, pose); var h = p.Hand(0);
                Pistol(g, h);
                var handLayer = g.Save();
                g.SetClip(new RectangleF(h.X - 10, h.Y - 11, 20, 22)); View(box, pose); g.Restore(handLayer);
                Arrow(g, Add(h, 52, 12), Add(h, 52, -62)); Dot(g, h); break;
            }
            case "wheel_watch":
            {
                var point = Hand(V(.50f, .16f, 1.25f), X, -Z, Pointing);
                var p = View(a, Pose(null, 35, 0, V(.24f, .13f, 1.38f), .8f, point)); var h = p.Hand(0);
                using var pen = new Pen(Ink, 2); var centre = new PointF(337, 75);
                g.DrawEllipse(pen, centre.X - 48, centre.Y - 48, 96, 96);
                for (int i = 0; i < 8; i++) { float t = i * MathF.PI / 4; g.DrawLine(rule, centre, Add(centre, 48 * MathF.Cos(t), 48 * MathF.Sin(t))); }
                Arrow(g, h, Add(centre, -10, 13));
                var watch = Hand(V(.40f, .20f, 1.43f), Z, -X, Holding);
                var q = View(b, Pose(null, 35, 20, V(.32f, .19f, 1.43f), .48f, watch)); var wrist = q.Wrist(0);
                using var rust = new SolidBrush(Rust); using var mana = new SolidBrush(Color.FromArgb(98, 119, 126));
                using var caseInk = new SolidBrush(Ink); using var rim = new Pen(Paper, 1);
                var watchBox = new RectangleF(wrist.X - 22, wrist.Y - 8, 44, 26);
                g.FillRectangle(caseInk, watchBox); g.DrawRectangle(rim, watchBox.X, watchBox.Y, watchBox.Width, watchBox.Height);
                g.FillRectangle(rust, wrist.X - 18, wrist.Y - 4, 36, 6); g.FillRectangle(mana, wrist.X - 18, wrist.Y + 6, 30, 6); break;
            }
            case "temples":
            {
                var left = Hand(V(.05f, .13f, 1.71f), Z, -Y, Holding); var right = Hand(V(.05f, -.13f, 1.71f), Z, Y, Holding);
                var p = View(a, Pose(null, 0, 0, V(0, 0, 1.64f), .64f, left)); var q = View(b, Pose(right, 0, 0, V(0, 0, 1.64f), .64f));
                // Anatomical left appears on the viewer's right in these front views.
                var h = p.Hand(0); var k = q.Hand(1);
                Dot(g, h); Dot(g, k); Book(g, Add(h, 63, -25));
                g.DrawRectangle(rule, k.X - 107, k.Y - 25, 51, 36); g.DrawLine(rule, k.X - 99, k.Y - 14, k.X - 64, k.Y - 14); g.DrawLine(rule, k.X - 99, k.Y - 5, k.X - 73, k.Y - 5); break;
            }
            case "menu_point":
            {
                var hand = Hand(V(.52f, -.16f, 1.30f), X, -Z, Pointing);
                var p = View(new(10, 165, 620, 580), Pose(hand, -30, 8, V(.12f, -.06f, 1.35f), 1.25f)); var h = p.Hand(1);
                using var bg = new SolidBrush(Color.FromArgb(39, 37, 36)); using var highlight = new SolidBrush(Color.FromArgb(216, 209, 189));
                g.FillRectangle(bg, 529, 70, 287, 257); g.FillRectangle(highlight, 550, 145, 240, 32);
                using (var row = new Pen(Color.FromArgb(170, Paper), 2))
                    for (int y = 110; y <= 290; y += 45) g.DrawLine(row, 557, y, 776, y);
                Arrow(g, h, new(548, 160)); break;
            }
        }
    }
    static void Bottle(Graphics g, PointF p, float scale)
    {
        var s = g.Save(); g.TranslateTransform(p.X, p.Y); g.ScaleTransform(scale, scale); using var pen = new Pen(Ink, 2);
        g.DrawPolygon(pen, new PointF[] { new(-7, -50), new(7, -50), new(7, -29), new(18, -17), new(18, 36), new(-18, 36), new(-18, -17), new(-7, -29) }); g.Restore(s);
    }
    static void DrawGrip(Graphics g, PointF p)
    {
        using var pen = new Pen(Ink, 2);
        g.DrawArc(pen, p.X - 27, p.Y - 29, 50, 70, -80, 275);
        for (int i = 0; i < 4; i++) g.DrawArc(pen, p.X - 8, p.Y - 20 + i * 10, 27, 16, -110, 260);
        g.DrawArc(pen, p.X - 33, p.Y - 2, 39, 22, 90, 200);
    }
    static void Label(Graphics g, string text, float x, float y, float size = 15, bool bold = true)
    {
        using var font = new Font("Segoe UI", size, bold ? FontStyle.Bold : FontStyle.Regular, GraphicsUnit.Pixel);
        using var brush = new SolidBrush(Ink);
        g.DrawString(text, font, brush, x, y);
    }

    // Photo reference and generation provenance live beside the embedded asset.
    static readonly Bitmap ControllerPhoto = LoadControllerPhoto();
    static Bitmap LoadControllerPhoto()
    {
        using var stream = typeof(GuideFigures).Assembly.GetManifestResourceStream("guide/touch-plus.png")
            ?? throw new InvalidOperationException("Controller image is missing.");
        using var image = new Bitmap(stream);
        return new Bitmap(image);
    }
    static PointF ControllerPoint(PointF p, bool left, string input, float scale = 1)
    {
        var point = (left, input) switch
        {
            (true, "stick") => new PointF(-19, -35), (false, "stick") => new PointF(21, -23),
            (true, "X") => new PointF(5, -23), (true, "Y") => new PointF(8, -33),
            (false, "A") => new PointF(3, -17), (false, "B") => new PointF(-5, -31),
            (true, "menu") => new PointF(-12, -18),
            (true, "grip") => new PointF(30, 19), (false, "grip") => new PointF(-25, 19),
            _ => throw new ArgumentException("No visible controller button: " + input)
        };
        return Add(p, point.X * scale, point.Y * scale);
    }
    static void Controller(Graphics g, PointF p, bool left, string active, float scale = 1)
    {
        var source = left ? new RectangleF(180, 55, 550, 845) : new RectangleF(970, 55, 540, 845);
        g.DrawImage(ControllerPhoto, new RectangleF(p.X - 43 * scale, p.Y - 48 * scale, 86 * scale, 134 * scale), source, GraphicsUnit.Pixel);
        using var ring = new Pen(Rust, 2.4f);
        foreach (string input in active.Split(' ', StringSplitOptions.RemoveEmptyEntries))
        {
            if (input == "trigger") continue; // Behind the faceplate in the reference; never mark the grip as trigger.
            var point = ControllerPoint(p, left, input, scale);
            float radius = 7 * scale;
            g.DrawEllipse(ring, point.X - radius, point.Y - radius, radius * 2, radius * 2);
        }
    }

    static void Draw(Graphics g, PoseRender.GuideRenderer renderer, string ch, string id)
    {
        if (Height(id) == 232)
        {
            var state = g.Save(); g.TranslateTransform(0, 12); g.ScaleTransform(.76f, .76f);
            DrawMotion(g, renderer, ch, id); g.Restore(state);
            var captions = id switch
            {
                "sword_throw" => ("1  Hold grip + swing", "2  Release grip to throw"),
                "enemy_grab" => ("1  Touch + hold grip", "2  Hold grip + pull hard"),
                "sword_draw_block" => ("1  Grip at shoulder", "2  Blade in front: block"),
                "stealth" => ("Stab from behind / side", "Grip neck from behind"),
                "bodies_drag_carry" => ("1  Grip limb + lift", "2  Release at shoulder"),
                "left_hip" => ("Grip at left hip: draw", "Left trigger: fire / cast"),
                "wheel_watch" => ("Hold X + point to choose", "Turn wrist to your face"),
                _ => ("Left temple: journal", "Right temple: guide")
            };
            Label(g, captions.Item1, 10, 191, 22); Label(g, captions.Item2, 337, 191, 22);
            using var rule = new Pen(Color.FromArgb(110, Ink), 1); g.DrawLine(rule, 659, 16, 659, 215);
            bool left = id is "left_hip" or "wheel_watch";
            string active = id switch { "wheel_watch" => "X", "left_hip" => "grip trigger", _ => "grip" };
            Label(g, left ? "LEFT HAND" : id is "enemy_grab" or "bodies_drag_carry" or "temples" or "stealth" ? "EITHER HAND" : "RIGHT HAND", 678, 16, 22);
            Controller(g, new(753, 91), left, active, .93f);
            // Leaders point to the highlighted physical button, away from the motion artwork.
            var target = ControllerPoint(new(753, 91), left, active == "X" ? "X" : "grip", .93f);
            using var leader = new Pen(Rust, 1.8f); g.DrawLines(leader, new[] { target, new PointF(686, 161), new PointF(686, 169) });
            Dot(g, target);
            Label(g, active == "X" ? "HOLD X" : "SIDE GRIP", 680, 172, 22);
            Label(g, id == "sword_throw" ? "Release to throw" : id == "wheel_watch" ? "Release to equip" : "Middle finger", 680, 201, 18, false);
            return;
        }
        if (id == "move_jump_lean")
        {
            Label(g, "QUEST 3 TOUCH PLUS", 230, 14, 24);
            Controller(g, new(242, 119), true, "stick", 1.12f);
            Controller(g, new(620, 119), false, "stick A", 1.12f);
            Label(g, "LEFT", 208, 217, 22); Label(g, "RIGHT", 579, 217, 22);
            using (var leader = new Pen(Rust, 2))
            {
                g.DrawLines(leader, new PointF[] { ControllerPoint(new(242, 119), true, "stick", 1.12f), new(142, 83), new(94, 83) });
                g.DrawLines(leader, new PointF[] { ControllerPoint(new(620, 119), false, "stick", 1.12f), new(714, 75), new(759, 75) });
                g.DrawLines(leader, new PointF[] { ControllerPoint(new(620, 119), false, "A", 1.12f), new(723, 159), new(759, 159) });
            }
            Label(g, "STICK", 46, 67, 22); Label(g, "STICK", 761, 59, 22); Label(g, "A", 766, 143, 22);
            Label(g, "Stick: move with your gaze", 40, 250, 22);
            Label(g, "Click stick: sprint", 40, 280, 22);
            Label(g, "Stick sideways: turn", 454, 250, 22);
            Label(g, "Stick down: crouch / stand", 454, 280, 22);
            Label(g, "A: jump / climb / swim up", 454, 310, 22);
            Label(g, "Click stick: zoom", 454, 340, 22);
            using var rule = new Pen(Color.FromArgb(100, Ink), 1); g.DrawLine(rule, 40, 378, 824, 378);
            var state = g.Save(); g.TranslateTransform(246, 423); g.ScaleTransform(.43f, .43f);
            DrawMotion(g, renderer, ch, id); g.Restore(state);
            Label(g, "JUMP: throw empty hands overhead", 210, 382, 22);
            Label(g, "Keep triggers released", 304, 413, 18, false);
            Label(g, "CROUCH: lower body", 75, 767, 22);
            Label(g, "LEAN: head sideways", 490, 767, 22);
            return;
        }
        var motion = g.Save(); g.ScaleTransform(1, id == "menu_point" ? .64f : .80f); DrawMotion(g, renderer, ch, id); g.Restore(motion);
        float divider = id == "menu_point" ? 538 : 650;
        using (var line = new Pen(Color.FromArgb(110, Ink), 1)) g.DrawLine(line, 40, divider, 824, divider);
        if (id == "hands_push")
        {
            Label(g, "OPEN PALM: push slowly to peek", 194, 20, 24);
            Label(g, "1  Grip to pick up", 85, 600, 22); Label(g, "2  Swing + release to throw", 438, 600, 22);
            Controller(g, new(150, 720), false, "grip", .65f);
            Label(g, "SIDE GRIP", 205, 676, 24); Label(g, "Hold to keep an object in your hand", 205, 711, 22, false);
            Label(g, "Release mid-swing to throw", 205, 745, 22, false);
        }
        else
        {
            Label(g, "1  Point at a menu item", 45, 25, 26);
            Label(g, "2  Right trigger: select", 45, 62, 24);
            Label(g, "Index trigger is behind the faceplate", 45, 96, 20, false);
            Label(g, "LEFT CONTROLLER", 94, 550, 22);
            Label(g, "RIGHT CONTROLLER", 497, 550, 22);
            Controller(g, new(190, 627), true, "Y menu", .83f);
            Controller(g, new(598, 627), false, "trigger grip B", .83f);
            Label(g, "Y: talk / hold for pause", 44, 710, 23);
            Label(g, "Menu: pause", 44, 751, 23);
            Label(g, "Side grip: change tab", 444, 710, 23);
            Label(g, "Hold B: holster weapons", 444, 751, 23);
            using var leader = new Pen(Rust, 2);
            g.DrawLines(leader, new PointF[] { ControllerPoint(new(190, 627), true, "Y", .83f), new(312, 608), new(312, 699) });
            g.DrawLines(leader, new PointF[] { ControllerPoint(new(190, 627), true, "menu", .83f), new(83, 672), new(83, 742) });
            g.DrawLines(leader, new PointF[] { ControllerPoint(new(598, 627), false, "grip", .83f), new(474, 651), new(474, 699) });
            g.DrawLines(leader, new PointF[] { ControllerPoint(new(598, 627), false, "B", .83f), new(832, 608), new(832, 742) });
        }
    }
    static void Book(Graphics g, PointF p)
    {
        using var pen = new Pen(Ink, 2);
        g.DrawPolygon(pen, new PointF[] { p, Add(p, -24, -6), Add(p, -24, 29), Add(p, 0, 35), Add(p, 24, 29), Add(p, 24, -6) }); g.DrawLine(pen, p, Add(p, 0, 35));
    }
}
