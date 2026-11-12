// v1223 pure check of the held thing's rigid local transform (src/weapon_draw_place.hpp RigidLocal, Place::place_one_view on a held side):
// a carried prop's draws are composed from F_draw, which is not always the F the publisher read; the measured L = inverse(F_read) * M then
// wobbles. Settles on 3 agreeing renders, rejects a single outlier, follows 4 agreeing renders of a new value, resets on a new held thing,
// and every draw of one render (both eyes) uses the same L, so the part sits at B * L exactly.
#include <cmath>
#include <cstdio>
#include "../src/weapon_draw_place.hpp"
using namespace d2vr::weapon_draw;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static Mat rigid(double yaw,double x,double y,double z) {Mat m=identity();const double c=std::cos(yaw),s=std::sin(yaw);m[0]=c;m[1]=-s;m[4]=s;m[5]=c;m[3]=x;m[7]=y;m[11]=z;return m;}
static std::array<float,15> raw_of(const Mat& m) {
    std::array<float,15> r{};for(int i=0;i<3;++i) r[static_cast<std::size_t>(i)]=static_cast<float>(m[static_cast<std::size_t>(i*4+3)]);
    for(int c=0;c<3;++c) for(int k=0;k<3;++k) r[static_cast<std::size_t>(3+c*3+k)]=static_cast<float>(m[static_cast<std::size_t>(k*4+c)]);
    r[12]=r[13]=r[14]=1;return r;
}
static double worst(const float* a,const Mat& b) {double w=0;for(int i=0;i<16;++i) w=std::max(w,std::abs(a[i]-b[static_cast<std::size_t>(i)]));return w;}
static Mat vp_of(double eye_x) {Mat v=identity();v[0]=1.2;v[5]=0;v[6]=1.2;v[9]=0;v[10]=0;v[11]=0.01;v[12]=0;v[13]=1;v[14]=0;v[15]=0.4;v[3]=eye_x;return v;}
int main() {
    const Mat l_true=rigid(0,0.03,0,-0.02);
    { // consensus on its own
        Mat out{};const std::uint64_t key=7;
        const auto sample=[&](std::uint32_t frame,const Mat& l) {const bool ok=RigidLocal::use(1,key,frame,l,out);RigidLocal::fold(1,0x55);return ok;};
        RigidLocal::fold(1,0x55);
        CHECK(!sample(1,l_true) && !sample(2,l_true));           // not settled yet: the caller keeps the measured L
        CHECK(!sample(3,l_true));                                  // the third agreeing render settles at the fold
        CHECK(sample(4,l_true) && distance(out,l_true)<1e-12);
        Mat off=l_true;off[3]+=0.012;                               // one render composed from another frame: 12 mm
        CHECK(sample(5,off) && distance(out,l_true)<1e-12);
        CHECK(sample(6,l_true) && distance(out,l_true)<1e-12);    // the outlier was rejected
        Mat moved=l_true;moved[7]+=0.004;                           // a genuine change: 4 renders agree on it
        for(std::uint32_t f=7;f<11;++f) {CHECK(sample(f,moved));CHECK(distance(out,l_true)<1e-12);}
        CHECK(sample(11,moved) && distance(out,moved)<1e-12);     // followed
        RigidLocal::fold(1,0x66);                                   // a new held thing
        CHECK(!RigidLocal::use(1,key,12,l_true,out));
        const auto j=RigidLocal::json();
        CHECK(j.find("\"outliers\":5")!=std::string::npos && j.find("\"follows\":1")!=std::string::npos && j.find("\"resets\":1")!=std::string::npos);
        std::printf("%s\n",j.c_str());
    }
    { // through the one-view placement of a held side: renders whose draws come from F_draw != F_read, both eyes of each render
        RigidLocal::clear();
        double v1222_worst=0,v1223_worst=0;
        for(std::uint32_t n=0;n<12;++n) {
            const Mat b=rigid(0.02*n,0.02*n,0.3,0.05);                     // the drawn hand's binding
            const Mat f_draw=rigid(0.02*n-0.05,0.02*n-0.04,0.3,0.05);       // what the engine composed this render from
            Mat f_read=f_draw;f_read[3]+=(n==1 || n==7 || n==10)*0.01;      // the publisher read a frame 1 cm off in renders 1, 7, 10
            const auto raw=raw_of(f_read);const std::array<const float*,2> raws{nullptr,raw.data()};const std::array<const Mat*,2> bounds{nullptr,&b};
            Place::publish_bound(200+n,raws,bounds,{0,0.5},{0,0x77});
            for(int eye=0;eye<2;++eye) {
                const Mat vp=vp_of(eye?0.032:-0.032);const Mat drawn=mul(mul(vp,f_draw),l_true);
                float cur[16],now[16],before[16];for(int i=0;i<16;++i) cur[i]=static_cast<float>(drawn[static_cast<std::size_t>(i)]);
                bool own=false;const auto r=Place::place_one_view(vp,vp,cur,now,before,own,nullptr,42);
                CHECK(r.side==1);
                const double e=worst(now,mul(mul(vp,b),l_true));
                (n>=5?v1223_worst:v1222_worst)=std::max(n>=5?v1223_worst:v1222_worst,e); // settles at the fold after render 4
            }
        }
        std::printf("held part vs VP*B*L: before settling (v1222 placement) %.4f, settled %.2g\n",v1222_worst,v1223_worst);
        CHECK(v1222_worst>1e-3);   // the measured L carried the 1 cm read error
        CHECK(v1223_worst<1e-5);   // both eyes of every settled render: exactly B * L, the outlier renders included
    }
    if(failures) {std::printf("rigid_local_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: rigid_local_check (settle, outlier, follow, reset, both eyes at B*L)\n");
    return 0;
}
