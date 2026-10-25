using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;
using System.Runtime.InteropServices;
using DishonoredVR;

static class Program
{
    // [STAThread]: WinForms common dialogs (OpenFileDialog etc.), Clipboard and drag-drop need a single-threaded apartment; top-level statements run MTA and ShowDialog threw ThreadStateException.
    [STAThread]
    static int Main(string[] args)
    {
        // ApplicationConfiguration.Initialize() minus EnableVisualStyles (app.manifest enables Common Controls 6 instead; see there).
        Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
        Application.SetCompatibleTextRenderingDefault(false);
        // Review flags: --page play|settings|tools, --screenshot <png> (PrintWindow of the client area, window shown without activation), --exit, --no-art.
        string? Flag(string name) { int i = Array.IndexOf(args, name); return i >= 0 && i + 1 < args.Length ? args[i + 1] : null; }
        // Headless art check: extract into a temp folder (or --out <dir>) and compare with a tools/extract_game_art.py output folder (exit code 0 = match).
        if (Flag("--check-art") is string reference) return ArtImport.Check(reference, Flag("--out"), Console.Out);
        // Headless calibration-pose render (PoseRender) into <dir>, plus poses_sheet.png for review.
        if (Flag("--render-poses") is string poseDir) return PoseRender.Check(poseDir, Console.Out);
        // Headless probe check (no game access): probe configuration and launch record against an inject_probe.py headset record, DLL exports.
        if (Flag("--check-probe") is string record) return Probe.Check(record, Flag("--dll"), Console.Out);
        // Developer mode uses the repo the exe was built in; a copy anywhere else is standalone and works in %LOCALAPPDATA%\DishonoredVR
        // (or --home <dir>).
        var root = Env.FindRoot(args);
        var env = root != null ? new Env(root) : new Env(Path.GetFullPath(Flag("--home") ?? ModHome.Dir)) { Standalone = true };
        int startPage = Math.Max(0, Array.IndexOf(new[] { "play", "settings", "tools" }, (Flag("--page") ?? "play").ToLowerInvariant()));
        Application.Run(new MainForm(env, startPage, Flag("--screenshot"), args.Contains("--exit"), args.Contains("--no-art")));
        return 0;
    }
}

sealed class MainForm : Form
{
    readonly Env env;
    readonly string? shotFile; readonly bool exitAfterShot, noArt;
    readonly ArtPanel[] pages = { new(), new(), new() };
    readonly ArtButton[] nav = { new("Play", Look.Nav), new("Settings", Look.Nav), new("Tools", Look.Nav) };
    readonly ArtButton importArt = new("Import game art", Look.Secondary);
    int page;
    readonly RichTextBox log = new(), toolOut = new();
    readonly ArtButton launch = new("Launch VR", Look.Primary), stop = new("Stop VR", Look.Secondary), recheck = new("Re-check", Look.Secondary),
        tabSetup = new("Setup checks", Look.Tab), tabLog = new("Launch log", Look.Tab);
    readonly System.Windows.Forms.Timer poll = new() { Interval = 1000 };
    Candidate? candidate; List<Check> checks = new(); bool ready;
    string? artState = "checking";  // null = art present; otherwise importing progress or why it is missing
    bool launching; bool checking; bool stopping; bool importing; bool showLog;
    string statusText = "", statusDetail = ""; Color statusColor = Ui.Dim;

    // settings
    readonly OptionList options = new();
    readonly Option res, aa, hud, turn, snap, smooth, mirror, sharp, hint, crosshair, recenter, cutscenes;
    readonly ArtButton save = new("Save", Look.Primary), reload = new("Reload", Look.Secondary);
    string settingsMsg = ""; Color settingsMsgColor = Ui.Dim;
    // settings: the Features view (default-on features, each switchable; changes are written at once)
    readonly CheckList featList = new();
    readonly ArtButton featToggle = new("Features", Look.Secondary), restore = new("Restore defaults", Look.Secondary);
    readonly ArtButton dlssButton = new(DlssProvider.Prompt, Look.Secondary);
    static readonly (string text, object val)[] AaBase = { ("Off", 0), ("Game TAA", 1), ("FSR 3 - native", 2), ("FSR 3 - quality (77%)", 3), ("FSR 3 - balanced (67%)", 4), ("FSR 4 - native", 5), ("FSR 4 - quality (77%)", 6) };
    static readonly (string text, object val)[] AaDlss = { ("DLAA (NVIDIA DLSS)", 7), ("DLSS - quality (NVIDIA DLSS)", 8) };
    const string AaHelp = "FSR 3 native is the accepted default. Quality/balanced render fewer pixels for speed. FSR 4 is the newer upscaler.";
    IReadOnlyList<Feature> features = Array.Empty<Feature>();
    string? featuresError; bool showFeatures, loadingFeatures;
    Control settingsLast = null!;  // the last button of the row; the message line sits to its right
    Prefs prefs = new();

    // tools
    readonly TextBox custom = new();
    readonly ArtButton[] cmdButtons, diagButtons;
    readonly ArtButton uninstallProxy = new("Uninstall proxy", Look.Secondary);
    readonly ArtButton send = new("Send", Look.Secondary), result = new("Show result", Look.Secondary), selftest = new("Run self-test", Look.Secondary);

