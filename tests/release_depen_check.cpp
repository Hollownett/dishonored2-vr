// The released object's depenetration request (release_depen.hpp): it names entity, entity id and physics handle, is re-validated at the dispatcher
// and cancelled by a new hold, so a recycled entity or a re-grabbed object never receives the push.
#include "../src/release_depen.hpp"
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <functional>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
constexpr std::uintptr_t entity=0x1000;constexpr std::uint32_t id=0x20002,handle=0x1000007;
struct Live {std::uint32_t id=::id,handle=::handle;bool held=false;bool readable=true;};
ReleaseDepen::Verdict take(ReleaseDepen& d,const Live& live,std::uint64_t now,ReleaseDepenRequest& out) {
    return d.take(now,
        [&](std::uintptr_t) -> std::optional<std::uint32_t> {if(!live.readable) return std::nullopt;return live.id;},
        [&](std::uintptr_t) -> std::optional<std::uint32_t> {if(!live.readable) return std::nullopt;return live.handle;},
        [&](std::uintptr_t) {return live.held;},out);
}
void depen_writes_the_named_object() {
    ReleaseDepen d;ReleaseDepenRequest out{};d.request({entity,id,handle,{0,0,1},1000});
    CHECK(d.pending() && take(d,Live{},1050,out)==ReleaseDepen::Verdict::ready && out.entity==entity && out.handle==handle && out.velocity.z==1);
    CHECK(!d.pending() && take(d,Live{},1050,out)==ReleaseDepen::Verdict::none);   // one request, consumed once
}
void depen_cancelled_on_regrab() {
    ReleaseDepen d;ReleaseDepenRequest out{};
    d.request({entity,id,handle,{0,0,1},1000});d.cancel();   // a new hold begins (held_clamp_begin)
    CHECK(!d.pending() && take(d,Live{},1010,out)==ReleaseDepen::Verdict::none);
    d.request({entity,id,handle,{0,0,1},1000});
    Live held;held.held=true;   // or a lane holds it by the time the dispatcher runs
    CHECK(take(d,held,1010,out)==ReleaseDepen::Verdict::held && !d.pending());
}
void depen_skips_recycled_handle() {
    ReleaseDepen d;ReleaseDepenRequest out{};
    d.request({entity,id,handle,{0,0,1},1000});
    Live other_handle;other_handle.handle=handle+1;   // the address now holds a different body
    CHECK(take(d,other_handle,1010,out)==ReleaseDepen::Verdict::mismatch);
    d.request({entity,id,handle,{0,0,1},1000});
    Live other_id;other_id.id=id+0x10000;   // a different entity allocated at the address
    CHECK(take(d,other_id,1010,out)==ReleaseDepen::Verdict::mismatch);
    d.request({entity,id,handle,{0,0,1},1000});
    Live gone;gone.readable=false;
    CHECK(take(d,gone,1010,out)==ReleaseDepen::Verdict::mismatch);
}
void depen_refuses_stale_and_fast_and_unnamed() {
    ReleaseDepen d;ReleaseDepenRequest out{};
    d.request({entity,id,handle,{0,0,1},1000});CHECK(take(d,Live{},1101,out)==ReleaseDepen::Verdict::stale);
    d.request({entity,id,handle,{0,0,1.6},1000});CHECK(take(d,Live{},1010,out)==ReleaseDepen::Verdict::rejected);
    d.request({entity,id,handle,{0,0,std::numeric_limits<double>::quiet_NaN()},1000});CHECK(take(d,Live{},1010,out)==ReleaseDepen::Verdict::rejected);
    d.request({entity,0,handle,{0,0,1},1000});CHECK(!d.pending());   // no id, no request
    d.request({entity,id,0,{0,0,1},1000});CHECK(!d.pending());       // no handle, no request
}
}
int main() {
    depen_writes_the_named_object();
    depen_cancelled_on_regrab();
    depen_skips_recycled_handle();
    depen_refuses_stale_and_fast_and_unnamed();
    std::puts("release_depen_check OK");
}
