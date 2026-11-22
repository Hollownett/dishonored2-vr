namespace DishonoredVR;

/// <summary>Everything about one supported game that the launcher used to hard-code for Dishonored 2: Steam folder/exe/process/app id, the pinned
/// executable (SHA-256 and image size), the Saved Games config folder, the per-user mod home and where the embedded mod payload lives.
/// <see cref="D2"/> carries exactly the values the launcher always used; <see cref="Doto"/> is Dishonored: Death of the Outsider.</summary>
public sealed class GameProfile
{
    public required string Id { get; init; }              // --game <id>
    public required string DisplayName { get; init; }     // "Dishonored 2", "Death of the Outsider"
    public required string SteamFolder { get; init; }     // steamapps\common\<SteamFolder>
    public required string ExeName { get; init; }
    public required string ProcessName { get; init; }     // Process.GetProcessesByName
    public required int SteamAppId { get; init; }
    public required string ExeSha256 { get; init; }       // the one supported build
    public required uint ImageSize { get; init; }         // SizeOfImage of that exe
    public required string SavedGamesBase { get; init; }  // under %USERPROFILE%\Saved Games\
    /// <summary>The config file name, or null when it is looked up (the first *Config.cfg in <see cref="SavedGamesBase"/>).</summary>
    public string? ConfigFileName { get; init; }
    public required string ModHomeFolder { get; init; }   // under %LOCALAPPDATA%
    /// <summary>Resource prefix of the profile's own mod payload (candidate manifest, DLL); everything else is shared under "home/".</summary>
    public required string PayloadPrefix { get; init; }
    /// <summary>The 45 s inject_probe fallback (hard-coded D2 addresses) exists only for Dishonored 2; other games are proxy-only.</summary>
    public required bool InjectFallback { get; init; }
    public required string NotIncludedMessage { get; init; }
    /// <summary>The resource containers under base\ (each with a final &lt;name&gt;_patch.index): Dishonored 2 has game1..game4, Death of the Outsider game1..game3.</summary>
    public string[] ResourceContainers { get; init; } = { "game1", "game2", "game3", "game4" };

    public bool IsD2 => ReferenceEquals(this, D2);
    public string SteamUri => "steam://rungameid/" + SteamAppId;
    /// <summary>SteamVR's per-application section in steamvr.vrsettings.</summary>
    public string SteamVrSection => "steam.app." + SteamAppId;

    public string DefaultModHome => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), ModHomeFolder);
    public string SavedGamesDir => Path.Combine(Environment.ExpandEnvironmentVariables(@"%USERPROFILE%\Saved Games"), SavedGamesBase);

    /// <summary>The game's config file, or null when it does not exist (yet).</summary>
    public string? FindConfig()
    {
        if (ConfigFileName != null)
        {
            var f = Path.Combine(SavedGamesDir, ConfigFileName);
            return File.Exists(f) ? f : null;
        }
        return Directory.Exists(SavedGamesDir) ? Directory.EnumerateFiles(SavedGamesDir, "*Config.cfg").Order(StringComparer.OrdinalIgnoreCase).FirstOrDefault() : null;
    }

    public static readonly GameProfile D2 = new()
    {
        Id = "d2", DisplayName = "Dishonored 2", SteamFolder = "Dishonored2", ExeName = "Dishonored2.exe", ProcessName = "Dishonored2",
        SteamAppId = 403640, ExeSha256 = Probe.GameHash, ImageSize = 68988928,
        SavedGamesBase = @"Arkane Studios\Dishonored2\base", ConfigFileName = "dishonored2Config.cfg",
        ModHomeFolder = "DishonoredVR", PayloadPrefix = "home/", InjectFallback = true,
        NotIncludedMessage = "This launcher build carries no mod (no embedded candidate manifest).",
    };

    public static readonly GameProfile Doto = new()
    {
        Id = "doto", DisplayName = "Death of the Outsider", SteamFolder = "Dishonored_DeathOfTheOutsider", ExeName = "Dishonored_DO.exe", ProcessName = "Dishonored_DO",
        SteamAppId = 614570, ExeSha256 = "be73d979fdb5a6eacbcd385944c6060b97c997d06ab9d72fb39f275a866b955d", ImageSize = 0x431f000,
        SavedGamesBase = @"Arkane Studios\Dishonored_DO\base", ConfigFileName = null,
        ModHomeFolder = "DishonoredVR-DOTO", PayloadPrefix = "dotohome/", InjectFallback = false,
        NotIncludedMessage = "Death of the Outsider build not included in this launcher.",
        ResourceContainers = new[] { "game1", "game2", "game3" },
    };

    public static GameProfile? ById(string? id) => id?.ToLowerInvariant() switch { "d2" or "dishonored2" => D2, "doto" or "outsider" or "deathoftheoutsider" => Doto, _ => null };
}
