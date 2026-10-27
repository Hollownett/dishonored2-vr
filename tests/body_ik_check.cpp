// Full-body IK scenario check on the real player rig; optional JSON export for
// tools/render_body_ik.py:  body_ik_check.exe [frames.json] [corvo|emily]
#include "../src/body_ik.hpp"
#include "player_rig_fixture.hpp"
#include "player_clip_fixture.hpp"
#include <cstdio>
#include <functional>
#include <string>
#include <fstream>
#include <iostream>

using namespace d2vr;
using namespace d2vr::body;

static Skeleton load(const std::vector<FixtureJoint>& joints) {
    Skeleton s;
    for(const auto& j:joints) {
        s.names.push_back(j.name);s.parent.push_back(j.parent);
        s.local.push_back({normalized({j.q[0],j.q[1],j.q[2],j.q[3]}),{j.t[0],j.t[1],j.t[2]}});
    }
    s.finish();return s;
}
static int failures=0;
static void expect(bool ok,const std::string& what) {if(!ok && ++failures<=20) std::cerr<<"FAIL "<<what<<"\n";}
static double angle_between(Quaternion a,Quaternion b) {
    const double d=std::abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w);
    return 2*std::acos(std::min(1.,d));
}
static Quaternion eye_rotation(double yaw,double pitch_down,double roll=0) {
    return mul(rot_z(yaw),mul(axis_angle({0,1,0},pitch_down),axis_angle({1,0,0},roll)));
}
// A real HMD swings about the neck: eye = neck pivot + R * (eye offset from the neck).
static Xf head_on_neck(Vec3 neck,Quaternion q) {return {q,neck+rotated(q,{.09,0,.12})};}

struct Scenario {const char* name;double seconds;std::function<Inputs(double)> at;std::vector<double> exports;};

