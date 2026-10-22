#pragma once
// Bonelab feel Task 14: read-only samples and a parser for the one-shot living-guard lever probe (dev knob build/guard-probe, off by default).
// The probe measures which engine lever gives a stagger and a pull on a living guard without ragdolling him (docs/BONELAB-FEEL-GUARD-PULL-RE.md).
// Pure: no engine calls here. The stealth test (NativeMelee::unaware, RVA 0x5bf560) is passed in as a callable by the caller, so this header
// stays testable off the game.
#include "native_rigid_body.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace d2vr {
struct GuardSample {std::uint64_t tick{};std::uint32_t entity_id{};Vec3 position{};unsigned char state_d1{};bool unaware{};bool bodies_dynamic{};bool read_ok{};bool unaware_ok{};};
inline std::string guard_sample_csv_header() {return "tick_ms,entity_id,x,y,z,state_d1,unaware,bodies_dynamic,read_ok,unaware_ok";}
inline std::string guard_sample_csv_row(const GuardSample& s) {
    char b[176];
    std::snprintf(b,sizeof b,"%llu,%u,%.4f,%.4f,%.4f,%u,%d,%d,%d,%d",static_cast<unsigned long long>(s.tick),s.entity_id,s.position.x,s.position.y,s.position.z,
        static_cast<unsigned>(s.state_d1),s.unaware?1:0,s.bodies_dynamic?1:0,s.read_ok?1:0,s.unaware_ok?1:0);
    return b;
}
enum class GuardExperiment {none,shove,shove_decl,teleport_step};
struct GuardProbePlan {GuardExperiment experiment{};std::string decl;double step_m{};unsigned steps{};};
inline constexpr double guard_probe_max_step_m=.5;
inline constexpr unsigned guard_probe_max_steps=20;
// The running experiment (game thread only): kept outside the hook class because clang-cl needs member initializers complete before a static of the type.
struct GuardProbeRun {bool active{};std::uintptr_t npc{};Vec3 hand{};std::uint64_t until{},next_sample{},next_step{};std::uint32_t id{};unsigned steps_left{};double step_m{};std::ofstream out;};
// "shove" | "shove_decl <name>" | "teleport_step <metres> <count>"; anything else (empty, unknown word, bad number, out of bounds, trailing words) is rejected.
// <name> is a damage decl under components/base/damage/ (lower-case letters, digits and underscores, up to 64).
inline std::optional<GuardProbePlan> parse_guard_probe(const std::string& file_text) {
    std::istringstream in(file_text);
    std::string word,a,b,extra;
    if(!(in>>word)) return {};
    GuardProbePlan plan;
    if(word=="shove") {
        if(in>>extra) return {};
        plan.experiment=GuardExperiment::shove;return plan;
    }
    if(word=="shove_decl") {
        if(!(in>>a) || (in>>extra) || a.empty() || a.size()>64) return {};
        for(const char c:a) if(!((c>='a' && c<='z') || (c>='0' && c<='9') || c=='_')) return {};
        plan.experiment=GuardExperiment::shove_decl;plan.decl=a;return plan;
    }
    if(word=="teleport_step") {
        if(!(in>>a>>b) || (in>>extra)) return {};
        char* end{};const double step=std::strtod(a.c_str(),&end);
        if(end==a.c_str() || *end || !std::isfinite(step) || step<=0 || step>guard_probe_max_step_m) return {};
        const long count=std::strtol(b.c_str(),&end,10);
        if(end==b.c_str() || *end || count<1 || count>static_cast<long>(guard_probe_max_steps)) return {};
        plan.experiment=GuardExperiment::teleport_step;plan.step_m=step;plan.steps=static_cast<unsigned>(count);return plan;
    }
    return {};
}
// One sample of a living guard: id (+0xa8), state byte (+0xd1), drawn model origin (model +0x1860), the engine's stealth test and whether his bone-0
// body is dynamic (a ragdoll: flags & 7 == 2, what native_ragdoll_body requires). read_ok is false if any plain read failed (the rest is then zero).
// `unaware` is a callable (npc) -> std::optional<bool> (empty = the engine test did not run), called only when the reads succeeded. bodies_dynamic false means "not a
// readable dynamic ragdoll": it cannot tell a keyframed guard from a failed physics read, so read it together with state_d1 and the position.
template<class Read,class Unaware> GuardSample sample_guard(Read& read,std::uintptr_t base,std::uintptr_t npc,std::uint64_t tick,Unaware&& unaware) {
    GuardSample s;s.tick=tick;
    if(!rig::pointer(npc)) return s;
    const auto id=rig::value<std::uint32_t>(read,npc+0xa8);
    const auto state=rig::value<unsigned char>(read,npc+0xd1);
    const auto model=rig::value<std::uintptr_t>(read,npc+0xf0);
    const auto at=model && rig::pointer(*model)?rig::value<std::array<float,3>>(read,*model+0x1860):std::nullopt;
    if(!id || !state || !at || !std::isfinite((*at)[0]) || !std::isfinite((*at)[1]) || !std::isfinite((*at)[2])) return s;
    s.entity_id=*id;s.state_d1=*state;s.position={(*at)[0],(*at)[1],(*at)[2]};
    s.bodies_dynamic=native_ragdoll_body(read,base,npc,0).has_value();
    if(const std::optional<bool> a=unaware(npc)) {s.unaware=*a;s.unaware_ok=true;} // a stealth test that did not run is not "alerted"
    s.read_ok=true;
    return s;
}
}
