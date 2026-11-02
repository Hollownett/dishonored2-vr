using System.Text.RegularExpressions;
using System.Runtime.InteropServices;

namespace DishonoredVR;

/// <summary>The Markdown document is shared by the illustrated and selectable text views.</summary>
sealed class ControlsDocument
{
    public static readonly string[] PageIds = { "body", "controllers", "tips" };
    public static readonly string[] FigureIds = { "sword_draw_block", "enemy_grab" };
    public sealed class Lesson(string title)
    {
        public readonly string Title = title;
        public string Figure = "", Alt = "";
        public readonly List<string> Lines = new();
    }
    public sealed class Page(string id, string name)
    {
        public readonly string Id = id, Name = name;
        public readonly List<Lesson> Lessons = new();
    }
    public string Title = "VR controls and gestures";
    public readonly List<string> Intro = new(), Errors = new();
    public readonly List<Page> Pages = new();

    public static ControlsDocument Parse(string markdown, bool strict = false)
    {
        var doc = new ControlsDocument(); Page? page = null; Lesson? lesson = null;
        int number = 0; bool comment = false;
        foreach (string raw in markdown.Replace("\r", "").Split('\n'))
        {
            number++; string line = raw.Trim();
            if (comment) { if (line.Contains("-->", StringComparison.Ordinal)) comment = false; continue; }
            if (line.StartsWith("<!-- vr-", StringComparison.Ordinal))
            {
                var m = Regex.Match(line, @"^<!-- vr-(page|figure):([a-z0-9_-]+)\|(.+?) -->$");
                bool valid = m.Success && !string.IsNullOrWhiteSpace(m.Groups[3].Value);
                string kind = m.Groups[1].Value, id = m.Groups[2].Value, text = m.Groups[3].Value;
                if (valid && kind == "page") valid = PageIds.Contains(id) && !doc.Pages.Any(p => p.Id == id);
                if (valid && kind == "figure") valid = FigureIds.Contains(id) && lesson != null && lesson.Figure == "" && lesson.Lines.Count == 0 &&
                    !doc.Pages.SelectMany(p => p.Lessons).Any(l => l.Figure == id);
                if (!valid) { doc.Errors.Add($"Line {number}: malformed or unknown guide directive."); continue; }
                if (kind == "page") { page = new Page(id, text); doc.Pages.Add(page); lesson = null; }
                else { lesson!.Figure = id; lesson.Alt = text; }
                continue;
            }
            if (line.StartsWith("<!--", StringComparison.Ordinal)) { comment = !line.Contains("-->", StringComparison.Ordinal); continue; }
            if (line == "") continue;
            if (line.StartsWith("# ", StringComparison.Ordinal)) doc.Title = line[2..];
            else if (line.StartsWith("## ", StringComparison.Ordinal))
            {
                // Old documents without directives remain fully readable.
                if (page == null) { page = new Page("body", "Body"); doc.Pages.Add(page); }
                lesson = new Lesson(line[3..]); page.Lessons.Add(lesson);
            }
            else if (lesson != null) lesson.Lines.Add(line);
            else doc.Intro.Add(line);
        }
        if (strict && doc.Errors.Count > 0) throw new FormatException(string.Join(Environment.NewLine, doc.Errors));
        return doc;
    }