static int run(int argc,char** argv) {
    const double deg=kPi/180,eye_height=1.75,span=1.55,neck_height=eye_height-.12;
    const auto body=load(kFixtureBody),left=load(kFixtureLeftArm),right=load(kFixtureRightArm);
    BodySolver probe(body,left,right,{eye_height,span});
    {
        const double fitted_height=1.6458796925,actual_eye=1.7704911709;
        BodySolver supported(body,left,right,{fitted_height,0}),floor_conflict(body,left,right,{fitted_height,0});
        Inputs in;in.eye={eye_rotation(0,0),{0,0,actual_eye}};in.hand_valid[0]=in.hand_valid[1]=false;
        in.ground=actual_eye-fitted_height;const auto supported_pose=supported.solve(in);
        in.ground=0;const auto conflict=floor_conflict.solve(in);
        for(const auto name:{"center_neck_jnt","center_spine03_jnt"}) {
            const auto j=body.find(name);
            std::cerr<<"ground-anchor "<<name<<" delta="<<length(conflict.body[j].p-supported_pose.body[j].p)<<"\n";
            expect(length(conflict.body[j].p-supported_pose.body[j].p)<.003,
                std::string("captured ground mismatch preserves upper-body anchor ")+name);
        }
        for(const auto name:{"left_ankle_jnt","right_ankle_jnt"}) {
            const auto j=floor_conflict.skeleton(0).find(name);
            expect(std::abs(conflict.body[j].p.z-floor_conflict.skeleton(0).bind[j].p.z)<.003,
                "captured stationary floor mismatch keeps ankle on actual ground");
        }
        // A larger physically impossible floor conflict stays bounded; it
        // deliberately retains reach-clamped feet rather than stretching legs.
        BodySolver impossible(body,left,right,{fitted_height,0});
        in.eye.p.z=fitted_height+.6;const auto limited=impossible.solve(in);
        const auto& impossible_bind=impossible.skeleton(0);
        const auto root_index=impossible_bind.find("center_root_jnt");
        const double nominal_root=impossible_bind.bind[root_index].p.z+.6;
        expect(nominal_root-limited.body[root_index].p.z<=.15*impossible.scale()+.003,
            "infeasible floor conflict cannot stretch pelvis-to-spine correction without bound");
        const auto limited_ankle=impossible_bind.find("left_ankle_jnt");
        expect(limited.body[limited_ankle].p.z-impossible_bind.bind[limited_ankle].p.z>.2,
            "impossible floor conflict retains explicit reach-clamped floating-foot fallback");
        for(unsigned direction=0;direction<3;++direction)for(unsigned hz:{10u,30u,90u}) {
            BodySolver fast(body,left,right,{fitted_height,0});
            Inputs walking;walking.eye={eye_rotation(0,0),{0,0,fitted_height}};
            walking.hand_valid[0]=walking.hand_valid[1]=false;walking.dt=hz==10?.095:1./hz;
            const double frame_dt=walking.dt;
            const auto substeps=static_cast<unsigned>(std::ceil(frame_dt/(1./30)));
            walking.dt=frame_dt/substeps;
            (void)fast.solve(walking);
            double worst_lag=0,minimum_relative=1e9,maximum_relative=-1e9,maximum_lift=0;
            const auto& fitted=fast.skeleton(0);const int root=fitted.find("center_root_jnt"),ankle=fitted.find("left_ankle_jnt");
            for(unsigned frame=0;frame<hz*12;++frame) {
                Output posed;
                for(unsigned step=1;step<=substeps;++step) {
                    const double travelled=6.1*frame_dt*(frame+static_cast<double>(step)/substeps);
                    walking.eye.p.x=direction==0?travelled:direction==1?-travelled:0;
                    walking.eye.p.y=direction==2?travelled:0;
                    posed=fast.solve(walking);
                }
                for(unsigned side=0;side<2;++side) {
                    const auto a=fitted.find(side?"right_ankle_jnt":"left_ankle_jnt");
                    worst_lag=std::max(worst_lag,length(posed.body[a].p-posed.feet[side]));
                }
                const double relative=direction==2?posed.body[ankle].p.y-posed.body[root].p.y:posed.body[ankle].p.x-posed.body[root].p.x;
                minimum_relative=std::min(minimum_relative,relative);maximum_relative=std::max(maximum_relative,relative);
                maximum_lift=std::max(maximum_lift,posed.body[ankle].p.z-fitted.bind[ankle].p.z);
            }
            std::cerr<<"captured-speed direction="<<direction<<" "<<hz<<"Hz lag="<<worst_lag<<" excursion="<<maximum_relative-minimum_relative<<" lift="<<maximum_lift<<"\n";
            expect(worst_lag<.003,"captured speed keeps visible ankle coherent with internal foot target");
            expect(maximum_relative-minimum_relative>.12 && maximum_lift>.015,"captured speed retains visible ankle swing");
            walking.dt=0;const auto duplicate=fast.solve(walking);
            const auto repeated=fast.solve(walking);
            expect(duplicate.phase[0]==repeated.phase[0] && duplicate.phase[1]==repeated.phase[1] &&
                length(duplicate.feet[0]-repeated.feet[0])<1e-12 && length(duplicate.feet[1]-repeated.feet[1])<1e-12,
                "fast recovery also preserves duplicate-time foot state");
        }
    }
    {
        // Span fitting supports the wizard range and rejects unrepresentable
        // dimensions rather than silently returning a different-sized arm.
        BodySolver native(body,left,right,{body.bind[body.find("camera_jnt")].p.z,0});
        const auto chest=body.bind[body.find("center_spine03_jnt")];
        const auto point=[&](int side,const char* suffix) {
            const auto& arm=native.skeleton(side+1);const std::string prefix=side?"right":"left";
            return mul(chest,arm.bind[arm.find(prefix+suffix)]).p;
        };
        const auto delta=point(0,"_hand_attachment_jnt")-point(1,"_hand_attachment_jnt");
        const auto limbs=(point(0,"_wrist_jnt")-point(0,"_arm_jnt"))-(point(1,"_wrist_jnt")-point(1,"_arm_jnt"));
        for(const double desired:{.75,1.3}) {
            BodySolver fitted(body,left,right,{body.bind[body.find("camera_jnt")].p.z,length(delta+limbs*(desired-1))});
            expect(std::abs(fitted.arm_scale()-desired)<1e-9,"arm fit beyond old clamp preserves measured span");
        }
        Inputs in;in.eye.p={0,0,eye_height};in.hand_valid[0]=in.hand_valid[1]=false;
        const auto before=probe.solve(in);in.dt=0;
        const auto same=probe.solve(in);
        expect(same.phase[0]==before.phase[0] && same.phase[1]==before.phase[1] &&
            length(same.feet[0]-before.feet[0])<1e-9 && length(same.feet[1]-before.feet[1])<1e-9,
            "duplicate millisecond refresh preserves feet and gait phase");
        probe.reset();
    }
    {
        const auto rejects=[](const std::function<void()>& f) {
            try {f();} catch(const std::invalid_argument&) {return true;} return false;
        };
        auto broken=body;broken.parent.pop_back();
        expect(rejects([&] {BodySolver s(broken,left,right,{eye_height,span});}),"incomplete skeleton rejected");
        broken=body;broken.parent[0]=-2;
        expect(rejects([&] {broken.finish();}),"invalid parent rejected");
        expect(rejects([&] {BodySolver s(body,left,right,{0,span});}),"invalid calibration rejected");
        Inputs in;in.eye.p={0,0,eye_height};in.hand_valid[0]=in.hand_valid[1]=false;
        auto bad=in;bad.dt=std::numeric_limits<double>::quiet_NaN();
        expect(rejects([&] {probe.solve(bad);}),"NaN dt rejected before persistent state mutation");
        NativePose malformed;malformed.body.resize(1);bad=in;bad.native=&malformed;
        expect(rejects([&] {probe.solve(bad);}),"wrong native body size rejected");
        bad=in;bad.own=native_ownership("Assassination");
        expect(rejects([&] {probe.solve(bad);}),"authored action never silently substitutes procedural arms");
        probe.solve(in);in.eye.p.x=10;probe.reset();
        BodySolver fresh(body,left,right,{eye_height,span});
        const auto reset=probe.solve(in),first=fresh.solve(in);
        expect(length(reset.feet[0]-first.feet[0])<1e-9 && length(reset.feet[1]-first.feet[1])<1e-9,
            "epoch reset replants feet at new owner and clears old motion");
        probe.reset();
    }
    // Native animated arm pose (captured in game) for the hand subtree; "none" = procedural fingers.
    const std::string native_label=argc>4?argv[4]:"sword_idle";
    std::vector<Xf> native[2];
    for(const auto& arm:kFixtureNativeArms) if(native_label==arm.label) {
        auto& v=native[arm.side];
        for(const auto& j:arm.joints) {
            const auto* c=j.columns;
            v.push_back({from_axes(unit({c[0],c[1],c[2]}),unit({c[3],c[4],c[5]}),unit({c[6],c[7],c[8]})),{j.t[0],j.t[1],j.t[2]}});
        }
    }
    // Bind hand_attachment rotation (yaw 0) and its finger/palm directions, to author hand targets.
    const int s3=body.find("center_spine03_jnt");
    Quaternion bind_hand[2];
    for(int i=0;i<2;++i) {
        const auto& arm=i?right:left;
        bind_hand[i]=mul(body.bind[s3],arm.bind[arm.find(i?"right_hand_attachment_jnt":"left_hand_attachment_jnt")]).q;
    }
    // Hand rotation from finger direction and palm normal (world), relative to the T-pose bind.
    const auto hand_rot=[&](int i,Vec3 fingers,Vec3 palm) {
        return mul(frame_map({0,i?-1.:1.,0},{0,0,-1},fingers,palm),bind_hand[i]);
    };
    const auto frame=[&](double yaw,Vec3 v) {return rotated(rot_z(yaw),v);};
    // Hands held as on Quest controllers: fingers forward-down, palms facing inward.
    const auto hold=[&](Inputs& in,double yaw,Vec3 eye,Vec3 l,Vec3 r) {
        in.hand[0]={hand_rot(0,frame(yaw,unit({1,0,-.35})),frame(yaw,{0,-1,0})),eye+frame(yaw,l)};
        in.hand[1]={hand_rot(1,frame(yaw,unit({1,0,-.35})),frame(yaw,{0,1,0})),eye+frame(yaw,r)};
        for(int i=0;i<2;++i) {in.grip[i]=.45;in.trigger[i]=.25;}
    };
    {
        BodySolver socket_solver(body,left,right,{eye_height,span});
        Inputs socket_input;socket_input.eye={eye_rotation(0,0),{0,0,eye_height}};
        hold(socket_input,0,socket_input.eye.p,{.35,.2,-.35},{.35,-.2,-.35});
        auto native_left=left.bind;const auto attachment=left.find("left_hand_attachment_jnt");
        const auto parent=left.parent[attachment],finger=left.find("left_index_02_jnt");
        auto native_local=mul(inverse(native_left[parent]),native_left[attachment]);
        native_local.q=mul(native_local.q,axis_angle({0,0,1},35*deg));
        native_left[attachment]=mul(native_left[parent],native_local);socket_input.native_arm[0]=&native_left;
        socket_input.native_socket_token[0]=100;socket_input.grip[0]=socket_input.trigger[0]=1;
        const auto normal=socket_solver.solve(socket_input);
        const auto normal_socket=mul(inverse(normal.arm[0][parent]),normal.arm[0][attachment]);
        socket_input.hand_empty[0]=true;
        const auto procedural=socket_solver.solve(socket_input);
        const auto retained=mul(inverse(procedural.arm[0][parent]),procedural.arm[0][attachment]);
        expect(angle_between(retained.q,normal_socket.q)<1e-6,"qualified left socket survives procedural finger transition");
        expect(angle_between(procedural.arm[0][finger].q,normal.arm[0][finger].q)>.1,"qualified socket does not freeze procedural fingers");
        socket_input.native_socket_token[0]=0;
        const auto unqualified=socket_solver.solve(socket_input);
        const auto bind_socket=mul(inverse(unqualified.arm[0][parent]),unqualified.arm[0][attachment]);
        expect(angle_between(bind_socket.q,normal_socket.q)>30*deg,"unqualified procedural path reproduces native-to-bind gun socket discrepancy");
        native_local.q=mul(native_local.q,axis_angle({0,0,1},40*deg));
        native_left[attachment]=mul(native_left[parent],native_local);socket_input.native_socket_token[0]=101;
        const auto replaced=socket_solver.solve(socket_input);
        const auto replacement=mul(inverse(replaced.arm[0][parent]),replaced.arm[0][attachment]);
        expect(angle_between(replacement.q,native_local.q)<1e-6,"new socket token initializes current native local rather than old cached weapon");
        socket_solver.reset();socket_input.native_socket_token[0]=101;
        const auto reset=socket_solver.solve(socket_input);
        expect(angle_between(mul(inverse(reset.arm[0][parent]),reset.arm[0][attachment]).q,native_local.q)<1e-6,"solver reset clears socket lifetime state");
    }
    if(native[1].size()==69) {  // empty hand curls; held socket is stable, player-reversible
        BodySolver sol(body,left,right,{eye_height,span});
        const int wr=right.find("right_wrist_jnt"),at=right.find("right_hand_attachment_jnt"),ix=right.find("right_index_02_jnt");
        Inputs in;in.eye={eye_rotation(0,0),{0,0,eye_height}};hold(in,0,in.eye.p,{.35,.2,-.35},{.35,-.2,-.35});in.dt=1/90.;
        const auto swung=[&](double degrees) {
            auto n=native[1];const int p=right.parent[at];
            const auto l=mul(inverse(n[p]),n[at]);
            n[at]=mul(n[p],{mul(axis_angle({0,0,1},degrees*deg),l.q),l.p});return n;
        };
        const auto sock=[&](double degrees,bool reverse=false,bool empty=false,int joint=-1) {
            const auto n=swung(degrees);in.native_arm[1]=&n;in.reverse_grip[1]=reverse;in.hand_empty[1]=empty;
            const auto out=sol.solve(in);const auto& a=out.arm[1];
            return mul(inverse(a[wr]),a[joint<0?at:joint]);
        };
        Xf base;for(int n=0;n<30;++n) base=sock(0);
        for(int n=0;n<10;++n) sock(9.*(n+1));               // 810 deg/s native swing
        for(int n=0;n<15;++n) sock(90);
        for(int n=0;n<10;++n) sock(0);
        expect(angle_between(sock(0).q,base.q)<2*deg,"fast native attachment swing ignored");
        double a=0;for(int n=0;n<540;++n) sock(a+=60./540);  // 10 deg/s drift
        for(int n=0;n<90;++n) sock(60);
        const auto drifted=sock(60);
        expect(angle_between(drifted.q,base.q)>50*deg,"slow steady attachment change adopted");
        Xf flipped;for(int n=0;n<12;++n) flipped=sock(60,true); // the reverse blends in over 0.1 s (G2)
        expect(angle_between(flipped.q,drifted.q)>179*deg,"reverse grip flips the socket 180 degrees");
        {   // the flip reverses the blade about the fist (headset v559, 2026-10-19: flipped about the palm centre the sword sat 9 cm off the fist):
            // the hilt line keeps passing through the wrap centre of the curled fingers and the same handle point stays there (tests/reverse_grip_check.cpp
            // covers the same on the rig fixture without clips)
            const auto grip=[&](bool reverse,Vec3& point,Vec3& blade,double& miss) {
                const auto n=swung(60);in.native_arm[1]=&n;in.reverse_grip[1]=reverse;in.hand_empty[1]=false;
                Output out;for(int frame=0;frame<12;++frame) out=sol.solve(in); // a mid-hold reverse change blends over 0.1 s (G2): 12 frames settle both directions
                const auto& a=out.arm[1];
                Vec3 sum{};for(const char* f:{"index","medium","ring","pinky"}) for(const char* k:{"02","03","04"}) sum=sum+a[right.find(std::string("right_")+f+"_"+k+"_jnt")].p*(1./12);
                blade=rotated(a[at].q,{1,0,0});const auto d=sum-a[at].p;
                point=a[at].p+blade*dot(d,blade);miss=out.hilt_miss[1];
            };
            Vec3 pf,bf,pr,br;double mf=0,mr=0;grip(false,pf,bf,mf);grip(true,pr,br,mr);in.reverse_grip[1]=false;
            std::printf("hilt point nearest the fist: forward (%.4f %.4f %.4f) reversed (%.4f %.4f %.4f), moved %.1f mm; hilt miss %.1f / %.1f mm\n",
                pf.x,pf.y,pf.z,pr.x,pr.y,pr.z,length(pr-pf)*1000,mf*1000,mr*1000);
            expect(length(pr-pf)<.015,"reverse grip keeps the same handle point in the fist");
            expect(mf<.015 && mr<.015,"reverse grip keeps the hilt through the fist");
            expect(dot(bf,br)<-.99,"reverse grip points the blade the other way");
        }
        {   // native sneak stance = the idle attachment turned 180 deg about its own Z; the forward grip stays
            for(int n=0;n<120;++n) sock(0);
            const auto fwd=sock(0);
            auto sneak=native[1];const int p=right.parent[at];
            const auto l=mul(inverse(sneak[p]),sneak[at]);
            sneak[at]=mul(sneak[p],{mul(l.q,axis_angle({0,0,1},kPi)),l.p-Vec3{-.0453,-.0262,.0012}}); // clip sneak socket
            in.native_arm[1]=&sneak;Xf got;
            for(int n=0;n<120;++n) {const auto out=sol.solve(in);got=mul(inverse(out.arm[1][wr]),out.arm[1][at]);}
            expect(angle_between(got.q,fwd.q)<2*deg,"sneak reverse grip kept forward");
            expect(length(got.p-fwd.p)<.002,"sneak grip keeps the standing socket (fist on the handle)");
        }
        const auto held=sock(0,false,false,ix),curled=sock(0,false,true,ix);
        in.grip[1]=1;in.trigger[1]=1;
        expect(angle_between(sock(0,false,true,ix).q,held.q)>.3,"empty hand curls with grip");
        expect(angle_between(sock(0,false,false,ix).q,held.q)<1e-6,"held hand keeps native fingers");
        (void)curled;
        {   // Contact turns (finger_ik.hpp): the empty hand bends each joint by its own turn, whatever grip/trigger say (grip alone
            // used to leave the index at the relaxed curl, and no held shape ever reached the fingers).
            const int i2=right.find("right_index_02_jnt"),i3=right.find("right_index_03_jnt");
            const auto bend=[&](bool has,float turn) {
                const auto n=swung(0);in.native_arm[1]=&n;in.hand_empty[1]=true;in.grip[1]=1;in.trigger[1]=0;
                in.has_finger[1]=has;for(auto& f:in.finger_turn[1]) for(auto& t:f) t=turn;
                Xf last;for(int k=0;k<5;++k) {const auto out=sol.solve(in);last=mul(inverse(out.arm[1][i2]),out.arm[1][i3]);}
                return angle_between(last.q,right.local[i3].q);
            };
            expect(std::abs(bend(false,0)-18*deg)<1*deg,"index without trigger keeps the relaxed curl");
            expect(std::abs(bend(true,1.2f)-1.2)<.02,"finger turn closes the joint by exactly its angle");
            expect(std::abs(bend(true,0.f))<.02,"zero turn leaves the bind pose");
            {   // How the fingers were posed is reported per hand (the status JSON's finger route): contact turns, grip alone, the game's fingers.
                const auto n=swung(0);in.native_arm[1]=&n;in.grip[1]=1;in.trigger[1]=0;
                in.hand_empty[1]=true;in.has_finger[1]=true;for(auto& f:in.finger_turn[1]) for(auto& t:f) t=.3f;
                expect(sol.solve(in).finger_mode[1]==1,"an empty hand given contact turns is posed by them");
                in.has_finger[1]=false;
                expect(sol.solve(in).finger_mode[1]==2,"an empty hand without turns is posed by grip and trigger");
                in.hand_empty[1]=false;in.has_finger[1]=true;
                expect(sol.solve(in).finger_mode[1]==3,"a hand the game says is not empty keeps the game's fingers, turns or not");
            }
            in.has_finger[1]=true;in.finger_turn[1][1][1]=std::nanf("");
            bool rejected=false;try {(void)sol.solve(in);} catch(const std::invalid_argument&) {rejected=true;}
            expect(rejected,"non-finite finger turn rejected");
            in.has_finger[1]=false;in.hand_empty[1]=false;
            // The hinge the finger turns about comes from the contact when it is given (finger_ik.hpp hinge_axes): the bones bend about the axis
            // that was tested, not the solver's own; the metacarpals stay on the bind pose the contact planned from.
            {
                const auto n=swung(0);in.native_arm[1]=&n;in.hand_empty[1]=true;in.grip[1]=1;in.trigger[1]=1;
                in.has_finger[1]=true;for(auto& f:in.finger_turn[1]) for(auto& t:f) t=0;in.finger_turn[1][1][1]=1.f;
                for(auto& f:in.finger_axis[1]) for(auto& ax:f) {ax[0]=0;ax[1]=0;ax[2]=1;}
                in.has_finger_axis[1]=true;
                Xf last;for(int k=0;k<5;++k) {const auto out=sol.solve(in);last=mul(inverse(out.arm[1][i2]),out.arm[1][i3]);}
                const auto q=mul(conj(right.local[i3].q),last.q);
                const double sine=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z);
                const Vec3 axis_world=rotated(right.bind[i3].q,Vec3{q.x/sine,q.y/sine,q.z/sine});
                expect(std::abs(2*std::atan2(sine,std::abs(q.w))-1.)<.02,"supplied hinge: the joint turns by its angle");
                expect(std::abs(std::abs(axis_world.z)-1)<.01,"supplied hinge: about the axis that was tested");
                const int m1=right.find("right_index_01_jnt");const auto out=sol.solve(in);
                const auto meta=mul(inverse(out.arm[1][right.parent[m1]]),out.arm[1][m1]);
                expect(angle_between(meta.q,right.local[m1].q)<1e-6,"metacarpal stays on the bind pose under contact turns");
                in.finger_axis[1][1][1][2]=3.f;
                bool rejected_axis=false;try {(void)sol.solve(in);} catch(const std::invalid_argument&) {rejected_axis=true;}
                expect(rejected_axis,"non-unit finger axis rejected");
                in.has_finger_axis[1]=false;in.has_finger[1]=false;in.hand_empty[1]=false;
            }
            // The thumb opens past its bind pose (finger_ik.hpp finger_open_degrees): a negative turn bends the joint the other way; a turn
            // beyond what a thumb can open is refused.
            {
                const int t1=right.find("right_thumb_01_jnt"),t2=right.find("right_thumb_02_jnt");
                const auto thumb=[&](float turn) {
                    const auto n=swung(0);in.native_arm[1]=&n;in.hand_empty[1]=true;in.grip[1]=0;in.trigger[1]=0;
                    in.has_finger[1]=true;for(auto& f:in.finger_turn[1]) for(auto& t:f) t=0;in.finger_turn[1][0][1]=turn; // thumb_02 is chain joint 1
                    Xf last;for(int k=0;k<5;++k) {const auto out=sol.solve(in);last=mul(inverse(out.arm[1][t1]),out.arm[1][t2]);}
                    return last;
                };
                const auto closed=thumb(.5f),opened=thumb(-.5f),plain=thumb(0.f);
                expect(std::abs(angle_between(closed.q,plain.q)-.5)<.02 && std::abs(angle_between(opened.q,plain.q)-.5)<.02,"thumb turns by its angle either way");
                expect(angle_between(opened.q,closed.q)>.9,"a negative thumb turn bends the other way");
                bool refused=false;try {(void)thumb(-2.f);} catch(const std::invalid_argument&) {refused=true;}
                expect(refused,"a thumb turn past its open limit is refused");
                in.has_finger[1]=false;in.hand_empty[1]=false;
            }
        }
    }

    const Vec3 rest_l{.28,.2,-.62},rest_r{.28,-.2,-.62};
    // Synthetic authored native clips on the real (unscaled) rig, standing in for decoded game
    // animations: deltas are world-axis rotations applied in each joint's bind frame.
    struct Clip {double root_drop=0,root_fwd=0,torso=0,hip=0,knee=0,ankle=0,arm_fwd=0,arm_up=0,elbow=0;};
    // base: optional model-space rest pose (e.g. the captured native idle arm); default bind.
    const auto pose_rig=[&](const Skeleton& sk,const Xf& frame_bind,const std::function<Quaternion(const std::string&)>& delta,Vec3 root_shift,
                            const std::vector<Xf>* base=nullptr) {
        std::vector<Xf> w(sk.local.size());
        for(size_t j=0;j<w.size();++j) {
            auto l=base?(sk.parent[j]<0?(*base)[j]:mul(inverse((*base)[sk.parent[j]]),(*base)[j])):sk.local[j];
            const auto bind_q=mul(frame_bind.q,base?(*base)[j].q:sk.bind[j].q);
            l.q=mul(l.q,mul(conj(bind_q),mul(delta(sk.names[j]),bind_q)));
            if(sk.names[j]=="center_root_jnt") l.p=l.p+root_shift;
            w[j]=sk.parent[j]<0?l:mul(w[sk.parent[j]],l);
        }
        return w;
    };
    const auto make_native=[&](const Clip& c,Xf model) {
        NativePose n;n.model=model;
        const auto Y=[&](double a) {return axis_angle({0,1,0},a*deg);};
        n.body=pose_rig(body,{},[&](const std::string& j) {
            if(j.find("spine0")!=std::string::npos) return Y(c.torso/3);
            if(j.find("_femur_")!=std::string::npos) return Y(-c.hip);
            if(j.find("_tibia_")!=std::string::npos) return Y(c.knee);
            if(j.find("_ankle_")!=std::string::npos) return Y(c.ankle);
            return Quaternion{};},{c.root_fwd,0,-c.root_drop});
        for(int i=0;i<2;++i) {
            const double sgn=i?1:-1;
            n.arm[i]=pose_rig(i?right:left,body.bind[s3],[&](const std::string& j) {
                if(j.find("_arm_jnt")!=std::string::npos) return mul(Y(-c.arm_up),axis_angle({0,0,1},sgn*c.arm_fwd*deg));
                if(j.find("_elbow_jnt")!=std::string::npos) return axis_angle({0,0,1},sgn*c.elbow*deg);
                return Quaternion{};},{},native[i].size()==69?&native[i]:nullptr);
            // The authored clip's hand is the same native hand the VR side uses (one animation).
            if(native[i].size()==69) {
                const auto& sk=i?right:left;
                const int wrist=sk.find(i?"right_wrist_jnt":"left_wrist_jnt");
                for(size_t j=0;j<sk.local.size();++j) {
                    bool under=false;
                    for(int a=sk.parent[j];a>=0;a=sk.parent[a]) if(a==wrist) {under=true;break;}
                    if(under) n.arm[i][j]=mul(n.arm[i][sk.parent[j]],mul(inverse(native[i][sk.parent[j]]),native[i][j]));
                }
            }
        }
        return n;
    };
    const double avatar_scale=probe.scale();
    const auto native_camera=[&](const NativePose& n) {
        return mul(n.model,Xf{n.body[body.find("camera_jnt")].q,n.body[body.find("camera_jnt")].p*avatar_scale});
    };
    // Real game clips (decoded .md6anim): local TRS per frame -> model space, nlerp between frames.
    const auto clip_part=[&](const FixtureClipPart& c,const Skeleton& sk,double time) {
        const double f=std::clamp(time*c.fps,0.,double(c.frames-1));
        const int f0=int(f),f1=std::min(c.frames-1,f0+1);const double a=f-f0;
        std::vector<Xf> w(sk.local.size());
        for(int j=0;j<c.joints;++j) {
            const float* p=c.data+(size_t(f0)*c.joints+j)*7;const float* q=c.data+(size_t(f1)*c.joints+j)*7;
            const Xf l{nlerp(normalized({p[0],p[1],p[2],p[3]}),normalized({q[0],q[1],q[2],q[3]}),a),
                       Vec3{p[4],p[5],p[6]}+(Vec3{q[4],q[5],q[6]}-Vec3{p[4],p[5],p[6]})*a};
            w[j]=sk.parent[j]<0?l:mul(w[sk.parent[j]],l);
        }
        return w;
    };
    const auto clip_pose=[&](const FixtureClip& c,double time) {
        NativePose n;n.body=clip_part(c.part[0],body,time);n.arm[0]=clip_part(c.part[1],left,time);n.arm[1]=clip_part(c.part[2],right,time);
        return n;
    };
    const auto find_clip=[&](const char* name)->const FixtureClip& {
        for(const auto& c:kFixtureClips) if(std::string(name)==c.name) return c;
        throw std::invalid_argument(name);
    };
    NativePose scratch;
    // Idle, then a real game move owned natively (eye follows the clip camera, as the game's camera
    // animation does), then the blend back to tracked idle.
    const auto real_move=[&](const char* clip,Ownership own) {
        const auto& c=find_clip(clip);
        const double start=.6,length=(c.part[0].frames-1)/c.part[0].fps;
        return [&,clip,own,start,length](double t) {
            const auto& cl=find_clip(clip);
            Inputs in;Vec3 e{0,0,eye_height};hold(in,0,e,rest_l,rest_r);
            const double local=std::clamp(t-start,0.,length);
            scratch=clip_pose(cl,local);
            in.native=&scratch;
            if(t>=start && t<start+length) in.own=own;
            const auto cam=native_camera(scratch).p;
            // The camera follows the clip in 0.25 s and stands back up over a realistic 1 s.
            const double follow=smooth01((t-start)/.25)*(1-smooth01((t-start-length)/1.));
            const Vec3 settle{cam.x,cam.y,eye_height};
            const auto base=t<start+length?e:settle;
            e=base+(cam-base)*follow;
            if(t>=start+length) e=settle+(cam-settle)*follow;
            in.eye={eye_rotation(0,15*deg),e};hold(in,0,e,rest_l,rest_r);
            return in;
        };
    };
    const auto clip_seconds=[&](const char* clip) {const auto& c=find_clip(clip);return (c.part[0].frames-1)/c.part[0].fps;};
    std::vector<Scenario> scenarios={
        {"tpose",1.5,[&](double) {Inputs in;in.eye={eye_rotation(0,0),{0,0,eye_height}};
            for(int i=0;i<2;++i) {in.hand[i]={bind_hand[i],{.02,(i?-1:1)*span*.5,eye_height-.27}};in.grip[i]=in.trigger[i]=1;}return in;},{1.4}},
        // Calibration step 1 (shown on the VR settings page): fists at the shoulders, forearms up, palms in.
        {"shoulders",1.5,[&](double) {Inputs in;in.eye={eye_rotation(0,0),{0,0,eye_height}};
            for(int i=0;i<2;++i) {in.hand[i]={hand_rot(i,{0,0,1},{0,i?1.:-1.,0}),{.1,(i?-1:1)*.2,eye_height-.3}};in.grip[i]=in.trigger[i]=1;}return in;},{1.4}},
        {"idle",1.5,[&](double) {Inputs in;Vec3 e{0,0,eye_height};in.eye={eye_rotation(0,0),e};hold(in,0,e,rest_l,rest_r);return in;},{1.4}},
        {"look_down",2,[&](double t) {Inputs in;in.eye=head_on_neck({-.09,0,neck_height},eye_rotation(0,70*deg*smooth01(t/1.2)));
            hold(in,0,{0,0,eye_height},rest_l,rest_r);return in;},{1.9}},
        {"crouch",2.5,[&](double t) {Inputs in;const double c=smooth01((t-.3)/1.5);Vec3 e{.05*c,0,eye_height-.7*c};
            in.eye={eye_rotation(0,20*deg*c),e};hold(in,0,e,{.35,.2,-.45},{.35,-.2,-.45});return in;},{1.0,2.4}},
        {"reach_up",2,[&](double t) {Inputs in;Vec3 e{0,0,eye_height};in.eye=head_on_neck({-.09,0,neck_height},eye_rotation(0,-25*deg*smooth01(t)));
            const double c=smooth01(t/1.2);
            in.hand[0]={hand_rot(0,unit({.2,.1,1}),{0,-1,0}),e+(rest_l*(1-c)+Vec3{.15,.25,.45}*c)};
            in.hand[1]={hand_rot(1,unit({.2,-.1,1}),{0,1,0}),e+(rest_r*(1-c)+Vec3{.15,-.25,.45}*c)};return in;},{1.9}},
        {"cross_body",2,[&](double t) {Inputs in;Vec3 e{0,0,eye_height};in.eye=head_on_neck({-.09,0,neck_height},eye_rotation(15*deg*smooth01(t),15*deg*smooth01(t)));
            const double c=smooth01(t/1.2);hold(in,0,e,rest_l,rest_r);
            in.hand[1]={hand_rot(1,unit({.3,1,0}),{-1,0,0}),e+(rest_r*(1-c)+Vec3{.3,.18,-.3}*c)};return in;},{1.9}},
        {"turn",3,[&](double t) {Inputs in;const double yaw=180*deg*smooth01((t-.3)/2);Vec3 e{0,0,eye_height};
            in.eye=head_on_neck(rotated(rot_z(yaw),{-.09,0,0})+Vec3{0,0,neck_height},eye_rotation(yaw,10*deg));hold(in,yaw,e,rest_l,rest_r);return in;},{1.0,1.6,2.9}},
        {"walk",3,[&](double t) {Inputs in;const double v=1.4*smooth01(t/.5);const double x=t<.5?1.4*t*t:1.4*(t-.25);
            Vec3 e{x,0,eye_height+.02*std::sin(t*2*kPi*1.8)*(v/1.4)};in.eye={eye_rotation(0,15*deg),e};
            hold(in,0,e,rest_l,rest_r);return in;},{1.2,1.4,1.6,1.8,2.9}},
        {"lean_forward",2,[&](double t) {Inputs in;const double c=smooth01(t/1.2);Vec3 e{.35*c,0,eye_height-.3*c};
            in.eye={eye_rotation(0,35*deg*c),e};hold(in,0,e,{.4,.2,-.4},{.4,-.2,-.4});return in;},{1.9}},
        {"aim",2,[&](double t) {Inputs in;Vec3 e{0,0,eye_height};in.eye={eye_rotation(0,0),e};const double c=smooth01(t/1.2);
            hold(in,0,e,rest_l,rest_r);
            in.hand[1]={hand_rot(1,unit({1,0,-.1}),{0,1,0}),e+(rest_r*(1-c)+Vec3{.55,-.05,-.12}*c)};
            in.hand[0]={hand_rot(0,unit({1,-.3,0}),{0,-1,-.3}),e+(rest_l*(1-c)+Vec3{.42,.05,-.2}*c)};
            in.grip[1]=1;in.trigger[1]=c;in.grip[0]=.6;return in;},{1.9}},
        {"peek",2,[&](double t) {Inputs in;const double c=smooth01(t/1.2);Vec3 e{0,.25*c,eye_height-.05*c};
            in.eye={eye_rotation(0,0,-15*deg*c),e};hold(in,0,e,rest_l,rest_r);return in;},{1.9}},
        // Walk, jump (legs tuck natively), land and keep walking.
        {"jump",3,[&](double t) {Inputs in;const double x=std::max(0.,t-.3);
            const double u=(t-1.2)/.6;const bool air=u>=0 && u<1;const double lift=air?.35*std::sin(kPi*u):0;
            Vec3 e{x,0,eye_height+lift};in.eye={eye_rotation(0,10*deg),e};hold(in,0,e,rest_l,rest_r);
            if(air) {
                const double k=std::sin(kPi*u);
                scratch=make_native({0,0,0,75*k,110*k,25*k},{{},{x-.05,0,lift}});
                in.native=&scratch;in.own=native_ownership("Jumping");
            }
            return in;},{1.15,1.5,1.85,2.3}},
        // Full-body authored takedown: lunge and stab with both arms, camera follows the clip.
        {"assassination",3.2,[&](double t) {Inputs in;Vec3 e{0,0,eye_height};
            const double r=smooth01((t-.8)/.35)*(1-smooth01((t-2.2)/.4));
            hold(in,0,e,rest_l,rest_r);
            if(t>=.8 && t<2.6) {
                scratch=make_native({.35*r,.15*r,30*r,60*r,95*r,15*r,85*r,10*r,30*r},{{},{0,0,0}});
                in.native=&scratch;if(t<2.4) in.own=native_ownership("Assassination");
                const auto cam=native_camera(scratch);
                e=e+(cam.p-e)*r;
            }
            in.eye={eye_rotation(0,15*deg),e};return in;},{.7,1.0,1.6,2.5,3.1}},
        {"real_land_big",.6+clip_seconds("land_big")+1.6,real_move("land_big",{1,1,{0,0},.1,.3}),
            {.5,.72,.6+clip_seconds("land_big")*.4,.6+clip_seconds("land_big")*.8,.6+clip_seconds("land_big")+.15,.6+clip_seconds("land_big")+1.5}},
        {"real_assassination",.6+clip_seconds("assassination_back")+1.6,real_move("assassination_back",native_ownership("Assassination")),
            {.5,.72,.6+clip_seconds("assassination_back")*.4,.6+clip_seconds("assassination_back")*.8,.6+clip_seconds("assassination_back")+.15,.6+clip_seconds("assassination_back")+1.5}},
        {"real_vault",.6+clip_seconds("vault")+1.6,real_move("vault",native_ownership("Vault")),
            {.5,.72,.6+clip_seconds("vault")*.4,.6+clip_seconds("vault")*.8,.6+clip_seconds("vault")+.15,.6+clip_seconds("vault")+1.5}},
        {"real_fatality",.6+clip_seconds("guard_fatality")+1.6,real_move("guard_fatality",native_ownership("NpcFatality")),
            {.5,.72,.6+clip_seconds("guard_fatality")*.3,.6+clip_seconds("guard_fatality")*.6,.6+clip_seconds("guard_fatality")+.15,.6+clip_seconds("guard_fatality")+1.5}},
        {"real_choke",.6+clip_seconds("choke")+1.6,real_move("choke",native_ownership("Choke")),
            {.5,.72,.6+clip_seconds("choke")*.5,.6+clip_seconds("choke")+.15,.6+clip_seconds("choke")+1.5}},
        // Mantle onto a 0.9 m ledge: arms reach up onto it, body climbs, feet land on the new floor.
        {"mantle",3,[&](double t) {Inputs in;const double u=(t-.6)/1.2;Vec3 e{0,0,eye_height};
            const double climb=smooth01(u);in.ground=.9*smooth01((u-.85)/.15);
            hold(in,0,e+Vec3{.5*climb,0,.9*climb},rest_l,rest_r);
            if(u>=0 && u<1.15) {
                const double r=smooth01(u/.2)*(1-smooth01((u-.8)/.2)),k=std::sin(kPi*std::clamp(u,0.,1.));
                scratch=make_native({0,0,20*k,90*k,110*k,20*k,70*r,60*r,20*r},{{},{.5*climb-.05,0,.9*climb}});
                in.native=&scratch;if(u<1) in.own=native_ownership("Mantle");
                e=native_camera(scratch).p;
            } else e=e+Vec3{.5*climb,0,.9*climb};
            in.eye={eye_rotation(0,20*deg),e};return in;},{.5,.9,1.3,1.9,2.8}},
    };
    std::string json="{\"character\":\""+std::string(argc>2?argv[2]:"corvo")+"\",\"frames\":[";
    bool first_frame=true;
    const bool dense=argc>3 && std::string(argv[3])=="dense";
    std::string dense_json;bool dense_first=true;
    const auto put=[&](const Xf& x,double s) {
        Vec3 c[3]={rotated(x.q,{s,0,0}),rotated(x.q,{0,s,0}),rotated(x.q,{0,0,s})};
        char buf[400];
        std::snprintf(buf,sizeof buf,"[%.6f,%.6f,%.6f,0,%.6f,%.6f,%.6f,0,%.6f,%.6f,%.6f,0,%.6f,%.6f,%.6f,1]",
            c[0].x,c[0].y,c[0].z,c[1].x,c[1].y,c[1].z,c[2].x,c[2].y,c[2].z,x.p.x,x.p.y,x.p.z);
        return std::string(buf);
    };
    const auto list=[&](const std::vector<Xf>& v,const Xf* frame,double s) {
        std::string r="[";
        for(size_t i=0;i<v.size();++i) r+=(i?",":"")+put(frame?mul(inverse(*frame),v[i]):v[i],s);
        return r+"]";
    };
    for(const auto& sc:scenarios) {
        BodySolver solver(body,left,right,{eye_height,span});
        const double dt=1/90.;
        std::vector<Xf> previous_body,previous_arm[2];
        Vec3 previous_feet[2]{};bool was_stepping[2]{};double was_owned_legs=0;
        std::vector<Quaternion> previous_q,previous_native_q;std::vector<double> previous_speed,previous_native_speed;
        Vec3 previous_native_root{};bool has_native_root=false;Quaternion previous_eye_q{},previous_hand_q[2]{};double previous_owned[4]{};
        size_t next=0;
        for(int n=0;n*dt<=sc.seconds;++n) {
            const double t=n*dt;
            auto in=sc.at(t);in.dt=dt;
            if(std::string(sc.name)!="tpose" && std::string(sc.name)!="shoulders") for(int i=0;i<2;++i) if(native[i].size()==69) in.native_arm[i]=&native[i];
            const auto out=solver.solve(in);
            const std::string tag=std::string(sc.name)+" t="+std::to_string(t);
            if(argc>3 && std::string(argv[3])==sc.name && t>.6 && t<.85) {
                const auto& rh=out.arm[1][solver.skeleton(2).find("right_hand_attachment_jnt")];
                std::printf("own %.3f %.3f %.3f %.3f hand %.3f %.3f %.3f reach %d ",out.owned[0],out.owned[1],out.owned[2],out.owned[3],rh.p.x,rh.p.y,rh.p.z,out.hand_reached[1]);
                const auto& p=out.body[solver.skeleton(0).find("center_root_jnt")].p;
                std::printf("%5.3f pelvis %.3f %.3f %.3f | L %.3f %.3f %.3f %d | R %.3f %.3f %.3f %d\n",t,p.x,p.y,p.z,
                    out.feet[0].x,out.feet[0].y,out.feet[0].z,out.stepping[0],out.feet[1].x,out.feet[1].y,out.feet[1].z,out.stepping[1]);
            }
            const auto& sk=solver.skeleton(0);
            const int camera=sk.find("camera_jnt");
            expect(length(out.body[camera].p-in.eye.p)<1e-6 && angle_between(out.body[camera].q,in.eye.q)<1e-5,tag+" head exact");
            for(int i=0;i<2;++i) {
                const auto& arm=solver.skeleton(1+i);
                const int attach=arm.find(i?"right_hand_attachment_jnt":"left_hand_attachment_jnt");
                const auto hand=out.arm[i][attach];
                // Controller fixes the wrist through the bind wrist->attachment offset;
                // the native animated attachment local then carries the weapon.
                const int wrist_joint=arm.find(i?"right_wrist_jnt":"left_wrist_jnt");
                auto bind_attach=mul(inverse(arm.bind[wrist_joint]),arm.bind[attach]);bind_attach.p=bind_attach.p*solver.scale();
                const auto want=mul(in.hand[i],inverse(bind_attach)),wrist=out.arm[i][wrist_joint];
                if(out.hand_reached[i] && in.native_arm[i])
                    expect(length(wrist.p-want.p)<2e-4 && angle_between(wrist.q,want.q)<1e-4,tag+" hand exact");
                else if(out.hand_reached[i])
                    expect(length(hand.p-in.hand[i].p)<2e-4 && angle_between(hand.q,in.hand[i].q)<1e-4,tag+" hand exact");
                expect(length(out.arm_frame[i].p-out.body[s3].p)<1e-9,tag+" arm attached");
                const int femur=sk.find(i?"right_femur_jnt":"left_femur_jnt"),tibia=sk.find(i?"right_tibia_jnt":"left_tibia_jnt"),
                    ankle=sk.find(i?"right_ankle_jnt":"left_ankle_jnt");
                expect(std::abs(length(out.body[tibia].p-out.body[femur].p)-length(sk.bind[tibia].p-sk.bind[femur].p))<1e-6 &&
                       std::abs(length(out.body[ankle].p-out.body[tibia].p)-length(sk.bind[ankle].p-sk.bind[tibia].p))<1e-6,tag+" leg lengths");
                if(!out.stepping[i] && out.owned[0]==0) expect(std::abs(out.body[ankle].p.z-in.ground-sk.bind[ankle].p.z)<1e-3,tag+" planted foot on ground");
                if(!out.stepping[i] && !out.dragged[i] && out.owned[0]==0 && !previous_body.empty() && !was_stepping[i] && was_owned_legs==0)
                    expect(std::hypot(out.feet[i].x-previous_feet[i].x,out.feet[i].y-previous_feet[i].y)<1e-4,tag+" planted foot does not slide");
                previous_feet[i]=out.feet[i];was_stepping[i]=out.stepping[i];
                const auto mid=(out.body[femur].p+out.body[ankle].p)*.5;
                const auto foot_fwd=rotated(out.body[ankle].q,rotated(conj(sk.bind[ankle].q),{1,0,0}));
                if(out.owned[0]==0) expect(dot(out.body[tibia].p-mid,foot_fwd)>-.005,tag+" knee forward");
            }
            // Native reference (world) for this frame: the authored clip's own motion is allowed;
            // blending must not add speed or pops beyond it.
            std::vector<Quaternion> native_q;Vec3 native_root{};
            const int root=solver.skeleton(0).find("center_root_jnt");
            if(in.native) {
                const auto& np=*in.native;
                for(const auto& x:np.body) native_q.push_back(mul(np.model.q,x.q));
                const auto chest=mul(np.model.q,np.body[s3].q);
                for(int i=0;i<2;++i) for(const auto& x:np.arm[i]) native_q.push_back(mul(chest,x.q));
                native_root=rotated(np.model.q,np.body[root].p*avatar_scale)+np.model.p;
            }
            std::vector<Quaternion> out_q;
            for(const auto& x:out.body) out_q.push_back(x.q);
            for(int i=0;i<2;++i) for(const auto& x:out.arm[i]) out_q.push_back(x.q);
            if(!previous_q.empty()) {
                std::vector<double> speed(out_q.size()),native_speed(out_q.size(),0.);
                const bool have_native=native_q.size()==out_q.size() && previous_native_q.size()==out_q.size();
                for(size_t j=0;j<out_q.size();++j) {
                    speed[j]=angle_between(out_q[j],previous_q[j]);
                    if(have_native) native_speed[j]=angle_between(native_q[j],previous_native_q[j]);
                }
                double worst_speed=-1,worst_pop=-1;std::string at_speed,at_pop;
                const auto joint_name=[&](size_t j) {
                    return j<out.body.size()?solver.skeleton(0).names[j]:
                        solver.skeleton(j<out.body.size()+69?1:2).names[(j-out.body.size())%69];
                };
                // Tracked inputs (eye, controllers) move joints too: allow their own angular speed.
                double input_speed=angle_between(in.eye.q,previous_eye_q);
                for(int i=0;i<2;++i) input_speed=std::max(input_speed,angle_between(in.hand[i].q,previous_hand_q[i]));
                for(size_t j=0;j<out_q.size();++j) {
                    const double excess=speed[j]-native_speed[j]-input_speed;
                    if(excess>worst_speed) {worst_speed=excess;at_speed=joint_name(j);}
                    if(!previous_speed.empty()) {
                        const double native_change=previous_native_speed.size()==out_q.size()?std::abs(native_speed[j]-previous_native_speed[j]):0.;
                        // Fingers (small, leaf joints inheriting every parent) get 8 deg; the rest 6.
                        const auto name=joint_name(j);
                        const bool finger=name.find("thumb")!=std::string::npos || name.find("index")!=std::string::npos ||
                            name.find("medium")!=std::string::npos || name.find("middle")!=std::string::npos || name.find("ring_")!=std::string::npos || name.find("pinky")!=std::string::npos;
                        const double pop=std::abs(speed[j]-previous_speed[j])-native_change-(finger?2*deg:0.);
                        if(pop>worst_pop) {worst_pop=pop;at_pop=name;}
                    }
                }
                // An authored move starting (any region blending toward native) may snap the body into
                // it quickly: 16 deg/frame then; 12 otherwise. Pops (discontinuities) stay strict.
                bool blending_in=false;
                for(int k=0;k<4;++k) if(out.owned[k]>previous_owned[k]+1e-9) blending_in=true;
                expect(worst_speed<(blending_in?16:12)*deg,tag+" speed beyond the authored clip "+std::to_string(worst_speed/deg)+" deg/frame at "+at_speed);
                expect(worst_pop<6*deg,tag+" pop "+std::to_string(worst_pop/deg)+" deg/frame change at "+at_pop);
                previous_speed=speed;previous_native_speed=have_native?native_speed:std::vector<double>(out_q.size(),0.);
                const double native_step=in.native && has_native_root?length(native_root-previous_native_root):0.;
                expect(length(out.body[root].p-previous_body[root].p)<.06+native_step,
                       tag+" pelvis jump "+std::to_string(length(out.body[root].p-previous_body[root].p)));
            }
            for(int k=0;k<4;++k) previous_owned[k]=out.owned[k];
            previous_eye_q=in.eye.q;previous_hand_q[0]=in.hand[0].q;previous_hand_q[1]=in.hand[1].q;
            previous_q=out_q;previous_native_q=native_q;previous_native_root=native_root;has_native_root=in.native!=nullptr;
            previous_body=out.body;previous_arm[0]=out.arm[0];previous_arm[1]=out.arm[1];was_owned_legs=out.owned[0];
            // Dense mode (argv[3]=="dense" [argv[4] native label]): every third frame (30 fps) of
            // each scenario goes to <argv[1] directory>/<scenario>.json for video rendering.
            const bool dense_frame=dense && n%3==0;
            if(dense_frame || (!dense && next<sc.exports.size() && t+1e-9>=sc.exports[next])) {
                if(!dense) ++next;
                char label[96];std::snprintf(label,sizeof label,"%s_%.1fs",sc.name,t);
                std::string f=std::string((dense?dense_first:first_frame)?"":",")+"{\"label\":\""+label+"\",\"t\":"+std::to_string(t)+
                    ",\"body\":"+list(out.body,nullptr,out.scale)+
                    ",\"left_arm\":"+list(out.arm[0],&out.arm_frame[0],out.scale)+",\"right_arm\":"+list(out.arm[1],&out.arm_frame[1],out.scale)+
                    ",\"left_arm_frame\":"+put(out.arm_frame[0],1)+",\"right_arm_frame\":"+put(out.arm_frame[1],1)+
                    ",\"eye\":{\"position\":["+std::to_string(in.eye.p.x)+","+std::to_string(in.eye.p.y)+","+std::to_string(in.eye.p.z)+
                    "],\"rotation_xyzw\":["+std::to_string(in.eye.q.x)+","+std::to_string(in.eye.q.y)+","+std::to_string(in.eye.q.z)+","+std::to_string(in.eye.q.w)+"]}"+
                    ",\"owned\":["+std::to_string(out.owned[0])+","+std::to_string(out.owned[1])+","+std::to_string(out.owned[2])+","+std::to_string(out.owned[3])+"]"+
                    ",\"markers\":[";
                for(int i=0;i<2;++i) f+=std::string(i?",":"")+"{\"name\":\""+(i?"right":"left")+"_grip\",\"position\":["+
                    std::to_string(in.hand[i].p.x)+","+std::to_string(in.hand[i].p.y)+","+std::to_string(in.hand[i].p.z)+"]}";
                if(dense) {dense_json+=f+"]}";dense_first=false;}
                else {json+=f+"]}";first_frame=false;}
            }
        }
        if(dense) {
            const std::string dir=std::string(argv[1]).substr(0,std::string(argv[1]).find_last_of("/\\")+1);
            std::ofstream f(dir+sc.name+".json",std::ios::binary);
            f<<"{\"character\":\""<<(argc>2?argv[2]:"corvo")<<"\",\"scenario\":\""<<sc.name<<"\",\"fps\":30,\"frames\":["<<dense_json<<"]}";
            dense_json.clear();dense_first=true;
        }
    }
    // Moving with a native walk cycle: legs copy the native local rotations and move smoothly.
    for(const double speed:{3.,5.}) {
        BodySolver solver(body,left,right,{eye_height,span});
        const auto& sk=solver.skeleton(0);
        const int root=sk.find("center_root_jnt");
        const char* legs[6]={"left_femur_jnt","left_tibia_jnt","right_femur_jnt","right_tibia_jnt","left_ankle_jnt","right_ankle_jnt"};
        double worst_angle=0,worst_jump=0;Vec3 prev_ankle[2]{};double x=0;
        for(int n=0;n<360;++n) {
            const double dt=1/90.,t=n*dt;x+=speed*dt;
            const double ph=2*kPi*(speed>4?1.4:.9)*t,sw=30*std::sin(ph),kn=40*std::max(0.,std::sin(ph));
            NativePose np;
            np.body=pose_rig(body,{},[&](const std::string& j) {
                const auto Y=[&](double a) {return axis_angle({0,1,0},a*deg);};
                if(j=="left_femur_jnt") return Y(-sw);
                if(j=="right_femur_jnt") return Y(sw);
                if(j=="left_tibia_jnt") return Y(kn);
                if(j=="right_tibia_jnt") return Y(40*std::max(0.,-std::sin(ph)));
                return Quaternion{};},{});
            Inputs in;in.dt=dt;in.hand_valid[0]=in.hand_valid[1]=false;
            in.eye={eye_rotation(0,0),{x,0,eye_height}};in.native=&np;
            const auto out=solver.solve(in);
            for(int i=0;i<2;++i) {
                const int a=sk.find(legs[4+i]);
                if(n>5) worst_jump=std::max(worst_jump,length(out.body[a].p-prev_ankle[i]));
                prev_ankle[i]=out.body[a].p;
            }
            if(n<90) continue; // 0.2 s ease plus the speed filter have settled
            for(int k=0;k<4;++k) {
                const int j=sk.find(legs[k]),par=sk.parent[j];
                const auto mine=mul(conj(out.body[par].q),out.body[j].q),theirs=mul(conj(np.body[par].q),np.body[j].q);
                worst_angle=std::max(worst_angle,angle_between(mine,theirs));
            }
            (void)root;
        }
        std::cerr<<"native walk "<<speed<<" leg angle err "<<worst_angle/deg<<" deg, ankle jump "<<worst_jump<<std::endl;
        expect(worst_angle<3*deg,"moving legs follow the native cycle");
        expect(worst_jump<.15,"moving ankles move smoothly frame to frame");
    }
    // Running 4-6 m/s: feet stay bounded around the pelvis and never trail the eye far.
    for(const double speed:{4.,5.,6.}) {
        BodySolver solver(body,left,right,{eye_height,span});
        const double sw=probe.scale();double worst_back=-9,worst_pelvis=0,x=0;
        for(int n=0;n<300;++n) {
            const double dt=(n%3==0)?1/30.:1/60.;x+=speed*dt;
            Inputs in;in.dt=dt;in.hand_valid[0]=in.hand_valid[1]=false;
            in.eye={eye_rotation(0,0),{x,0,eye_height}};
            const auto out=solver.solve(in);
            if(n<3) continue;
            const auto pel=out.body[solver.skeleton(0).find("center_root_jnt")].p;
            for(int i=0;i<2;++i) {
                const auto a=out.body[solver.skeleton(0).find(i?"right_ankle_jnt":"left_ankle_jnt")].p;
                worst_back=std::max(worst_back,in.eye.p.x-a.x);
                worst_pelvis=std::max(worst_pelvis,std::hypot(a.x-pel.x,a.y-pel.y));
            }
        }
        std::cerr<<"run "<<speed<<" foot behind eye "<<worst_back<<" foot-pelvis "<<worst_pelvis<<std::endl;
        expect(worst_back<.3,"running feet never trail the eye by more than 0.3 m");
        expect(worst_pelvis<.3*sw,"running feet stay within bounded distance of pelvis");
    }
    {   // Forearm roll (headset v627 thin right wrist): a hinge flip that winds the unwrap a full turn must not pin the helpers at the clamp.
        const double truth=18*deg;double roll=0;bool has=false;
        for(int k=0;k<=6;++k) {roll=forearm_roll(roll,has,wrap_pi(truth+k*120*deg));has=true;expect(std::abs(roll)<=2.3+1e-12,"forearm roll stays within the clamp");}
        for(int k=0;k<3;++k) roll=forearm_roll(roll,true,truth);
        expect(std::abs(roll-truth)<1e-9,"forearm roll returns to the hand after a wound-up turn");
        has=false;
        for(int a=-120;a<=120;a+=10) {roll=forearm_roll(roll,has,a*deg);has=true;expect(std::abs(roll-a*deg)<1e-9,"forearm roll follows a smooth sweep");}
        roll=forearm_roll(roll,true,170*deg);roll=forearm_roll(roll,true,-170*deg);   // across +-180: held at the limit, not flipped to the other side
        expect(std::abs(roll-2.3)<1e-12,"forearm roll holds the near limit across +-180");
    }
    json+="]}";
    if(argc>1) {
        std::ofstream f(argv[1],std::ios::binary);f<<json;
        if(!f) {std::cerr<<"cannot write "<<argv[1]<<"\n";return 2;}
    }
    std::cout<<"scale "<<probe.scale()<<" arm_scale "<<probe.arm_scale()<<"\n";
    if(failures) {std::cerr<<failures<<" failures\n";return 1;}
    std::cout<<"body_ik_check passed\n";
    return 0;
}

int main(int argc,char** argv) {
    try {return run(argc,argv);}
    catch(const std::exception& e) {std::cerr<<"exception: "<<e.what()<<"\n";return 3;}
}
