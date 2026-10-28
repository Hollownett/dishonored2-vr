#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace d2vr {
// Native Setup/recording and the next displayed eye image overlap.
template<class T> class NoteFrameHistory {
    struct Entry {std::uint64_t recording_frame;std::shared_ptr<T> value;};
    std::array<std::shared_ptr<const Entry>,3> slots_{};
public:
    void publish(std::uint64_t recording_frame,std::shared_ptr<T> value) {
        if(!recording_frame || !value)return;
        std::shared_ptr<const Entry> entry=std::make_shared<Entry>(Entry{recording_frame,std::move(value)});
        std::atomic_store(&slots_[recording_frame%slots_.size()],std::move(entry));
    }
    std::shared_ptr<T> completed(std::uint64_t display_frame,bool eye_pair_ready) const {
        if(!eye_pair_ready || display_frame<=1)return {};
        const auto recording_frame=display_frame-1;
        const auto entry=std::atomic_load(&slots_[recording_frame%slots_.size()]);
        return entry && entry->recording_frame==recording_frame?entry->value:std::shared_ptr<T>{};
    }
    void clear() {for(auto& slot:slots_)std::atomic_store(&slot,std::shared_ptr<const Entry>{});}
};
}
