// Pure helpers and shared constants for the Linux/Proton dinput8 proxy. Header-only, no Windows API.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace d2vr_linux {

inline constexpr char kProxyMarker[] = "D2VR-LINUX-PROXY-1";
// Present in the binary once it honours the sim knobs below; `d2vr-linux sim` refuses an older installed proxy.
inline constexpr char kSimMarker[] = "D2VR-LINUX-SIM-1";
inline constexpr wchar_t kDefaultModHome[] = L"C:\\users\\steamuser\\AppData\\Local\\DishonoredVR";
inline constexpr wchar_t kIniName[] = L"d2vr-linux.ini";
inline constexpr wchar_t kStartTrigger[] = L"linux-start";
inline constexpr wchar_t kStopRequest[] = L"linux-stop";
inline constexpr wchar_t kSettleKnob[] = L"linux-settle-seconds";
inline constexpr wchar_t kLogName[] = L"linux-proxy.log";
// Developer knobs for simulated test runs (linux/d2vr-linux sim); never present in a normal headset session.
inline constexpr wchar_t kProbeOverride[] = L"linux-probe-override.cfg";
inline constexpr wchar_t kAutoContinueKnob[] = L"linux-autocontinue";
inline constexpr unsigned kDefaultAutoContinueSeconds = 10;
// 1|8|16|64|512|1024|2048|8192|16384|32768 (launcher/Probe.cs RequiredCapabilities)
inline constexpr unsigned long kRequiredCapabilities = 61017;
// Start gate (docs/VR-START-INVESTIGATION.md): VR starts this long after the game window has a client area. 20 s was clean on the
// sim rig, starts in the first ~12 s stalled at the menu. build\linux-settle-seconds = 45 restores the old window+45 s wait.
inline constexpr unsigned kDefaultSettleMs = 20000;
inline constexpr unsigned kLegacySettleSeconds = 45;
// A present that is never seen falls back to a timeout from the window (never idle forever).
inline constexpr unsigned kDefaultStartTimeoutMs = 60000;
inline constexpr wchar_t kStartModeKnob[] = L"linux-start-mode";           // "legacy" = old window+delay logic, no present gate
inline constexpr wchar_t kStartTimeoutKnob[] = L"linux-start-timeout-seconds";  // developer/test: timeout when no present is seen

inline bool is_space(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\v' || c == L'\f'; }

// First non-blank line of d2vr-linux.ini, with BOM, whitespace and trailing backslashes stripped.
inline std::wstring mod_home_from_ini(const std::wstring& text) {
    size_t pos = 0;
    if (!text.empty() && text[0] == 0xFEFF) pos = 1;
    while (pos <= text.size()) {
        size_t end = text.find(L'\n', pos);
        if (end == std::wstring::npos) end = text.size();
        size_t b = pos, e = end;
        while (b < e && is_space(text[b])) ++b;
        while (e > b && is_space(text[e - 1])) --e;
        while (e > b && text[e - 1] == L'\\') --e;
        if (e > b) return text.substr(b, e - b);
        if (end >= text.size()) break;
        pos = end + 1;
    }
    return kDefaultModHome;
}

// Probe.ProbeConfig (launcher/Probe.cs:122).
inline std::string probe_config(unsigned long pid) {
    return std::to_string(pid) + " 0 6 1 0 1 1 1 1 1 1 0 1 0 0 1 1 1.0 1 0 1 45 90 1 2 0 1 1 1 0 0 0\n";
}

// Developer override of probe_config: the text of build\linux-probe-override.cfg with every "{pid}" replaced by the game pid,
// whitespace-trimmed and newline-terminated. A blank file gives nullopt (the default config is used).
inline std::optional<std::string> probe_config_from_override(const std::string& text, unsigned long pid) {
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; };
    size_t b = 0, e = text.size();
    while (b < e && sp(text[b])) ++b;
    while (e > b && sp(text[e - 1])) --e;
    if (b == e) return std::nullopt;
    std::string out;
    const std::string token = "{pid}", number = std::to_string(pid);
    for (size_t i = b; i < e;) {
        if (text.compare(i, token.size(), token) == 0 && i + token.size() <= e) {
            out += number;
            i += token.size();
        } else {
            out += text[i++];
        }
    }
    return out + "\n";
}

