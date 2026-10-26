#include "../src/takedown_prompt_size.hpp"
#include "../src/native_prompt_anchor.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
using namespace d2vr;
int main() {
    const NativeInteractionTargetIdentity loot{10,1,0,3,1},guard{20,2,0,3,2},other_guard{30,3,0,3,2};
    assert(native_prompt_anchor(guard,loot,guard)==NativePromptAnchor::takedown);
    assert(native_prompt_anchor(loot,loot,guard)==NativePromptAnchor::interaction);
    assert(native_prompt_anchor(other_guard,loot,guard)==NativePromptAnchor::none);
    assert(native_prompt_anchor(guard,loot,{})==NativePromptAnchor::none);
    assert(native_prompt_anchor({},loot,guard)==NativePromptAnchor::interaction);
    assert(native_prompt_anchor({},{},guard)==NativePromptAnchor::takedown);
    assert(native_prompt_anchor({},{},{})==NativePromptAnchor::unbound);
    for(double d:{0.5,1.0,1.5,2.0,3.0}) assert(takedown_prompt_width(d,false)>=0.25);   // readable at kill range
    assert(takedown_prompt_width(1.5,true)<0.08);                                       // the old size, kept behind the knob
    assert(takedown_prompt_width(1.5,false)>3*takedown_prompt_width(1.5,true));
    assert(takedown_prompt_width(100,false)<=1.0 && takedown_prompt_width(-1,false)==0.25);
    assert(takedown_prompt_width(5,false)>=takedown_prompt_width(2,false));              // never shrinks with distance
    std::ifstream in("src/dxgi_probe.cpp");std::string s((std::istreambuf_iterator<char>(in)),{});
    if(!s.empty()) { // wiring pin (run from the repo root)
        assert(s.find("takedown_prompt_width(dist,legacy_takedown_prompt)")!=std::string::npos);
        assert(s.find("legacy-takedown-prompt")!=std::string::npos);
    }
    std::puts("PASS takedown prompt size");
}
