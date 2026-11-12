// v1193 pure check of src/buffer_views.hpp: render R's draws recorded while render R+1 (the other eye) already wrote its view take the
// view their bound b1 holds, so their rig-target E is R's. With the newest view (the old fallback) the retargeted point lands the eye
// separation's parallax away (the phantom sword of the v1190/v1191 swing dumps); with the b1 view it lands exactly.
#include <cmath>
#include <cstdio>
#include "../src/buffer_views.hpp"
#include "../src/rig_target.hpp"
#include "../src/dlss_jitter.hpp"
#include <set>
#include <vector>
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
     CHECK(view_source(false,true,true,&b_r,&shared_b1,false)==5);        // v1197: an unclassified b1, own view of another buffer: that own view
     CHECK(view_source(false,false,true,&shared_b1,&shared_b1,true)==4);} // paired frames keep the old rule
    // v1199: the overlap that broke v1197. Jobs: R-1 (right eye, job 7) on context A wrote its own b1 A1; R (left eye, job 9) records on
    // context A again, binding the engine's per-view globals buffer G, which no classified write on A filled (the render thread's list does);
    // meanwhile R+1 (right eye, job 10) on context B wrote B1 = the newest view anywhere. v1197 took A's own older view (R-1: wrong eye),
    // v1193-v1196 the newest (R+1: wrong eye). v1199 calls that draw uncertain; a draw that binds the b1 its own job wrote is certain.
    {int a1=0,g=0;const View r_prev{99,camera(.032)};
     const View own_a=r_prev;const std::uint64_t own_job=7,job_r=9;      // context A's last classified write: R-1's, into A1, in job 7
     const unsigned v1197=view_source(false,true,true,&a1,&g,false);     // R's draw binds G (never classified)
     CHECK(v1197==5 && own_a.frame!=r.frame);                          // v1197: own older view = R-1 = the other eye
     CHECK(!view_certain(true,&a1,&g,own_job,job_r,false));             // v1199: another buffer -> uncertain
     CHECK(!view_certain(true,&a1,&a1,own_job,job_r,false));            // the same buffer, but written by another job (another list) -> uncertain
     CHECK(!view_certain(true,&a1,&a1,0,0,false));                      // outside any pass job on a deferred context -> uncertain
     CHECK(view_certain(true,&a1,&a1,job_r,job_r,false));               // R's own job wrote the b1 it binds -> certain (R's view)
     CHECK(view_certain(true,&a1,&a1,0,0,true));                        // immediate context: writes and draws in execution order
     CHECK(!view_certain(false,&a1,&a1,job_r,job_r,false) && !view_certain(true,nullptr,nullptr,job_r,job_r,false));
     // and the retarget error each rule would have caused for R's draw (rows drawn with R's VP)
     rig_target::M4 inv_prev{};CHECK(rig_target::inverse(r_prev.vp,inv_prev));
     const rig_target::M4 e_prev=rig_target::mul(camera(-.032),inv_prev); // R-1's E: right -> left
     const double v1197_err=std::abs(via(e_prev)[0]-truth[0]);
     std::printf("v1197 own-older E error %.3f NDC (~%.0f px); v1199 leaves the draw alone\n",v1197_err,v1197_err*961);
     CHECK(v1197_err>.1);}
    // v1200: the render a draw's rows were built with, by content (rows_shear / pick_by_shear). Six renders alternate eyes 6.4 cm apart
    // (symmetric frusta as in the sim: the only per-render projection difference is the TAA jitter, uniform +-0.5 px at 1922 x 2070, the
    // hardest case); the eyes also turn a little between renders. Random rigid models (any orientation, non-uniform scale, 0.2-60 m away)
    // drawn with one render's VP in float (as the engine), picked among all six with the default tolerances.
    {std::uint32_t seed=12345;const auto rnd=[&] {seed=seed*1664525u+1013904223u;return (seed>>8)/16777216.0;};
     const auto mul4=[](const rig_target::M4& a,const rig_target::M4& b) {return rig_target::mul(a,b);};
     const auto rot=[](double ax,double ay,double az) {const double cx=std::cos(ax),sx=std::sin(ax),cy=std::cos(ay),sy=std::sin(ay),cz=std::cos(az),sz=std::sin(az);
         rig_target::M4 x{1,0,0,0, 0,cx,-sx,0, 0,sx,cx,0, 0,0,0,1},y{cy,0,sy,0, 0,1,0,0, -sy,0,cy,0, 0,0,0,1},z{cz,-sz,0,0, sz,cz,0,0, 0,0,1,0, 0,0,0,1};
         return rig_target::mul(rig_target::mul(z,y),x);};
     constexpr int R=6;rig_target::M4 vp[R];float vpf[R][16];ProjectionXY pr[R];
     for(int i=0;i<R;++i) {
         rig_target::M4 p{};p[0]=1/0.8;p[5]=1/0.8;p[10]=1.4e-6;p[11]=.01;p[14]=-1;
         p[2]=(rnd()-.5)*2/1922;p[6]=(rnd()-.5)*2/2070;                              // jitter (NDC)
         rig_target::M4 v=rot(.01*i,.3+.004*i,.002*i);v[3]=(i&1?.032:-.032)+100;v[7]=-40;v[11]=-12; // eye offset + world translation
         vp[i]=mul4(p,v);for(int k=0;k<16;++k) vpf[i][k]=static_cast<float>(vp[i][k]);pr[i]=projection_of(vpf[i]);CHECK(pr[i].ok);
     }
     int right=0,wrong=0,ambiguous=0,none=0;double worst_true=0;
     for(int n=0;n<20000;++n) {
         const int truth_i=static_cast<int>(rnd()*R)%R;
         rig_target::M4 m=rot(rnd()*6.3,rnd()*6.3,rnd()*6.3);const double sx=.2+rnd()*3,sy=.2+rnd()*3,sz=.2+rnd()*3;
         for(int q=0;q<3;++q) {m[q*4]*=sx;m[q*4+1]*=sy;m[q*4+2]*=sz;}
         m[3]=(rnd()-.5)*120;m[7]=(rnd()-.5)*120;m[11]=(rnd()-.5)*20;
         float rows[16];for(int q=0;q<4;++q) for(int c=0;c<4;++c) {float t=0;for(int k=0;k<4;++k) t+=vpf[truth_i][q*4+k]*static_cast<float>(m[k*4+c]);rows[q*4+c]=t;}
         rows[8]=rows[9]=rows[10]=0;rows[11]=.01f; // a layout's synthesized z row: never read
         double sh[R];for(int i=0;i<R;++i) sh[i]=rows_shear(pr[i],rows);
         worst_true=std::max(worst_true,sh[truth_i]);
         int second=-1;const int pick=pick_by_shear(sh,R,1e-4,4.0,2e-6,second);
         if(pick==truth_i) ++right;else if(pick>=0) ++wrong;else if(pick==-2) ++ambiguous;else ++none;
     }
     std::printf("content pick: %d right, %d wrong, %d ambiguous, %d none of 20000 (worst true-render shear %.2g)\n",right,wrong,ambiguous,none,worst_true);
     CHECK(wrong==0 && right>=19800);
     // rows that are not of the M * VP form (a view-model projection: other focal length) fit none
     {rig_target::M4 p{};p[0]=2.2;p[5]=2.2;p[10]=1.4e-6;p[11]=.01;p[14]=-1;const rig_target::M4 vm=mul4(p,rot(.0,.3,0));
      rig_target::M4 m=rot(.4,1.1,2.0);float rows[16];for(int k=0;k<16;++k) rows[k]=static_cast<float>(mul4(vm,m)[k]);
      double sh[R];for(int i=0;i<R;++i) sh[i]=rows_shear(pr[i],rows);int second=-1;
      std::printf("view-model rows: best shear %.3g -> pick %d\n",*std::min_element(sh,sh+R),pick_by_shear(sh,R,1e-4,4.0,2e-6,second));}
     // v1201 ties: renders 0 and 2 (eye 0) share one projection (repeated jitter), render 1 (eye 1) differs: a draw of render 2 names eye 0
     // and its newest render; if eye 1's projection equals too, it is ambiguous
     {const unsigned eyes[3]{0,1,0};const std::uint32_t frames[3]{10,11,12};bool tied=false;
      const double same_eye[3]{1e-7,3e-4,1.2e-7};CHECK(pick_by_shear_eyes(same_eye,eyes,frames,3,1e-4,4.0,2e-6,tied)==2 && tied);
      const double cross[3]{1e-7,1.1e-7,3e-4};CHECK(pick_by_shear_eyes(cross,eyes,frames,3,1e-4,4.0,2e-6,tied)==-2);
      const double clear[3]{3e-4,1e-7,4e-4};CHECK(pick_by_shear_eyes(clear,eyes,frames,3,1e-4,4.0,2e-6,tied)==1 && !tied);
      const double nofit[3]{.3,.2,.4};CHECK(pick_by_shear_eyes(nofit,eyes,frames,3,1e-4,4.0,2e-6,tied)==-1);}
    }
    // v1202: one-view jitter. Renders alternate eyes (either eye first), each eye's phase counts from the same reset; over 10k renders no two
    // renders within 3 of each other share a sample, for the engine pattern (8) and every Halton owner length (8..32); each eye still visits
    // every phase in order (same coverage as before). Without the offset the neighbours share (the v1201 ties).
    for(int first=0;first<2;++first) for(std::uint32_t phases=8;phases<=32;++phases) for(int dlss=0;dlss<2;++dlss) {
        if(!dlss && phases!=8) continue;
        std::vector<std::array<float,2>> s_new,s_old;
        for(int n=0;n<10000;++n) {const int eye=(n+first)&1;const std::uint32_t phase=static_cast<std::uint32_t>(n/2);
            s_new.push_back(taa_sample_px(alternate_jitter_index(phase,eye,phases),dlss!=0,phases));s_old.push_back(taa_sample_px(phase,dlss!=0,phases));}
        int clash=0,clash_old=0;
        for(int n=0;n<10000;++n) for(int d=1;d<=3 && n+d<10000;++d) {clash+=s_new[static_cast<std::size_t>(n)]==s_new[static_cast<std::size_t>(n+d)];clash_old+=s_old[static_cast<std::size_t>(n)]==s_old[static_cast<std::size_t>(n+d)];}
        std::set<std::array<float,2>> eye0,eye1;for(int n=0;n<static_cast<int>(2*phases);++n) ((n+first)&1?eye1:eye0).insert(s_new[static_cast<std::size_t>(n)]);
        CHECK(clash==0 && clash_old>0 && eye0.size()==phases && eye1.size()==phases);
        if(clash || eye0.size()!=phases) std::printf("jitter phases %u dlss %d: clash %d (old %d), coverage %zu/%zu\n",phases,dlss,clash,clash_old,eye0.size(),eye1.size());
    }
    std::printf("one-view jitter: no neighbour within 3 renders shares a sample (8..32 phases), each eye covers every phase\n");
    // eviction keeps the newest buffers
    int more[4]{};for(auto& b:more) views.store(&b,View{});CHECK(!views.find(&b_r,got) && views.find(&more[3],got));
    if(failures) {std::printf("buffer_views_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: buffer_views_check (draw takes its b1's view; the newest view's E misplaces it by the eye parallax)\n");
    return 0;
}
