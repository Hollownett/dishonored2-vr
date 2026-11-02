// v931: Place certifies which eye a weapon draw really renders (its cached previous MVP is the same instance in the other view), finds a held
// weapon's parts by orientation and reach, and re-places them in the hand each render draws, with a previous MVP that follows that hand.
// A simulated stereo engine renders view 0 then view 1 per render (a draw's cache = the MVP of the last view that drew it), with canted eyes,
// skipped engine frames, parts at fixed offsets from the weapon's tick frame, and a hand sampled per frame while the tick reads a stale one.
#include "../src/weapon_draw_place.hpp"
#include <chrono>
#include <cstdio>
#include <map>
#include <vector>

using namespace d2vr::weapon_draw;
static int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)

static Mat rot(double ax,double ay,double az,double angle) { // Rodrigues, column vectors
    const double n=std::sqrt(ax*ax+ay*ay+az*az);ax/=n;ay/=n;az/=n;
    const double c=std::cos(angle),s=std::sin(angle),t=1-c;
    Mat m=identity();
    m[0]=t*ax*ax+c;m[1]=t*ax*ay-s*az;m[2]=t*ax*az+s*ay;
    m[4]=t*ax*ay+s*az;m[5]=t*ay*ay+c;m[6]=t*ay*az-s*ax;
    m[8]=t*ax*az-s*ay;m[9]=t*ay*az+s*ax;m[10]=t*az*az+c;
    return m;
}
static Mat rigid(Mat r,double x,double y,double z) {r[3]=x;r[7]=y;r[11]=z;return r;}
static Mat inv(const Mat& m) {Mat o{};CHECK(inverse(m,o));return o;}
static std::array<float,15> raw_of(const Mat& m) {
    std::array<float,15> f{};
    for(int r=0;r<3;++r) f[static_cast<std::size_t>(r)]=float(m[static_cast<std::size_t>(r*4+3)]);
    for(int c=0;c<3;++c) for(int r=0;r<3;++r) f[static_cast<std::size_t>(3+c*3+r)]=float(m[static_cast<std::size_t>(r*4+c)]);
    f[12]=f[13]=f[14]=1;
    return f;
}
static std::array<float,16> floats(const Mat& m) {std::array<float,16> f{};for(std::size_t i=0;i<16;++i) f[i]=float(m[i]);return f;}
static Mat from_floats(const std::array<float,16>& f) {return widen(f.data());}
static double dist(const Mat& a,const Mat& b) {return std::hypot(a[3]-b[3],std::hypot(a[7]-b[7],a[11]-b[11]));}

// Reverse-Z infinite projection (z row 0,0,0,near) with the frame's jitter, times a rigid eye view.
static Mat vp_of(const Mat& eye_pose,int frame) {
    Mat p{};p[0]=1.2;p[2]=.1+.0007*std::sin(frame*1.7);p[5]=1.6;p[6]=-.05+.0007*std::cos(frame*2.3);p[11]=.05;p[14]=-1;
    return mul(p,inv(eye_pose));
}
struct Wp {Mat vp[2]{},inv[2]{};};   // the two views of an engine frame
static std::map<std::uint32_t,Wp> frames_seen;
static const Views* provider(std::uint32_t frame,std::uint32_t previous) {
    static Views v;
    v=Views{};v.prev_frame=previous;
    const auto it=frames_seen.find(frame);
    if(it==frames_seen.end()) return nullptr;
    for(unsigned e=0;e<2;++e) {v.ok[e]=true;v.vp[e]=it->second.vp[e];v.inv[e]=it->second.inv[e];}
    if(previous) if(const auto p=frames_seen.find(previous);p!=frames_seen.end()) for(unsigned e=0;e<2;++e) {v.prev_ok[e]=true;v.prev_vp[e]=p->second.vp[e];v.prev_inv[e]=p->second.inv[e];}
    return &v;
}

