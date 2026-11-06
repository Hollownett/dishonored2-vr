// v1046: the one diagnostics switch. Off (default): Dir/file() give EMPTY paths (never a relative name), LatestFileWriter queues nothing for them,
// CrashObserver::install(empty) installs nothing; always() stays the real folder.
#include "../src/diagnostics.hpp"
#include "../src/latest_file_writer.hpp"
#include "../src/crash_observer.hpp"
#include <cassert>
#include <iostream>
namespace fs=std::filesystem;
int main() {
    using namespace d2vr;
    assert(!diag::on()); // off by default
    diag::Dir dir;dir=fs::path(L"C:/x/artifacts");
    assert((dir/L"a.csv").empty() && diag::file(L"b").empty() && dir.when_on().empty());
    assert(dir.always()==fs::path(L"C:/x/artifacts"));
    diag::set(true);
    assert(dir/L"a.csv"==(fs::path(L"C:/x/artifacts")/L"a.csv") && dir.when_on()==dir.always() && !diag::file(L"b").empty());
    diag::set(false);
    assert((dir/(std::wstring(L"n")+L".rgba")).empty()); // a composed name is empty too, never "n.rgba" relative to the game folder
    const auto tmp=fs::temp_directory_path()/L"d2vr_diag_check";fs::create_directories(tmp);
    const auto f=tmp/L"w.txt";fs::remove(f);
    LatestFileWriter w;
    w.post(fs::path{},"x");w.post(dir/L"q","y",true); // no-ops: no queue, no thread
    w.stop();
    assert(!fs::exists(f) && !fs::exists(L"q") && !fs::exists(L"w.txt"));
    w.open();w.post(f,"ok");w.stop();assert(fs::exists(f));
    CrashObserver observer;observer.install(fs::path{},0x10000,1); // empty = off: no handler, no files
    assert(observer.records()==0 && observer.remove());
    fs::remove_all(tmp);
    std::cout<<"diagnostics_check OK\n";
}