    public MainForm(Env e, int startPage, string? shot, bool exitAfter, bool plainStyle)
    {
        env = e; page = startPage; shotFile = shot; exitAfterShot = exitAfter; noArt = plainStyle;
        Text = "Dishonored 2 VR Launcher"; BackColor = Ui.Ink; ForeColor = Ui.Text;
        AutoScaleMode = AutoScaleMode.None;  // layout and fonts scale by DeviceDpi in DoLayout / Ui.Font
        DoubleBuffered = true; StartPosition = FormStartPosition.Manual;
        LoadArt();

        for (int i = 0; i < 3; i++) { int idx = i; nav[i].Click += (_, _) => ShowPage(idx); Controls.Add(nav[i]); }
        importArt.Click += async (_, _) => await ImportArt(); Controls.Add(importArt);
        foreach (var p in pages) { p.Visible = false; Controls.Add(p); }

        // ---- Play page
        var pp = pages[0];
        launch.Click += async (_, _) => await LaunchVr();
        stop.Click += async (_, _) => await StopVr(); stop.Enabled = false;
        recheck.Click += async (_, _) => await RefreshChecks();
        tabSetup.Click += (_, _) => ShowLog(false); tabLog.Click += (_, _) => ShowLog(true);
        StyleText(log); log.Visible = false;
        pp.Controls.AddRange(new Control[] { launch, stop, recheck, tabSetup, tabLog, log });
        pp.Painter = PaintPlay;
        tabSetup.Selected = true;

        // ---- Settings page
        res = new Option("Resolution", "Render scale per eye, 0.50 to 2.00. Higher is sharper but heavier on the GPU.")
        { IsSlider = true, Min = 50, Max = 200, Step = 5, Page = 25, Format = v => (v / 100.0).ToString("0.00", CultureInfo.InvariantCulture) + "x" };
        aa = new Option("Anti-aliasing", AaHelp, AaBase);
        RefreshDlssChoices();
        sharp = new Option("Sharpening", "Contrast-adaptive sharpening after upscaling.", ("Off", 0.0), ("Low", .25), ("Medium", .5), ("High", .75), ("Max", 1.0));
        hud = new Option("HUD size", "How large the game HUD is drawn around the centre of your view.", ("55% (centre)", 55), ("65% (default)", 65), ("75%", 75), ("85%", 85), ("100% (game layout)", 100), ("125%", 125), ("150%", 150), ("200%", 200));
        mirror = new Option("Monitor view", "What the desktop window mirrors while you play in VR.", ("Right eye", 0), ("Left eye", 1), ("Both eyes", 2), ("Off (fastest)", 3));
        hint = new Option("Controls hint", "Shows the controller guide image in VR.", ("Off", 0), ("On", 1));
        crosshair = new Option("Crosshair", "Shows the game's crosshair where your gun or power aims. Off hides it.", ("On (default)", 1), ("Off", 0));
        recenter = new Option("Recenter on scripted moments", "When the game turns or moves you in a scripted moment, re-aim the view so you keep facing the action. Off leaves the view where you were.", ("On (default)", 1), ("Off", 0));
        cutscenes = new Option("Cutscenes", "VR comfort plays cutscenes around you with comfort limits on camera motion. VR full follows the camera exactly. Theater shows them on a flat screen in front of you.", ("VR comfort (default)", "vr_comfort"), ("VR full", "vr_full"), ("Theater", "theater"));
        turn = new Option("Turning", "Right stick turning style. Snap is the comfort-friendly choice.", ("Native (game default)", 0), ("Snap turn", 1), ("Smooth turn", 2));
        snap = new Option("Snap angle", "Degrees per snap step (used by Snap turn).", ("30 degrees", 30.0), ("45 degrees", 45.0), ("90 degrees", 90.0)) { Active = () => Equals(Val<int>(turn), 1) };
        smooth = new Option("Smooth speed", "Turn speed for Smooth turn.", ("45 deg/s (slow)", 45.0), ("90 deg/s", 90.0), ("120 deg/s (fast)", 120.0)) { Active = () => Equals(Val<int>(turn), 2) };
        options.Rows.AddRange(new object[] { "DISPLAY", res, aa, sharp, hud, mirror, hint, crosshair, "TURNING", turn, snap, smooth, "CUTSCENES", recenter, cutscenes });
        options.Selected = 1;
        options.SelectionChanged += () => pages[1].Invalidate(new Rectangle(0, options.Bottom, pages[1].Width, pages[1].Height - options.Bottom));
        save.Click += (_, _) => SaveSettings(); reload.Click += (_, _) => LoadSettings();
        try
        {
            using var fs = typeof(Features).Assembly.GetManifestResourceStream("features.json")!;
            features = Features.Load(fs);
        }
        catch (Exception ex) { featuresError = "Features list unavailable: " + ex.Message; }
        foreach (var f in features)
        {
            var row = new ArtCheck(f.Label, f.Help); var feat = f;
            row.Toggled += (_, _) => { if (!loadingFeatures) SetFeature(feat, row); };
            featList.Add(row);
        }
        featList.Visible = false; restore.Visible = false; featToggle.Enabled = featuresError == null;
        featToggle.Click += (_, _) => ShowFeatures(!showFeatures);
        restore.Click += (_, _) => RestoreFeatureDefaults();
        dlssButton.Click += (_, _) => PickDlssProvider();
        pages[1].Controls.AddRange(new Control[] { options, featList, save, reload, restore, featToggle, dlssButton });
        pages[1].Painter = PaintSettings;

        // ---- Tools page
        ArtButton Cmd(string label, string text) { var b = new ArtButton(label, Look.Secondary); b.Click += (_, _) => SendCommand(text); return b; }
        cmdButtons = new[] { Cmd("Unlock (runes, coins, items)", "unlock"), Cmd("God mode on", "cvar ark_god_player 1"), Cmd("God mode off", "cvar ark_god_player 0"),
            Cmd("UI dump on", "uidump on"), Cmd("UI dump off", "uidump off") };
        custom.BorderStyle = BorderStyle.None; custom.BackColor = Ui.PaneFill; custom.ForeColor = Ui.Text; custom.PlaceholderText = "cvar <name> <int>";
        send.Click += (_, _) => { if (custom.Text.Trim() != "") SendCommand(custom.Text.Trim()); };
        result.Click += (_, _) => ShowResult();
        selftest.Click += async (_, _) => await SelfTest(selftest);
        var openArt = new ArtButton("Open artifacts", Look.Secondary); openArt.Click += (_, _) => Open(env.Art);
        var openRuns = new ArtButton("Open runs", Look.Secondary); openRuns.Click += (_, _) => Open(Directory.Exists(env.P("artifacts", "runs")) ? env.P("artifacts", "runs") : env.Art);
        var openRoot = new ArtButton("Open mod folder", Look.Secondary); openRoot.Click += (_, _) => Open(env.Root);
        // The self-test is a repo Python tool: developer mode only.
        uninstallProxy.Click += (_, _) => UninstallProxy();
        diagButtons = env.Standalone ? new[] { uninstallProxy, openArt, openRuns, openRoot } : new[] { uninstallProxy, selftest, openArt, openRuns, openRoot };
        StyleText(toolOut);
        pages[2].Controls.AddRange(cmdButtons); pages[2].Controls.AddRange(new Control[] { custom, send, result }); pages[2].Controls.AddRange(diagButtons); pages[2].Controls.Add(toolOut);
        pages[2].Painter = PaintTools;

        poll.Tick += (_, _) => Tick(); poll.Start();
        Shown += async (_, _) =>
        {
            ShowPage(page);
            if (env.Standalone)
                try { env.Pinned = await Task.Run(() => ModHome.Install(env.Root)); }
                catch (Exception ex) { env.InstallError = ex.Message; Append(log, "Mod install failed: " + ex.Message, Ui.Bad); }
            else if (Environment.GetCommandLineArgs().Contains("--seed-features"))  // developer mode only on request: repo knob files are hand-kept
                try
                {
                    using var fs = typeof(Features).Assembly.GetManifestResourceStream("features.json")!;
                    Append(log, $"Features: {Features.Apply(env.P("build"), Features.Load(fs))} seeded", Ui.Text);
                }
                catch (Exception ex) { Append(log, "Features not applied: " + ex.Message, Ui.Bad); }
            artState = Ui.Art.Present || noArt ? null : Env.FindGameDir() == null ? "needs Dishonored 2 installed" : "not imported yet";
            await RefreshChecks(); LoadSettings();
            if (shotFile == null && !exitAfterShot) _ = AutoFindDlss();  // review runs must not write a knob into a real mod home
            // First start (or art missing): import it from the user's own install; the calibration poses follow, and are
            // rendered alone when only they are missing. Review screenshots wait for it.
            var import = artState == "not imported yet" ? ImportArt(false)
                : Ui.Art.Present && Env.FindGameDir() != null && PoseRender.Missing(env.P("artifacts", "game-art")) ? ImportArt(false, posesOnly: true)
                : Task.CompletedTask;
            if (shotFile == null) return;
            await import;
            await Task.Delay(600);
            try { SaveShot(shotFile); } catch (Exception ex) { Console.Error.WriteLine("Screenshot failed: " + ex.Message); }
            if (exitAfterShot) Close();
        };
        FormClosing += (_, ev) =>
        {
            if (launching && MessageBox.Show("VR is running from this launcher. Closing it leaves VR running (reopen the launcher to stop it), " +
                "but this session's launch record and run archive will not be written.\nClose anyway?", Text, MessageBoxButtons.YesNo) != DialogResult.Yes) ev.Cancel = true;
        };
        FormClosed += (_, _) => { dlssCts?.Cancel(); poll.Dispose(); Ui.Art.Dispose(); Ui.Backdrop?.Dispose(); Ui.Backdrop = null; };
    }