// build\linux-autocontinue: seconds after StartProbe begins before Enter is posted to the title/menus (empty or invalid: 10).
inline unsigned autocontinue_seconds_from_knob(const std::string& text) {
    size_t b = 0, e = text.size();
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && sp(text[b])) ++b;
    while (e > b && sp(text[e - 1])) --e;
    if (b == e) return kDefaultAutoContinueSeconds;
    unsigned long v = 0;
    for (size_t i = b; i < e; ++i) {
        if (text[i] < '0' || text[i] > '9') return kDefaultAutoContinueSeconds;
        v = v * 10 + static_cast<unsigned long>(text[i] - '0');
        if (v > 600) v = 600;
    }
    return static_cast<unsigned>(v);
}

// artifacts\xr-live-status.json (compact JSON from the mod): does it say "gameplay":true (or a nonzero number)?
inline bool status_says_gameplay(const std::string& json) {
    const std::string key = "\"gameplay\"";
    size_t at = json.find(key);
    if (at == std::string::npos) return false;
    at += key.size();
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (at < json.size() && sp(json[at])) ++at;
    if (at >= json.size() || json[at] != ':') return false;
    ++at;
    while (at < json.size() && sp(json[at])) ++at;
    if (json.compare(at, 4, "true") == 0) return true;
    return at < json.size() && json[at] >= '1' && json[at] <= '9';
}

