#pragma once
// More forgiving VR choke takedowns: the player's choke sensor decl (arkDeclCpntPlayerSensorChoke, vtable 0x1b3e018, reached as
// *(choke sensor component+8)) holds the gates the sensor reads every frame (docs/native-takedown-range.md): +0x70 max 3D distance,
// +0x7c vertical, +0x80/+0x84 yaw window, +0x88/+0x8c yaw window when the target is vulnerable. In VR the body origin trails the
// head and the body heading differs from the gaze, so the shipped gates trip early. Only the player owns these sensors, so scaling
// the decl scales the instance. Writes happen only when all six floats still hold the shipped values; stop restores them.
#include "game_rig.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace d2vr {
class NativeTakedownRange {
public:
    inline static constexpr std::uintptr_t decl_vtable_rva=0x1b3e018;
    inline static constexpr std::array<std::uintptr_t,6> offsets{0x70,0x7c,0x80,0x84,0x88,0x8c};
    inline static constexpr std::array<float,6> shipped{3.f,1.7f,-40.f,40.f,-50.f,50.f};
    template<class Read,class Write> bool apply(Read& read,std::uintptr_t base,std::uintptr_t choke_sensor,float factor,Write& write) {
        using namespace rig;
        const auto owner=lease(read,base,choke_sensor,0x1b3e158);
        if(applied_ && (!owner || !same(choke_owner_,*owner))) {applied_=false;decl_=0;choke_owner_={};choke_written_={};}
        if(!owner) return false;
        const auto decl=choke_sensor?value<std::uintptr_t>(read,choke_sensor+8):std::nullopt;
        if(!decl || !typed(read,base,*decl,decl_vtable_rva)) return false;
        if(*decl==decl_ && applied_ && same(choke_owner_,*owner))
            return std::all_of(choke_written_.begin(),choke_written_.end(),[](bool owned){return owned;});
        if(applied_ && !restore(read,write)) return false;
        std::array<float,6> now{};
        for(unsigned i=0;i<6;++i) {const auto v=value<float>(read,*decl+offsets[i]);if(!v) return false;now[i]=*v;}
        for(unsigned i=0;i<6;++i) if(std::fabs(now[i]-shipped[i])>1e-3f) return false; // not the shipped data: leave it
        decl_=*decl;factor_=factor;choke_owner_=*owner;applied_=true; // before writing: a failed write rolls back what was written
        choke_written_={};
        for(unsigned i=0;i<6;++i) {const float v=shipped[i]*factor;if(!write(*decl+offsets[i],shipped[i],v)) {restore(read,write);return false;}choke_written_[i]=true;}
        return true;
    }
    // Kill takedowns: the assassination rule handler's float conditions "height <= 1.7" and "distance <= 3.0" (measured from the
    // body origin, which trails the HMD in VR). Rule handlers hang in the GlobalAssassination sensor's hash map (+0x190 buckets,
    // +0x198 count; node +0x28 handler, +0x30 next); handler +0x20 rules (count low 24 bits of +0x28); rule +0 conditions
    // (+8 count); float condition (vtable 0x1ca2140): +0x28 operator (4 = <=), +0x2c value. Read live 2026-10-12: 14 rules with
    // exactly {<=1.7, <=3.0}. Only those are scaled, matched by value; everything else is left alone.
    inline static constexpr std::uintptr_t float_condition_vtable_rva=0x1ca2140;
    template<class Read,class Write> unsigned apply_kill(Read& read,std::uintptr_t base,std::uintptr_t global,float factor,Write& write) {
        using namespace rig;
        const auto owner=lease(read,base,global,0x1b3f998);
        if(global==kill_global_ && owner && same(kill_owner_,*owner)) return static_cast<unsigned>(kill_.size());
        if(!restore_kill(read,write)) return 0;
        if(!owner) return 0;
        const auto buckets=value<std::uintptr_t>(read,global+0x190);const auto count=value<std::int32_t>(read,global+0x198);
        if(!buckets || !pointer(*buckets) || !count || *count<=0 || *count>4096) return 0;
        std::vector<std::pair<std::uintptr_t,float>> found;
        for(std::int32_t b=0;b<*count;++b) {
            auto node=value<std::uintptr_t>(read,*buckets+b*8ull);
            for(unsigned guard=0;node && pointer(*node) && guard<64;++guard,node=value<std::uintptr_t>(read,*node+0x30)) {
                const auto handler=value<std::uintptr_t>(read,*node+0x28);if(!handler || !pointer(*handler)) continue;
                const auto rules=value<std::uintptr_t>(read,*handler+0x20);const auto n=value<std::uint32_t>(read,*handler+0x28);
                if(!rules || !pointer(*rules) || !n) continue;
                for(std::uint32_t r=0;r<std::min<std::uint32_t>(*n&0xffffff,64);++r) {
                    const auto rule=value<std::uintptr_t>(read,*rules+r*8ull);if(!rule || !pointer(*rule)) continue;
                    const auto conds=value<std::uintptr_t>(read,*rule);const auto m=value<std::uint32_t>(read,*rule+8);
                    if(!conds || !pointer(*conds) || !m) continue;
                    std::vector<std::pair<std::uintptr_t,float>> floats;bool other_float=false;
                    for(std::uint32_t c=0;c<std::min<std::uint32_t>(*m&0xffffff,16);++c) {
                        const auto cond=value<std::uintptr_t>(read,*conds+c*8ull);
                        if(!cond || !typed(read,base,*cond,float_condition_vtable_rva)) continue;
                        const auto op=value<std::int32_t>(read,*cond+0x28);const auto v=value<float>(read,*cond+0x2c);
                        if(!op || !v) {other_float=true;continue;}
                        if(*op==4 && (std::fabs(*v-1.7f)<1e-4f || std::fabs(*v-3.f)<1e-4f)) floats.emplace_back(*cond+0x2c,*v);else other_float=true;
                    }
                    if(!other_float && floats.size()==2 && std::fabs(floats[0].second-floats[1].second)>1.f)
                        found.insert(found.end(),floats.begin(),floats.end());
                }
            }
        }
        if(found.empty()) return 0; // rules not loaded yet: retry later
        kill_global_=global;
        kill_owner_=*owner;
        for(const auto& [at,v]:found) {const float scaled=v*factor;if(write(at,v,scaled)) kill_.push_back({at,v,scaled});}
        return static_cast<unsigned>(kill_.size());
    }
    template<class Read,class Write> bool restore_kill(Read& read,Write& write) {
        if(!kill_.empty() && !valid(read,kill_owner_)) kill_.clear();
        for(auto patch=kill_.begin();patch!=kill_.end();) {
            if(!condition_owned(read,kill_owner_.sensor,patch->address-0x2c) ||
               !rig::typed(read,kill_owner_.base,patch->address-0x2c,float_condition_vtable_rva) ||
               rig::value<std::int32_t>(read,patch->address-4)!=4) {patch=kill_.erase(patch);continue;}
            const auto now=rig::value<float>(read,patch->address);
            if(!now || (*now==patch->scaled && !write(patch->address,patch->scaled,patch->original))) ++patch;
            else patch=kill_.erase(patch);
        }
        if(kill_.empty()) kill_global_=0;
        return kill_.empty();
    }
    template<class Read,class Write> bool restore(Read& read,Write& write) {
        const bool kill_restored=restore_kill(read,write);
        if(!applied_) return kill_restored;
        if(valid(read,choke_owner_) && rig::value<std::uintptr_t>(read,choke_owner_.sensor+8)==decl_ &&
           rig::typed(read,choke_owner_.base,decl_,decl_vtable_rva)) {
            for(unsigned i=0;i<6;++i) if(choke_written_[i]) {
                const auto value=rig::value<float>(read,decl_+offsets[i]);
                if(value && (*value!=shipped[i]*factor_ || write(decl_+offsets[i],shipped[i]*factor_,shipped[i]))) choke_written_[i]=false;
            }
        } else choke_written_={};
        applied_=std::any_of(choke_written_.begin(),choke_written_.end(),[](bool owned){return owned;});
        if(!applied_) decl_=0;
        return kill_restored && !applied_;
    }
    bool pending() const noexcept {return applied_ || !kill_.empty();}
    bool applied() const noexcept {return applied_;}
    unsigned kill_rules() const noexcept {return static_cast<unsigned>(kill_.size()/2);}
private:
    template<class Read> static bool condition_owned(Read& read,std::uintptr_t global,std::uintptr_t condition) {
        using namespace rig;
        const auto buckets=value<std::uintptr_t>(read,global+0x190);const auto count=value<std::int32_t>(read,global+0x198);
        if(!buckets || !pointer(*buckets) || !count || *count<=0 || *count>4096) return false;
        for(std::int32_t bucket=0;bucket<*count;++bucket) {
            auto node=value<std::uintptr_t>(read,*buckets+bucket*8ull);
            for(unsigned guard=0;node && pointer(*node) && guard<64;++guard,node=value<std::uintptr_t>(read,*node+0x30)) {
                const auto handler=value<std::uintptr_t>(read,*node+0x28);if(!handler || !pointer(*handler)) continue;
                const auto rules=value<std::uintptr_t>(read,*handler+0x20);const auto rule_count=value<std::uint32_t>(read,*handler+0x28);
                if(!rules || !pointer(*rules) || !rule_count) continue;
                for(std::uint32_t rule_index=0;rule_index<std::min<std::uint32_t>(*rule_count&0xffffff,64);++rule_index) {
                    const auto rule=value<std::uintptr_t>(read,*rules+rule_index*8ull);if(!rule || !pointer(*rule)) continue;
                    const auto conditions=value<std::uintptr_t>(read,*rule);const auto condition_count=value<std::uint32_t>(read,*rule+8);
                    if(!conditions || !pointer(*conditions) || !condition_count) continue;
                    for(std::uint32_t condition_index=0;condition_index<std::min<std::uint32_t>(*condition_count&0xffffff,16);++condition_index)
                        if(value<std::uintptr_t>(read,*conditions+condition_index*8ull)==condition) return true;
                }
            }
        }
        return false;
    }
    struct Lease {std::uintptr_t base{},game{},sensor{},vtable{},player{};std::uint32_t spawn{};std::int32_t generation{};};
    static bool same(const Lease& first,const Lease& second) {
        return first.base==second.base && first.game==second.game && first.sensor==second.sensor && first.vtable==second.vtable &&
            first.player==second.player && first.spawn==second.spawn && first.generation==second.generation;
    }
    template<class Read> static std::optional<Lease> lease(Read& read,std::uintptr_t base,std::uintptr_t sensor,std::uintptr_t vtable) {
        using namespace rig;
        if(!typed(read,base,sensor,vtable)) return {};
        const auto game=value<std::uintptr_t>(read,base+0x39a5290),player=value<std::uintptr_t>(read,sensor+0x10);
        const auto generation=value<std::int32_t>(read,sensor+0x18);
        if(!game || !typed(read,base,*game,0x1c940a8) || !player || !typed(read,base,*player,0x1c5e428) ||
           value<std::uintptr_t>(read,*game+0x3130b0)!=player || !generation || *generation==-1) return {};
        const auto spawn=value<std::uint32_t>(read,*player+0xa8);
        if(!spawn || !*spawn) return {};
        return Lease{base,*game,sensor,vtable,*player,*spawn,*generation};
    }
    template<class Read> static bool valid(Read& read,const Lease& owner) {
        const auto current=lease(read,owner.base,owner.sensor,owner.vtable);
        return current && same(owner,*current);
    }
    struct KillPatch {std::uintptr_t address{};float original{},scaled{};};
    std::uintptr_t decl_{};float factor_{1};bool applied_{};
    std::array<bool,6> choke_written_{};
    Lease choke_owner_{},kill_owner_{};
    std::uintptr_t kill_global_{};std::vector<KillPatch> kill_;
};
} // namespace d2vr
