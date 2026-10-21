// Unit check for linux/proxy/proxy_core.hpp (runs under Wine).
#include "proxy_core.hpp"

#include <cstdio>

using namespace d2vr_linux;

static int g_failed = 0;

static void expect(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++g_failed;
    }
}

int main() {
    expect(probe_config(4242) == "4242 0 6 1 0 1 1 1 1 1 1 0 1 0 0 1 1 1.0 1 0 1 45 90 1 2 0 1 1 1 0 0 0\n", "cfg");
    expect(mod_home_from_ini(L"\xFEFF  Z:\\home\\u\\mod home\\\r\n") == L"Z:\\home\\u\\mod home", "ini bom/crlf/trailing slash");
    expect(mod_home_from_ini(L"\r\n \r\n") == kDefaultModHome, "ini empty -> default");
    std::wstring why;
    expect(*select_mod_dll({L"x.txt", L"d2vr_render_probe_arm_v562.dll"}, why) == L"d2vr_render_probe_arm_v562.dll", "one dll");
    expect(!select_mod_dll({L"d2vr_render_probe_arm_v1.dll", L"d2vr_render_probe_arm_v2.dll"}, why) &&
               why.find(L"found 2") != std::wstring::npos,
           "two dlls");
    expect(!select_mod_dll({L"d2vr_render_probe.dll", L"D2VR_RENDER_PROBE_ARM_V1.DLL"}, why), "regex exact");
    expect(capabilities_ok(524287) && !capabilities_ok(61017 & ~16u), "caps");
    expect(is_game_window_title(L"Dishonored 2") && !is_game_window_title(L"Steam"), "title");
    expect(start_settle_ms_from_knob("") == 20000 && start_settle_ms_from_knob("45") == 45000 && start_settle_ms_from_knob("0") == 0 &&
               start_settle_ms_from_knob("abc") == 20000 && start_settle_ms_from_knob("99999") == 3600000 &&
               start_settle_ms_from_knob("  7 \n") == 7000,
           "knob_parsing");
    expect(kDefaultSettleMs == 20000, "default settle is 20 s");
    expect(settle_knob_is_legacy("45") && settle_knob_is_legacy(" 45\n") && !settle_knob_is_legacy("") && !settle_knob_is_legacy("20") &&
               !settle_knob_is_legacy("045x"),
           "settle knob 45 is legacy");
    expect(start_mode_is_legacy("legacy\n") && start_mode_is_legacy("LEGACY") && !start_mode_is_legacy("legacy-ish") &&
               !start_mode_is_legacy(""),
           "legacy_mode_knob");
    expect(start_timeout_ms("", 20000) == 60000 && start_timeout_ms("", 600000) == 600000 && start_timeout_ms("3\n", 20000) == 3000 &&
               start_timeout_ms("0", 20000) == 60000 && start_timeout_ms("x", 20000) == 60000,
           "timeout knob");
    {
        StartGateInput g{};
        g.settle_ms = 3000;
        g.timeout_ms = 60000;
        g.window = false;
        g.present_seen = true;
        g.now_ms = 1ull << 40;
        expect(start_gate(g) == StartGateDecision::wait, "gate_never_opens_before_window");
        g.trigger = true;
        expect(start_gate(g) == StartGateDecision::start_trigger, "trigger_wins_immediately");
        g.trigger = false;
        g.window = true;
        g.window_at_ms = 1000;
        g.present_at_ms = 2000;
        g.now_ms = 4999;
        expect(start_gate(g) == StartGateDecision::wait, "gate_requires_present_and_settle: before");
        g.now_ms = 5000;
        expect(start_gate(g) == StartGateDecision::start_present, "gate_requires_present_and_settle: at");
        g.settle_ms = 0;
        g.now_ms = 2000;
        expect(start_gate(g) == StartGateDecision::start_present, "settle_knob_zero_starts_at_present");
        g.settle_ms = 20000;
        g.now_ms = 21999;
        expect(start_gate(g) == StartGateDecision::wait && (g.now_ms = 22000, start_gate(g) == StartGateDecision::start_present),
               "default 20 s settle");
        g.present_seen = false;
        g.settle_ms = 3000;
        g.now_ms = 60999;
        expect(start_gate(g) == StartGateDecision::wait, "timeout_fallback_is_old_delay: before");
        g.now_ms = 61000;
        expect(start_gate(g) == StartGateDecision::start_timeout, "timeout_fallback_is_old_delay: at");
        g.timeout_ms = 45000;
        g.now_ms = 45999;
        expect(start_gate(g) == StartGateDecision::wait, "legacy timeout 45 s: before");
        g.now_ms = 46000;
        expect(start_gate(g) == StartGateDecision::start_timeout, "legacy timeout 45 s: at");
        g.now_ms = 500;
        g.present_seen = true;
        expect(start_gate(g) == StartGateDecision::wait, "time_going_backwards_waits");
    }
    expect(json_number("{\"a\":1,\"logic_mask\":64,\"b\":x}", "logic_mask") == 64 && !json_number("{\"logic_mask\":-1}", "logic_mask") &&
               !json_number("{}", "logic_mask"),
           "json_number");
    expect(focus_cvars_to_append("seta a \"1\"") ==
               "\r\nwin_pauseOnFocusLoss \"0\"\r\nin_controlInactiveWindow \"1\"\r\nr_fullscreen \"0\"\r\n",
           "focus append");
    expect(focus_cvars_to_append("win_pauseOnFocusLoss \"1\"\nin_controlInactiveWindow \"0\"\nr_fullscreen \"1\"\n").empty(),
           "focus present (value not forced)");

    expect(probe_config_from_override("  123 {pid} x\r\n\n", 4242).value() == "123 4242 x\n", "override substitutes {pid}");
    expect(probe_config_from_override("{pid} 0 {pid}", 7).value() == "7 0 7\n", "override substitutes every {pid}");
    expect(probe_config_from_override("{pid", 7).value() == "{pid\n", "override keeps a broken token");
    expect(!probe_config_from_override(" \r\n\t", 7).has_value(), "blank override falls back");
    expect(!probe_config_from_override("", 7).has_value(), "empty override falls back");
    expect(autocontinue_seconds_from_knob("") == 10 && autocontinue_seconds_from_knob("x") == 10 &&
               autocontinue_seconds_from_knob(" 25\n") == 25 && autocontinue_seconds_from_knob("0") == 0 &&
               autocontinue_seconds_from_knob("99999") == 600,
           "autocontinue knob");
    expect(status_says_gameplay("{\"a\":1,\"gameplay\":true,\"b\":2}") && status_says_gameplay("{\"gameplay\" : 1}") &&
               !status_says_gameplay("{\"gameplay\":false}") && !status_says_gameplay("{\"gameplay\":0}") &&
               !status_says_gameplay("{\"gameplay_at_capture\":true}") && !status_says_gameplay(""),
           "status gameplay");

    if (g_failed != 0) return 1;
    std::printf("proxy_core_check: all passed\n");
    return 0;
}
