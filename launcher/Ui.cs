using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Text.Json;

namespace DishonoredVR;

/// <summary>Real Dishonored 2 art from artifacts\game-art (written from the user's own install by tools\extract_game_art.py).
/// Bitmaps load on first use and are converted to premultiplied ARGB; missing keys return null so every caller has a plain fallback.</summary>
public sealed class GameArt : IDisposable
{
    readonly string dir = "";
    readonly Dictionary<string, string> files = new();
    readonly Dictionary<string, Bitmap?> cache = new();
    public bool Present => files.Count > 0;

    public GameArt() { }
    public GameArt(string dir)
    {
        this.dir = dir;
        try
        {
            using var doc = JsonDocument.Parse(File.ReadAllText(Path.Combine(dir, "manifest.json")));
            foreach (var k in doc.RootElement.GetProperty("keys").EnumerateObject())
                if (k.Value.TryGetProperty("file", out var f) && f.GetString() is string s) files[k.Name] = Path.GetFileName(s);
        }
        catch { files.Clear(); }  // no or unreadable manifest: plain style
    }

    public Bitmap? this[string key]
    {
        get
        {
            if (cache.TryGetValue(key, out var b)) return b;
            if (files.TryGetValue(key, out var f))
                try
                {
                    using var src = new Bitmap(Path.Combine(dir, f));  // disposed right away so the extractor can rewrite the file
                    b = new Bitmap(src.Width, src.Height, PixelFormat.Format32bppPArgb);
                    using var g = Graphics.FromImage(b);
                    g.CompositingMode = CompositingMode.SourceCopy;
                    g.DrawImage(src, new Rectangle(0, 0, src.Width, src.Height));
                }
                catch { b?.Dispose(); b = null; }
            return cache[key] = b;
        }
    }

    public void Dispose() { foreach (var b in cache.Values) b?.Dispose(); cache.Clear(); }
}

/// <summary>Palette, fonts and drawing helpers for the game-style look. Each helper draws the game's bitmap when present, else a plain shape.</summary>
static class Ui
{
    public static readonly Color Ink = Color.FromArgb(9, 8, 10), Text = Color.FromArgb(232, 227, 216), Soft = Color.FromArgb(198, 192, 180),
        Dim = Color.FromArgb(152, 146, 135), Faint = Color.FromArgb(106, 101, 94), Gold = Color.FromArgb(206, 174, 112),
        Good = Color.FromArgb(146, 208, 178), Teal = Color.FromArgb(124, 202, 218), Bad = Color.FromArgb(218, 92, 76),
        OnBrush = Color.FromArgb(24, 22, 21), PaneFill = Color.FromArgb(12, 11, 12), Edge = Color.FromArgb(70, 65, 60);
    public const string Sans = "Segoe UI", Mono = "Consolas";
    internal static readonly string Serif = Installed("Palatino Linotype") ? "Palatino Linotype" : "Georgia";

    public static GameArt Art = new();
    /// <summary>The window's pre-rendered background; art controls paint their slice of it so they look transparent.</summary>
    public static Bitmap? Backdrop;
    public static Control? BackdropOwner;

    static bool Installed(string family) { using var f = new Font(family, 10f); return f.Name == family; }

    static readonly Dictionary<(string, float, FontStyle, int), Font> fonts = new();
    /// <summary>Pixel-unit font for the control's current DPI (so text scales per monitor without WinForms autoscaling).</summary>
    public static Font Font(Control c, float pt, FontStyle st = FontStyle.Regular, string? family = null)
    {
        var key = (family ?? Serif, pt, st, c.DeviceDpi);
        if (!fonts.TryGetValue(key, out var f)) fonts[key] = f = new Font(key.Item1, pt * c.DeviceDpi / 72f, st, GraphicsUnit.Pixel);
        return f;
    }

    public static int S(Control c, float v) => (int)Math.Round(v * c.DeviceDpi / 96f);
    public static Color Mix(Color a, Color b, float t) => Color.FromArgb((int)(a.R + (b.R - a.R) * t), (int)(a.G + (b.G - a.G) * t), (int)(a.B + (b.B - a.B) * t));

    public static void Quality(Graphics g)
    {
        g.InterpolationMode = InterpolationMode.HighQualityBicubic; g.PixelOffsetMode = PixelOffsetMode.Half;
        g.SmoothingMode = SmoothingMode.AntiAlias; g.CompositingQuality = CompositingQuality.HighQuality;
    }

    public static void PaintBackdrop(Control c, Graphics g)
    {
        if (Backdrop == null || BackdropOwner == null) { g.Clear(Ink); return; }
        var p = c == BackdropOwner ? Point.Empty : BackdropOwner.PointToClient(c.PointToScreen(Point.Empty));
        var st = g.Save();
        g.CompositingMode = CompositingMode.SourceCopy; g.InterpolationMode = InterpolationMode.NearestNeighbor; g.PixelOffsetMode = PixelOffsetMode.None;
        g.DrawImage(Backdrop, new Rectangle(0, 0, c.Width, c.Height), new Rectangle(p.X, p.Y, c.Width, c.Height), GraphicsUnit.Pixel);
        g.Restore(st);
    }