    internal static void CheckParser()
    {
        using (var resource = typeof(ControlsDocument).Assembly.GetManifestResourceStream("home/VR-CONTROLS.md")
            ?? throw new InvalidOperationException("Embedded controls document is missing."))
        using (var reader = new StreamReader(resource))
        {
            string markdown = reader.ReadToEnd(); var actual = Parse(markdown, true);
            var lessons = actual.Pages.SelectMany(p => p.Lessons).ToArray();
            string[] source = markdown.Replace("\r", "").Split('\n').Select(s => s.Trim()).ToArray();
            if (!actual.Pages.Select(p => p.Id).SequenceEqual(PageIds) || lessons.Length != 5 ||
                !lessons.Where(l => l.Figure != "").Select(l => l.Figure).SequenceEqual(FigureIds) ||
                !lessons.Select(l => l.Title).SequenceEqual(source.Where(s => s.StartsWith("## ", StringComparison.Ordinal)).Select(s => s[3..])) ||
                !lessons.SelectMany(l => l.Lines).Where(s => s.StartsWith("- ", StringComparison.Ordinal)).SequenceEqual(source.Where(s => s.StartsWith("- ", StringComparison.Ordinal))) ||
                lessons.Sum(l => l.Lines.Count(s => s.StartsWith("- ", StringComparison.Ordinal))) != 26)
                throw new InvalidOperationException("Embedded guide must preserve all three pages, two figures, five headings and twenty-six bullets.");
            var withoutArt = Parse(Regex.Replace(markdown, @"(?m)^<!-- vr-figure:.*\r?\n?", ""), true);
            if (!withoutArt.Pages.SelectMany(p => p.Lessons).SelectMany(l => l.Lines).SequenceEqual(lessons.SelectMany(l => l.Lines)))
                throw new InvalidOperationException("Guide instructions changed when figures were omitted.");
        }
        var sample = Parse("# Guide\nIntro\n<!-- ignored -->\n<!-- vr-page:body|Body -->\n## Throw\n<!-- vr-figure:sword_draw_block|Open hand -->\n- **Release** — throw", true);
        if (sample.Pages.Count != 1 || sample.Pages[0].Lessons.Single().Figure != "sword_draw_block" || sample.Pages[0].Lessons[0].Lines.Single() != "- **Release** — throw")
            throw new InvalidOperationException("Guide parser lost a heading, figure or bullet.");
        var invalid = Parse("<!-- vr-page:../../file|Unsafe -->\n<!-- vr-page:body|Safe -->\n## Keep me\n<!-- vr-figure:unknown|Missing -->\n- Keep this bullet");
        if (invalid.Errors.Count != 2 || invalid.Pages.Single().Lessons.Single().Lines.Single() != "- Keep this bullet")
            throw new InvalidOperationException("Guide parser did not safely reject unknown identifiers.");
        foreach (string bad in new[]
        {
            "<!-- vr-page:body|   -->",
            "<!-- vr-page:body|Safe -->\n## Lesson\n<!-- vr-figure:sword_draw_block|   -->",
            "<!-- vr-page:body|Safe -->\n## First\n<!-- vr-figure:sword_draw_block|One -->\n## Second\n<!-- vr-figure:sword_draw_block|Two -->",
            "<!-- vr-page:body|Safe -->\n## Lesson\n- First instruction\n<!-- vr-figure:sword_draw_block|Too late -->"
        })
        {
            if (Parse(bad).Errors.Count != 1) throw new InvalidOperationException("Guide parser accepted a blank, duplicate or misplaced directive.");
            bool rejected = false;
            try { Parse(bad, true); } catch (FormatException) { rejected = true; }
            if (!rejected) throw new InvalidOperationException("Strict guide validation accepted a malformed directive.");
        }
        try { Parse("<!-- vr-page:unknown|Invalid -->", true); }
        catch (FormatException) { return; }
        throw new InvalidOperationException("Guide validation accepted an invalid directive.");
    }
}

/// <summary>Native desktop lessons; images are optional and never replace the instructions.</summary>
sealed class ControlsGuide : ArtControl
{
    readonly ControlsDocument document;
    readonly Panel scroll = new() { AutoScroll = true, BackColor = Ui.PaneFill, TabStop = true };
    readonly List<ArtButton> selectors = new();
    readonly ArtButton corvo = new("Corvo", Look.Tab), emily = new("Emily", Look.Tab), all = new("All controls", Look.Tab);
    readonly List<LessonView> lessons = new();
    readonly RichTextBox text = new();
    string artDir, character = "corvo";
    readonly bool noArt;
    GameArt art;
    int selected, headerBottom;
    bool textMode, arranging;

