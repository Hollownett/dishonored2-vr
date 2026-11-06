#pragma once
// Background file writer for the 1 Hz diagnostics: the Present thread builds the text and hands it over; the
// thread does the open/write/close.  Latest-wins per path (a newer text replaces an unwritten older one);
// append posts for a path concatenate up to a cap (the lifecycle CSV) and drop beyond it.  post() only takes a
// mutex the worker holds for pointer swaps, never during I/O.  The instance is leaked on purpose: a static
// std::thread destructor at DLL unload would terminate; stop() joins explicitly and close() refuses later posts.
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace d2vr {
class LatestFileWriter {
public:
    void post(const std::filesystem::path& path,std::string text,bool append=false,std::atomic<bool>* failed=nullptr) {
        if(path.empty()) return; // v1046: diagnostics off (d2vr::diag::Dir gives an empty path): nothing queued, no thread started
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(closed_) return;
            auto& slot=pending_[path];
            if(append && slot.append) {if(slot.text.size()<kAppendCap) slot.text+=text;}
            else {slot.text=std::move(text);slot.append=append;}
            slot.failed=failed;
            if(!thread_.joinable()) thread_=std::thread([this]{run();});
        }
        wake_.notify_one();
    }
    // Drains what is pending, joins the thread, and refuses posts until open().
    void stop() {
        std::thread joiner;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_=true;quit_=true;joiner=std::move(thread_);
        }
        wake_.notify_one();
        if(joiner.joinable()) joiner.join();
    }
    void open() {std::lock_guard<std::mutex> lock(mutex_);closed_=false;quit_=false;}
    static LatestFileWriter& instance() {static LatestFileWriter& writer=*new LatestFileWriter;return writer;}
private:
    struct Slot {std::string text;bool append{};std::atomic<bool>* failed{};};
    static constexpr std::size_t kAppendCap=64*1024;
    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        for(;;) {
            wake_.wait(lock,[this]{return quit_ || !pending_.empty();});
            auto work=std::move(pending_);pending_.clear();
            const bool last=quit_;
            lock.unlock();
            for(auto& [path,slot]:work) {
                std::ofstream out(path,slot.append?std::ios::app:std::ios::trunc);
                out<<slot.text;
                if(!out && slot.failed) slot.failed->store(true);
            }
            lock.lock();
            if(last && pending_.empty()) return;
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    std::map<std::filesystem::path,Slot> pending_;
    std::thread thread_;
    bool closed_{},quit_{};
};
}