    /// <summary>Alpha and optional flat tint (keeps the art's shape, replaces its colour), with edge clamping for stretched slices.</summary>
    public static ImageAttributes Attr(float alpha, Color? tint = null)
    {
        var a = new ImageAttributes();
        a.SetWrapMode(WrapMode.TileFlipXY);
        if (alpha < 1 || tint != null)
        {
            var m = new ColorMatrix { Matrix33 = alpha };
            if (tint is Color t) { m.Matrix00 = m.Matrix11 = m.Matrix22 = 0; m.Matrix40 = t.R / 255f; m.Matrix41 = t.G / 255f; m.Matrix42 = t.B / 255f; }
            a.SetColorMatrix(m);
        }
        return a;
    }

    public static void Image(Graphics g, Bitmap? b, RectangleF r, float alpha = 1, Color? tint = null)
    {
        if (b == null || r.Width < 1 || r.Height < 1) return;
        using var a = Attr(alpha, tint);
        g.DrawImage(b, Rectangle.Round(r), 0, 0, b.Width, b.Height, GraphicsUnit.Pixel, a);
    }

    /// <summary>Stretches b over r horizontally but keeps its end caps (source pixels) in proportion, like the game's resizable widgets.</summary>
    static void Slice3(Graphics g, Bitmap b, Rectangle r, int capL, int capR, float alpha, Color? tint = null)
    {
        if (r.Width < 4 || r.Height < 2) return;
        using var a = Attr(alpha, tint);
        float s = (float)r.Height / b.Height;
        int l = (int)Math.Min(capL * s, r.Width / 2f), rr = (int)Math.Min(capR * s, r.Width / 2f);
        g.DrawImage(b, new Rectangle(r.X, r.Y, l, r.Height), 0, 0, capL, b.Height, GraphicsUnit.Pixel, a);
        g.DrawImage(b, new Rectangle(r.X + l, r.Y, r.Width - l - rr, r.Height), capL, 0, b.Width - capL - capR, b.Height, GraphicsUnit.Pixel, a);
        g.DrawImage(b, new Rectangle(r.Right - rr, r.Y, rr, r.Height), b.Width - capR, 0, capR, b.Height, GraphicsUnit.Pixel, a);
    }

    static Point[] Ribbon(Rectangle r)
    {
        int n = r.Height / 3;
        return new[] { new Point(r.X + n, r.Y), new Point(r.Right, r.Y), new Point(r.Right - n, r.Bottom), new Point(r.X + n, r.Bottom), new Point(r.X, r.Y + r.Height / 2) };
    }

    /// <summary>The white brush-stroke selection highlight.</summary>
    public static void Brush(Graphics g, Rectangle r, float alpha = 1)
    {
        if (Art["highlight_brush"] is Bitmap b) { Slice3(g, b, r, 20, 46, alpha); return; }
        using var br = new SolidBrush(Color.FromArgb((int)(alpha * 255), 214, 222, 218));
        g.FillPolygon(br, Ribbon(r));
    }

    /// <summary>The outlined option-row frame, used for secondary buttons.</summary>
    public static void Frame(Graphics g, Rectangle r, float alpha)
    {
        if (Art["frame"] is Bitmap b) { Slice3(g, b, r, 24, 24, alpha, Soft); return; }
        using var p = new Pen(Color.FromArgb((int)(alpha * 255), Soft));
        var pts = new[] { new Point(r.X + r.Height / 3, r.Y + 1), new Point(r.Right - r.Height / 3, r.Y + 1), new Point(r.Right - 1, r.Y + r.Height / 2),
            new Point(r.Right - r.Height / 3, r.Bottom - 1), new Point(r.X + r.Height / 3, r.Bottom - 1), new Point(r.X + 1, r.Y + r.Height / 2) };
        g.DrawPolygon(p, pts);
    }

    /// <summary>The glowing selection bar from the main menu; draws nothing without art.</summary>
    public static void Glow(Graphics g, Rectangle r, float alpha) => Image(g, Art["highlight_glow_bar"], r, alpha);

