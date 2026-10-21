#pragma once
// Bookkeeping for the held object's collision-layer swap (RIGID_BODY -> DEBRIS while held). Every swap ends in exactly one restore, or
// one counted give-up, and a restore is only ever written to the very object that was swapped (entity id, body identity and pool handle
// re-read just before the write; any mismatch gives up and never writes). A restore that cannot run yet is retried for a bounded number
// of attempts; an unreadable frame is one failed attempt. Several swaps may be pending (a new grab never overwrites an old restore).
#include <array>
#include <cstdint>
namespace d2vr {
struct LayerEntry {
    std::uintptr_t entity{};std::uint32_t entity_id{},handle{},filter{};std::int32_t generation{};std::uint32_t flags{};
    bool held{};unsigned attempts{};bool used{};
};
class LayerLedger {
public:
    enum class Probe {match,unreadable,mismatch};
    enum class Write {ok,failed,mismatch};
    static constexpr unsigned max_attempts=180,capacity=4;
    // A swap happened (the object is held). With every slot used the oldest is given up (counted).
    void swapped(LayerEntry e) noexcept {
        e.used=true;e.held=true;e.attempts=0;++swaps_;
        unsigned slot=capacity;
        for(unsigned i=0;i<capacity;++i) if(!list_[i].used) {slot=i;break;}
        if(slot==capacity) {   // full: the oldest non-held entry goes (the held one is the newest and never the victim unless alone)
            unsigned oldest=0;for(unsigned i=1;i<capacity;++i) if(order_[i]<order_[oldest]) oldest=i;
            ++abandoned_;slot=oldest;
        }
        list_[slot]=e;order_[slot]=++clock_;
    }
    // The hold on this entity ended (any way): its restore may now run.
    void released(std::uintptr_t entity) noexcept {for(auto& e:list_) if(e.used && e.entity==entity) e.held=false;}
    // Level load or epoch change: nothing is written; every pending entry is given up.
    void clear_all() noexcept {for(auto& e:list_) if(e.used) {e.used=false;++abandoned_;}}
    // One service pass over the released entries. probe(e): re-read the object's identity. write(e): the final re-read plus the write.
    // done(e, restored): called once per entry that left the list.
    template<class ProbeFn,class WriteFn,class DoneFn> void service(ProbeFn probe,WriteFn write,DoneFn done) {
        for(auto& e:list_) {
            if(!e.used || e.held) continue;
            const auto p=probe(e);
            if(p==Probe::mismatch) {finish(e,false,done);continue;}
            ++e.attempts;
            if(p==Probe::match) {
                const auto w=write(e);
                if(w==Write::ok) {finish(e,true,done);continue;}
                if(w==Write::mismatch) {finish(e,false,done);continue;}
            }
            if(e.attempts>=max_attempts) finish(e,false,done);
        }
    }
    unsigned pending() const noexcept {unsigned n=0;for(const auto& e:list_) n+=e.used;return n;}
    std::uint64_t swaps() const noexcept {return swaps_;}
    std::uint64_t restores() const noexcept {return restores_;}
    std::uint64_t abandoned() const noexcept {return abandoned_;}
    std::uint64_t releases() const noexcept {return restores_+abandoned_;}
    double restores_per_release() const noexcept {return releases()?double(restores_)/double(releases()):0.;}
private:
    template<class DoneFn> void finish(LayerEntry& e,bool restored,DoneFn& done) {
        if(restored) ++restores_;else ++abandoned_;
        const LayerEntry copy=e;e.used=false;done(copy,restored);
    }
    std::array<LayerEntry,capacity> list_{};std::array<std::uint64_t,capacity> order_{};std::uint64_t clock_{},swaps_{},restores_{},abandoned_{};
};
} // namespace d2vr
