// v1193 pure check of src/buffer_views.hpp: render R's draws recorded while render R+1 (the other eye) already wrote its view take the
// view their bound b1 holds, so their rig-target E is R's. With the newest view (the old fallback) the retargeted point lands the eye
// separation's parallax away (the phantom sword of the v1190/v1191 swing dumps); with the b1 view it lands exactly.
#include <cmath>
#include <cstdio>
#include "../src/buffer_views.hpp"
#include "../src/rig_target.hpp"
using namespace d2vr;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
struct View {unsigned frame{};rig_target::M4 vp{};};
static rig_target::M4 camera(double eye_x) { // reverse-Z perspective (column vectors) of a camera at x looking down -z
    rig_target::M4 p{};p[0]=1.2;p[5]=1.2;p[11]=.01;p[14]=-1; // z_clip = near, w = -z_view
    rig_target::M4 v{};v[0]=v[5]=v[10]=v[15]=1;v[3]=-eye_x;
    return rig_target::mul(p,v);
}
static std::array<double,3> ndc(const rig_target::M4& m,const std::array<double,4>& x) {
    double c[4]{};for(int r=0;r<4;++r) for(int k=0;k<4;++k) c[r]+=m[static_cast<std::size_t>(r*4+k)]*x[static_cast<std::size_t>(k)];
    return {c[0]/c[3],c[1]/c[3],c[2]/c[3]};
}
int main() {
    BufferViews<View,4> views;
    int b_r=0,b_next=0;                                   // two rotating per-view buffers
    const View r{100,camera(-.032)},next{101,camera(.032)}; // render R = left eye, R+1 = right eye (stale target of R)
    views.store(&b_r,r);views.store(&b_next,next);         // R+1's view written before R's draws are recorded
    View got{};CHECK(views.find(&b_r,got) && got.frame==100 && !views.find(nullptr,got));
    // E of R maps R's clip space to its stale eye's (the right eye at R's Setup); the book is keyed by VP
    rig_target::M4 inv_r{},inv_next{};CHECK(rig_target::inverse(r.vp,inv_r) && rig_target::inverse(next.vp,inv_next));
    const rig_target::M4 e_r=rig_target::mul(camera(.032),inv_r),e_next=rig_target::mul(camera(-.032),inv_next);
    const std::array<double,4> sword{.05,-.1,-.4,1};       // a held point 0.4 m ahead
    const auto truth=ndc(camera(.032),sword);
    const auto via=[&](const rig_target::M4& e) {return ndc(rig_target::mul(e,r.vp),sword);}; // E * (rows drawn with R's VP)
    const double good=std::abs(via(e_r)[0]-truth[0]),bad=std::abs(via(e_next)[0]-truth[0]);
    std::printf("retarget error (NDC x): b1 view %.2g, newest view %.3f (~%.0f px at 1922 wide)\n",good,bad,bad*961);
    CHECK(good<1e-9 && bad>.1);
    // v1194: the remaining window. One b1 reused by both renders: the next render's write lands in the shared table before this render's
    // late draws are recorded; the recording context's own snapshot (it wrote R's view into that b1) still names R.
    {int shared_b1=0;views.store(&shared_b1,r);const View own_snapshot=r;views.store(&shared_b1,next);
     View table{};CHECK(views.find(&shared_b1,table) && table.frame==101);
     const unsigned src=view_source(false,true,true,&shared_b1,&shared_b1,true);
     CHECK(src==1 && (src==1?own_snapshot:table).frame==100);
     CHECK(view_source(true,true,true,&shared_b1,&shared_b1,true)==0);    // own newest write
     CHECK(view_source(false,true,true,&b_r,&shared_b1,true)==2);         // own write was another buffer: the b1 table
     CHECK(view_source(false,true,false,nullptr,&shared_b1,false)==4);    // nothing known: the newest (counted)
     CHECK(view_source(false,false,true,&shared_b1,&shared_b1,true)==4);} // paired frames keep the old rule
    // eviction keeps the newest buffers
    int more[4]{};for(auto& b:more) views.store(&b,View{});CHECK(!views.find(&b_r,got) && views.find(&more[3],got));
    if(failures) {std::printf("buffer_views_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: buffer_views_check (draw takes its b1's view; the newest view's E misplaces it by the eye parallax)\n");
    return 0;
}
