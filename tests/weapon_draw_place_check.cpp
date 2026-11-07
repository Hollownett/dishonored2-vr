// v1106 (round 6): the held weapon's draw-time RIGID BINDING. A writer (or the copy service) states the weapon frame F and the exact hand
// frame H_read it composed it from; G = inverse(H_read) * F is learned once per (weapon, hand, grip), validated, and kept. Every draw of
// the weapon then becomes H_image * G, where H_image is the hand THIS render draws, in every eye pass, with the previous rows from the
// previous render's drawn hand. The lab covers native, crossed and copied weapons, a G that is never re-learned, a G refreshed on a
// regrip/weapon swap, true previous rows, identical depth/colour rows, two weapons that never swap, and an invalid G that leaves the
// engine's rows in place (counted) until a valid one exists.
#include "../src/weapon_draw_place.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

using namespace d2vr::weapon_draw;
static int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)

static Mat rot(double ax,double ay,double az,double angle) {
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
static double dist(const Mat& a,const Mat& b) {return std::hypot(a[3]-b[3],std::hypot(a[7]-b[7],a[11]-b[11]));}
static bool same_rows(const float* a,const float* b,double tolerance=0) {
    for(int i=0;i<16;++i) if(!(std::abs(double(a[i])-double(b[i]))<=tolerance)) return false;
    return true;
}
static double worst_rows(const float* a,const float* b) {
    double w=0;for(int i=0;i<16;++i) w=std::max(w,std::abs(double(a[i])-double(b[i])));return w;
}
static Mat y180() {Mat m=identity();m[0]=-1;m[10]=-1;return m;}
static Mat mirror_grip(const Mat& m) {   // held_copy::mirror in matrix form: diag(1,-1,1) * R * diag(1,-1,1), y negated
    Mat s=identity();s[5]=-1;
    Mat out=mul(mul(s,m),s);
    out[7]=-m[7];
    return out;
}

// Reverse-Z infinite projection with jitter, times the eye view (the float VP is what the engine stores).
static Mat vp_of(const Mat& eye_pose,int frame) {
    Mat p{};p[0]=1.2;p[2]=.1+.0007*std::sin(frame*1.7);p[5]=1.6;p[6]=-.05+.0007*std::cos(frame*2.3);
    p[10]=1;p[11]=.05;p[15]=1;   // invertible, constant clip w=1: the lab checks the placement algebra, not a perspective
    return mul(p,inv(eye_pose));
}
struct Wp {Mat vp[2]{},inv[2]{};};
static std::map<std::uint32_t,Wp> frames_seen;
static const Views* provider(std::uint32_t frame,std::uint32_t previous0,std::uint32_t previous1) {
    static Views v;
    v=Views{};v.prev_frame[0]=previous0;v.prev_frame[1]=previous1;
    const auto it=frames_seen.find(frame);if(it==frames_seen.end()) return nullptr;
    for(unsigned e=0;e<2;++e) {v.ok[e]=true;v.vp[e]=it->second.vp[e];v.inv[e]=it->second.inv[e];}
    const std::uint32_t previous[2]={previous0,previous1};
    for(unsigned s=0;s<2;++s) if(previous[s]) if(const auto p=frames_seen.find(previous[s]);p!=frames_seen.end())
        for(unsigned e=0;e<2;++e) {v.prev_ok[s][e]=true;v.prev_vp[s][e]=p->second.vp[e];v.prev_inv[s][e]=p->second.inv[e];}
    return &v;
}
static Mat hand_at(int t,unsigned side) {
    return rigid(rot(0,0,1,.45+.8*t/90.0),-91.8,-92.5+.55*side,4.9+.02*std::sin(t*.3));
}
static Mat eye_pose(int t,unsigned eye) {
    Mat looking=identity();looking[0]=0;looking[2]=-1;looking[4]=-1;looking[5]=0;looking[6]=0;looking[9]=1;looking[10]=0; // forward = -Z
    const Mat head=mul(rigid(rot(0,0,1,.8*t/90.0),-91.8,-92.1,5.0),looking);   // 0.4 m beside the hand: every draw is in front
    return mul(head,mul(rigid(identity(),eye?.032:-.032,0,0),rot(0,0,1,eye?-.105:.105)));
}
// Publish one frame and return the bound per side plus the expected view rows of one part under each eye.
struct Published {std::uint32_t frame{};Mat raw[2]{};Mat bound[2]{};bool have[2]{};};
static Published publish_frame(std::uint32_t frame,const Mat& f0,bool have0,const Mat& b0,const Mat& f1,bool have1,const Mat& b1,int view_t) {
    Published p;p.frame=frame;p.raw[0]=f0;p.raw[1]=f1;p.have[0]=have0;p.have[1]=have1;p.bound[0]=b0;p.bound[1]=b1;
    Wp w;for(unsigned e=0;e<2;++e) {w.vp[e]=vp_of(eye_pose(view_t,e),view_t);w.inv[e]=inv(w.vp[e]);}
    frames_seen[frame]=w;
    std::array<std::array<float,15>,2> raw{};
    std::array<Mat,2> bound{};
    std::array<const float*,2> raw_ptr{};
    std::array<const Mat*,2> bound_ptr{};
    if(have0) {raw[0]=raw_of(f0);raw_ptr[0]=raw[0].data();bound[0]=b0;bound_ptr[0]=&bound[0];}
    if(have1) {raw[1]=raw_of(f1);raw_ptr[1]=raw[1].data();bound[1]=b1;bound_ptr[1]=&bound[1];}
    Place::publish_bound(frame,raw_ptr,bound_ptr);
    return p;
}
static void check_placed(const Published& p,unsigned side,const Mat& part,const char* tag) {
    const Wp& w=frames_seen.at(p.frame);
    const unsigned e=1;
    const Mat f=p.raw[side],b=p.bound[side];
    const Mat model=mul(f,part);
    const auto c=floats(mul(w.vp[e],model));
    std::array<float,16> o{},q{};
    const Result r=Place::place(p.frame,provider,c.data(),nullptr,o.data(),q.data(),-1,int(e));
    const Mat want=mul(mul(w.vp[e],b),part);
    const double err=worst_rows(o.data(),floats(want).data());
    if(!(r.side==int(side) && r.stage==placed_pass && err<2e-4)) {
        const auto* sl=Place::slot_of(p.frame);
        std::printf("FAIL %s side=%u got side=%d stage=%u err=%.2e w=%.4f slot=[%d/%d %d/%d]\n",tag,side,r.side,unsigned(r.stage),err,double(c[15]),
            int(sl&&sl->side[0].valid),int(sl&&sl->side[0].bound),int(sl&&sl->side[1].valid),int(sl&&sl->side[1].bound));
        ++failures;
    }
}

int main() {
    const Mat part=rigid(identity(),.11,0,.065);   // a fixed part offset from F; the rig must not change it
    const Mat native_grip=rigid(rot(.2,.6,.3,.4),.04,-.02,.03);
    const Mat cross_grip=mul(y180(),native_grip);
    const Mat copy_offset=rigid(rot(.1,.2,.3,.7),.03,.01,-.02);
    const Mat copy_offset_mirror=mirror_grip(copy_offset);

    // ---- 1. native: G learned once, every frame every eye pass placed from the DRAWN hand --------------------------------
    {
        Binder::clear();Place::clear();
        const std::uintptr_t identity=0x1001;
        int frames=0,passes=0;
        double worst=0;
        // one writer composition is not enough (a note can be newer than F); two consistent ones bind
        {const Mat read=hand_at(0,0),f=mul(read,native_grip);GripCandidate c{};
         candidate_from_read(read,f,c,identity,write_engine,space_joint59,0,100);
         CHECK(!Binder::observe(0,c));CHECK(Binder::observe(0,c));}
        for(int render=0;render<40;++render) {
            const std::uint32_t frame=100u+static_cast<std::uint32_t>(render);
            const int t=render;
            const Mat read=hand_at(t-t%3,0),image=hand_at(t,0);
            const Mat f=mul(read,native_grip),b=mul(image,native_grip);
            GripCandidate c{};
            candidate_from_read(read,f,c,identity,write_engine,space_joint59,render,frame);
            const bool bound=Binder::observe(0,c);
            CHECK(bound);
            Mat got{};
            CHECK(Binder::frame(0,image,got) && dist(got,b)<1e-9);
            const auto p=publish_frame(frame,f,true,b,Mat{},false,Mat{},t);
            for(unsigned e=0;e<2;++e) {
                const Mat model=mul(f,part);
                const auto frame_it=frames_seen.find(frame);
                const auto cur=floats(mul(frame_it->second.vp[e],model));
                std::array<float,16> o{},q{};
                const Result r=Place::place(frame,provider,cur.data(),nullptr,o.data(),q.data(),-1,int(e));
                const Mat want=mul(mul(frames_seen.at(frame).vp[e],b),part);
                worst=std::max(worst,worst_rows(o.data(),floats(want).data()));
                CHECK(r.side==0 && r.eye==int(e) && r.stage==placed_pass);
                ++passes;
            }
            const auto s=Binder::summary(0);
            CHECK(s.bound && s.learns==1 && s.refreshes==0 && s.source==write_engine && s.space==space_joint59);
            (void)p;++frames;
        }
        CHECK(frames==40 && passes==80 && worst<2e-4);
        std::printf("v1106 native: frames=%d eye_passes=%d learns=%llu refreshes=%llu worst=%.2e bound=%d\n",
            frames,passes,Binder::summary(0).learns,Binder::summary(0).refreshes,worst,int(Binder::summary(0).bound));
    }

    // ---- 2. previous rows are the previous render's DRAWN hand: true motion, and depth/colour identical ----------------
    {
        Binder::clear();Place::clear();
        const std::uintptr_t identity=0x2002;
        const Mat g=rigid(rot(.3,.2,.8,.5),.02,.01,-.04);
        Mat b_prev{};
        std::uint32_t p_prev{};
        int checked=0;double worst_prev=0,motion=0;bool have_prev=false;
        {const Mat read=hand_at(-1,0),f=mul(read,g);GripCandidate c{};
         candidate_from_read(read,f,c,identity,write_engine,space_joint59,0,300);
         CHECK(!Binder::observe(0,c));CHECK(Binder::observe(0,c));}
        for(int render=0;render<8;++render) {
            const std::uint32_t frame=300u+static_cast<std::uint32_t>(render);
            const int t=render*2;
            const Mat read=hand_at(t-1,0),image=hand_at(t,0);
            const Mat f=mul(read,g),b=mul(image,g);
            GripCandidate c{};candidate_from_read(read,f,c,identity,write_engine,space_joint59,render,frame);
            CHECK(Binder::observe(0,c));
            const auto p=publish_frame(frame,f,true,b,Mat{},false,Mat{},t);
            const Mat model=mul(f,part);const auto crows=floats(mul(frames_seen.at(frame).vp[1],model));
            const auto prows=floats(mul(frames_seen.at(frame).vp[0],model));
            std::array<float,16> depth_cur{},depth_prev{},colour_cur{},colour_prev{};
            const Result depth=Place::place(frame,provider,crows.data(),prows.data(),depth_cur.data(),depth_prev.data(),-1,1);
            const Result colour=Place::place(frame,provider,crows.data(),nullptr,colour_cur.data(),colour_prev.data(),-1,1);
            CHECK(depth.side==0 && colour.side==0 && depth.stage==placed_pass && colour.stage==placed_pass);
            CHECK(same_rows(depth_cur.data(),colour_cur.data()));   // depth and colour current rows agree bit for bit
            CHECK(same_rows(depth_prev.data(),colour_prev.data())); // and the publisher fills the same previous rows
            if(have_prev) {
                const Mat want_prev=mul(mul(frames_seen.at(p_prev).vp[1],b_prev),part);
                worst_prev=std::max(worst_prev,worst_rows(depth_prev.data(),floats(want_prev).data()));
                for(int i=0;i<16;++i) motion=std::max(motion,std::abs(double(depth_prev[i])-double(depth_cur[i]))/(1.+std::abs(double(depth_cur[i]))));
                ++checked;
            }
            b_prev=b;p_prev=frame;have_prev=true;
            (void)p;
        }
        CHECK(checked==7 && worst_prev<2e-4 && motion>1e-5);
        std::printf("v1106 previous rows: checked=%d worst=%.2e motion=%.2e (the previous drawn hand, not the engine tick)\n",checked,worst_prev,motion);
    }

    // ---- 3. crossed native: G carries the y180 cross correction; the origin hand is the physical hand ------------------
    {
        Binder::clear();Place::clear();
        const std::uintptr_t identity=0x3003;
        const int t=7;
        const Mat read=hand_at(t-1,0),image=hand_at(t,0);
        const Mat f=mul(read,cross_grip),b=mul(image,cross_grip);
        GripCandidate c{};candidate_from_read(read,f,c,identity,write_cross,space_joint59,7,400);
        CHECK(!Binder::observe(0,c));CHECK(Binder::observe(0,c));
        Mat got{};CHECK(Binder::frame(0,image,got) && dist(got,b)<1e-9);
        CHECK(angle(got,image)>2.5);   // the engine's y180 is inside G, so the weapon is the mirrored hold
        const auto p=publish_frame(400,f,true,b,Mat{},false,Mat{},t);
        check_placed(p,0,part,"crossed");
        CHECK(Binder::summary(0).source==write_cross);
        std::printf("v1106 crossed: source=cross space=joint59 G-y180-angle=%.1f deg bound=%d\n",angle(got,image)*57.29577951308232,int(Binder::summary(0).bound));
    }

    // ---- 4. copy: the exact applied offset (and its mirror) is learned at once, and kept with no writer note ------------
    {
        Binder::clear();Place::clear();
        const int t=11;
        const Mat read_grip=hand_at(t-1,1),image_grip=hand_at(t,1);
        const Mat f=mul(read_grip,copy_offset_mirror),b=mul(image_grip,copy_offset_mirror);
        GripCandidate c{};candidate_from_offset(copy_offset_mirror,c,0x4004,write_held,11,500);
        CHECK(Binder::observe(1,c));   // exact: bound on the first placement
        const auto p=publish_frame(500,Mat{},false,Mat{},f,true,b,t);
        check_placed(p,1,part,"copy");
        // no writer note for the next ten renders: the binding is kept and placed from the drawn grip every frame
        int placed=0;
        for(int i=0;i<10;++i) {
            const std::uint32_t frame=501u+static_cast<std::uint32_t>(i);
            const Mat moving=mul(hand_at(t+i,1),copy_offset_mirror);
            GripCandidate none{};
            CHECK(Binder::observe(1,none));
            const auto p2=publish_frame(frame,Mat{},false,Mat{},f,true,moving,t+i);
            check_placed(p2,1,part,"copy-kept");
            ++placed;
        }
        CHECK(placed==10 && Binder::summary(1).learns==1 && Binder::summary(1).refreshes==0 && Binder::summary(1).space==space_grip);
        std::printf("v1106 copy: offset-grip bound=1 kept_frames=%d learns=%llu space=grip\n",placed,Binder::summary(1).learns);
    }

    // ---- 5. two weapons, two hands: each is placed with its OWN drawn hand, never swapped ------------------------------
    {
        Binder::clear();Place::clear();
        const Mat g0=rigid(rot(.2,.1,.7,.6),.03,-.01,.02),g1=rigid(rot(.5,.4,.1,-.4),-.02,.04,.03);
        const int t=20;
        const Mat read0=hand_at(t-1,0),read1=hand_at(t-1,1);
        const Mat image0=hand_at(t,0),image1=hand_at(t,1);
        const Mat f0=mul(read0,g0),f1=mul(read1,g1);
        GripCandidate c0{},c1{};
        candidate_from_read(read0,f0,c0,0x5005,write_engine,space_joint59,20,600);
        candidate_from_read(read1,f1,c1,0x5006,write_engine,space_joint59,20,600);
        for(int i=0;i<2;++i) {Binder::observe(0,c0);Binder::observe(1,c1);}
        const Mat b0=mul(image0,g0),b1=mul(image1,g1);
        const auto p=publish_frame(600,f0,true,b0,f1,true,b1,t);
        check_placed(p,0,part,"two-hand left");
        check_placed(p,1,part,"two-hand right");
        // each draw sits near its own bound origin; the two hands are far enough apart that the side is never a guess
        CHECK(dist(b0,b1)>.4);
        std::printf("v1106 two weapons: left=own right=own separation=%.3f m\n",dist(b0,b1));
    }

    // ---- 6. G refreshed on a REGRIP (three consistent compositions) and on a WEAPON SWAP (new identity) ----------------
    {
        Binder::clear();Place::clear();
        const int t=30;
        const Mat read=hand_at(t-1,0),image=hand_at(t,0);
        const Mat g1=rigid(rot(.1,.2,.3,.2),.02,.01,.01);
        const Mat g2=rigid(rot(.7,.2,.1,.9),.08,-.03,.05);   // >5 cm / >15 deg away: a regrip
        const Mat f1=mul(read,g1);
        GripCandidate c1{};candidate_from_read(read,f1,c1,0x6007,write_engine,space_joint59,30,700);
        CHECK(!Binder::observe(0,c1));CHECK(Binder::observe(0,c1));
        Mat before{};Binder::frame(0,image,before);
        CHECK(dist(before,mul(image,g1))<1e-9);
        // the regrip is observed: the first two do not displace G (no flicker), the third replaces it
        const Mat f2=mul(read,g2);GripCandidate c2{};candidate_from_read(read,f2,c2,0x6007,write_engine,space_joint59,31,701);
        CHECK(Binder::observe(0,c2));CHECK(Binder::observe(0,c2));
        Mat still{};Binder::frame(0,image,still);
        CHECK(dist(still,mul(image,g1))<1e-9);
        CHECK(Binder::observe(0,c2));
        Mat after{};Binder::frame(0,image,after);
        CHECK(dist(after,mul(image,g2))<1e-9);
        CHECK(Binder::summary(0).refreshes==1 && Binder::summary(0).learns==1);
        // a weapon swap is a new identity: the old G is dropped at once, the new native G needs two compositions
        const Mat g3=rigid(rot(.3,.8,.2,-.6),-.01,.02,.04);
        const Mat f3=mul(read,g3);GripCandidate c3{};candidate_from_read(read,f3,c3,0x6008,write_engine,space_joint59,40,710);
        CHECK(!Binder::observe(0,c3));   // no old G is reused for the new weapon
        Mat none{};CHECK(!Binder::frame(0,image,none));
        CHECK(Binder::observe(0,c3));
        Mat swapped{};Binder::frame(0,image,swapped);
        CHECK(dist(swapped,mul(image,g3))<1e-9);
        CHECK(Binder::summary(0).learns==2);
        std::printf("v1106 refresh: regrip_refreshes=%llu swap_learns=%llu\n",Binder::summary(0).refreshes,Binder::summary(0).learns);
    }

    // ---- 7. an invalid G is rejected; the engine's rows stay (counted) until a valid G exists -------------------------
    {
        Binder::clear();Place::clear();
        const int t=40;
        const Mat read=hand_at(t-1,0),image=hand_at(t,0);
        const Mat good=rigid(rot(.2,.4,.1,.3),.03,.01,-.02);
        const Mat far=rigid(rot(.2,.4,.1,.3),.45,0,0);      // |translation| 0.45 m > 0.3 m: rejected
        const Mat sheared=[] {Mat m=rigid(rot(.1,.1,.1,.2),.02,0,0);m[0]*=1.2;return m;}();
        CHECK(!valid_grip(far) && !valid_grip(sheared) && valid_grip(good));
        const Mat f_far=mul(read,far);GripCandidate bad{};candidate_from_read(read,f_far,bad,0x7009,write_engine,space_joint59,50,800);
        CHECK(!bad.valid && bad.attempted);
        CHECK(!Binder::observe(0,bad));
        CHECK(Binder::summary(0).rejects==1 && Binder::summary(0).fallbacks==1 && !Binder::bound(0));
        // the publisher passes no bound frame: Place leaves the draw's rows untouched (the engine's rows)
        const std::uint32_t frame=800;
        Wp w;for(unsigned e=0;e<2;++e) {w.vp[e]=vp_of(eye_pose(t,e),t);w.inv[e]=inv(w.vp[e]);}
        frames_seen[frame]=w;
        const Mat f= mul(read,good), model=mul(f,part);
        const auto engine_raw=raw_of(f);
        std::array<const float*,2> raw_ptr{engine_raw.data(),nullptr};
        std::array<const Mat*,2> no_bound{nullptr,nullptr};
        Place::publish_bound(frame,raw_ptr,no_bound);
        std::array<float,16> cur=floats(mul(w.vp[1],model)),out{},prev{};
        for(auto& v:out) v=7.f;for(auto& v:prev) v=8.f;
        const Result r=Place::place(frame,provider,cur.data(),nullptr,out.data(),prev.data(),-1,1);
        CHECK(r.side<0 && r.stage==unbound);
        CHECK(out[0]==7.f && prev[0]==8.f);   // untouched: the engine's rows
        // a valid G arrives: two consistent compositions bind it and the same draw is placed
        const Mat f_good=mul(read,good);GripCandidate c{};candidate_from_read(read,f_good,c,0x7009,write_engine,space_joint59,51,801);
        CHECK(!Binder::observe(0,c));CHECK(Binder::observe(0,c));
        const Mat b=mul(image,good);
        const auto p=publish_frame(801,f_good,true,b,Mat{},false,Mat{},t);
        check_placed(p,0,part,"valid-after-reject");
        CHECK(Binder::summary(0).rejects==1 && Binder::summary(0).learns==1);
        std::printf("v1106 invalid G: rejected=1 fallback_frames=1 bound_after_valid=%d\n",int(Binder::summary(0).bound));
    }

    // ---- 8. two-pass colour/depth equivalence and the single-frame cost ----------------------------------------------
    {
        Binder::clear();Place::clear();
        const int t=50;const std::uint32_t frame=900;
        const Mat read=hand_at(t-1,0),image=hand_at(t,0),g=rigid(rot(.1,.4,.2,.3),.02,.02,.01);
        const Mat f=mul(read,g),b=mul(image,g);
        GripCandidate c{};candidate_from_read(read,f,c,0x800a,write_engine,space_joint59,60,frame);
        Binder::observe(0,c);Binder::observe(0,c);
        const auto p=publish_frame(frame,f,true,b,Mat{},false,Mat{},t);
        const Mat model=mul(f,part);const auto cur=floats(mul(frames_seen.at(frame).vp[1],model));
        std::array<float,16> c1{},p1{},c2{},p2{};
        const Result colour=Place::place(frame,provider,cur.data(),nullptr,c1.data(),p1.data(),-1,1);
        const Result depth=Place::place(frame,provider,cur.data(),cur.data(),c2.data(),p2.data(),-1,1);
        CHECK(colour.side==0 && depth.side==0 && same_rows(c1.data(),c2.data()) && same_rows(p1.data(),p2.data()));
        std::array<float,16> o{},q{};
        const auto started=std::chrono::steady_clock::now();
        int hits=0;
        for(int i=0;i<50000;++i) hits+=Place::place(frame,provider,cur.data(),nullptr,o.data(),q.data(),-1,1).side>=0;
        const double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-started).count()/50000.;
        CHECK(hits==50000 && ns<20000.);
        std::printf("v1106 depth/colour identical=1 cost=%.1f ns\n",ns);
    }

    // ---- v1072: the race. The engine composed this render's constants from the PREVIOUS read F (the game wrote the next tick's F before
    // the eye-0 begin). A held thing (2 cm reach) moving 8 cm per render: every such draw is placed at B exactly, from the alt frame,
    // with previous rows from the previous drawn B; a draw of another thing (other id) never takes the alt; the current F still wins.
    {
        Place::clear();
        const Mat g=rigid(rot(.2,.5,.3,.6),.03,-.01,.02);
        const std::array<double,2> reach{.5,0.};   // v1072: a held thing's parts sit at fixed offsets from F like a weapon's (13 cm here)
        std::array<std::uintptr_t,2> ids{0x9001,0};
        Mat f_prev{},b_prev{};std::uint32_t frame_prev{};
        int alt_ok=0,cur_ok=0;double worst=0,worst_prev=0;
        for(int render=0;render<12;++render) {
            const std::uint32_t frame=700u+static_cast<std::uint32_t>(render);
            const Mat f=rigid(rot(0,0,1,.45+.03*render),-91.8+.08*render,-92.5,4.9);   // the physics frame: 8 cm and 1.7 deg a render
            const Mat b=mul(rigid(rot(0,0,1,.45+.03*render),-91.8+.08*render+.15,-92.5,4.9),g);   // the drawn hand 15 cm ahead
            Wp w;for(unsigned e=0;e<2;++e) {w.vp[e]=vp_of(eye_pose(render,e),render);w.inv[e]=inv(w.vp[e]);}
            frames_seen[frame]=w;
            const auto raw=raw_of(f);std::array<const float*,2> raw_ptr{raw.data(),nullptr};std::array<const Mat*,2> bound_ptr{&b,nullptr};
            Place::publish_bound(frame,raw_ptr,bound_ptr,reach,ids);
            if(render>0) {
                // a draw composed from the previous read
                const auto cur=floats(mul(w.vp[1],mul(f_prev,part)));
                std::array<float,16> o{},q{};
                const Result r=Place::place(frame,provider,cur.data(),nullptr,o.data(),q.data(),-1,1);
                const double err=worst_rows(o.data(),floats(mul(mul(w.vp[1],b),part)).data());
                const double err_prev=worst_rows(q.data(),floats(mul(mul(frames_seen.at(frame_prev).vp[1],b_prev),part)).data());
                if(r.side==0 && r.alt && r.stage==placed_pass) ++alt_ok;
                worst=std::max(worst,err);worst_prev=std::max(worst_prev,err_prev);
                // the current read still places without the alt
                const auto now_rows=floats(mul(w.vp[1],mul(f,part)));
                const Result rc=Place::place(frame,provider,now_rows.data(),nullptr,o.data(),q.data(),-1,1);
                if(rc.side==0 && !rc.alt && worst_rows(o.data(),floats(mul(mul(w.vp[1],b),part)).data())<2e-4) ++cur_ok;
            }
            f_prev=f;b_prev=b;frame_prev=frame;
        }
        CHECK(alt_ok==11 && cur_ok==11 && worst<2e-4 && worst_prev<2e-4);
        // another thing in the hand (a new id): the previous thing's frame is no alternative
        {
            ids[0]=0x9002;const std::uint32_t frame=720u;
            const Mat f=rigid(rot(1,0,0,.9),-90.8,-92.5,4.9),b=mul(f,g);   // another orientation (a twin with the same axes is not this check)
            Wp w;for(unsigned e=0;e<2;++e) {w.vp[e]=vp_of(eye_pose(20,e),20);w.inv[e]=inv(w.vp[e]);}
            frames_seen[frame]=w;
            const auto raw=raw_of(f);std::array<const float*,2> raw_ptr{raw.data(),nullptr};std::array<const Mat*,2> bound_ptr{&b,nullptr};
            Place::publish_bound(frame,raw_ptr,bound_ptr,reach,ids);
            const auto cur=floats(mul(w.vp[1],mul(f_prev,part)));
            std::array<float,16> o{},q{};
            CHECK(Place::place(frame,provider,cur.data(),nullptr,o.data(),q.data(),-1,1).side<0);
        }
        std::printf("v1072 race: alt placed %d/11, current placed %d/11, worst %.2e, prev worst %.2e\n",alt_ok,cur_ok,worst,worst_prev);
    }

    std::printf(failures?"weapon_draw_place_check FAILED\n":"weapon_draw_place_check OK\n");
    return failures?1:0;
}
