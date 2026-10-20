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
    expect(settle_seconds_from_knob("2\n") == 2 && settle_seconds_from_knob("abc") == 45 &&
               settle_seconds_from_knob("99999") == 3600,
           "settle");
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