    /// <summary>Ornamental divider tinted gold: the centre ornament keeps its shape, the rules stretch.</summary>
    public static void Divider(Graphics g, Rectangle r, float alpha = .75f)
    {
        if (Art["divider"] is not Bitmap b)
        {
            using var p = new Pen(Color.FromArgb((int)(alpha * 160), Gold));
            int cy = r.Y + r.Height / 2, cx = r.X + r.Width / 2;
            g.DrawLine(p, r.X, cy, r.Right, cy);
            Diamond(g, cx, cy, r.Height / 4f, Color.FromArgb((int)(alpha * 255), Gold), true);
            return;
        }
        using var a = Attr(alpha, Gold);
        float s = (float)r.Height / b.Height;
        int mid = b.Width / 5, midW = Math.Min((int)(mid * s), r.Width), side = (r.Width - midW) / 2, sx = (b.Width - mid) / 2;
        g.DrawImage(b, new Rectangle(r.X, r.Y, side, r.Height), 0, 0, sx, b.Height, GraphicsUnit.Pixel, a);
        g.DrawImage(b, new Rectangle(r.X + side, r.Y, midW, r.Height), sx, 0, mid, b.Height, GraphicsUnit.Pixel, a);
        g.DrawImage(b, new Rectangle(r.X + side + midW, r.Y, r.Right - r.X - side - midW, r.Height), sx + mid, 0, b.Width - sx - mid, b.Height, GraphicsUnit.Pixel, a);
    }

    /// <summary>Selector arrow (the game's chevron); faded when the value cannot move further.</summary>
    public static void Arrow(Graphics g, Rectangle r, bool left, float alpha, Color? tint)
    {
        if (Art[left ? "arrow_left" : "arrow_right"] is Bitmap b) { Image(g, b, r, alpha, tint); return; }
        var c = Color.FromArgb((int)(alpha * 255), tint ?? Soft);
        int x0 = left ? r.Right : r.X, x1 = left ? r.X : r.Right, cy = r.Y + r.Height / 2;
        using var p = new Pen(c, Math.Max(2f, r.Width / 5f));
        g.DrawLines(p, new[] { new Point(x0, r.Y + r.Height / 5), new Point(x1, cy), new Point(x0, r.Bottom - r.Height / 5) });
    }

    /// <summary>Slider with the game's track and lens knob; t is 0..1.</summary>
    public static void Slider(Graphics g, Rectangle track, float t, int knobH)
    {
        int kx = track.X + (int)(t * track.Width), cy = track.Y + track.Height / 2;
        if (Art["slider_track"] is Bitmap tb && Art["slider_knob"] is Bitmap kb)
        {
            Slice3(g, tb, track, 8, 8, 1);
            int kw = knobH * kb.Width / kb.Height;
            Image(g, kb, new Rectangle(kx - kw / 2, cy - knobH / 2, kw, knobH));
            return;
        }
        using (var p = new Pen(Faint, Math.Max(2, track.Height / 3))) g.DrawLine(p, track.X, cy, track.Right, cy);
        using (var p = new Pen(Soft, Math.Max(2, track.Height / 3))) g.DrawLine(p, track.X, cy, kx, cy);
        Diamond(g, kx, cy, knobH / 3f, Text, true);
    }

    public static void Diamond(Graphics g, float cx, float cy, float r, Color c, bool fill)
    {
        var p = new[] { new PointF(cx, cy - r), new PointF(cx + r, cy), new PointF(cx, cy + r), new PointF(cx - r, cy) };
        if (fill) { using var b = new SolidBrush(c); g.FillPolygon(b, p); }
        else { using var pen = new Pen(c, Math.Max(1.2f, r / 3)); g.DrawPolygon(pen, p); }
    }

    /// <summary>Dark ink plate (the game's menu panel bitmap) with a hairline edge, behind text panes and descriptions.</summary>
    public static void Pane(Graphics g, Rectangle r)
    {
        using (var b = new SolidBrush(Color.FromArgb(225, PaneFill))) g.FillRectangle(b, r);
        using var p = new Pen(Color.FromArgb(150, Edge));
        g.DrawRectangle(p, r.X, r.Y, r.Width - 1, r.Height - 1);
    }

    /// <summary>Uppercase caption with letter spacing (the game's menu style). Returns the width; measures only when g is null.</summary>
    public static int Spaced(Graphics? g, string s, Font f, Color c, int x, int y, float track)
    {
        float cx = x;
        const TextFormatFlags fl = TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix;
        foreach (var ch in s)
        {
            var t = ch.ToString();
            int w = ch == ' ' ? (int)(f.Size * .3f) : TextRenderer.MeasureText(t, f, Size.Empty, fl).Width;
            if (g != null && ch != ' ') TextRenderer.DrawText(g, t, f, new Point((int)cx, y), c, fl);
            cx += w + track;
        }
        return s.Length == 0 ? 0 : (int)(cx - track - x);
    }

