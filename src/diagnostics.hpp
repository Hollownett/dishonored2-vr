#pragma once
// v1046 (user, headset v1045: "diagnostic like frame data collection and all other data collections is totally optional and can be
// turned off, so the actual player and user don't waste any disk space ... by default, it should be turned off"): ONE switch for every
// file the mod writes for troubleshooting: journals, traces, CSVs, image captures, crash records, the full status. A v1045 headset
// session wrote 109 files / 619 MB with the old "frame diagnostics" option off.
// Off by default. build/diagnostics (the launcher's "Save diagnostic logs" feature) turns it on for the whole process; it is read once at
// activation, before any writer runs.
// Not diagnostics (written either way, small and needed by the launcher or the player): the settings file, aa-gpu-caps.txt (the
// launcher's default AA row), the small live status the launcher's Stop button and status line read, the answer to a launcher Tools
// command (vr-command-result.txt), the probe's restoration report and failure note.
#include <atomic>
#include <filesystem>
#include <utility>

namespace d2vr::diag {
inline std::atomic<bool> enabled{false};
inline bool on() noexcept {return enabled.load(std::memory_order_relaxed);}
inline void set(bool value) noexcept {enabled.store(value,std::memory_order_relaxed);}
// A diagnostic file's path: the path itself while diagnostics are on, else an EMPTY path. An ofstream on an empty path opens nothing
// (its writes are no-ops); writers that take a path skip an empty one (never a relative path: that would land in the game's folder).
inline std::filesystem::path file(const std::filesystem::path& path) {return on()?path:std::filesystem::path{};}
// The artifacts folder as the diagnostic writers see it: `dir/L"name"` is that file while diagnostics are on and an empty path while
// they are off, so a writer added later is off by default too. always() is the real folder, for the few files that are not diagnostics.
class Dir {
    std::filesystem::path path_;
public:
    Dir()=default;
    Dir& operator=(std::filesystem::path path) {path_=std::move(path);return *this;}
    std::filesystem::path operator/(const std::filesystem::path& name) const {return file(path_/name);}
    const std::filesystem::path& always() const noexcept {return path_;}
    // The folder itself as a diagnostic directory argument: empty while off (modules take an empty directory as "no logging"; never compose onto it).
    std::filesystem::path when_on() const {return file(path_);}
    bool empty() const noexcept {return path_.empty();}
};
} // namespace d2vr::diag