    public ControlsGuide(string markdown, string artDir, bool noArt = false)
    {
        document = ControlsDocument.Parse(markdown); this.artDir = artDir; this.noArt = noArt;
        art = noArt ? new GameArt() : new GameArt(artDir);
        AccessibleName = "VR controls and gestures"; AccessibleRole = AccessibleRole.Grouping;
        foreach (string id in ControlsDocument.PageIds)
        {
            var page = document.Pages.FirstOrDefault(p => p.Id == id);
            var button = new ArtButton(page?.Name ?? DefaultPageName(id), Look.Tab) { AccessibleName = page?.Name ?? DefaultPageName(id) };
            button.Click += (_, _) => SelectPage(id); selectors.Add(button); Controls.Add(button);
        }
        corvo.Click += (_, _) => SelectCharacter("corvo"); emily.Click += (_, _) => SelectCharacter("emily");
        all.Click += (_, _) => { textMode = !textMode; Rebuild(); };
        Controls.AddRange(new Control[] { corvo, emily, all, scroll });
        StyleText(text); text.AccessibleName = "All controls, selectable text";
        DarkScrollbars(scroll);
        text.KeyDown += NavigateText;
        Rebuild();
    }

    static string DefaultPageName(string id) => id switch
    {
        "body" => "Body", "controllers" => "Controllers", _ => "Gestures"
    };
    public void SelectPage(string id)
    {
        int i = Array.IndexOf(ControlsDocument.PageIds, id); if (i < 0) return;
        selected = i; textMode = false; Rebuild();
    }
    public void SelectCharacter(string value)
    {
        if (value is not ("corvo" or "emily")) return;
        character = value; Rebuild();
    }
    public void ReloadArt() => ReloadArt(artDir);
    public void ReloadArt(string directory)
    {
        artDir = directory; var old = art; art = noArt ? new GameArt() : new GameArt(artDir);
        Rebuild(); old.Dispose();
    }
    protected override void Dispose(bool disposing)
    {
        if (disposing) { art.Dispose(); text.Dispose(); }
        base.Dispose(disposing);
    }
    static void StyleText(RichTextBox box)
    {
        box.ReadOnly = true; box.BorderStyle = BorderStyle.None; box.BackColor = Ui.PaneFill; box.ForeColor = Ui.Text;
        box.DetectUrls = false; box.HideSelection = false; box.WordWrap = true; box.Cursor = Cursors.Default;
        DarkScrollbars(box);
    }
    [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
    static extern int SetWindowTheme(IntPtr handle, string subAppName, string? subIdList);
    static void DarkScrollbars(Control control)
    {
        control.HandleCreated += (_, _) => SetWindowTheme(control.Handle, "DarkMode_Explorer", null);
        if (control.IsHandleCreated) SetWindowTheme(control.Handle, "DarkMode_Explorer", null);
    }
    void NavigateText(object? sender, KeyEventArgs e)
    {
        if (e.Modifiers != Keys.None || e.KeyCode is not (Keys.Left or Keys.Right)) return;
        ChangePage(e.KeyCode); e.Handled = true; e.SuppressKeyPress = true;
    }
    void ChangePage(Keys key) => SelectPage(ControlsDocument.PageIds[Math.Clamp(selected + (key == Keys.Left ? -1 : 1), 0, selectors.Count - 1)]);
    static void Add(RichTextBox box, string value, Font font, Color color)
    {
        box.SelectionStart = box.TextLength; box.SelectionLength = 0; box.SelectionFont = font; box.SelectionColor = color; box.AppendText(value);
    }
    internal static void Body(RichTextBox box, IEnumerable<string> lines)
    {
        box.Clear(); var font = Ui.Font(box, 11.5f, FontStyle.Regular, Ui.Sans); var bold = Ui.Font(box, 11.5f, FontStyle.Bold, Ui.Sans);
        foreach (string raw in lines)
        {
            bool bullet = raw.StartsWith("- ", StringComparison.Ordinal); string line = bullet ? raw[2..] : raw;
            if (bullet) Add(box, "◆  ", Ui.Font(box, 7f, FontStyle.Regular, Ui.Sans), Ui.Gold);
            int pos = 0; bool input = false;
            while (pos < line.Length)
            {
                int next = line.IndexOf("**", pos, StringComparison.Ordinal);
                if (next < 0) next = line.Length;
                Add(box, line[pos..next], input ? bold : font, input ? Ui.Gold : Ui.Text);
                if (next == line.Length) break;
                input = !input; pos = next + 2;
            }
            Add(box, "\n\n", font, Ui.Text);
        }
        box.SelectionStart = 0; box.SelectionLength = 0;
    }
    void FillAll()
    {
        text.Clear(); Add(text, document.Title + "\n\n", Ui.Font(this, 18), Ui.Gold);
        Add(text, string.Join("\n", document.Intro) + "\n", Ui.Font(this, 11.5f, FontStyle.Regular, Ui.Sans), Ui.Text);
        foreach (var page in document.Pages)
        foreach (var lesson in page.Lessons)
        {
            Add(text, "\n" + lesson.Title + "\n", Ui.Font(this, 18), Ui.Gold);
            using var body = new RichTextBox(); Body(body, lesson.Lines);
            text.SelectionStart = text.TextLength; text.SelectedRtf = body.Rtf;
        }
        text.SelectionStart = 0; text.ScrollToCaret();
    }
    void Rebuild()
    {
        scroll.SuspendLayout(); scroll.Controls.Remove(text);
        foreach (var lesson in lessons) lesson.Dispose(); lessons.Clear();
        for (int i = 0; i < selectors.Count; i++) { selectors[i].Selected = !textMode && i == selected; selectors[i].Invalidate(); }
        corvo.Selected = character == "corvo"; emily.Selected = character == "emily"; all.Selected = textMode;
        corvo.Invalidate(); emily.Invalidate(); all.Invalidate();
        if (textMode) { FillAll(); scroll.Controls.Add(text); }
        else
        {
            var page = document.Pages.FirstOrDefault(p => p.Id == ControlsDocument.PageIds[selected]);
            if (page != null) foreach (var lesson in page.Lessons)
            {
                var view = new LessonView(lesson, lesson.Figure == "" ? null : art[$"guide_{lesson.Figure}_{character}"], Diagram(page.Id, lesson));
                view.NavigateText += NavigateText;
                lessons.Add(view); scroll.Controls.Add(view);
            }
        }
        scroll.AutoScrollPosition = Point.Empty; scroll.ResumeLayout(); Arrange(); Invalidate();
    }
    /// <summary>The Body and Controllers pages are the game guide's pictures: a ring on each button or zone, joined by a leader to its label.</summary>
    Bitmap? Diagram(string page, ControlsDocument.Lesson lesson)
    {
        var bullets = lesson.Lines.Select(l => System.Text.RegularExpressions.Regex.Match(l, @"^- \*\*(.+?)\*\*\s*(?:—|-|:)\s*(.*)$"))
            .Where(m => m.Success).Select(m => (Key: m.Groups[1].Value, Text: m.Groups[2].Value)).ToList();
        if (bullets.Count != lesson.Lines.Count(l => l.StartsWith("- ", StringComparison.Ordinal))) return null;
        float s = DeviceDpi / 96f;
        try
        {
            if (page == "controllers")
            {
                // Labels on the controller's outer side, as in the game.
                bool left = lesson.Title.StartsWith("Left", StringComparison.OrdinalIgnoreCase);
                return GuideFigures.Callouts(GuideFigures.ControllerImage[left ? 0 : 1], .56f, bullets.Select(b =>
                {
                    var at = GuideFigures.ControllerAnchor(left, b.Key);
                    return new GuideFigures.Callout(b.Key, b.Text, at, new[] { at }, left);
                }).ToList(), s);
            }
            if (page == "body" && art[$"guide_body_{character}"] is Bitmap body && bullets.Count == GuideFigures.BodyMarks.Length)
                // The right-side zones label on the left of the figure (it faces you), as in the game.
                return GuideFigures.Callouts(body, .5f, bullets.Select((b, i) =>
                {
                    var rings = GuideFigures.BodyMarks[i].Select(a => (PointF)GuideFigures.BodyMapAnchors[a]).ToArray();
                    return new GuideFigures.Callout(b.Key, b.Text, rings[0], rings, rings[0].X < GuideFigures.BodyMapSize.Width / 2f);
                }).ToList(), s, 280);
        }
        catch (ArgumentException) { }  // an edited guide names a button the picture lacks: the text stays
        return null;
    }
    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        if (ContainsFocus && keyData is Keys.Left or Keys.Right)
        {
            ChangePage(keyData); return true;
        }
        return base.ProcessCmdKey(ref msg, keyData);
    }
    protected override void OnResize(EventArgs e) { base.OnResize(e); Arrange(); }
    protected override void OnDpiChangedAfterParent(EventArgs e) { base.OnDpiChangedAfterParent(e); Rebuild(); }
    void Arrange()
    {
        if (arranging || selectors.Count == 0) return;
        arranging = true;
        try
        {
            int scrollY = -scroll.AutoScrollPosition.Y;
            scroll.AutoScrollPosition = Point.Empty;
            int margin = S(24), width = Math.Max(S(80), ClientSize.Width - margin * 2), x = margin, y = S(96), height = S(34);
            foreach (var button in selectors.Concat(new[] { corvo, emily, all }))
            {
                int w = Math.Min(width, button.Natural + S(6));
                if (x > margin && x + w > margin + width) { x = margin; y += height + S(4); }
                button.Bounds = new Rectangle(x, y, w, height); x += w + S(5);
            }
            headerBottom = y + height + S(28);
            scroll.Bounds = new Rectangle(margin, headerBottom, width, Math.Max(1, Height - headerBottom - margin));
            int usable = Math.Max(S(60), scroll.ClientSize.Width - SystemInformation.VerticalScrollBarWidth - S(3));
            if (textMode) { text.Bounds = new Rectangle(0, 0, usable, Math.Max(1, scroll.ClientSize.Height)); scroll.AutoScrollMinSize = Size.Empty; }
            else
            {
                // Tall diagrams need a full row so the controls remain beside the motion illustration.
                bool tall = lessons.Any(l => l.TallFigure);
                int columns = !tall && lessons.Count > 1 && usable >= S(760) ? 2 : 1, gap = S(28), columnWidth = (usable - (columns - 1) * gap) / columns;
                int[] bottoms = new int[columns];
                for (int i = 0; i < lessons.Count; i++)
                {
                    var view = lessons[i]; int column = i % columns;
                    view.Bounds = new Rectangle(column * (columnWidth + gap), bottoms[column], columnWidth, view.MeasureHeight(columnWidth));
                    bottoms[column] = view.Bottom + gap;
                }
                scroll.AutoScrollMinSize = new Size(0, Math.Max(0, bottoms.Max() - gap));
                scroll.AutoScrollPosition = new Point(0, scrollY);
            }
        }
        finally { arranging = false; }
    }
    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e); var g = e.Graphics; Ui.Quality(g); Ui.Pane(g, ClientRectangle);
        Ui.Spaced(g, document.Title.ToUpperInvariant(), Ui.Font(this, 19), Ui.Text, S(24), S(14), S(3));
        TextRenderer.DrawText(g, string.Join(" ", document.Intro), Ui.Font(this, 10.5f, FontStyle.Regular, Ui.Sans), new Rectangle(S(24), S(52), Width - S(48), S(42)), Ui.Soft, TextFormatFlags.NoPadding | TextFormatFlags.WordBreak);
        using var pen = new Pen(Ui.Gold); g.DrawLine(pen, S(24), headerBottom - S(15), Width - S(24), headerBottom - S(15));
    }

    sealed class LessonView : Control
    {
        readonly ControlsDocument.Lesson lesson;
        readonly Bitmap? figure;
        readonly bool diagram;  // a Body/Controllers callout picture this view owns; it carries the instructions itself
        readonly RichTextBox body = new LessonText();
        int headingHeight, figureWidth;
        public bool TallFigure => lesson.Figure != "" && GuideFigures.Height(lesson.Figure) > 232;
        bool HasFigure => lesson.Figure != "" || diagram;
        public event KeyEventHandler? NavigateText;
        public LessonView(ControlsDocument.Lesson lesson, Bitmap? figure, Bitmap? diagram = null)
        {
            this.lesson = lesson; this.figure = diagram ?? figure; this.diagram = diagram != null;
            SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint, true);
            BackColor = Ui.PaneFill; AccessibleName = lesson.Title; AccessibleDescription = lesson.Alt;
            StyleText(body); body.ScrollBars = RichTextBoxScrollBars.None; body.TabStop = true; body.AccessibleName = lesson.Title + " instructions";
            Controls.Add(body); Body(body, lesson.Lines); body.Visible = !this.diagram;
            if (this.diagram) AccessibleDescription = string.Join(" ", lesson.Lines);
            body.KeyDown += (sender, e) => NavigateText?.Invoke(sender, e);
        }
        protected override void Dispose(bool disposing)
        {
            if (disposing && diagram) figure?.Dispose();
            base.Dispose(disposing);
        }
        int S(float v) => Ui.S(this, v);
        int FigureHeight(int width) => !HasFigure ? 0 : Math.Max(1,
            (int)Math.Round((double)width * (figure?.Height ?? GuideFigures.Height(lesson.Figure)) / (figure?.Width ?? 864)));
        public int MeasureHeight(int width)
        {
            headingHeight = S(50);
            // A callout picture is drawn at device size (shrunk only to fit); a tall motion figure takes half the row.
            if (diagram) { figureWidth = Math.Min(width, figure!.Width); return headingHeight + FigureHeight(figureWidth) + S(8); }
            bool beside = TallFigure && width >= S(680);
            figureWidth = beside ? (width - S(28)) / 2 : width;
            int bodyX = beside ? figureWidth + S(28) : S(6);
            body.Width = Math.Max(1, width - bodyX - S(6));
            // RichEdit measures the actual styled, wrapped text at the current desktop DPI.
            _ = body.Handle;
            body.SelectionStart = 0; body.SelectionLength = 0; body.ScrollToCaret();
            int height = body.GetPositionFromCharIndex(body.TextLength).Y + Ui.Font(this, 11.5f, FontStyle.Regular, Ui.Sans).Height + S(10);
            body.Bounds = new Rectangle(bodyX, headingHeight + (beside || !HasFigure ? 0 : FigureHeight(figureWidth) + S(14)), body.Width, height);
            return Math.Max(body.Bottom, headingHeight + FigureHeight(figureWidth)) + S(8);
        }
        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e); var g = e.Graphics; Ui.Quality(g);
            // The game's menu headings: a brass diamond and spaced capitals.
            var font = Ui.Font(this, 16);
            Ui.Diamond(g, S(5), S(6) + font.Height / 2f, S(4), Ui.Gold, true);
            Ui.Spaced(g, lesson.Title.ToUpperInvariant(), font, Ui.Gold, S(18), S(6), S(2.5f));
            if (!HasFigure) return;
            var r = new Rectangle(0, headingHeight, figureWidth, FigureHeight(figureWidth));
            if (figure != null)
            {
                g.DrawImage(figure, r);
                // A plate in a thin brass frame, as on the game's pages.
                if (!diagram) using (var frame = new Pen(Color.FromArgb(120, 102, 70))) g.DrawRectangle(frame, r.X, r.Y, r.Width - 1, r.Height - 1);
            }
            else TextRenderer.DrawText(g, lesson.Alt, Ui.Font(this, 14, FontStyle.Regular, Ui.Sans), Rectangle.Inflate(r, -S(16), -S(12)), Ui.Text,
                TextFormatFlags.WordBreak | TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.NoPrefix);
        }
    }

    sealed class LessonText : RichTextBox
    {
        protected override void WndProc(ref Message m)
        {
            // RichEdit otherwise consumes the wheel even with its own scrollbars hidden.
            if (m.Msg == 0x020A && Parent?.Parent is Panel panel)
            {
                int delta = unchecked((short)(m.WParam.ToInt64() >> 16));
                int lines = SystemInformation.MouseWheelScrollLines;
                int step = lines < 0 ? panel.ClientSize.Height : Math.Max(1, lines) * Font.Height;
                panel.AutoScrollPosition = new Point(0, Math.Max(0, -panel.AutoScrollPosition.Y - delta * step / 120));
                return;
            }
            base.WndProc(ref m);
        }
    }
}