    /// <summary>Shortens s in the middle ("C:\Program...\Dishonored2") until it fits w pixels.</summary>
    public static string Fit(string s, Font f, int w)
    {
        int Width(string t) => TextRenderer.MeasureText(t, f, Size.Empty, TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width;
        for (int keep = s.Length; keep > 2; keep--)
        {
            var t = keep == s.Length ? s : s[..(keep / 2)] + "..." + s[^(keep - keep / 2)..];
            if (Width(t) <= w) return t;
        }
        return "...";
    }

    /// <summary>Section heading: gold spaced caption followed by the divider ornament.</summary>
    public static void Section(Graphics g, Control c, string caption, Rectangle r)
    {
        int x = r.X;
        if (caption != "")
        {
            var f = Font(c, 9.5f, FontStyle.Bold);
            x += Spaced(g, caption, f, Gold, r.X, r.Y + (r.Height - f.Height) / 2, 2.6f * c.DeviceDpi / 96f) + S(c, 12);
        }
        int h = S(c, 14);
        Divider(g, new Rectangle(x, r.Y + (r.Height - h) / 2, r.Right - x, h));
    }
}

/// <summary>Owner-drawn control whose background is its slice of the window backdrop.</summary>
class ArtControl : Control
{
    public ArtControl() => SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint | ControlStyles.ResizeRedraw, true);
    protected override void OnPaintBackground(PaintEventArgs e) => Ui.PaintBackdrop(this, e.Graphics);
    protected int S(float v) => Ui.S(this, v);
}

/// <summary>A page: paints section headings, text-pane plates and anything else via Painter, under its child controls.</summary>
sealed class ArtPanel : ArtControl
{
    public readonly List<(string Caption, Rectangle Rect)> Sections = new();
    public readonly List<Rectangle> Panes = new();
    public Action<Graphics>? Painter;
    public ArtPanel() { SetStyle(ControlStyles.Selectable, false); TabStop = false; }
    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics; Ui.Quality(g);
        foreach (var r in Panes) Ui.Pane(g, r);
        foreach (var (c, r) in Sections) Ui.Section(g, this, c, r);
        Painter?.Invoke(g);
    }
}

enum Look { Nav, Primary, Secondary, Tab }

/// <summary>Button drawn with game art. Still a real Button: Tab focus, Enter/Space, accessibility name.</summary>
sealed class ArtButton : Button
{
    public readonly Look Look;
    public bool Selected;
    bool hover;

    public ArtButton(string text, Look look)
    {
        Text = text; Look = look; Cursor = Cursors.Hand; FlatStyle = FlatStyle.Flat;
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.UserPaint, true);
    }

    int S(float v) => Ui.S(this, v);
    Font LabelFont => Ui.Font(this, Look switch { Look.Nav => 15f, Look.Primary => 14f, Look.Tab => 10f, _ => 10f },
        Look is Look.Primary or Look.Tab ? FontStyle.Bold : FontStyle.Regular);
    float Track => (Look is Look.Nav or Look.Primary ? 2.6f : 1.7f) * DeviceDpi / 96f;
    int TextWidth => Ui.Spaced(null, Text.ToUpperInvariant(), LabelFont, Color.Empty, 0, 0, Track);
    /// <summary>Width that fits the label with the look's padding.</summary>
    public int Natural => TextWidth + S(Look switch { Look.Primary => 84, Look.Tab => 20, Look.Nav => 70, _ => 44 });

    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }
    protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
    protected override void OnLostFocus(EventArgs e) { Invalidate(); base.OnLostFocus(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics; Ui.PaintBackdrop(this, g); Ui.Quality(g);  // ButtonBase is Opaque: OnPaintBackground is never called
        var f = LabelFont; var t = Text.ToUpperInvariant(); int tw = TextWidth, ty = (Height - f.Height) / 2;
        bool hot = Enabled && (hover || (Focused && ShowFocusCues));
        int tx = (Width - tw) / 2; Color fg;
        switch (Look)
        {
            case Look.Nav:
                tx = S(28);
                if (Selected) Ui.Brush(g, new Rectangle(0, S(5), Math.Min(Width, tw + S(70)), Height - S(10)));
                else if (hot) Ui.Brush(g, new Rectangle(0, S(5), Math.Min(Width, tw + S(70)), Height - S(10)), .16f);
                fg = Selected ? Ui.OnBrush : hot ? Ui.Text : Ui.Soft;
                break;
            case Look.Primary:
                var br = Rectangle.Inflate(ClientRectangle, -S(6), -S(9));
                if (hot) Ui.Glow(g, new Rectangle(br.X + S(14), 0, br.Width - S(28), Height), .7f);
                Ui.Brush(g, br, Enabled ? 1f : .28f);
                fg = Enabled ? Ui.OnBrush : Ui.Dim;
                break;
            case Look.Secondary:
                Ui.Frame(g, Rectangle.Inflate(ClientRectangle, -S(1), -S(3)), !Enabled ? .22f : hot ? 1f : .55f);
                if (hot) Ui.Brush(g, Rectangle.Inflate(ClientRectangle, -S(10), -S(9)), .1f);
                fg = !Enabled ? Ui.Faint : hot ? Ui.Text : Ui.Soft;
                break;
            default:
                tx = S(13);
                fg = Selected ? Ui.Gold : hot ? Ui.Text : Ui.Dim;
                if (Selected) Ui.Diamond(g, S(4), Height / 2f, S(3), Ui.Gold, true);
                break;
        }
        Ui.Spaced(g, t, f, fg, tx, ty, Track);
        if (Focused && ShowFocusCues && Look != Look.Primary)
            using (var p = new Pen(Ui.Gold, Math.Max(1, S(1.5f)))) g.DrawLine(p, tx, ty + f.Height + S(1), tx + tw, ty + f.Height + S(1));
    }
}