    // ---------- window, DPI, art
    int S(float v) => Ui.S(this, v);
    protected override bool ShowWithoutActivation => shotFile != null;  // review screenshots never take focus

    [DllImport("dwmapi.dll")] static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
    [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)] static extern int SetWindowTheme(IntPtr hwnd, string app, string? idList);
    [DllImport("user32.dll")] static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        int dark = 1, caption = Ui.Ink.B << 16 | Ui.Ink.G << 8 | Ui.Ink.R;
        DwmSetWindowAttribute(Handle, 20, ref dark, 4);     // DWMWA_USE_IMMERSIVE_DARK_MODE
        DwmSetWindowAttribute(Handle, 35, ref caption, 4);  // DWMWA_CAPTION_COLOR (Windows 11; ignored elsewhere)
    }

    protected override void OnLoad(EventArgs e)
    {
        base.OnLoad(e);
        var wa = Screen.FromPoint(Cursor.Position).WorkingArea;
        MinimumSize = SizeFromClientSize(new Size(Math.Min(S(980), wa.Width - S(40)), Math.Min(S(600), wa.Height - S(60))));
        var want = SizeFromClientSize(new Size(S(1280), S(760)));
        Size = new Size(Math.Min(want.Width, wa.Width), Math.Min(want.Height, wa.Height));
        Location = new Point(wa.X + (wa.Width - Width) / 2, wa.Y + (wa.Height - Height) / 2);
        DoLayout();
    }
    protected override void OnResize(EventArgs e) { base.OnResize(e); DoLayout(); }
    protected override void OnDpiChanged(DpiChangedEventArgs e) { base.OnDpiChanged(e); DoLayout(); }

    void LoadArt()
    {
        var old = Ui.Art;
        Ui.Art = noArt ? new GameArt() : new GameArt(env.P("artifacts", "game-art"));
        old.Dispose();
        importArt.Visible = !Ui.Art.Present;
    }

    /// <summary>Imports the game's UI art from the user's install into artifacts\game-art (ArtImport, the port of tools\extract_game_art.py),
    /// then renders the calibration avatar poses next to it (PoseRender, the port of tools\render_calibration_poses.py).</summary>
    async Task ImportArt(bool openLog = true, bool posesOnly = false)
    {
        if (importing) return;
        var game = Env.FindGameDir();
        if (game == null) { artState = "needs Dishonored 2 installed"; await RefreshChecks(); return; }
        importing = true; importArt.Enabled = false;
        if (openLog) { ShowPage(0); ShowLog(true); }
        var dir = env.P("artifacts", "game-art");
        void Progress(string what, int done, int total) => BeginInvoke(() => { artState = $"{what} {done} of {total}..."; checks = env.RunChecks(artState); pages[0].Invalidate(); Invalidate(); });
        bool artOk = posesOnly;
        if (!posesOnly)
            try
            {
                Append(log, "Importing game art from " + game, Ui.Teal);
                int n = await Task.Run(() => ArtImport.Run(game, dir, l => Append(log, l), (d, t) => Progress("importing", d, t)));
                Append(log, $"Art import finished: {n} of {ArtImport.Count} images.", n == ArtImport.Count ? Ui.Good : Ui.Gold);
                artState = null; artOk = true;
            }
            catch (Exception ex) { Append(log, "Art import failed: " + ex.Message, Ui.Bad); artState = "import failed (see Launch log)"; }
        if (artOk)
        {
            try
            {
                Append(log, "Rendering the calibration poses from " + game, Ui.Teal);
                await Task.Run(() => PoseRender.Run(game, dir, l => Append(log, l), (d, t) => Progress("rendering poses", d, t)));
            }
            catch (Exception ex) { Append(log, "Calibration poses failed: " + ex.Message + " (VR shows the painted portrait instead)", Ui.Gold); }
            artState = null;
        }
        importing = false; importArt.Enabled = true;
        LoadArt(); DoLayout();
        if (Ui.Art.Present) artState = null;
        await RefreshChecks();
    }

    void SaveShot(string file)
    {
        using var bmp = new Bitmap(ClientSize.Width, ClientSize.Height, PixelFormat.Format32bppRgb);
        using (var g = Graphics.FromImage(bmp))
        {
            var hdc = g.GetHdc();
            try { PrintWindow(Handle, hdc, 3); }  // PW_CLIENTONLY | PW_RENDERFULLCONTENT, as tools/session_recorder.py grab()
            finally { g.ReleaseHdc(hdc); }
        }
        var path = Path.GetFullPath(file);
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        bmp.Save(path, ImageFormat.Png);
    }

    // ---------- layout and backdrop (logical 96-dpi units through S())
    Rectangle LogoRect()
    {
        int w = Math.Min(S(400), ClientSize.Width * 34 / 100);
        return new Rectangle(S(40), S(30), w, w * 240 / 1472);
    }

    Rectangle PageRect(int p)
    {
        float wl = ClientSize.Width * 96f / DeviceDpi;
        float cw = p switch { 0 => Math.Clamp(wl * .40f, 470, 520), 1 => Math.Clamp(wl - 300 - 340, 520, 590), _ => Math.Clamp(wl - 300 - 360, 520, 640) };
        return new Rectangle(S(300), S(176), S(cw), ClientSize.Height - S(176) - S(26));
    }

    void DoLayout()
    {
        if (!IsHandleCreated || ClientSize.Width < 50 || ClientSize.Height < 50) return;
        SuspendLayout();
        for (int i = 0; i < 3; i++) { nav[i].Bounds = new Rectangle(S(40), S(176) + i * S(50), S(224), S(46)); nav[i].TabIndex = i; }
        importArt.Bounds = new Rectangle(S(44), ClientSize.Height - S(120), Math.Max(S(200), importArt.Natural), S(38)); importArt.TabIndex = 6;
        for (int i = 0; i < 3; i++) { pages[i].Bounds = PageRect(i); pages[i].TabIndex = 3 + i; }
        var mono = Ui.Font(this, 9.5f, FontStyle.Regular, Ui.Mono);
        foreach (var t in new[] { log, toolOut }) if (!ReferenceEquals(t.Font, mono)) t.Font = mono;
        var sans = Ui.Font(this, 10.5f, FontStyle.Regular, Ui.Sans);
        if (!ReferenceEquals(custom.Font, sans)) custom.Font = sans;
        LayoutPlay(); LayoutSettings(); LayoutTools();
        ResumeLayout(false);
        RebuildBackdrop();
        Invalidate(true);
    }

    /// <summary>Places buttons left to right at their natural width, wrapping inside the page. Returns the y below the last row.</summary>
    int Flow(int x, int y, int width, int h, params ArtButton[] bs)
    {
        int cx = x;
        foreach (var b in bs)
        {
            int w = b.Natural;
            if (cx > x && cx + w > x + width) { cx = x; y += h + S(8); }
            b.Bounds = new Rectangle(cx, y, w, h); cx += w + S(10);
        }
        return y + h;
    }

    void LayoutPlay()
    {
        var p = pages[0]; int w = p.Width;
        launch.Bounds = new Rectangle(-S(6), S(92), Math.Max(S(212), launch.Natural), S(58));
        Flow(launch.Right + S(8), S(101), w - launch.Right - S(8), S(40), stop, recheck);
        Flow(0, S(176), w, S(30), tabSetup, tabLog);
        int tx = tabLog.Right + S(12);
        p.Sections.Clear(); p.Sections.Add(("", new Rectangle(tx, S(176), w - tx, S(30))));
        int top = S(216);
        p.Panes.Clear();
        if (showLog) p.Panes.Add(new Rectangle(0, top, w, p.Height - top));
        log.Bounds = new Rectangle(S(10), top + S(8), w - S(20), p.Height - top - S(16));
    }

    void LayoutSettings()
    {
        var p = pages[1]; int w = p.Width, plateH = S(84), btnH = S(54), gap = S(12);
        if (showFeatures)
        {
            featList.Bounds = new Rectangle(0, 0, w, Math.Max(S(120), p.Height - btnH - gap * 2));
            int fy = featList.Bottom + gap + (btnH - S(40)) / 2;
            restore.Bounds = new Rectangle(0, fy, Math.Max(S(180), restore.Natural), S(40));
            featToggle.Bounds = new Rectangle(restore.Right + S(10), fy, Math.Max(S(150), featToggle.Natural), S(40));
            settingsLast = featToggle; featList.Relayout();
            return;
        }
        options.Bounds = new Rectangle(0, 0, w, Math.Max(S(200), Math.Min(options.IdealHeight, p.Height - plateH - btnH - gap * 2)));
        int y = options.Bottom + gap + plateH + gap;
        save.Bounds = new Rectangle(-S(6), y, Math.Max(S(150), save.Natural), btnH);
        reload.Bounds = new Rectangle(save.Right + S(6), y + (btnH - S(40)) / 2, Math.Max(S(120), reload.Natural), S(40));
        featToggle.Bounds = new Rectangle(reload.Right + S(10), reload.Top, Math.Max(S(150), featToggle.Natural), S(40));
        dlssButton.Bounds = new Rectangle(featToggle.Right + S(10), reload.Top, Math.Max(S(150), Math.Min(dlssButton.Natural, w - featToggle.Right - S(10))), S(40));
        settingsLast = dlssButton;
    }

    /// <summary>The DLAA/DLSS Quality rows exist only while an NVIDIA driver and the user's nvngx_dlss.dll are present; the option keeps the user's value.</summary>
    void RefreshDlssChoices()
    {
        var current = aa.Items.Count > 0 ? aa.Items[aa.Index].Val : 2;
        var offered = DlssProvider.Available(env.Root);
        aa.Items.Clear();
        foreach (var (t, v) in AaBase) aa.Items.Add(new Item(t, v));
        if (offered) foreach (var (t, v) in AaDlss) aa.Items.Add(new Item(t, v));
        aa.Help = offered ? AaHelp + " " + DlssProvider.Attribution : AaHelp; // the attribution the DLSS licence asks for, where the modes are offered
        Select(aa, current, $"Mode {current}");
        options.Invalidate();
    }

    /// <summary>At start, only when no nvngx_dlss.dll resolves and an NVIDIA driver is present: look through the user's own Steam games (off the UI thread, 5 s budget) and record the best one. Manual picking still wins.</summary>
    CancellationTokenSource? dlssCts;
    async Task AutoFindDlss()
    {
        try
        {
            if (DlssProvider.Resolve(env.Root) != null || !DlssProvider.NvidiaDriverPresent()) return;
            var cts = dlssCts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
            var found = await Task.Run(() => DlssFinder.FindAndConfigure(env.Root, DlssFinder.SteamRoot(), cts.Token));
            if (found == null || IsDisposed || Disposing || !IsHandleCreated) return;
            RefreshDlssChoices();
            SetSettingsMsg($"DLSS found in {found.Source["auto: ".Length..]}: {found.Path}. {DlssProvider.Attribution} Applies the next time VR starts.", Ui.Good);
        }
        catch (Exception) { }  // best effort: manual picking stays available
    }

    void PickDlssProvider()
    {
        try
        {
            using var dialog = new OpenFileDialog { Title = DlssProvider.Prompt, Filter = DlssProvider.DllName + "|" + DlssProvider.DllName, FileName = DlssProvider.DllName, CheckFileExists = true };
            if (dialog.ShowDialog(this) != DialogResult.OK) return;
            var found = DlssProvider.Configure(env.Root, dialog.FileName);
            RefreshDlssChoices();
            SetSettingsMsg($"DLSS provider recorded: {found.Path}. {DlssProvider.Attribution} Applies the next time VR starts.", Ui.Good);
        }
        catch (Exception ex) { SetSettingsMsg("DLSS provider not changed: " + ex.Message, Ui.Bad); }
    }

    void LayoutTools()
    {
        var p = pages[2]; int w = p.Width, y = 0;
        p.Sections.Clear(); p.Panes.Clear();
        p.Sections.Add(("TEST COMMANDS", new Rectangle(0, y, w, S(28)))); y += S(56);
        y = Flow(0, y, w, S(38), cmdButtons) + S(10);
        var box = new Rectangle(0, y, Math.Min(S(260), w / 2), S(38));
        p.Panes.Add(box);
        custom.Bounds = new Rectangle(box.X + S(10), box.Y + (box.Height - custom.PreferredHeight) / 2, box.Width - S(20), custom.PreferredHeight);
        y = Flow(box.Right + S(10), y, w - box.Right - S(10), S(38), send, result) + S(18);
        p.Sections.Add(("DIAGNOSTICS", new Rectangle(0, y, w, S(28)))); y += S(36);
        y = Flow(0, y, w, S(38), diagButtons) + S(18);
        p.Sections.Add(("OUTPUT", new Rectangle(0, y, w, S(28)))); y += S(36);
        var pane = new Rectangle(0, y, w, Math.Max(S(60), p.Height - y));
        p.Panes.Add(pane);
        toolOut.Bounds = Rectangle.Inflate(pane, -S(10), -S(8));
    }

    /// <summary>Renders the static window background for the current page: hero art, ink veil, paper grain, vignette, logo.</summary>
    void RebuildBackdrop()
    {
        int W = ClientSize.Width, H = ClientSize.Height;
        var bmp = new Bitmap(W, H, PixelFormat.Format32bppPArgb);
        using (var g = Graphics.FromImage(bmp))
        {
            Ui.Quality(g); g.Clear(Ui.Ink);
            var art = Ui.Art; float k = DeviceDpi / 96f; int cr = pages[page].Right;
            var hero = art[page switch { 0 => "key_art", 1 => "portrait_emily", _ => "portrait_corvo" }];
            if (hero != null)
            {
                if (page == 0)
                {   // split mask/face key art: keep the whole head right of the content, clipped to the window
                    float s = H * 1.08f / hero.Height, w = hero.Width * s, half = 250 * s;
                    float fx = Math.Min(Math.Max(W * .74f, cr + half + 20 * k), W - half - 10 * k);
                    Ui.Image(g, hero, new RectangleF(fx - w / 2, (H - hero.Height * s) * .35f, w, hero.Height * s));
                }
                else
                {   // painted pause-menu portrait, anchored bottom right as in the game's pause menu
                    float h = H * .92f, w = h * hero.Width / hero.Height;
                    Ui.Image(g, hero, new RectangleF(Math.Max(W - w * .88f, cr - w * .03f), H - h, w, h), .92f);
                }
            }
            else
            {
                using var glow = new LinearGradientBrush(new Rectangle(0, 0, W, H), Ui.Ink, Color.FromArgb(28, 23, 28), 20f);
                g.FillRectangle(glow, 0, 0, W, H);
            }
            // ink veil: solid behind the menu, fading into the art
            float x0 = cr - 50 * k, x1 = cr + 190 * k;
            using (var solid = new SolidBrush(Ui.Ink)) g.FillRectangle(solid, 0, 0, x0, H);
            using (var veil = new LinearGradientBrush(new PointF(x0 - 1, 0), new PointF(x1, 0), Ui.Ink, Color.FromArgb(0, Ui.Ink)))
            {
                veil.Blend = new Blend { Factors = new[] { 0f, .45f, .82f, 1f }, Positions = new[] { 0f, .3f, .65f, 1f } };
                g.FillRectangle(veil, x0 - 1, 0, x1 - x0 + 1, H);
            }
            using (var low = new LinearGradientBrush(new PointF(0, H - 170 * k), new PointF(0, H + 1), Color.FromArgb(0, Ui.Ink), Color.FromArgb(215, Ui.Ink)))
                g.FillRectangle(low, 0, H - 170 * k, W, 170 * k + 1);
            if (art["paper_texture"] is Bitmap paper)
            {
                using var ia = Ui.Attr(.6f, Color.FromArgb(84, 74, 68));  // neutral warm grain
                using var tb = new TextureBrush(paper, new Rectangle(0, 0, paper.Width, paper.Height), ia);
                tb.ScaleTransform(k, k);
                g.FillRectangle(tb, 0, 0, W, H);
            }
            Ui.Image(g, art["menu_vignette"], new RectangleF(0, 0, W, H));
            Ui.Image(g, art["logo"], LogoRect());
        }
        var old = Ui.Backdrop; Ui.Backdrop = bmp; Ui.BackdropOwner = this; old?.Dispose();
    }

    protected override void OnPaintBackground(PaintEventArgs e) => Ui.PaintBackdrop(this, e.Graphics);
    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics; var logo = LogoRect(); float k = DeviceDpi / 96f;
        if (Ui.Art["logo"] == null) Ui.Spaced(g, "DISHONORED 2", Ui.Font(this, 25f), Ui.Text, logo.X, logo.Y + S(4), 5 * k);
        Ui.Spaced(g, "VIRTUAL  REALITY", Ui.Font(this, 9.5f), Ui.Soft, logo.X + S(8), logo.Bottom + S(10), 5.5f * k);
        var small = Ui.Font(this, 8.5f, FontStyle.Regular, Ui.Sans);
        const TextFormatFlags wrap = TextFormatFlags.WordBreak | TextFormatFlags.NoPadding;
        TextRenderer.DrawText(g, "Never sends global input.\nThe game keeps focus.", small, new Rectangle(S(46), ClientSize.Height - S(58), S(230), S(40)), Ui.Faint, wrap);
        if (importArt.Visible)
            TextRenderer.DrawText(g, importing ? "Importing art from your Dishonored 2 install: " + artState : "Import art from your Dishonored 2 install for the full look.", small, new Rectangle(S(46), importArt.Top - S(40), S(220), S(36)), Ui.Dim, wrap);
    }

    // ---------- helpers
    static void StyleText(TextBoxBase r)
    {
        r.BackColor = Ui.PaneFill; r.ForeColor = Ui.Text; r.BorderStyle = BorderStyle.None; r.ReadOnly = true; r.HideSelection = false;
        if (r is RichTextBox rt) rt.DetectUrls = false;
        r.HandleCreated += (_, _) => SetWindowTheme(r.Handle, "DarkMode_Explorer", null);  // dark scrollbars
    }

    void ShowPage(int i)
    {
        page = i;
        for (int k = 0; k < 3; k++) { pages[k].Visible = k == i; nav[k].Selected = k == i; nav[k].Invalidate(); }
        RebuildBackdrop(); Invalidate(true);
        if (i == 1) LoadSettings();
    }

    void ShowLog(bool on)
    {
        showLog = on; tabSetup.Selected = !on; tabLog.Selected = on; log.Visible = on;
        LayoutPlay(); pages[0].Invalidate(true);
    }

    void Append(RichTextBox box, string line, Color? c = null)
    {
        if (InvokeRequired) { BeginInvoke(() => Append(box, line, c)); return; }
        var col = c ?? (line.StartsWith("PASS") ? Ui.Good : line.StartsWith("FAIL") || line.Contains("Error", StringComparison.OrdinalIgnoreCase) || line.Contains("throw") ? Ui.Bad : Ui.Text);
        box.SelectionStart = box.TextLength; box.SelectionColor = col; box.AppendText(line + "\n"); box.ScrollToCaret();
    }

    void SetStatus(string s, Color c, string detail = "")
    {
        if (s == statusText && c == statusColor && detail == statusDetail) return;
        statusText = s; statusColor = c; statusDetail = detail;
        pages[0].Invalidate(new Rectangle(0, 0, pages[0].Width, S(92)));
    }

    void PaintPlay(Graphics g)
    {
        var p = pages[0];
        const TextFormatFlags np = TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix;
        TextRenderer.DrawText(g, statusText, Ui.Font(p, 25f), new Point(0, -S(2)), statusColor, np);
        TextRenderer.DrawText(g, statusDetail, Ui.Font(p, 10f, FontStyle.Regular, Ui.Sans), new Rectangle(S(1), S(44), p.Width - S(4), S(42)), Ui.Dim, np | TextFormatFlags.WordBreak);
        if (showLog) return;
        var name = Ui.Font(p, 12.5f); var det = Ui.Font(p, 9.5f, FontStyle.Regular, Ui.Sans); var fix = Ui.Font(p, 9f, FontStyle.Regular, Ui.Sans);
        int y = S(230), col = S(170), dw = p.Width - col;
        if (checks.Count == 0) { TextRenderer.DrawText(g, "Checking your setup...", det, new Point(S(26), y), Ui.Dim, np); return; }
        foreach (var c in checks)
        {
            bool usable = c.Fix.StartsWith("Usable");
            var mark = c.Ok ? (usable ? Ui.Gold : Ui.Good) : Ui.Bad;
            Ui.Diamond(g, S(8), y + name.Height / 2f, S(5.5f), mark, c.Ok);
            TextRenderer.DrawText(g, c.Name, name, new Rectangle(S(26), y, col - S(30), name.Height), Ui.Text, np | TextFormatFlags.EndEllipsis);
            TextRenderer.DrawText(g, Ui.Fit(c.Detail, det, dw), det, new Point(col, y + (name.Height - det.Height) / 2 + S(1)), c.Ok ? Ui.Soft : Ui.Bad, np);
            y += name.Height + S(6);
            if (!c.Ok || usable)
            {
                var r = new Rectangle(col, y - S(2), dw, 0);
                int h = TextRenderer.MeasureText(g, "Fix: " + c.Fix, fix, new Size(dw, 0), np | TextFormatFlags.WordBreak).Height;
                TextRenderer.DrawText(g, "Fix: " + c.Fix, fix, r with { Height = h }, Ui.Mix(mark, Ui.Dim, .45f), np | TextFormatFlags.WordBreak);
                y += h + S(4);
            }
            y += S(10);
        }
    }

    // ---------- checks
    async Task RefreshChecks()
    {
        candidate = env.LatestCandidate(out _);
        checks = await Task.Run(() => env.RunChecks(artState));
        ready = env.Ready(candidate);
        pages[0].Invalidate();
        Tick();
    }

    bool Ready => ready;

    // ---------- launch / stop (Probe: the C# port of run_demo.ps1 -Launch -SkipPreflightWait, inject_probe.py and game_keys.py)
    async Task LaunchVr()
    {
        if (launching || checking) return;
        checking = true; launch.Enabled = false; // a second click during the re-check must not start a second launch
        try { await RefreshChecks(); } // never trust a cached ready: the game may have been updated while the launcher sat open
        finally { checking = false; }
        if (!ready) { launch.Enabled = Ready; SetStatus("Cannot launch", Ui.Bad, "Fix the red items under Setup checks first."); return; }
        launching = true; stopping = false; launch.Enabled = false; stop.Enabled = true;
        log.Clear(); ShowLog(true);
        Append(log, $"Launching {candidate!.Dll} from {env.Root}", Ui.Teal);
        try
        {
            if (await Probe.Launch(candidate, env.Root, l => Append(log, l), () => env.LiveStatus().gameplay))
                Append(log, "VR session ended and the game was restored.", Ui.Good);
            else Append(log, "Launch done; the in-game proxy runs VR. Stop VR ends it.", Ui.Good);
        }
        catch (Exception ex) { Append(log, "Launch failed: " + ex.Message, Ui.Bad); }
        launching = false; launch.Enabled = true; Tick();
    }

    /// <summary>Removes our dinput8 proxy and d2vr-linux.ini from the game folder (a foreign dinput8.dll is left alone).</summary>
    void UninstallProxy()
    {
        var gameDir = Env.FindGameDir();
        if (gameDir == null) { Append(toolOut, "Dishonored 2 folder not found.", Ui.Bad); return; }
        try
        {
            var removed = ProxyInstall.Uninstall(gameDir);
            Append(toolOut, removed.Count == 0 ? "No d2vr proxy in the game folder." : "Removed from the game folder: " + string.Join(", ", removed), Ui.Good);
        }
        catch (Exception ex) { Append(toolOut, "Uninstall failed (close the game first): " + ex.Message, Ui.Bad); }
    }

    async Task StopVr()
    {
        var g = Env.Game();
        if (g == null) { Append(log, "Nothing to stop (game not running).", Ui.Dim); return; }
        if (ProxyInstall.StopViaProxy(Probe.ProxyLoadedIn(g), Probe.SessionPath))
        {
            stopping = true; stop.Enabled = false; ShowLog(true);
            Append(log, $"Stopping VR through the proxy in Dishonored2 (pid {g.Id})", Ui.Teal);
            try { Probe.RequestProxyStop(env.Root); Append(log, "Stop requested. If VR has not started yet the request is held until it has, then stops it at once; otherwise the mod is restoring the game.", Ui.Good); }
            catch (Exception ex) { Append(log, "Stop failed: " + ex.Message, Ui.Bad); }
            stopping = false; return;
        }
        var c = candidate ?? env.LatestCandidate(out _);
        if (c == null) { Append(log, "Nothing to stop (no mod candidate).", Ui.Dim); return; }
        stopping = true; stop.Enabled = false; ShowLog(true);
        Append(log, $"Stopping {c.Dll} in Dishonored2 (pid {g.Id})", Ui.Teal);
        try { await Task.Run(() => Probe.Stop(env.Root, c.Dll, g.Id, l => Append(log, l))); Append(log, "Stop accepted; the mod is restoring the game.", Ui.Good); }
        catch (Exception ex) { Append(log, "Stop failed: " + ex.Message, Ui.Bad); }
        stopping = false;
    }

    void Tick()
    {
        var g = Env.Game();
        var (fresh, gp, logic) = env.LiveStatus();
        launch.Enabled = !launching && !checking && Ready;
        stop.Enabled = g != null && (fresh || launching) && !stopping;
        if (stopping) SetStatus("Stopping VR", Ui.Gold, "Waiting for the mod to restore the game.");
        else if (g != null && fresh && gp) SetStatus("VR running", Ui.Good, $"Gameplay active (logic mask {logic}). Put on your headset.");
        else if (g != null && fresh) SetStatus("Load your save", Ui.Teal, $"VR is attached; waiting for gameplay (logic mask {logic}). Load a save or continue from the menu.");
        else if (g != null && launching) SetStatus("Starting", Ui.Gold, "Game is up; waiting for the mod to attach (can take a minute).");
        else if (g != null) SetStatus("Load your save", Ui.Teal, "Game is running without VR. Press Launch VR to attach the mod.");
        else if (launching) SetStatus("Starting", Ui.Gold, "Starting Dishonored 2 through Steam. At the title screen the launcher attaches VR and continues your last save.");
        else SetStatus("Stopped", Ui.Dim, Ready ? "Ready. Press Launch VR." : "Fix the red items under Setup checks, then Re-check.");
    }

    // ---------- settings
    static void Select(Option o, object v, string custom)
    {
        int i = o.Items.FindIndex(it => Equals(it.Val, v) || (v is double a && it.Val is double b && Math.Abs(a - b) < 1e-9));
        if (i < 0) { o.Items.Add(new Item(custom, v)); i = o.Items.Count - 1; }
        o.Index = i;
    }
    static T Val<T>(Option o) => (T)o.Items[o.Index].Val;

    void PaintSettings(Graphics g)
    {
        var p = pages[1]; var o = showFeatures ? null : options.Current;
        if (showFeatures) { PaintSettingsMsg(g); return; }
        var plate = new Rectangle(0, options.Bottom + S(12), p.Width, S(84));
        Ui.Image(g, Ui.Art["menu_background"], Rectangle.Inflate(plate, S(30), S(26)), .95f);
        Ui.Divider(g, new Rectangle(0, plate.Y, plate.Width, S(14)), .55f);
        const TextFormatFlags np = TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix;
        if (o != null)
        {
            Ui.Spaced(g, o.Name.ToUpperInvariant(), Ui.Font(p, 9.5f, FontStyle.Bold), Ui.Gold, S(28), plate.Y + S(22), 2.4f * DeviceDpi / 96f);
            TextRenderer.DrawText(g, o.Help, Ui.Font(p, 10.5f, FontStyle.Regular, Ui.Sans), new Rectangle(S(28), plate.Y + S(44), plate.Width - S(40), plate.Height - S(44)), Ui.Soft, np | TextFormatFlags.WordBreak | TextFormatFlags.EndEllipsis);
        }
        PaintSettingsMsg(g);
    }

    void PaintSettingsMsg(Graphics g)
    {
        var p = pages[1]; int x = settingsLast.Right + S(18);
        int top = showFeatures ? featList.Bottom + S(12) : save.Top, h = showFeatures ? S(54) : save.Height;
        var m = new Rectangle(x, top, Math.Max(S(60), p.Width - x), h);
        TextRenderer.DrawText(g, settingsMsg, Ui.Font(p, 9.5f, FontStyle.Regular, Ui.Sans), m, settingsMsgColor,
            TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix | TextFormatFlags.WordBreak | TextFormatFlags.VerticalCenter);
    }

    /// <summary>Switches the Settings page between the option list and the Features list.</summary>
    void ShowFeatures(bool on)
    {
        showFeatures = on;
        options.Visible = !on; save.Visible = !on; reload.Visible = !on; featList.Visible = on; restore.Visible = on; dlssButton.Visible = !on;
        featToggle.Text = on ? "Back to options" : "Features";
        if (on) LoadFeatures(); else SetSettingsMsg("", Ui.Dim);
        LayoutSettings(); pages[1].Invalidate(true);
        if (on && featList.Rows.Count > 0) featList.Rows[0].Focus(); else if (!on) options.Focus();
    }

    string FeatureBuild => env.P("build");

    /// <summary>Reads the knob files into the check rows (no writes).</summary>
    void LoadFeatures()
    {
        if (featuresError != null) { SetSettingsMsg(featuresError, Ui.Bad); return; }
        loadingFeatures = true;
        try
        {
            var st = Features.State(FeatureBuild, features);
            for (int i = 0; i < features.Count; i++) featList.Rows[i].SetSilently(st[features[i].Id].On);
            if (showFeatures) SetSettingsMsg("Each switch is saved at once. It applies the next time VR starts.", Ui.Dim);
        }
        catch (Exception ex) { SetSettingsMsg("Cannot read feature state: " + ex.Message, Ui.Bad); }
        finally { loadingFeatures = false; }
    }

    void SetFeature(Feature f, ArtCheck row)
    {
        try
        {
            Features.Set(FeatureBuild, f, row.Checked);
            SetSettingsMsg($"{f.Label}: {(row.Checked ? "on" : "off")}. Applies the next time VR starts.", Ui.Good);
        }
        catch (Exception ex)
        {
            LoadFeatures();  // the boxes always show what is really on disk
            SetSettingsMsg("Not changed: " + ex.Message, Ui.Bad);
        }
    }

    void RestoreFeatureDefaults()
    {
        try
        {
            Features.RestoreDefaults(FeatureBuild, features);
            LoadFeatures();
            SetSettingsMsg("Defaults restored. Your saved tuning files were kept. Applies the next time VR starts.", Ui.Good);
        }
        catch (Exception ex) { LoadFeatures(); SetSettingsMsg("Not restored: " + ex.Message, Ui.Bad); }
    }

    void SetSettingsMsg(string text, Color c) { settingsMsg = text; settingsMsgColor = c; pages[1].Invalidate(); }

    void ApplyToForm(Prefs d, Prefs? onlyIfSameAs)
    {
        // onlyIfSameAs != null: a form field the user edited (differs from that baseline) is kept.
        var b = onlyIfSameAs ?? d;
        bool Take<T>(T form, T baseline) => onlyIfSameAs == null || Equals(form, baseline);
        if (Take((int)res.Value, (int)Math.Clamp(Math.Round(b.Resolution * 100), 50, 200))) res.Value = (int)Math.Clamp(Math.Round(d.Resolution * 100), 50, 200);
        if (Take(Val<int>(aa), b.TemporalAa)) Select(aa, d.TemporalAa, $"Mode {d.TemporalAa}");
        if (Take(Val<int>(hud), b.Hud)) Select(hud, d.Hud, $"{d.Hud}%");
        if (Take(Val<int>(turn), b.Mode)) Select(turn, d.Mode, $"Mode {d.Mode}");
        if (Take(Val<double>(snap), b.SnapDeg)) Select(snap, d.SnapDeg, $"{d.SnapDeg} degrees");
        if (Take(Val<double>(smooth), b.SmoothDps)) Select(smooth, d.SmoothDps, $"{d.SmoothDps} deg/s");
        if (Take(Val<int>(mirror), b.Mirror)) Select(mirror, d.Mirror, $"Mode {d.Mirror}");
        if (Take(Val<double>(sharp), b.Sharpness)) Select(sharp, d.Sharpness, $"{d.Sharpness:0.##}");
        if (Take(Val<int>(hint), b.Hint != 0 ? 1 : 0)) Select(hint, d.Hint != 0 ? 1 : 0, "");
        if (Take(Val<int>(crosshair), b.Crosshair != 0 ? 1 : 0)) Select(crosshair, d.Crosshair != 0 ? 1 : 0, "");
        if (Take(Val<int>(recenter), b.ScriptedRecenter != 0 ? 1 : 0)) Select(recenter, d.ScriptedRecenter != 0 ? 1 : 0, "");
        if (Take(Val<string>(cutscenes), b.CutsceneMode)) Select(cutscenes, d.CutsceneMode, "");
        RefreshDlssChoices();
        options.Invalidate();
    }

    void LoadSettings()
    {
        try
        {
            var d = Prefs.Load(env.SettingsFile);
            prefs = d; aa.Touched = false;
            ApplyToForm(d, null);
            SetSettingsMsg((File.Exists(env.SettingsFile) ? "Loaded build\\vr-settings.cfg. The in-game VR menu wins: the launcher shows what the game saved. Changes apply the next time VR starts. " : "No saved file yet; showing defaults. ") + DlssProvider.Status(env.Root), Ui.Dim);
        }
        catch (Exception ex) { SetSettingsMsg(ex.Message, Ui.Bad); }
        LoadFeatures();
    }

    /// <summary>The game saves settings from its VR menu while the launcher is open: on focus, take those values for every field the user has not edited here.</summary>
    protected override void OnActivated(EventArgs e)
    {
        base.OnActivated(e);
        if (options == null || res == null || crosshair == null || recenter == null || cutscenes == null) return;
        try { var d = Prefs.Load(env.SettingsFile); ApplyToForm(d, prefs); prefs = d; }
        catch (Exception ex) { SetSettingsMsg(ex.Message, Ui.Bad); }
    }

    Prefs ReadForm()
    {
        var e = new Prefs { Measurements = prefs.Measurements, Aa = prefs.Aa, AaChosen = prefs.AaChosen };
        e.Resolution = res.Value / 100.0; e.TemporalAa = Val<int>(aa); e.AaTouched = aa.Touched; e.Hud = Val<int>(hud); e.Mode = Val<int>(turn);
        e.SnapDeg = Val<double>(snap); e.SmoothDps = Val<double>(smooth); e.Mirror = Val<int>(mirror); e.Sharpness = Val<double>(sharp);
        e.Hint = Val<int>(hint); e.Crosshair = Val<int>(crosshair);
        e.ScriptedRecenter = Val<int>(recenter); e.CutsceneMode = Val<string>(cutscenes);
        return e;
    }

    void SaveSettings()
    {
        try
        {
            var merged = Prefs.Merge(prefs, ReadForm(), Prefs.Load(env.SettingsFile));
            merged.Save(env.SettingsFile);
            prefs = merged; aa.Touched = false;
            ApplyToForm(merged, null);
            SetSettingsMsg("Saved. " + (Env.Game() != null ? "Restart VR (Stop, then Launch) to apply." : "Applies on next launch."), Ui.Good);
        }
        catch (Exception ex) { SetSettingsMsg("Not saved: " + ex.Message, Ui.Bad); }
    }

    // ---------- tools
    void PaintTools(Graphics g)
    {
        TextRenderer.DrawText(g, "The running mod polls artifacts\\vr-command.txt twice a second.", Ui.Font(pages[2], 9.5f, FontStyle.Regular, Ui.Sans),
            new Point(0, S(32)), Ui.Dim, TextFormatFlags.NoPadding | TextFormatFlags.NoPrefix);
    }

    void Open(string path) { Directory.CreateDirectory(path); Process.Start(new ProcessStartInfo("explorer.exe", $"\"{path}\"") { UseShellExecute = true }); }

    void SendCommand(string cmd)
    {
        try
        {
            var file = env.P("artifacts", "vr-command.txt");
            Directory.CreateDirectory(env.Art);
            File.WriteAllText(file, cmd + "\n");
            Append(toolOut, "Sent: " + cmd, Ui.Teal);
            Task.Delay(1500).ContinueWith(_ => BeginInvoke(ShowResult));
        }
        catch (Exception ex) { Append(toolOut, "Could not write command: " + ex.Message, Ui.Bad); }
    }

    void ShowResult()
    {
        var f = env.P("artifacts", "vr-command-result.txt");
        if (!File.Exists(f)) { Append(toolOut, "No vr-command-result.txt yet (VR must be running in gameplay).", Ui.Dim); return; }
        try
        {
            using var s = new FileStream(f, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete);
            var lines = new StreamReader(s).ReadToEnd().Split('\n', StringSplitOptions.RemoveEmptyEntries);
            Append(toolOut, "--- vr-command-result.txt (last lines) ---", Ui.Gold);
            foreach (var l in lines.TakeLast(8)) Append(toolOut, l.TrimEnd());
        }
        catch (Exception ex) { Append(toolOut, "Could not read result: " + ex.Message, Ui.Bad); }
    }

    async Task SelfTest(Button b)
    {
        var c = candidate ?? env.LatestCandidate(out _);
        if (c == null) { Append(toolOut, "Self-test needs a mod build (see Play page).", Ui.Bad); return; }
        if (Env.Game() == null) { Append(toolOut, "Start VR and load a save first; the self-test drives the running game.", Ui.Bad); return; }
        b.Enabled = false; toolOut.Clear();
        Append(toolOut, $"> python tools\\vr_selftest.py --dll {c.Dll} --scenario all", Ui.Teal);
        int pass = 0, fail = 0;
        try
        {
            var code = await Proc.Run("python", $"tools\\vr_selftest.py --dll {c.Dll} --scenario all", env.Root, l => { if (l.StartsWith("PASS")) pass++; else if (l.StartsWith("FAIL")) fail++; Append(toolOut, l); });
            Append(toolOut, $"Self-test finished: {pass} passed, {fail} failed (exit {code}).", fail == 0 && code == 0 ? Ui.Good : Ui.Bad);
        }
        catch (Exception ex) { Append(toolOut, "Self-test failed to run: " + ex.Message, Ui.Bad); }
        b.Enabled = true;
    }
}
