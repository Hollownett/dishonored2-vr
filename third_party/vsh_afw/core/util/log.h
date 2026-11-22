// Adapter for the VR Stereo Hub AFW (third_party/vsh_afw, modified): their dvr::log interface, routed to a rate-limited
// line in <mod home>\artifacts\afw.log through d2vr_vsh_afw_log (set by the Dishonored 2 mod). Not part of the original.
#pragma once
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <windows.h>
namespace dvr::log {
enum class Cat { d3d };
enum class Level { Info, Warn, Error };
using Sink = void (*)(const char* line);
inline Sink& sink() { static Sink s = nullptr; return s; }
inline void write(Level lv, const char* fmt, ...) {
    if (!sink()) return;
    char buf[512];
    va_list a; va_start(a, fmt); _vsnprintf_s(buf, sizeof buf, _TRUNCATE, fmt, a); va_end(a);
    char line[540];
    _snprintf_s(line, sizeof line, _TRUNCATE, "%s %s", lv == Level::Error ? "ERROR" : lv == Level::Warn ? "WARN " : "INFO ", buf);
    sink()(line);
}
}
#define DVR_INFO(...) ::dvr::log::write(::dvr::log::Level::Info, __VA_ARGS__)
#define DVR_WARN(...) ::dvr::log::write(::dvr::log::Level::Warn, __VA_ARGS__)
#define DVR_ERROR(...) ::dvr::log::write(::dvr::log::Level::Error, __VA_ARGS__)
// Once per `ms` per call site.
#define DVR_LOG_EVERY_MS(cat, level, ms, ...) do { static ULONGLONG dvr_last_ = 0; const ULONGLONG dvr_now_ = GetTickCount64(); \
    if (!dvr_last_ || dvr_now_ - dvr_last_ >= (ULONGLONG)(ms)) { dvr_last_ = dvr_now_; ::dvr::log::write(level, __VA_ARGS__); } } while (0)