// Matches ^d2vr_render_probe_arm_v[0-9]+\.dll$ (case-sensitive).
inline bool is_mod_dll_name(const std::wstring& n) {
    static const std::wstring prefix = L"d2vr_render_probe_arm_v";
    static const std::wstring suffix = L".dll";
    if (n.size() <= prefix.size() + suffix.size()) return false;
    if (n.compare(0, prefix.size(), prefix) != 0) return false;
    if (n.compare(n.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    for (size_t i = prefix.size(); i < n.size() - suffix.size(); ++i)
        if (n[i] < L'0' || n[i] > L'9') return false;
    return true;
}

inline std::optional<std::wstring> select_mod_dll(const std::vector<std::wstring>& names, std::wstring& why) {
    std::vector<std::wstring> hits;
    for (const auto& n : names)
        if (is_mod_dll_name(n)) hits.push_back(n);
    if (hits.size() == 1) return hits[0];
    why = L"expected exactly one d2vr_render_probe_arm_v*.dll in build (found " + std::to_wstring(hits.size()) + L")";
    return std::nullopt;
}

inline bool capabilities_ok(unsigned long caps) { return (caps & kRequiredCapabilities) == kRequiredCapabilities; }

inline bool is_game_window_title(const std::wstring& title) { return title.find(L"Dishonored") != std::wstring::npos; }

inline std::string trim_ascii(const std::string& text) {
    size_t b = 0, e = text.size();
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && sp(text[b])) ++b;
    while (e > b && sp(text[e - 1])) --e;
    return text.substr(b, e - b);
}

// build\linux-settle-seconds: decimal seconds 0..3600 -> milliseconds; blank or anything else gives the default.
inline unsigned start_settle_ms_from_knob(const std::string& text) {
    const std::string t = trim_ascii(text);
    if (t.empty()) return kDefaultSettleMs;
    unsigned long v = 0;
    for (char c : t) {
        if (c < '0' || c > '9') return kDefaultSettleMs;
        v = v * 10 + static_cast<unsigned long>(c - '0');
        if (v > 3600) v = 3600;
    }
    return static_cast<unsigned>(v * 1000);
}

// The old behaviour exactly: an explicit 45 in the settle knob.
inline bool settle_knob_is_legacy(const std::string& text) {
    const std::string t = trim_ascii(text);
    return !t.empty() && start_settle_ms_from_knob(t) == kLegacySettleSeconds * 1000 && t.find_first_not_of("0123456789") == std::string::npos;
}

// build\linux-start-mode: exactly "legacy", case-insensitive, whitespace-trimmed.
inline bool start_mode_is_legacy(const std::string& text) {
    std::string t = trim_ascii(text);
    for (char& c : t) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return t == "legacy";
}

// Timeout when no present is seen: the settle itself if longer than the default, else the default (a test that sets a
// 600 s settle to keep the proxy idle is not cut short at 60 s). build\linux-start-timeout-seconds overrides (1..3600).
inline unsigned start_timeout_ms(const std::string& timeout_knob, unsigned settle_ms) {
    const std::string t = trim_ascii(timeout_knob);
    if (!t.empty() && t.find_first_not_of("0123456789") == std::string::npos && t.size() <= 4) {
        const unsigned long v = std::stoul(t);
        if (v >= 1 && v <= 3600) return static_cast<unsigned>(v * 1000);
    }
    return settle_ms > kDefaultStartTimeoutMs ? settle_ms : kDefaultStartTimeoutMs;
}

// Legacy wait in ms: the settle knob if it is plain digits (blank, garbage and oversized files give the old 45 s).
inline unsigned legacy_wait_ms(const std::string& settle_knob) {
    const std::string t = trim_ascii(settle_knob);
    if (t.empty() || t.find_first_not_of("0123456789") != std::string::npos) return kLegacySettleSeconds * 1000;
    return start_settle_ms_from_knob(t);
}

// Knob and status files larger than this are treated as absent (and logged once).
inline constexpr size_t kMaxSmallFileBytes = 64 * 1024;

// At most kMax allowed probe lines per session, then exactly one "suppressed" line (allow() false, first_suppressed() true once).
struct ProbeLineBudget {
    static constexpr unsigned kMax = 200;
    unsigned used = 0;
    bool allow() { return used < kMax ? (++used, true) : (++used, false); }
    bool first_suppressed() const { return used == kMax + 1; }
};

struct StartGateInput {
    bool window;
    bool present_seen;
    unsigned long long now_ms;
    unsigned long long window_at_ms;
    unsigned long long present_at_ms;
    unsigned settle_ms;
    unsigned timeout_ms;
    bool trigger;
};
enum class StartGateDecision { wait, start_present, start_timeout, start_trigger };

// Trigger wins; else present + settle; else the timeout from the window; else wait. Time going backwards waits.
inline StartGateDecision start_gate(const StartGateInput& in) {
    if (in.trigger) return StartGateDecision::start_trigger;
    if (!in.window || in.now_ms < in.window_at_ms) return StartGateDecision::wait;
    if (in.present_seen) {
        const unsigned long long base = in.present_at_ms > in.window_at_ms ? in.present_at_ms : in.window_at_ms;
        if (in.now_ms >= base + in.settle_ms) return StartGateDecision::start_present;
    }
    if (in.now_ms - in.window_at_ms >= in.timeout_ms) return StartGateDecision::start_timeout;
    return StartGateDecision::wait;
}

// Number after "key": in the status JSON (first occurrence), for the proxy's session probes.
inline std::optional<unsigned long long> json_number(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\":";
    size_t at = json.find(needle);
    if (at == std::string::npos) return std::nullopt;
    at += needle.size();
    if (at >= json.size() || json[at] < '0' || json[at] > '9') return std::nullopt;
    unsigned long long v = 0;
    for (; at < json.size() && json[at] >= '0' && json[at] <= '9'; ++at) v = v * 10 + static_cast<unsigned long long>(json[at] - '0');
    return v;
}

// GameKeys.PrepareConfig (launcher/Probe.cs:550-555): cvars missing from the config, ready to append. r_fullscreen: the game
// defaults to exclusive fullscreen, and headset-sized output needs a windowed swapchain (StartProbe retries forever otherwise).
inline std::string focus_cvars_to_append(const std::string& cfg) {
    struct Cvar { const char* name; const char* value; };
    static const Cvar cvars[] = {{"win_pauseOnFocusLoss", "\"0\""}, {"in_controlInactiveWindow", "\"1\""}, {"r_fullscreen", "\"0\""}};
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; };
    std::vector<std::string> firsts;
    size_t pos = 0;
    while (pos <= cfg.size()) {
        size_t end = cfg.find('\n', pos);
        if (end == std::string::npos) end = cfg.size();
        size_t b = pos;
        while (b < end && ws(cfg[b])) ++b;
        size_t e = b;
        while (e < end && !ws(cfg[e])) ++e;
        if (e > b) firsts.push_back(cfg.substr(b, e - b));
        pos = end + 1;
    }
    std::string add;
    for (const auto& c : cvars) {
        bool present = false;
        for (const auto& f : firsts)
            if (f == c.name) present = true;
        if (!present) add += std::string(c.name) + " " + c.value + "\r\n";
    }
    if (!add.empty() && !cfg.empty() && cfg.back() != '\n') add.insert(0, "\r\n");
    return add;
}

}  // namespace d2vr_linux