/// <summary>A labelled check row with dim help text. Space or a click toggles it; Up/Down move to the neighbouring row.
/// Real focus, so Tab and screen readers work; the game's brush stroke marks the focused row.</summary>
sealed class ArtCheck : ArtControl
{
    bool isChecked, hover, spaceDown;
    public readonly string Label, Help;
    public event EventHandler? Toggled;
    public ArtCheck(string label, string help)
    {
        Label = label; Help = help; Cursor = Cursors.Hand; TabStop = true;
        SetStyle(ControlStyles.Selectable, true);
        AccessibleRole = AccessibleRole.CheckButton; AccessibleName = label; AccessibleDescription = help;
    }
    public int RowHeight => S(54);
    public bool Checked
    {
        get => isChecked;
        set { if (isChecked == value) return; isChecked = value; Invalidate(); Toggled?.Invoke(this, EventArgs.Empty); AccessibilityNotifyClients(AccessibleEvents.StateChange, -1); }
    }
    /// <summary>Sets the box without raising Toggled (used when loading the saved state).</summary>
    public void SetSilently(bool v) { if (isChecked == v) return; isChecked = v; Invalidate(); AccessibilityNotifyClients(AccessibleEvents.StateChange, -1); }

    protected override bool IsInputKey(Keys k) => (k & Keys.KeyCode) is Keys.Up or Keys.Down || base.IsInputKey(k);
    protected override void OnKeyDown(KeyEventArgs e)
    {
        switch (e.KeyCode)
        {
            case Keys.Space: if (!e.Alt && (Control.ModifierKeys == Keys.None) && !spaceDown) { spaceDown = true; Checked = !Checked; } break;
            case Keys.Up: Parent?.SelectNextControl(this, false, true, false, false); break;
            case Keys.Down: Parent?.SelectNextControl(this, true, true, false, false); break;
            default: base.OnKeyDown(e); return;
        }
        e.Handled = true;
    }
    protected override void OnKeyUp(KeyEventArgs e) { if (e.KeyCode == Keys.Space) spaceDown = false; base.OnKeyUp(e); }
    protected override void OnLostFocus(EventArgs e) { spaceDown = false; Invalidate(); base.OnLostFocus(e); }
    protected override void OnMouseDown(MouseEventArgs e) { Focus(); Checked = !Checked; base.OnMouseDown(e); }
    protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
    protected override void OnEnabledChanged(EventArgs e) { Invalidate(); base.OnEnabledChanged(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics; Ui.Quality(g);
        bool sel = Focused && Enabled;
        var hl = new Rectangle(0, S(2), Width, Height - S(4));
        if (sel) Ui.Brush(g, hl); else if (hover && Enabled) Ui.Brush(g, hl, .12f);
        int bs = S(18), bx = S(28), by = (Height - bs) / 2;
        var box = new Rectangle(bx, by, bs, bs);
        var ink = sel ? Ui.OnBrush : Enabled ? Ui.Soft : Ui.Faint;
        using (var pen = new Pen(ink, Math.Max(1.5f, S(1.5f)))) g.DrawRectangle(pen, box);
        if (isChecked) Ui.Diamond(g, box.X + bs / 2f, box.Y + bs / 2f, bs * .3f, sel ? Ui.OnBrush : Ui.Gold, true);
        const TextFormatFlags f = TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix | TextFormatFlags.EndEllipsis | TextFormatFlags.VerticalCenter;
        int tx = bx + bs + S(16), tw = Math.Max(10, Width - tx - S(14));
        var lf = Ui.Font(this, 12f); var hf = Ui.Font(this, 9f, FontStyle.Regular, Ui.Sans);
        TextRenderer.DrawText(g, Label, lf, new Rectangle(tx, S(6), tw, Height / 2 - S(6)), sel ? Ui.OnBrush : Enabled ? Ui.Text : Ui.Faint, f);
        TextRenderer.DrawText(g, Help, hf, new Rectangle(tx, Height / 2, tw, Height / 2 - S(5)), sel ? Ui.OnBrush : Ui.Dim, f);
    }
}

/// <summary>A vertical stack of ArtCheck rows that scrolls with the mouse wheel and keeps the focused row in view.</summary>
sealed class CheckList : ArtControl
{
    readonly List<ArtCheck> rows = new();
    int scroll;
    public CheckList() { SetStyle(ControlStyles.Selectable, false); TabStop = false; AccessibleRole = AccessibleRole.Grouping; AccessibleName = "Features"; }
    // WS_EX_COMPOSITED: the rows (one child window each) are painted together off screen, not one after another while scrolling.
    protected override CreateParams CreateParams { get { var cp = base.CreateParams; cp.ExStyle |= 0x02000000; return cp; } }
    [System.Runtime.InteropServices.DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, int msg, IntPtr w, IntPtr l);
    public IReadOnlyList<ArtCheck> Rows => rows;
    public void Add(ArtCheck c)
    {
        rows.Add(c); Controls.Add(c);
        c.GotFocus += (_, _) => EnsureVisible(c);
        c.MouseWheel += (_, e) => Wheel(e.Delta);
    }
    int Total => rows.Count == 0 ? 0 : rows.Sum(r => r.RowHeight);
    public void Relayout()
    {
        scroll = Math.Clamp(scroll, 0, Math.Max(0, Total - Height));
        int y = -scroll;
        // one repaint for the whole move (WM_SETREDRAW), not one per row
        if (IsHandleCreated) SendMessage(Handle, 0x000B, IntPtr.Zero, IntPtr.Zero);
        foreach (var r in rows) { r.Bounds = new Rectangle(0, y, Math.Max(10, Width - S(10)), r.RowHeight); y += r.RowHeight; }
        if (IsHandleCreated) SendMessage(Handle, 0x000B, (IntPtr)1, IntPtr.Zero);
        Invalidate(true);
    }
    /// <summary>One wheel notch scrolls three rows (the Windows default of three lines).</summary>
    void Wheel(int delta) { int row = rows.Count > 0 ? rows[0].RowHeight : S(54); scroll = Math.Clamp(scroll - delta * 3 * row / 120, 0, Math.Max(0, Total - Height)); Relayout(); }
    void EnsureVisible(ArtCheck c)
    {
        int top = c.Top + scroll;  // top in content coordinates
        if (top < scroll) scroll = top; else if (top + c.RowHeight > scroll + Height) scroll = top + c.RowHeight - Height;
        Relayout();
    }
    protected override void OnMouseWheel(MouseEventArgs e) { Wheel(e.Delta); base.OnMouseWheel(e); }
    /// <summary>Creates the row windows ahead of time, so the first opening of the list does not build them.</summary>
    public void Prewarm() { _ = Handle; foreach (var r in rows) _ = r.Handle; }
    protected override void OnResize(EventArgs e) { base.OnResize(e); Relayout(); }
    protected override void OnPaint(PaintEventArgs e)
    {
        if (Total > Height)  // a thin scroll thumb on the right edge
        {
            var g = e.Graphics; int th = Math.Max(S(24), Height * Height / Total), ty = (Height - th) * scroll / Math.Max(1, Total - Height);
            using var b = new SolidBrush(Color.FromArgb(110, Ui.Soft));
            g.FillRectangle(b, Width - S(4), ty, S(3), th);
        }
        base.OnPaint(e);
    }
}

public sealed record Item(string Text, object Val);

/// <summary>One settings row: a list of choices cycled with the arrows, or an integer slider.</summary>
sealed class Option
{
    public readonly string Name;
    public string Help;
    public readonly List<Item> Items = new();
    public int Index;
    public bool IsSlider; public int Value, Min, Max, Step = 1, Page = 1;
    public Func<int, string> Format = v => v.ToString();
    public Func<bool> Active = () => true;
    public Option(string name, string help, params (string text, object val)[] items) { Name = name; Help = help; foreach (var (t, v) in items) Items.Add(new Item(t, v)); }

