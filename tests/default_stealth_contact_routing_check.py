"""Compile the production contact decision and pin exact-victim RT ownership."""
from pathlib import Path
import subprocess
import tempfile


def fixture(source: str) -> str:
    begin = "// default_stealth_contact_begin"
    end = "// default_stealth_contact_end"
    assert source.count(begin) == source.count(end) == 1
    decision = source.split(begin, 1)[1].split(end, 1)[0]
    return r'''
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <cstdio>
#include <mutex>
using Read=bool (*)(std::uintptr_t,void*,std::size_t);
struct Contact {
    struct Hit {std::uintptr_t victim{123};} hit;
    unsigned owner{7};std::uint32_t victim_spawn{42};
    bool rear_contact{},native_offer_victim{},lethal_contact{};
};
template<class T> struct StealthHold {
    std::optional<T> payload;std::uint32_t target{};unsigned calls{};
    bool accepts{true};
    bool pending() const {return payload.has_value();}
    std::uint32_t victim() const {return target;}
    bool hold(const T& contact,std::uint32_t spawn,std::uint64_t now,bool offer,bool lethal) {
        assert(now==1000 && offer && lethal);++calls;
        if(!accepts)return false;
        payload=contact;target=spawn;return true;
    }
};
bool exact_offer=true;unsigned offer_calls{};
bool native_contact_assassination_offer(Read&,unsigned owner,std::uint32_t victim) {
    assert(owner==7 && victim==42);++offer_calls;return exact_offer;
}
struct Sword {float amount{};};
struct NativeMelee {
    inline static bool damageable_answer=true;
    inline static std::optional<bool> unaware_answer=true;
    inline static unsigned damageable_calls{},unaware_calls{};
    static bool damageable(std::uintptr_t,Read,std::uintptr_t victim) {
        assert(victim==123);++damageable_calls;return damageable_answer;
    }
    static std::optional<bool> unaware_result(std::uintptr_t,Read,std::uintptr_t victim) {
        assert(victim==123);++unaware_calls;return unaware_answer;
    }
};
struct Runner {
    std::uintptr_t base_{1};Read read_{};
    std::atomic<bool> legacy_contact_{false},stealth_enabled_{true};
    std::array<unsigned,5> stealth_reject_{},stealth_counts_{};
    std::array<unsigned,3> stealth_awareness_{};
    Contact contact_{};StealthHold<Contact> stealth_hold_;
    std::mutex trace_mutex_;std::optional<Contact> intent_contact_request_;
    bool killed{};float amount{};unsigned applied{};
    void run() {
        auto* self=this;
        const std::uint64_t final_now=1000;
        for(const auto& contact:std::array<Contact,1>{contact_}) {
            Sword strike{};bool stealth_kill=false;
            @DECISION@
            amount=strike.amount;killed=stealth_kill;++applied;
        }
    }
};
void reset(bool damageable,std::optional<bool> unaware) {
    NativeMelee::damageable_answer=damageable;NativeMelee::unaware_answer=unaware;
    NativeMelee::damageable_calls=NativeMelee::unaware_calls=0;
    exact_offer=true;offer_calls=0;
}
int main() {
    reset(true,true);Runner unaware;unaware.run();
    assert(unaware.applied==1 && unaware.killed && unaware.amount==1000.f);
    assert(unaware.stealth_counts_[0]==1 && unaware.stealth_awareness_[2]==1);
    assert(NativeMelee::damageable_calls==1 && NativeMelee::unaware_calls==1);
    reset(true,false);Runner aware;aware.run();
    assert(aware.applied==1 && !aware.killed && aware.amount==0.f);
    assert(aware.stealth_reject_[2]==1 && aware.stealth_awareness_[1]==1);
    assert(aware.stealth_counts_[0]==0);
    reset(true,std::nullopt);Runner unavailable;unavailable.run();
    assert(unavailable.applied==0 && !unavailable.killed);
    assert(unavailable.stealth_awareness_[0]==1 && unavailable.stealth_counts_[0]==0);
    for(const auto awareness:std::array<std::optional<bool>,3>{true,false,std::nullopt}) {
        reset(true,awareness);Runner rear;rear.contact_.rear_contact=true;rear.run();
        assert(rear.applied==1 && rear.killed && rear.amount==1000.f);
        assert(rear.stealth_counts_[0]==1);
    }
    reset(true,false);Runner neck;neck.contact_.rear_contact=true;neck.contact_.native_offer_victim=true;neck.run();
    assert(neck.applied==0 && !neck.killed && neck.stealth_hold_.calls==1 && offer_calls==1);
    assert(neck.intent_contact_request_ && neck.intent_contact_request_->lethal_contact);
    assert(neck.intent_contact_request_->victim_spawn==42 && neck.intent_contact_request_->owner==7);
    assert(neck.stealth_hold_.payload && neck.stealth_hold_.payload->lethal_contact);
    reset(true,true);Runner fake;fake.contact_.native_offer_victim=true;exact_offer=false;fake.run();
    assert(fake.applied==1 && fake.killed && fake.amount==1000.f && !fake.intent_contact_request_);
    assert(fake.stealth_hold_.calls==0 && offer_calls==1);
    reset(true,true);Runner different;different.contact_.native_offer_victim=true;
    different.stealth_hold_.payload=Contact{};different.stealth_hold_.target=43;different.run();
    assert(different.applied==1 && different.killed && different.amount==1000.f);
    assert(different.stealth_hold_.target==43 && different.stealth_hold_.calls==0 && !different.intent_contact_request_);
    reset(true,true);Runner same;same.contact_.native_offer_victim=true;
    same.stealth_hold_.payload=Contact{};same.stealth_hold_.target=42;same.run();
    assert(same.applied==0 && same.stealth_hold_.calls==0 && offer_calls==0 && !same.intent_contact_request_);
    reset(true,true);Runner refused;refused.contact_.native_offer_victim=true;refused.stealth_hold_.accepts=false;refused.run();
    assert(refused.applied==1 && refused.killed && refused.amount==1000.f && !refused.intent_contact_request_);
    reset(false,true);Runner invalid;invalid.run();
    assert(invalid.applied==1 && !invalid.killed && invalid.amount==0.f);
    assert(invalid.stealth_reject_[3]==1 && NativeMelee::unaware_calls==0);
    reset(true,true);Runner legacy;legacy.legacy_contact_=true;legacy.run();
    assert(legacy.applied==1 && !legacy.killed && legacy.amount==0.f);
    assert(NativeMelee::damageable_calls==0 && NativeMelee::unaware_calls==0);
    assert(legacy.stealth_counts_[0]==0);
    reset(true,true);Runner disabled;disabled.stealth_enabled_=false;disabled.run();
    assert(disabled.applied==1 && !disabled.killed && disabled.amount==0.f);
    assert(NativeMelee::damageable_calls==0 && NativeMelee::unaware_calls==0);
    std::puts("PASS extracted contact: rear regardless awareness, unaware lethal, neck reservation, offer/pending victim isolation and refusal fallback");
}
'''.replace("@DECISION@", decision)


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    source = (root / "src/native_interaction_aim.hpp").read_text(encoding="utf-8-sig")
    decision = source.split("// default_stealth_contact_begin", 1)[1].split("// default_stealth_contact_end", 1)[0]
    assert "speed" not in decision
    current = source.split("static bool contact_attack_current", 1)[1].split("static std::optional<OwnedSwordContact> take_contact_attack", 1)[0]
    for required in ("epoch==contact.pose_epoch", "sword_contact_owned(read,contact)",
                     "native_contact_scene(read,contact.owner)",
                     "native_contact_assassination_offer(read,contact.owner,contact.victim_spawn)"):
        assert required in current, required
    take = source.split("static std::optional<OwnedSwordContact> take_contact_attack", 1)[1].split("inline static SlowRejectLatch", 1)[0]
    assert "contact.swap(intent_contact_request_)" in take
    assert "contact_attack_current(read,*contact,now,epoch)" in take
    controller = (root / "src/dxgi_probe.cpp").read_text(encoding="utf-8-sig")
    compute = controller.split("auto contact_request=", 1)[1].split("// attack_intent_compute_end", 1)[0]
    assert "take_contact_attack(control_reader,f.now_ms,game_xr->pose_epoch())" in compute
    assert "contact_attack_current(control_reader,*contact_attack_target,f.now_ms,game_xr->pose_epoch())" in compute
    assert "attack_press.reset();contact_attack_target.reset();" in compute
    assert "contact_attack_target=contact_request" in compute
    assert "kill_sensor_spawn" not in compute and "victim_spawn=" not in compute
    with tempfile.TemporaryDirectory(prefix="d2vr-default-stealth-") as directory:
        temporary = Path(directory)
        translation_unit = temporary / "default_stealth.cpp"
        executable = temporary / "default_stealth.exe"
        translation_unit.write_text(fixture(source), encoding="utf-8")
        subprocess.run(["cl", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/O2", "/MT",
                        str(translation_unit), f"/Fo:{temporary / 'default_stealth.obj'}",
                        f"/Fe:{executable}"], check=True, cwd=temporary)
        subprocess.run([str(executable)], check=True, cwd=temporary)


if __name__ == "__main__":
    main()
