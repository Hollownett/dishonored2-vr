// Pure helpers and shared constants for the Linux/Proton dinput8 proxy. Header-only, no Windows API.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace d2vr_linux {

inline constexpr char kProxyMarker[] = "D2VR-LINUX-PROXY-1";
inline constexpr wchar_t kDefaultModHome[] = L"C:\\users\\steamuser\\AppData\\Local\\DishonoredVR";
inline constexpr wchar_t kIniName[] = L"d2vr-linux.ini";
inline constexpr wchar_t kStartTrigger[] = L"linux-start";
inline constexpr wchar_t kStopRequest[] = L"linux-stop";
inline constexpr wchar_t kSettleKnob[] = L"linux-settle-seconds";
inline constexpr wchar_t kLogName[] = L"linux-proxy.log";
// 1|8|16|64|512|1024|2048|8192|16384|32768 (launcher/Probe.cs RequiredCapabilities)
inline constexpr unsigned long kRequiredCapabilities = 61017;
inline constexpr unsigned kDefaultSettleSeconds = 45;

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

// Decimal integer clamped to 0..3600; anything else gives the default.
inline unsigned settle_seconds_from_knob(const std::string& text) {
    size_t b = 0, e = text.size();
    auto sp = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (b < e && sp(text[b])) ++b;
    while (e > b && sp(text[e - 1])) --e;
    if (b == e) return kDefaultSettleSeconds;
    unsigned long v = 0;
    for (size_t i = b; i < e; ++i) {
        if (text[i] < '0' || text[i] > '9') return kDefaultSettleSeconds;
        v = v * 10 + static_cast<unsigned long>(text[i] - '0');
        if (v > 3600) v = 3600;
    }
    return static_cast<unsigned>(v);
}

// GameKeys.PrepareConfig (launcher/Probe.cs:550-555): cvars missing from the config, ready to append.
inline std::string focus_cvars_to_append(const std::string& cfg) {
    struct Cvar { const char* name; const char* value; };
    static const Cvar cvars[] = {{"win_pauseOnFocusLoss", "\"0\""}, {"in_controlInactiveWindow", "\"1\""}};
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