    public bool Move(int dir, bool page = false)
    {
        int old = IsSlider ? Value : Index;
        if (IsSlider) Value = Math.Clamp(Value + dir * (page ? Page : Step), Min, Max);
        else Index = Math.Clamp(Index + dir, 0, Items.Count - 1);
        return old != (IsSlider ? Value : Index);
    }
}

/// <summary>The game-style options list: label left, value between selector arrows, brush highlight on the selected row.
/// Up/Down select, Left/Right change, PageUp/PageDown move sliders faster; mouse clicks the arrows or drags the slider.</summary>
sealed class OptionList : ArtControl
{
    public readonly List<object> Rows = new();  // string = section heading, Option = setting
    public int Selected = -1;
    public event Action? SelectionChanged;
    int hover = -1; bool drag;
    int scroll;  // pixels the rows are moved up when they do not all fit (mouse wheel, or the selection moving out of view)

    public OptionList() { SetStyle(ControlStyles.Selectable, true); TabStop = true; AccessibleRole = AccessibleRole.List; AccessibleName = "VR settings"; }
    public Option? Current => Selected >= 0 && Selected < Rows.Count ? Rows[Selected] as Option : null;
    int Headings => Rows.Count(r => r is string);
    public int IdealHeight => Headings * S(32) + (Rows.Count - Headings) * S(34);
    int HeadH => S(32);
    /// <summary>Rows shrink to fit down to a readable minimum; below that the list scrolls.</summary>
    int RowH { get { int opts = Rows.Count - Headings; return opts == 0 ? 0 : Math.Clamp((Height - Headings * HeadH) / opts, S(24), S(36)); } }
    int Total => Headings * HeadH + (Rows.Count - Headings) * RowH;
    int MaxScroll => Math.Max(0, Total - Height);

