// v1218 pure check of a placed (held) draw's previous rows (src/object_motion.hpp PrevMvpTable::placed_previous). A carried prop is re-placed
// at its draws: submitted rows = VP * B * inverse(F_read) * (F_draw * K), where F_draw is the frame the engine composed the draw from and
// F_read the frame the publisher read. The previous rows must be what this eye's previous render SUBMITTED for the same draw; the v1216
// prediction prevVP * B_prev * inverse(F_read) * M is right only while inverse(F_read) * F_draw stays the same between renders.
// Paired: L then R each frame, the engine's per-object previous rows = the last view's current rows. Alternate: one eye per frame, the
// previous render of an eye = two frames back.
#include "../src/object_motion.hpp"
#include <cstdio>
#include <memory>
using namespace d2vr;
using Mat=weapon_draw::Mat;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static Mat rigid(double yaw,double x,double y,double z) {Mat m=weapon_draw::identity();const double c=std::cos(yaw),s=std::sin(yaw);m[0]=c;m[1]=-s;m[4]=s;m[5]=c;m[3]=x;m[7]=y;m[11]=z;return m;}
static Mat vp_of(unsigned eye,unsigned frame) { // a camera 0.4 m behind, eyes 6.4 cm apart, the head moving 1 cm per frame, per-frame jitter
    Mat v=weapon_draw::identity();v[0]=1.2;v[5]=0;v[6]=1.2;v[9]=0;v[10]=0;v[11]=0.01;v[12]=0;v[13]=1;v[14]=0;v[15]=0.4;
    v[3]=(eye?0.032:-0.032)+0.01*frame;v[2]=1e-4*((frame*7+eye*3)%5);return v;
}
static void rows(const Mat& m,float* out) {for(int i=0;i<16;++i) out[i]=static_cast<float>(m[static_cast<std::size_t>(i)]);}
static double worst(const float* a,const float* b) {double w=0;for(int i=0;i<16;++i) w=std::max(w,static_cast<double>(std::abs(a[i]-b[i])));return w;}
struct Frame {Mat b,f_read,f_draw;};
static Frame frame_of(unsigned n) { // the hand moves 2 cm per frame; the read frame lags the drawn one by an error that changes every frame
    Frame f;f.b=rigid(0.02*n,0.02*n,0.3,0.05);f.f_draw=rigid(0.02*n-0.05,0.02*n-0.04,0.3,0.05);
    f.f_read=f.f_draw;f.f_read[3]+=(n%3)*0.01;f.f_read[7]-=(n%2)*0.008;return f;
}
static const Mat k=rigid(0,0.03,0.0,-0.02); // the part's offset in its frame
static void submitted_of(unsigned eye,unsigned n,float* out) {
    const Frame f=frame_of(n);Mat inv{};weapon_draw::inverse(f.f_read,inv);
    rows(weapon_draw::mul(weapon_draw::mul(weapon_draw::mul(vp_of(eye,n),f.b),inv),weapon_draw::mul(f.f_draw,k)),out);
}
static void engine_of(unsigned eye,unsigned n,float* out) {rows(weapon_draw::mul(weapon_draw::mul(vp_of(eye,n),frame_of(n).f_draw),k),out);}
static void predicted_of(unsigned eye,unsigned n,unsigned own,float* out) { // the v1216 previous: prevVP * B_prev * inverse(F_read) * M
    const Frame f=frame_of(n),p=frame_of(own);Mat inv{};weapon_draw::inverse(f.f_read,inv);
    rows(weapon_draw::mul(weapon_draw::mul(weapon_draw::mul(vp_of(eye,own),p.b),inv),weapon_draw::mul(f.f_draw,k)),out);
}
int main() {
    double v1216_worst=0;
    { // paired
        auto table=std::make_unique<PrevMvpTable>();auto placed=std::make_unique<PrevMvpTable>();
        float cache[16]{};bool cached=false;unsigned proven=0;
        for(unsigned n=10;n<30;++n) for(unsigned eye=0;eye<2;++eye) {
            float cur[16],sub[16],out[16],want[16],old[16];engine_of(eye,n,cur);submitted_of(eye,n,sub);
            float engine[16];std::memcpy(engine,cached?cache:cur,64);std::memcpy(cache,cur,64);cached=true;
            for(int pass=0;pass<2;++pass) { // a depth and a velocity pass of the same draw
                unsigned why=99;const bool ok=table->placed_previous(*placed,eye,n,n-1,false,cur,engine,sub,out,&why);
                if(n==10) {CHECK(!ok);CHECK(why==(eye?unsigned{PrevMvpTable::unproven}:unsigned{PrevMvpTable::kinds}));continue;} // v1226 reasons
                CHECK(ok);submitted_of(eye,n-1,want);CHECK(ok && worst(out,want)<1e-6);proven+=ok;
                predicted_of(eye,n,n-1,old);v1216_worst=std::max(v1216_worst,worst(old,want));
            }
        }
        CHECK(proven==2*2*19);
    }
    { // alternate: eye n%2 renders frame n; its previous render is n-2 (a skipped counter breaks the chain -> not proven)
        auto table=std::make_unique<PrevMvpTable>();auto placed=std::make_unique<PrevMvpTable>();
        float cache[16]{};bool cached=false;unsigned proven=0;
        for(unsigned n=40;n<60;++n) {
            if(n==50) continue; // no render 50: eye 0's 52 has own_frame 50, nothing recorded there
            const unsigned eye=n%2;float cur[16],sub[16],out[16],want[16];engine_of(eye,n,cur);submitted_of(eye,n,sub);
            float engine[16];std::memcpy(engine,cached?cache:cur,64);std::memcpy(cache,cur,64);cached=true;
            const bool ok=table->placed_previous(*placed,eye,n,n-2,true,cur,engine,sub,out);
            if(n<42 || n==52) {CHECK(!ok);continue;}
            CHECK(ok);submitted_of(eye,n-2,want);CHECK(ok && worst(out,want)<1e-6);proven+=ok;
        }
        CHECK(proven==16);
    }
    std::printf("v1216 predicted previous vs the submitted previous: worst row error %.4f (clip units)\n",v1216_worst);
    CHECK(v1216_worst>1e-3); // the old prediction is wrong in this case (the lab's reason to exist)
    if(failures) {std::printf("placed_history_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: placed_history_check (paired and alternate previous rows = the eye's previous submitted rows; reset/gap -> not proven)\n");
    return 0;
}