int main() {
    const Mat g=rigid(rot(.3,1,.2,.4),.05,.02,-.03);                          // grip: the weapon frame in the hand
    const auto hand_at=[](int t) {
        const double s=t/90.0;
        return mul(rigid(rot(0,0,1,.8*s),-91.8,-92.5,4.9),rigid(rot(1,0,0,.1*std::sin(6*s)),.3+.1*s+.01*std::sin(11*s),-.2+.02*std::sin(9*s),-.5+.015*std::sin(7*s)));
    };
    const Mat parts[3]={identity(),rigid(identity(),.11,0,.065),mul(rigid(identity(),-.03,.02,.01),rot(0,1,0,.0012))};   // fixed offsets (one slightly rotated)
    std::uint32_t engine_frame=25000,seed=12345;
    std::map<std::uint32_t,std::pair<Mat,Mat>> render_info;                   // frame -> (placed matrix, F)
    std::uint32_t previous_frame=0;
    int certified[2]={0,0},uncertified_first=0,checks=0;
    double worst_cur=0,worst_prev=0,engine_wobble_lo=1e9,engine_wobble_hi=-1e9;
    for(int render=0;render<160;++render) {
        seed=seed*1664525u+1013904223u;engine_frame+=1+((seed>>16)&1);        // engine frames are skipped now and then
        const int t=int(engine_frame-25000);
        const Mat image=hand_at(t),read=hand_at(t-t%3);
        const Mat tick_frame=mul(read,g),delta=mul(image,inv(read)),placed=mul(delta,tick_frame);
        Mat looking=identity();looking[0]=0;looking[2]=-1;looking[4]=-1;looking[5]=0;looking[6]=0;looking[9]=1;looking[10]=0;   // eye space: right = -y, up = +z, forward = +x
        const Mat head=mul(rigid(rot(0,0,1,.8*t/90.0),-91.8,-92.5,5.0),looking);
        Wp w;
        for(unsigned e=0;e<2;++e) {   // canted eyes: +-6 degrees about the vertical, 64 mm apart; the float VP is what the engine stores
            const Mat pose=mul(head,mul(rigid(identity(),e?.032:-.032,0,0),rot(0,0,1,e?-.105:.105)));
            const auto f=floats(vp_of(pose,int(engine_frame)));w.vp[e]=from_floats(f);w.inv[e]=inv(w.vp[e]);
        }
        frames_seen[engine_frame]=w;
        const auto raw=raw_of(tick_frame);
        Place::publish(engine_frame,{raw.data(),nullptr},{&delta,nullptr});
        engine_wobble_lo=std::min(engine_wobble_lo,dist(tick_frame,image));engine_wobble_hi=std::max(engine_wobble_hi,dist(tick_frame,image));
        // the engine: view 0 first, then view 1, the cache of an instance is the MVP of the last view that drew it
        static std::array<Mat,3> cache{};static std::array<bool,3> have{};
        for(unsigned e=0;e<2;++e) for(unsigned j=0;j<3;++j) {
            const Mat model=mul(tick_frame,parts[j]);
            const Mat cur=mul(w.vp[e],model);
            const Mat prev_rows=have[j]?cache[j]:cur;
            cache[j]=cur;have[j]=true;
            const auto c=floats(cur),p=floats(prev_rows);
            std::array<float,16> o{},q{};
            const Result r=Place::place(engine_frame,provider,c.data(),p.data(),o.data(),q.data());
            const bool first_view0=(render==0 && e==0);
            if(first_view0) {CHECK(r.side<0 && r.stage==no_certificate);++uncertified_first;continue;}   // nothing cached yet: prev == cur proves no eye
            ++checks;
            CHECK(r.side==0 && r.eye==int(e));
            if(r.side!=0) continue;
            ++certified[e];
            const Mat want=mul(mul(w.vp[e],delta),model);
            for(std::size_t i=0;i<16;++i) {worst_cur=std::max(worst_cur,std::abs(o[i]-want[i])/(1+std::abs(want[i])));}
            if(render>0) {   // the previous render's placed weapon under the previous render's view of this eye
                const auto info=render_info.at(previous_frame);
                const Mat before=mul(mul(frames_seen.at(previous_frame).vp[e],info.first),mul(inv(info.second),model));
                // (the previous placed matrix moved onto this part: placed(p) * inverse(F(p)) * F(p) * L = placed(p) * L)
                const Mat want_prev=mul(mul(frames_seen.at(previous_frame).vp[e],info.first),parts[j]);
                (void)before;
                for(std::size_t i=0;i<16;++i) worst_prev=std::max(worst_prev,std::abs(q[i]-want_prev[i])/(1+std::abs(want_prev[i])));
            }
        }
        render_info[engine_frame]={placed,tick_frame};previous_frame=engine_frame;
    }
    std::printf("certified %d/%d (view 0: %d, view 1: %d), worst relative error: current rows %.2e previous rows %.2e; tick frame to hand distance varies %.2f mm\n",
        certified[0]+certified[1],checks,certified[0],certified[1],worst_cur,worst_prev,(engine_wobble_hi-engine_wobble_lo)*1000);
    CHECK(certified[0]+certified[1]==checks && worst_cur<2e-4 && worst_prev<2e-4 && engine_wobble_hi-engine_wobble_lo>.005);

    // Draws that are not the weapon's, or cannot be certified, keep the engine's rows.
    {
        const Wp& w=frames_seen.at(engine_frame);
        const Mat tick_frame=render_info.at(engine_frame).second;
        std::array<float,16> o{},q{};
        // another object, a weapon part out of reach, a draw whose cached previous MVP is something else
        const Mat unrelated=rigid(rot(1,2,3,.7),-91.5,-92.2,5.2),near=mul(tick_frame,parts[1]);
        const auto c_un=floats(mul(w.vp[1],unrelated)),p_un=floats(mul(w.vp[0],unrelated));
        CHECK(Place::place(engine_frame,provider,c_un.data(),p_un.data(),o.data(),q.data()).side<0);
        const auto c_cert=floats(mul(w.vp[1],near)),p_cert=floats(mul(w.vp[0],near));   // a certifiable draw of the weapon, out of a short reach
        Place::reach.store(.05);
        const Result far=Place::place(engine_frame,provider,c_cert.data(),p_cert.data(),o.data(),q.data());
        Place::reach.store(.8);
        CHECK(far.side<0 && far.stage==far_away);
        CHECK(Place::place(engine_frame,provider,c_cert.data(),p_cert.data(),o.data(),q.data()).side==0);
        const auto c_near=floats(mul(w.vp[1],near)),p_bad=floats(mul(w.vp[0],mul(rigid(identity(),.02,0,0),near)));   // cached MVP of something else
        const Result bad=Place::place(engine_frame,provider,c_near.data(),p_bad.data(),o.data(),q.data());
        CHECK(bad.side<0 && bad.stage==no_certificate);
        // v960: the same uncertifiable draw is placed in the eye it is tagged with (its orientation fits that eye), not in the other, canted one
        {const Result t=Place::place(engine_frame,provider,c_near.data(),p_bad.data(),o.data(),q.data(),1);
         CHECK(t.side==0 && t.eye==1 && t.stage==placed_tagged);
         const Mat want=mul(mul(w.vp[1],render_info.at(engine_frame).first),parts[1]);double e=0;
         for(std::size_t i=0;i<16;++i) e=std::max(e,std::abs(o[i]-want[i])/(1+std::abs(want[i])));
         CHECK(e<2e-4);
         CHECK(Place::place(engine_frame,provider,c_near.data(),p_bad.data(),o.data(),q.data(),0).side<0);
         // v961: a draw placed without the previous render's views has no motion (previous rows = current rows), not the hand's world displacement
         const auto no_previous=[](std::uint32_t f,std::uint32_t) -> const Views* {return provider(f,0);};
         const Result np=Place::place(engine_frame,no_previous,c_near.data(),p_bad.data(),o.data(),q.data(),1);
         CHECK(np.side==0 && np.stage==placed_tagged);
         double still=0;for(std::size_t i=0;i<16;++i) still=std::max(still,double(std::abs(o[i]-q[i])/(1+std::abs(o[i]))));
         CHECK(still<1e-6);}
        // a frame nobody published, and a slot reused by a newer frame
        CHECK(Place::place(engine_frame+100,provider,c_near.data(),c_near.data(),o.data(),q.data()).side<0);
        // no view-projections known for the render: untouched
        const auto none=[](std::uint32_t,std::uint32_t) -> const Views* {return nullptr;};
        CHECK(Place::place(engine_frame,none,c_near.data(),c_near.data(),o.data(),q.data()).stage==no_views);
    }

    // Hot path: the common case is a draw that is not the weapon's.
    {
        const Wp& w=frames_seen.at(engine_frame);
        std::vector<std::array<float,16>> draws,previous;
        for(int k=0;k<512;++k) {const Mat m=rigid(rot(1,2,3,k*.1),.5+.01*k-91.8,.2-92.5,1.0+4.9);draws.push_back(floats(mul(w.vp[1],m)));previous.push_back(floats(mul(w.vp[0],m)));}
        const Mat weapon=mul(render_info.at(engine_frame).second,parts[1]);
        const auto wc=floats(mul(w.vp[1],weapon)),wp=floats(mul(w.vp[0],weapon));
        std::array<float,16> o{},q{};int hits=0;
        auto t0=std::chrono::steady_clock::now();
        for(int rep=0;rep<200;++rep) for(std::size_t k=0;k<draws.size();++k) hits+=Place::place(engine_frame,provider,draws[k].data(),previous[k].data(),o.data(),q.data()).side>=0;
        const double reject_ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t0).count()/(200.0*double(draws.size()));
        t0=std::chrono::steady_clock::now();
        for(int rep=0;rep<50000;++rep) hits+=Place::place(engine_frame,provider,wc.data(),wp.data(),o.data(),q.data()).side>=0;
        const double place_ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-t0).count()/50000.0;
        std::printf("draw cost (lab views provider included): %.1f ns for a draw that is not the weapon's, %.1f ns for the weapon's (%d hits)\n",reject_ns,place_ns,hits);
        CHECK(hits==50000 && reject_ns<2000 && place_ns<20000);
    }
    std::printf(failures?"weapon_draw_place_check FAILED\n":"weapon_draw_place_check OK\n");
    return failures?1:0;
}
