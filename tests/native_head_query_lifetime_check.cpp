#include "../src/native_head_query_lifetime.hpp"
#include <cassert>
#include <iostream>
using namespace d2vr;
int main() {
    HeadQueryOwner o{1,0x100000,3,4,5,6};
    auto key=[&](std::uint32_t token) {return HeadQueryKey{o,token,head_query_slot(o.manager,token)};};
    HeadQueryLifetime p; p.owner(o); const auto a=key(0x2005);
    assert(p.issued(a,a.token,4,1)); assert(!p.issued(a,a.token,4,1));
    assert(p.cancel_requested(a)); assert(!p.tracked_shape_retired(o.shape));
    assert(!p.retired(a,a.token,a.token+0x2000,HeadQueryRetirement::teardown_without_fence));
    assert(!p.retired(a,a.token,a.token+0x4000,HeadQueryRetirement::cancelled_before_cast));
    assert(p.retired(a,a.token,a.token+0x2000,HeadQueryRetirement::cancelled_before_cast));
    assert(p.tracked_shape_retired(o.shape));
    const auto b=key(0x6005); assert(p.issued(b,b.token,4,1)); assert(p.worker_begin(b,b.token));
    assert(p.cancel_requested(b)); assert(!p.retired(b,b.token,b.token+0x2000,HeadQueryRetirement::cancelled_before_cast));
    // Native completion/cleanup can precede the observer's worker-return event.
    assert(!p.retired(b,b.token,b.token+0x2000,HeadQueryRetirement::completed));
    assert(!p.worker_end(b,b.token+0x2000,2)); assert(!p.worker_end(b,b.token,1));
    assert(p.worker_end(b,b.token,3)); assert(!p.current_completed(b));
    assert(p.retired(b,b.token,b.token+0x2000,HeadQueryRetirement::completed));
    const auto c=key(0x8007); assert(p.issued(c,c.token,4,1)); assert(p.worker_begin(c,c.token)); assert(p.worker_end(c,c.token,2));
    assert(p.current_completed(c)); auto replacement=o; ++replacement.reference_epoch; p.owner(replacement);
    assert(!p.current_completed(c)); assert(!p.tracked_shape_retired(o.shape));
    assert(p.retired(c,c.token,c.token+0x2000,HeadQueryRetirement::completed)); assert(p.tracked_shape_retired(o.shape));
    p.owner(o); auto wrong=key(0x9008); ++wrong.slot; assert(!p.issued(wrong,wrong.token,4,1));
    assert(!p.issued(key(0x9008),0x9009,4,1)); assert(!p.issued(key(0x9008),0x9008,2,1));
    assert(!head_query_slot(o.manager,0xffffffffU)); assert(!head_query_slot(UINTPTR_MAX-1,1));
    const auto wrap=key(0xffffe001); assert(p.issued(wrap,wrap.token,4,1)); assert(p.cancel_requested(wrap));
    assert(p.retired(wrap,wrap.token,1,HeadQueryRetirement::cancelled_before_cast));
    for(unsigned i=0;i<8;++i) {auto k=key(0xa000+i); assert(p.issued(k,k.token,4,1));}
    auto over=key(0xa008); assert(!p.issued(over,over.token,4,1)); assert(!p.tracked_shape_retired(o.shape));
    std::cout<<"native head query lifetime evidence checks passed\n";
}
