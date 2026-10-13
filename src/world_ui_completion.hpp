#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace d2vr {
// CPU submission ownership only: the caller publishes after native execution, then
// copies on the same immediate context. Recording and FinishCommandList are not completion.
class WorldUiCompletion {
    struct Scope {unsigned movie,eye;std::uint64_t serial;};
    std::mutex mutex_;
    std::unordered_map<void*,std::vector<Scope>> recording_,lists_;
    std::array<std::array<std::uint64_t,2>,3> published_{};
    static bool valid(unsigned movie,unsigned eye,std::uint64_t scope) noexcept {
        return movie<3 && eye<2 && scope!=0;
    }
    void publish(const Scope& scope) noexcept {
        // Execution order is authoritative, including an older list executed after a newer list.
        published_[scope.movie][scope.eye]=scope.serial;++published_scopes;
    }
public:
    std::atomic<std::uint64_t> recorded_scopes{},finished_lists{},executed_lists{},immediate_scopes{},published_scopes{},rejected{};
    // Generic identities stay owned by the native renderer; call clear() after its scopes drain.
    bool recorded(void* context,unsigned movie,unsigned eye,std::uint64_t scope) {
        if(!context || !valid(movie,eye,scope)) {++rejected;return false;}
        std::lock_guard<std::mutex> lock(mutex_);
        auto& pending=recording_[context];
        for(const auto& item:pending) if(item.movie==movie && item.eye==eye && item.serial==scope) return true;
        pending.push_back({movie,eye,scope});++recorded_scopes;return true;
    }
    bool finished(void* context,void* list) {
        if(!context || !list) {++rejected;return false;}
        std::lock_guard<std::mutex> lock(mutex_);
        // A newly returned native list owns this identity even if an abandoned list used it earlier.
        lists_.erase(list);
        const auto pending=recording_.find(context);
        if(pending==recording_.end()) return false;
        lists_.emplace(list,std::move(pending->second));recording_.erase(pending);++finished_lists;return true;
    }
    // Invoke after native ExecuteCommandList (including all FSR split prefixes/remainder) returns.
    bool executed(void* list) {
        if(!list) {++rejected;return false;}
        std::lock_guard<std::mutex> lock(mutex_);
        const auto pending=lists_.find(list);if(pending==lists_.end()) return false;
        for(const auto& scope:pending->second) publish(scope);
        lists_.erase(pending);++executed_lists;return true;
    }
    // Immediate contexts publish only after the native movie draw returns, on its owning thread.
    bool immediate(unsigned movie,unsigned eye,std::uint64_t scope) {
        if(!valid(movie,eye,scope)) {++rejected;return false;}
        std::lock_guard<std::mutex> lock(mutex_);publish({movie,eye,scope});++immediate_scopes;return true;
    }
    bool ready(unsigned movie,unsigned eye,std::uint64_t scope) {
        if(!valid(movie,eye,scope)) return false;
        std::lock_guard<std::mutex> lock(mutex_);return published_[movie][eye]==scope;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);recording_.clear();lists_.clear();published_={};
    }
};
} // namespace d2vr
