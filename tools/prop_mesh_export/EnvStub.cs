namespace DishonoredVR; static class Env { public static string? FindGameDir() => null; }
static class ModHome { public static readonly string Dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "DishonoredVR"); }