    IEnumerable<(object Row, Rectangle R, int I)> RowRects()
    {
        int hh = HeadH, rh = RowH, y = -Math.Clamp(scroll, 0, MaxScroll);
        for (int i = 0; i < Rows.Count; i++) { int h = Rows[i] is string ? hh : rh; yield return (Rows[i], new Rectangle(0, y, Width, h), i); y += h; }
    }
    void ScrollTo(int value) { value = Math.Clamp(value, 0, MaxScroll); if (value == scroll) return; scroll = value; hover = -1; Invalidate(); }
    /// <summary>Brings row i into view, together with the section heading right above it.</summary>
    void EnsureVisible(int i)
    {
        int first = i > 0 && Rows[i - 1] is string ? i - 1 : i, top = 0, bottom = 0, y = 0;
        for (int k = 0; k <= i; k++) { int h = Rows[k] is string ? HeadH : RowH; if (k == first) top = y; y += h; bottom = y; }
        if (top < scroll) ScrollTo(top); else if (bottom > scroll + Height) ScrollTo(bottom - Height);
    }
    /// <summary>One wheel notch scrolls three rows (the Windows default of three lines), as the Features list does.</summary>
    protected override void OnMouseWheel(MouseEventArgs e) { ScrollTo(scroll - e.Delta * 3 * Math.Max(1, RowH) / 120); base.OnMouseWheel(e); }
    protected override void OnMouseEnter(EventArgs e) { if (MaxScroll > 0 && !Focused && CanFocus) Focus(); base.OnMouseEnter(e); }  // the wheel goes to the focused control
    protected override void OnResize(EventArgs e) { base.OnResize(e); scroll = Math.Clamp(scroll, 0, MaxScroll); if (Selected >= 0 && Selected < Rows.Count) EnsureVisible(Selected); }
    int LabelW => Math.Max(S(190), Width * 42 / 100);
    Rectangle ValueRect(Rectangle r) => new(LabelW, r.Y, r.Right - S(14) - LabelW, r.Height);
    Rectangle TrackRect(Rectangle r) { var v = ValueRect(r); return new Rectangle(v.X + S(6), r.Y + (r.Height - S(10)) / 2, v.Width - S(78), S(10)); }
    (Rectangle l, Rectangle r) Arrows(Rectangle row)
    {
        var v = ValueRect(row); int ah = Math.Min(S(20), row.Height - S(8)), aw = ah * 24 / 36, y = row.Y + (row.Height - ah) / 2;
        return (new Rectangle(v.X + S(4), y, aw, ah), new Rectangle(v.Right - S(4) - aw, y, aw, ah));
    }

    void Select(int i)
    {
        if (i == Selected || i < 0 || i >= Rows.Count || Rows[i] is not Option) return;
        Selected = i; EnsureVisible(i); Invalidate(); SelectionChanged?.Invoke();
        AccessibilityNotifyClients(AccessibleEvents.Focus, -1);
    }
    void Change(Option o, int dir, bool page = false)
    {
        if (!o.Move(dir, page)) return;
        Invalidate(); AccessibleDescription = $"{o.Name}: {ValueText(o)}";
    }
    static string ValueText(Option o) => o.IsSlider ? o.Format(o.Value) : o.Items.Count > 0 ? o.Items[o.Index].Text : "";

