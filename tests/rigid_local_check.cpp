// v1223/v1231 pure check of the held thing's rigid local transform (src/weapon_draw_place.hpp RigidLocal, Place::place_one_view on a held side).
// A carried physics prop's draws are composed from F_draw, often one or two ticks away from the F the publisher read; the measured
// L = inverse(F_read) * M is then off by the body's motion over that time. The true L recurs whenever they are in step. Consensus: settles on a
// value that recurred 3 times (not necessarily consecutively), never on a scattered mismatched one, keeps it while it still recurs, follows a
// genuine change (old value gone for a window), resets on a new held thing, and every draw of one render (both eyes) uses the same L.
#include <cmath>
#include <cstdio>
#include "../src/weapon_draw_place.hpp"
using namespace d2vr::weapon_draw;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
static Mat rigid(double yaw,double x,double y,double z) {Mat m=identity();const double c=std::cos(yaw),s=std::sin(yaw);m[0]=c;m[1]=-s;m[4]=s;m[5]=c;m[3]=x;m[7]=y;m[11]=z;return m;}
static Mat axis_angle(double ax,double ay,double az,double a) { // Rodrigues, unit axis
    const double c=std::cos(a),s=std::sin(a),t=1-c;Mat m=identity();
    m[0]=t*ax*ax+c;m[1]=t*ax*ay-s*az;m[2]=t*ax*az+s*ay;m[4]=t*ax*ay+s*az;m[5]=t*ay*ay+c;m[6]=t*ay*az-s*ax;m[8]=t*ax*az-s*ay;m[9]=t*ay*az+s*ax;m[10]=t*az*az+c;return m;
}
static std::array<float,15> raw_of(const Mat& m) {
    std::array<float,15> r{};for(int i=0;i<3;++i) r[static_cast<std::size_t>(i)]=static_cast<float>(m[static_cast<std::size_t>(i*4+3)]);
    for(int c=0;c<3;++c) for(int k=0;k<3;++k) r[static_cast<std::size_t>(3+c*3+k)]=static_cast<float>(m[static_cast<std::size_t>(k*4+c)]);
    r[12]=r[13]=r[14]=1;return r;
}
static double worst(const float* a,const Mat& b) {double w=0;for(int i=0;i<16;++i) w=std::max(w,std::abs(a[i]-b[static_cast<std::size_t>(i)]));return w;}
static Mat vp_of(double eye_x) {Mat v=identity();v[0]=1.2;v[5]=0;v[6]=1.2;v[9]=0;v[10]=0;v[11]=0.01;v[12]=0;v[13]=1;v[14]=0;v[15]=0.4;v[3]=eye_x;return v;}
static Mat inverse_of(const Mat& m) {Mat o{};inverse(m,o);return o;}
static bool same(const Mat& a,const Mat& b) {return distance(a,b)<=RigidLocal::agree_m && angle(a,b)<=RigidLocal::agree_rad;}
static unsigned rng=12345u;static double uni() {rng=rng*1664525u+1013904223u;return (rng>>8)*(1.0/16777216.0);}
static Mat mismatch_of(const Mat& l) { // the body's motion over one or two 16 ms ticks at 0.5-3 m/s and up to 4 rad/s, in its own frame
    const double dt=(uni()<.5?1:2)*.016,v=.5+2.5*uni(),w=4*uni();
    double ax=uni()-.5,ay=uni()-.5,az=uni()-.5;const double n=std::sqrt(ax*ax+ay*ay+az*az)+1e-9;ax/=n;ay/=n;az/=n;
    double dx=uni()-.5,dy=uni()-.5,dz=uni()-.5;const double dn=std::sqrt(dx*dx+dy*dy+dz*dz)+1e-9;
    Mat e=axis_angle(ax,ay,az,w*dt);e[3]=dx/dn*v*dt;e[7]=dy/dn*v*dt;e[11]=dz/dn*v*dt;
    return mul(e,l);
}
int main() {
    const Mat l_true=rigid(0,0.03,0,-0.02);
    Mat out{};const std::uint64_t key=7;std::uint32_t frame=1;
    const auto sample=[&](const Mat& l,bool& ok) {ok=RigidLocal::use(1,key,frame,l,out);RigidLocal::fold(1,0x55);++frame;return out;};
    bool ok=false;
    { // rules
        RigidLocal::clear();RigidLocal::fold(1,0x55);
        Mat off1=l_true;off1[3]+=.012;Mat off2=l_true;off2[7]-=.02;
        sample(l_true,ok);CHECK(!ok);sample(off1,ok);CHECK(!ok);
        sample(l_true,ok);CHECK(!ok);                                     // 2 hits now, folded after this draw
        sample(off2,ok);CHECK(ok && same(out,l_true));                     // early: the best candidate (2 hits), not the raw sample
        sample(l_true,ok);                                                  // third non-consecutive hit: settled at this fold
        sample(off1,ok);CHECK(ok && same(out,l_true));
        // a steady mismatched stretch while the true value keeps recurring: never followed
        for(int r=0;r<30;++r) {sample(r%5==4?l_true:off2,ok);CHECK(ok && same(out,l_true));}
        // a genuine change: the old value stops recurring; followed only after a whole window without it
        Mat moved=l_true;moved[7]+=.004;
        unsigned followed_at=0;
        for(unsigned r=0;r<RigidLocal::window+10;++r) {sample(moved,ok);if(!followed_at && same(out,moved)) followed_at=r;}
        CHECK(followed_at>=RigidLocal::window && followed_at<=RigidLocal::window+3);
        RigidLocal::fold(1,0x66);CHECK(!RigidLocal::use(1,key,frame,l_true,out)); // a new held thing
        std::printf("%s\n",RigidLocal::json().c_str());
    }
    { // streams with 30 / 60 / 85 % mismatched samples, several seeds: settle on the true L within ~0.5 s (45 renders at 90 Hz), never on another value
        const double shares[3]={.30,.60,.85};
        for(const double share:shares) {
            unsigned worst_settle=0,wrong=0,never=0;
            for(unsigned seed=1;seed<=20;++seed) {
                rng=seed*2654435761u;RigidLocal::clear();RigidLocal::fold(1,0x80+seed);
                unsigned settled_at=0;const auto settles0=RigidLocal::settles();
                for(unsigned r=1;r<=180;++r) {
                    Mat s=l_true;if(uni()<share) s=mismatch_of(l_true);else s[3]+=1e-7*(uni()-.5);
                    sample(s,ok);
                    if(ok && !same(out,l_true)) ++wrong;
                    if(!settled_at && RigidLocal::settles()>settles0) settled_at=r;
                }
                if(!settled_at) ++never;else worst_settle=std::max(worst_settle,settled_at);
            }
            std::printf("mismatch %.0f%%: slowest settle %u renders (20 seeds), never settled %u, draws placed with a wrong L %u\n",share*100,worst_settle,never,wrong);
            CHECK(never==0 && wrong==0);
            CHECK(worst_settle<=(share>.8?70u:45u));
        }
    }
    { // through the one-view placement of a held side: both eyes of each render at B * L, the read frame off in some renders
        RigidLocal::clear();
        double before=0,after=0;
        for(std::uint32_t n=0;n<12;++n) {
            const Mat b=rigid(0.02*n,0.02*n,0.3,0.05);
            const Mat f_draw=rigid(0.02*n-0.05,0.02*n-0.04,0.3,0.05);
            Mat f_read=f_draw;f_read[3]+=(n==1 || n==7 || n==10)*0.01;
            const auto raw=raw_of(f_read);const std::array<const float*,2> raws{nullptr,raw.data()};const std::array<const Mat*,2> bounds{nullptr,&b};
            Place::publish_bound(200+n,raws,bounds,{0,0.5},{0,0x77});
            for(int eye=0;eye<2;++eye) {
                const Mat vp=vp_of(eye?0.032:-0.032);const Mat drawn=mul(mul(vp,f_draw),l_true);
                float cur[16],now[16],prev[16];for(int i=0;i<16;++i) cur[i]=static_cast<float>(drawn[static_cast<std::size_t>(i)]);
                bool own=false;const auto r=Place::place_one_view(vp,vp,cur,now,prev,own,nullptr,42);
                CHECK(r.side==1);
                const double e=worst(now,mul(mul(vp,b),l_true));
                (n>=2?after:before)=std::max(n>=2?after:before,e);
            }
        }
        std::printf("held part vs VP*B*L: first renders %.4f, from the early candidate on %.2g\n",before,after);
        CHECK(before>1e-3);   // render 1 had only raw samples (one in step, one 1 cm off)
        CHECK(after<1e-5);    // both eyes of every later render: exactly B * L, the off renders included
    }
    { // v1237: a side whose 8 clusters were all taken by draws that are gone (passes / meshes drawn earlier in the hold) still settles a new draw.
      // Before v1237 a full side refused every new key until a cluster sat unused for 240 renders (user v1233 session: 33 % unsettled draws).
        RigidLocal::clear();RigidLocal::fold(1,0x91);
        for(std::uint64_t k=0;k<RigidLocal::clusters;++k) RigidLocal::use(1,100+k,frame,l_true,out);
        RigidLocal::fold(1,0x91);++frame;
        unsigned settled_at=0;
        for(unsigned r=1;r<=12;++r) {const bool placed=RigidLocal::use(1,200,frame,l_true,out);RigidLocal::fold(1,0x91);++frame;if(placed && !settled_at && same(out,l_true)) settled_at=r;}
        std::printf("full side, new draw: placed with the settled L from render %u\n",settled_at);
        CHECK(settled_at>0 && settled_at<=8);
    }
    { // v1237: a part bound to the held thing (a helmet on a carried head) has its own axes: matched by its child frame, moved by the thing's delta
        RigidLocal::clear();
        const Mat f=rigid(0.3,0.1,0.3,0.05),b=rigid(0.35,0.12,0.31,0.06);
        const Mat child=mul(f,axis_angle(0,1,0,.9));   // the helmet's model frame: 52 degrees off the head's axes
        const auto raw=raw_of(f);const std::array<const float*,2> raws{nullptr,raw.data()};const std::array<const Mat*,2> bounds{nullptr,&b};
        const Mat vp=vp_of(0.032);
        Mat drawn=mul(vp,child);float cur[16],now[16],prev[16];for(int i=0;i<16;++i) cur[i]=static_cast<float>(drawn[static_cast<std::size_t>(i)]);
        bool own=false;
        Place::publish_bound(400,raws,bounds,{0,0.5},{0,0x99});
        auto r=Place::place_one_view(vp,vp,cur,now,prev,own,nullptr,43);
        CHECK(r.side<0 && r.near_side==1 && !r.near_like);   // without the child frame: left at the engine pose (counted near-other)
        Place::stage(1,Place::kind_severed,&child,1);
        Place::publish_bound(401,raws,bounds,{0,0.5},{0,0x99});
        r=Place::place_one_view(vp,vp,cur,now,prev,own,nullptr,43);
        CHECK(r.side==1 && r.extra);
        const double e=worst(now,mul(mul(mul(vp,b),inverse_of(f)),child));
        std::printf("child frame placed with the held thing's delta: error %.2g\n",e);
        CHECK(e<1e-5);
        Place::census(r,true);
        Place::publish_bound(402,raws,bounds,{0,0.5},{0,0x99});   // closes render 401 (kind severed, placed, with previous rows)
        const std::string census=Place::census_json();std::printf("%s\n",census.c_str());
        CHECK(census.find("\"severed\":{\"renders\":1,\"mixed\":0,\"no_mv\":0,\"unplaced\":0,\"draws\":{\"placed\":1,\"with_prev\":1,\"child\":1")!=std::string::npos);
    }
    { // v1241: one mesh drawn by a depth pass and a colour pass (two keys, two clusters) whose samples differ by float noise (1e-6 m, 1e-5 deg),
      // in either order: both passes of every render write bit-identical rows (a forward pass testing depth against the prepass loses no pixel);
      // a genuinely different part 5 mm away keeps its own L.
        RigidLocal::clear();
        const auto noisy=[&](const Mat& l) {Mat n=mul(axis_angle(.6,.48,.64,(uni()-.5)*2e-5*0.017453292519943295),l);n[3]+=(uni()-.5)*2e-6;n[7]+=(uni()-.5)*2e-6;n[11]+=(uni()-.5)*2e-6;return n;};
        Mat l_part=l_true;l_part[3]+=.005;
        const auto [pairs0,spread0]=RigidLocal::pass_counts();
        unsigned differing=0,raw_differing=0,part_far=0;double part_err=0;
        for(std::uint32_t n=0;n<60;++n) {
            const Mat b=rigid(0.01*n,0.01*n,0.3,0.05),f=rigid(0.01*n-0.05,0.01*n-0.04,0.3,0.05);
            const auto raw=raw_of(f);const std::array<const float*,2> raws{nullptr,raw.data()};const std::array<const Mat*,2> bounds{nullptr,&b};
            Place::publish_bound(600+n,raws,bounds,{0,0.5},{0,0xa1});
            for(int eye=0;eye<2;++eye) {
                const Mat vp=vp_of(eye?0.032:-0.032);
                float placed[2][16]{},raw_rows[2][16]{};
                for(int k=0;k<2;++k) {
                    const int pass=(n+static_cast<unsigned>(eye))%2?1-k:k;   // the depth pass first in some renders, the colour pass in others
                    const Mat l=noisy(l_true);const Mat drawn=mul(mul(vp,f),l);const Mat ideal=mul(vp,mul(b,l));
                    float cur[16],prev[16];for(int i=0;i<16;++i) {cur[i]=static_cast<float>(drawn[static_cast<std::size_t>(i)]);raw_rows[pass][i]=static_cast<float>(ideal[static_cast<std::size_t>(i)]);}
                    bool own=false;const auto r=Place::place_one_view(vp,vp,cur,placed[pass],prev,own,nullptr,pass?12:11);
                    CHECK(r.side==1);
                }
                if(std::memcmp(placed[0],placed[1],sizeof placed[0])) ++differing;
                if(std::memcmp(raw_rows[0],raw_rows[1],sizeof raw_rows[0])) ++raw_differing;
                const Mat drawn=mul(mul(vp,f),noisy(l_part));float cur[16],now[16],prev[16];for(int i=0;i<16;++i) cur[i]=static_cast<float>(drawn[static_cast<std::size_t>(i)]);
                bool own=false;Place::place_one_view(vp,vp,cur,now,prev,own,nullptr,13);
                part_err=std::max(part_err,worst(now,mul(mul(vp,b),l_part)));
                if(worst(now,mul(mul(vp,b),l_true))>1e-3) ++part_far;
            }
        }
        const auto [pairs1,spread1]=RigidLocal::pass_counts();
        std::printf("depth/colour passes: %u of 120 eye-renders differ (each pass with its own sample: %u); pass pairs %llu, pass_spread_renders %llu; part 5 mm away: error %.2g, kept apart in %u of 120\n",
            differing,raw_differing,static_cast<unsigned long long>(pairs1-pairs0),static_cast<unsigned long long>(spread1-spread0),part_err,part_far);
        CHECK(differing==0);
        CHECK(raw_differing>0);   // the noise alone would have split the passes
        CHECK(spread1==spread0 && pairs1-pairs0>=120);
        CHECK(part_far==120 && part_err<1e-5);
    }
    if(failures) {std::printf("rigid_local_check: %d failure(s)\n",failures);return 1;}
    std::printf("PASS: rigid_local_check (consensus settle, early candidate, steady mismatch not followed, genuine change, reset, 30/60/85%% mismatch, both eyes, full side recycles, child frames, one bitwise L across passes)\n");
    return 0;
}
