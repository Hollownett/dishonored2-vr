// v1246: secondary-view guard logic (src/secondary_view_guard.hpp). build\check-secondary-view.cmd
#include "secondary_view_guard.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);std::exit(1);}} while(0)
using namespace d2vr::secondary_view;
int main() {
    // v1245 trace, Timepiece up: lens Begin of the eye-1 object first, then eye 0, then eye 1, then the null GUI view.
    const std::uintptr_t A=0x2580824802976,B=0x2580824851408;
    CHECK(classify(B,true,B,A,B)==Kind::secondary);
    CHECK(classify(A,false,B,A,B)==Kind::eye0);
    CHECK(classify(B,false,B,A,B)==Kind::eye1);
    CHECK(classify(0,false,B,A,B)==Kind::null_view);
    CHECK(classify(0x1234,false,B,A,B)==Kind::other);
    CHECK(classify(A,false,B,A,A)==Kind::other); // no distinct eye pair published: nothing is an eye
    CHECK(classify(A,true,A,A,B)==Kind::secondary); // a one-view frame lends slot 0
    // Timepiece camera: the engine camera's basis, 600 m down in z; an independent camera turns.
    const std::array<float,9> basis{-.890832f,.454332f,.00013f,-.454332f,-.890832f,-.00029f,0,-.0003f,1};
    auto turned=basis;turned[0]=.454332f;turned[1]=.890832f;
    CHECK(follows(basis,basis) && !follows(turned,basis));
    const auto t=translation({39.79f,-49.86f,-298.3f},{39.7732f,-49.8862f,301.7f},{39.8023f,-49.8292f,301.7f});
    CHECK(std::abs(t[2]+600.f)<.01f && std::abs(t[0])<.01f && std::abs(t[1])<.01f);
    // Replay gate.
    Record r;r.valid=true;r.view=B;r.prepared=0x100;r.allocation=0x200;r.frame=3503;
    const std::array<float,3> p{39.8f,-49.83f,301.7f};
    CHECK(replay_refusal(r,B,3504,0x200,p,p)==Refusal::none);
    CHECK(replay_refusal(r,A,3504,0x200,p,p)==Refusal::no_record);
    CHECK(replay_refusal(r,B,3505,0x200,p,p)==Refusal::stale);
    CHECK(replay_refusal(r,B,3504,0x300,p,p)==Refusal::allocation);
    CHECK(replay_refusal(r,B,3504,0x200,p,{39.8f,-49.83f,301.8f})==Refusal::prepared_moved);
    r.valid=false;CHECK(replay_refusal(r,B,3504,0x200,p,p)==Refusal::no_record);
    // History spans are disjoint and sum to the record size.
    std::size_t sum=0;for(const auto& s:history_spans) sum+=s.size;CHECK(sum==history_bytes);
    for(std::size_t i=0;i<history_spans.size();++i) for(std::size_t j=i+1;j<history_spans.size();++j) {
        const auto a=history_spans[i],b=history_spans[j];CHECK(a.offset+a.size<=b.offset || b.offset+b.size<=a.offset);}
    std::size_t v=0;for(const auto& s:verify_spans) v+=s.size;CHECK(v==sizeof(Record::verify));
    // v1248: the hook runs every frame while +0x88 stays set, but only frames with a lens Begin are lens passes; the hold expires 2 frames after.
    {std::uint64_t last=0;
     for(std::uint64_t f=100;f<110;++f) {last=lens_pass_frame(false,f,last);CHECK(!lens_paired_hold(last,f));}   // early returns only: never held
     last=lens_pass_frame(true,110,last);CHECK(last==110 && lens_paired_hold(last,110));
     for(std::uint64_t f=111;f<140;++f) {last=lens_pass_frame(false,f,last);CHECK(lens_paired_hold(last,f)==(f<=110+hold_frames));}   // expires
     last=lens_pass_frame(true,140,last);CHECK(lens_paired_hold(last,141) && !lens_paired_hold(last,143));}
    std::printf("PASS secondary_view_guard\n");
    return 0;
}