    protected override bool IsInputKey(Keys k) => (k & Keys.KeyCode) is Keys.Up or Keys.Down or Keys.Left or Keys.Right or Keys.PageUp or Keys.PageDown || base.IsInputKey(k);
    protected override void OnKeyDown(KeyEventArgs e)
    {
        var o = Current;
        switch (e.KeyCode)
        {
            case Keys.Up: for (int i = Selected - 1; i >= 0; i--) if (Rows[i] is Option) { Select(i); break; } break;
            case Keys.Down: for (int i = Selected + 1; i < Rows.Count; i++) if (Rows[i] is Option) { Select(i); break; } break;
            case Keys.Left: if (o != null) Change(o, -1); break;
            case Keys.Right: if (o != null) Change(o, 1); break;
            case Keys.PageUp: if (o != null) Change(o, 1, true); break;
            case Keys.PageDown: if (o != null) Change(o, -1, true); break;
            default: base.OnKeyDown(e); return;
        }
        e.Handled = true;
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        Focus();
        foreach (var (row, r, i) in RowRects())
        {
            if (!r.Contains(e.Location) || row is not Option o) continue;
            Select(i);
            if (o.IsSlider) { var t = TrackRect(r); if (e.X >= t.X - S(12) && e.X <= t.Right + S(12)) { drag = true; SlideTo(o, e.X, t); } }
            else
            {
                var (l, rr) = Arrows(r);
                if (e.X < l.Right + S(14) && e.X >= l.X - S(14)) Change(o, -1);
                else if (e.X > rr.X - S(14) && e.X <= rr.Right + S(14)) Change(o, 1);
            }
        }
        base.OnMouseDown(e);
    }
    void SlideTo(Option o, int x, Rectangle t)
    {
        int v = o.Min + (int)Math.Round((double)(x - t.X) / Math.Max(1, t.Width) * (o.Max - o.Min));
        v = Math.Clamp(v, o.Min, o.Max);
        if (v != o.Value) { o.Value = v; Invalidate(); }
    }
    protected override void OnMouseMove(MouseEventArgs e)
    {
        int h = -1;
        foreach (var (row, r, i) in RowRects())
        {
            if (drag && i == Selected && row is Option o) SlideTo(o, e.X, TrackRect(r));
            if (r.Contains(e.Location) && row is Option) h = i;
        }
        if (h != hover) { hover = h; Invalidate(); }
        base.OnMouseMove(e);
    }
    protected override void OnMouseUp(MouseEventArgs e) { drag = false; base.OnMouseUp(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = -1; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
    protected override void OnLostFocus(EventArgs e) { Invalidate(); base.OnLostFocus(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics; Ui.Quality(g);
        var label = Ui.Font(this, 12.5f); var value = Ui.Font(this, 12f);
        const TextFormatFlags vc = TextFormatFlags.VerticalCenter | TextFormatFlags.NoPadding | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix;
        foreach (var (row, r, i) in RowRects())
        {
            if (row is string h) { Ui.Section(g, this, h, r); continue; }
            var o = (Option)row; bool sel = i == Selected, on = o.Active();
            var hl = new Rectangle(r.X, r.Y + S(2), r.Width, r.Height - S(4));
            if (sel) Ui.Brush(g, hl, Focused ? 1f : .85f);
            else if (i == hover) Ui.Brush(g, hl, .12f);
            var fg = sel ? Ui.OnBrush : on ? Ui.Text : Ui.Faint;
            var lr = new Rectangle(S(28), r.Y, LabelW - S(36), r.Height);
            // a name wider than its column is drawn a size smaller instead of being cut with an ellipsis
            var lf = TextRenderer.MeasureText(g, o.Name, label, lr.Size, TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix).Width > lr.Width ? Ui.Font(this, 11f) : label;
            TextRenderer.DrawText(g, o.Name, lf, lr, fg, vc);
            var v = ValueRect(r);
            if (o.IsSlider)
            {
                Ui.Slider(g, TrackRect(r), (float)(o.Value - o.Min) / Math.Max(1, o.Max - o.Min), Math.Min(S(24), r.Height - S(6)));
                TextRenderer.DrawText(g, o.Format(o.Value), value, new Rectangle(v.Right - S(62), r.Y, S(62), r.Height), fg, vc | TextFormatFlags.Right);
            }
            else
            {
                var (al, ar) = Arrows(r); Color? tint = sel ? Ui.OnBrush : null;
                Ui.Arrow(g, al, true, o.Index > 0 ? (on ? 1f : .5f) : .18f, tint);
                Ui.Arrow(g, ar, false, o.Index < o.Items.Count - 1 ? (on ? 1f : .5f) : .18f, tint);
                TextRenderer.DrawText(g, ValueText(o), value, Rectangle.FromLTRB(al.Right + S(6), r.Y, ar.X - S(6), r.Bottom), fg, vc | TextFormatFlags.HorizontalCenter);
            }
        }
        if (MaxScroll > 0)  // a thin scroll thumb on the right edge, as on the Features list
        {
            // rows cut by the edge fade out, which also says there is more in that direction
            int sc = Math.Clamp(scroll, 0, MaxScroll), fh = S(26);
            if (sc < MaxScroll) { var fr = new Rectangle(0, Height - fh, Width - S(6), fh); using var fb = new System.Drawing.Drawing2D.LinearGradientBrush(new Rectangle(fr.X, fr.Y - 1, fr.Width, fr.Height + 2), Color.FromArgb(0, Ui.Ink), Color.FromArgb(245, Ui.Ink), 90f); g.FillRectangle(fb, fr); }
            if (sc > 0) { var fr = new Rectangle(0, 0, Width - S(6), fh); using var fb = new System.Drawing.Drawing2D.LinearGradientBrush(new Rectangle(fr.X, fr.Y - 1, fr.Width, fr.Height + 2), Color.FromArgb(245, Ui.Ink), Color.FromArgb(0, Ui.Ink), 90f); g.FillRectangle(fb, fr); }
            int th = Math.Max(S(24), Height * Height / Total), ty = (Height - th) * Math.Clamp(scroll, 0, MaxScroll) / MaxScroll;
            using var b = new SolidBrush(Color.FromArgb(110, Ui.Soft));
            g.FillRectangle(b, Width - S(4), ty, S(3), th);
        }
    }
}
