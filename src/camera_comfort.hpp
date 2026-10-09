#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace d2vr {
// The pinned native parser 0x102cb0 stores integer/float caches at +0x28/+0x2c.
// Shake consumer 0x88d410 reads +0x28 directly. This owns only those eight
// bytes during a session, never the string, flags, callback or saved config.
class CameraComfort {
    struct Entry {std::uintptr_t address{};std::uint64_t before{},ours{};bool owned{};};
    std::array<Entry,2> entries_{};
    unsigned preserved_changes_{};
    static std::uint64_t cached(int integer,float real) {
        std::uint64_t packed{};
        std::memcpy(&packed,&integer,4);std::memcpy(reinterpret_cast<char*>(&packed)+4,&real,4);
        return packed;
    }
public:
    bool active() const {for(const auto& e:entries_) if(e.owned) return true;return false;}
    unsigned preserved_changes() const {return preserved_changes_;}
    template<class Read,class CompareExchange>
    bool restore(Read& read,CompareExchange& exchange) {
        bool complete=true;
        for(auto& e:entries_) {
            if(!e.owned) continue;
            std::uint64_t current{};
            if(!read(e.address,&current,8)) {complete=false;continue;}
            if(current!=e.ours) {
                // Preserve a later native/user choice rather than restoring over it.
                if(current!=e.before) ++preserved_changes_;
                e.owned=false;continue;
            }
            if(exchange(e.address,e.ours,e.before)) e.owned=false;
            else complete=false; // Keep ownership for a bounded cleanup retry.
        }
        return complete;
    }
    template<class Read,class CompareExchange>
    void apply(std::uintptr_t base,Read& read,CompareExchange& exchange) {
        if(active()) throw std::runtime_error("Camera comfort override is still owned");
        struct Profile {std::uintptr_t rva,name;unsigned type;int integer;float real;};
        constexpr std::array<Profile,2> profiles{{
            {0x398e200,0x1c5a9c8,4,0,0.f}, // ark_playerBobAmount
            {0x39a41a0,0x1c8dec8,1,1,1.f}  // view_skipShakes
        }};
        entries_={};preserved_changes_=0;
        // Validate both variables before changing either one.
        for(unsigned i=0;i<profiles.size();++i) {
            const auto& p=profiles[i];std::array<std::byte,0x50> header{};
            if(!read(base+p.rva,header.data(),header.size())) throw std::runtime_error("Cannot inspect comfort variable");
            std::uintptr_t name{};unsigned flags{};int integer{};float real{};std::uint64_t before{};
            std::memcpy(&name,header.data()+0x30,8);std::memcpy(&flags,header.data()+0x48,4);
            std::memcpy(&integer,header.data()+0x28,4);std::memcpy(&real,header.data()+0x2c,4);
            std::memcpy(&before,header.data()+0x28,8);
            if(name!=base+p.name || (flags&7)!=p.type || !std::isfinite(real) || real<0 ||
               real>(p.type==1?1.f:32.f) || integer!=static_cast<int>(real) || (base+p.rva+0x28)%8)
                throw std::runtime_error("Comfort variable identity, cache or alignment differs");
            entries_[i]={base+p.rva+0x28,before,cached(p.integer,p.real),false};
        }
        for(auto& e:entries_) {
            if(!exchange(e.address,e.before,e.ours)) {
                restore(read,exchange);
                throw std::runtime_error("Comfort variable changed during atomic activation");
            }
            e.owned=true;
        }
    }
};
} // namespace d2vr
