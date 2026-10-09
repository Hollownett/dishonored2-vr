#include "../src/native_eye_sources.hpp"
#include "../src/game_stereo.hpp"
#include <cassert>
#include <iostream>
int main() {
    d2vr::NativeEyeSources sources;d2vr::NativeEyePair pair;
    constexpr std::uint32_t owner=100;
    assert(sources.match(0x10000,1,owner,owner)==-1);
    assert(!sources.publish(2,0x10000,1,owner,owner));assert(!sources.publish(0,0,1,owner,owner));
    assert(!sources.publish(0,0x10000,1,101,owner));assert(!sources.publish(0,0x10000,1,0,0));
    assert(sources.publish(0,0x10000,1,owner,owner));assert(sources.publish(1,0x20000,1,owner,owner));
    assert(sources.match(0x10000,1,owner,owner)==0 && sources.match(0x20000,1,owner,owner)==1);
    assert(!pair.begin(sources.match(0x10000,1,owner,owner)));
    assert(pair.begin(sources.match(0x20000,1,owner,owner)) && pair.complete());
    assert(sources.match(0x10000,2,owner,owner)==-1); // Address recycled in a later frame before setup.
    assert(sources.match(0x10000,1,101,owner)==-1);assert(sources.match(0x10000,1,owner,101)==-1);
    const auto epoch=sources.epoch();sources.invalidate();pair.reset();assert(sources.epoch()!=epoch);
    assert(sources.match(0x10000,1,owner,owner)==-1); // Resize/owner/allocation replacement within the SAME frame.
    assert(sources.publish(1,0x10000,1,owner,owner));
    assert(sources.match(0x10000,1,owner,owner)==1); // Reused pointer has its new eye role only.
    assert(!pair.begin(sources.match(0x10000,1,owner,owner)) && !pair.complete()); // Right-only pair rejected.
    sources.invalidate();pair.reset();
    assert(sources.publish(0,0x30000,3,owner,owner));assert(sources.publish(1,0x30000,3,owner,owner));
    assert(sources.match(0x30000,3,owner,owner)==-1); // Ambiguous same-frame address is never an eye.
    sources.invalidate();assert(sources.publish(0,0x40000,4,owner,owner));
    assert(sources.match(0x40000,4,owner,owner)==0 && sources.match(0x30000,4,owner,owner)==-1);
    std::cout<<"PASS: eye setup frame/epoch/thread qualification, same-frame owner invalidation, recycled/ambiguous addresses and exact pair ordering\n";
}
