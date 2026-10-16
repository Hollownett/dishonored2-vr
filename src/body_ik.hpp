#pragma once
// Full-body VR pose from head + two hands (Boneworks/VRIK-style), pure math.
// World: Z up, metres; the avatar faces +X at yaw 0 (+Y is its left), matching the
// native body skeleton's bind frame. Inputs are the HMD eye pose (camera_jnt) and
// the target world pose of each hand_attachment_jnt. Output is rigid world
// transforms for the 20 body joints and both 69-joint arms (scaled skeleton);
// export multiplies rotations by `scale` for skinning. No game calls or writes.
#include "arm_body.hpp"
#include <algorithm>
#include <string>
#include <vector>
#include <optional>

namespace d2vr::body {
using rig::kPi;
using rig::smooth01;
using rig::wrap_pi;

inline Quaternion conj(Quaternion q) {return {-q.x,-q.y,-q.z,q.w};}
inline Quaternion axis_angle(Vec3 axis,double angle) {
    const double n=length(axis);
    if(!(n>1e-12) || !std::isfinite(angle)) return {};
    const double s=std::sin(angle*.5)/n;
    return {axis.x*s,axis.y*s,axis.z*s,std::cos(angle*.5)};
}
inline Quaternion mul(Quaternion a,Quaternion b) {return normalized(multiplied(a,b));}
inline Quaternion nlerp(Quaternion a,Quaternion b,double t) {
    const double d=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(d<0) b={-b.x,-b.y,-b.z,-b.w};
    return normalized({a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t,a.z+(b.z-a.z)*t,a.w+(b.w-a.w)*t});
}
// Rotation whose columns are the given orthonormal axes.
inline Quaternion from_axes(Vec3 x,Vec3 y,Vec3 z) {
    const double m00=x.x,m11=y.y,m22=z.z,trace=m00+m11+m22;
    Quaternion q;
    if(trace>0) {const double s=std::sqrt(trace+1)*2;q={(y.z-z.y)/s,(z.x-x.z)/s,(x.y-y.x)/s,s*.25};}
    else if(m00>m11 && m00>m22) {const double s=std::sqrt(1+m00-m11-m22)*2;q={s*.25,(y.x+x.y)/s,(z.x+x.z)/s,(y.z-z.y)/s};}
    else if(m11>m22) {const double s=std::sqrt(1+m11-m00-m22)*2;q={(y.x+x.y)/s,s*.25,(z.y+y.z)/s,(z.x-x.z)/s};}
    else {const double s=std::sqrt(1+m22-m00-m11)*2;q={(z.x+x.z)/s,(z.y+y.z)/s,s*.25,(x.y-y.x)/s};}
    return normalized(q);
}
// Rotation taking direction a to b and hinge ha to hb (hinges made perpendicular).
inline Quaternion frame_map(Vec3 a,Vec3 ha,Vec3 b,Vec3 hb) {
    const auto basis=[](Vec3 d,Vec3 h,Vec3 out[3]) {
        d=unit(d);h=unit(h-d*dot(h,d));out[0]=d;out[1]=h;out[2]=cross(d,h);
    };
    Vec3 f0[3],f1[3];basis(a,ha,f0);basis(b,hb,f1);
    const auto r0=from_axes(f0[0],f0[1],f0[2]),r1=from_axes(f1[0],f1[1],f1[2]);
    return mul(r1,conj(r0));
}
inline Quaternion between(Vec3 a,Vec3 b) {
    a=unit(a);b=unit(b);
    const double c=std::clamp(dot(a,b),-1.,1.);
    if(c>1-1e-12) return {};
    auto axis=cross(a,b);
    if(length(axis)<1e-9) axis=std::abs(a.x)<.9?cross(a,{1,0,0}):cross(a,{0,1,0});
    return axis_angle(axis,std::acos(c));
}
// Signed twist of q about unit axis (swing-twist decomposition).
inline double twist_angle(Quaternion q,Vec3 axis) {
    const double p=q.x*axis.x+q.y*axis.y+q.z*axis.z;
    return wrap_pi(2*std::atan2(p,q.w));
}
inline Quaternion rot_z(double a) {return axis_angle({0,0,1},a);}

struct Xf {Quaternion q{};Vec3 p{};};
inline bool valid_pose(const Xf& x) {
    const double n=x.q.x*x.q.x+x.q.y*x.q.y+x.q.z*x.q.z+x.q.w*x.q.w;
    return finite(x.p) && std::isfinite(n) && std::abs(n-1)<.02;
}
inline Xf mul(const Xf& a,const Xf& b) {return {mul(a.q,b.q),a.p+rotated(a.q,b.p)};}
inline Xf inverse(const Xf& a) {const auto c=conj(a.q);return {c,rotated(c,a.p)*-1};}

struct Skeleton {
    std::vector<std::string> names;std::vector<int> parent;std::vector<Xf> local,bind;
    int find(const std::string& name) const {
        for(size_t i=0;i<names.size();++i) if(names[i]==name) return static_cast<int>(i);
        throw std::invalid_argument("Missing joint "+name);
    }
    void finish() {
        if(local.empty() || names.size()!=local.size() || parent.size()!=local.size())
            throw std::invalid_argument("Incomplete body skeleton");
        bind.resize(local.size());
        for(size_t i=0;i<local.size();++i) {
            if(parent[i]<-1 || parent[i]>=static_cast<int>(i) || !valid_pose(local[i]))
                throw std::invalid_argument("Invalid body skeleton joint");
            bind[i]=parent[i]<0?local[i]:mul(bind[parent[i]],local[i]);
        }
    }
};

// The game's own animated pose for this frame, in native units (unscaled): body joints in body
// model space, arm joints in arm-model space (arm frame = body center_spine03_jnt), and the body
// model frame in world. In game this is the renderer upload source the mod already hooks.
struct NativePose {Xf model;std::vector<Xf> body,arm[2];};
// Which regions an authored native state owns (1 = native), and how fast to blend in/out.
struct Ownership {double legs{},torso{},arm[2]{};double in_seconds=.15,out_seconds=.3;};
// Policy by player FSM state (RTTI class name without the arkPlayerState prefix). Hands stay
// tracked unless the action is a two-handed authored interaction; legs go native whenever the
// player is airborne or the move is authored; full-body authored moves own everything.
inline Ownership native_ownership(const std::string& state) {
    const auto is=[&](std::initializer_list<const char*> names) {
        for(auto n:names) if(state==n) return true;
        return false;
    };
    if(is({"Assassination","Choke","DropChoke","CureChoke","NpcFatality","BloodthirstyFatality","Versus","VersusGrab",
           "SyncAnim","KnockedDown","Dead","Stun","Stumble","KillCam","Crank","Peeking","GrabGrappledEntity"}))
        return {1,1,{1,1},.15,.35};
    if(is({"Mantle","Vault"})) return {1,1,{1,1},.25,.35};
    if(is({"Jumping","Falling","CatFall","Slipping"})) return {1,0,{0,0},.1,.2};
    if(is({"Sliding"})) return {1,.6,{0,0},.12,.3};
    // Carrying a body: the right arm holds it, the left hand stays the player's (user, headset v349). The Heart is held
    // like a grenade: both hands tracked, the native attachment carries it.
    if(is({"CarryCorpse"})) return {0,.3,{0,1},.2,.3};
    if(is({"DropCorpse"})) return {0,.3,{1,1},.2,.3};
    if(is({"EatRat","StoneShroud"})) return {0,0,{1,1},.2,.3};
    return {};
}

struct Calibration {
    double eye_height=1.70;   // standing HMD height above the floor
    double hand_span=0;       // T-pose hand_attachment distance; 0 = avatar proportions
    double shoulder_span=0,eye_to_shoulder=0; // optional measured fit in metres
    double torso_scale=1; // verified native chest-surface transverse fit
    double arm_reach[2]{}; // independent shoulder-to-attachment reach; 0 uses hand_span
};
struct Inputs {
    Xf eye;                   // camera_jnt world pose (looks along +X, +Z up)
    Xf hand[2];               // target world pose of left/right hand_attachment_jnt
    bool hand_valid[2]{true,true};
    double ground=0;          // floor height under the player
    double grip[2]{},trigger[2]{}; // controller analog values 0..1 drive the fingers
    // Empty hand: fingers curl from grip/trigger instead of copying the native (flat) pose.
    bool hand_empty[2]{};
    std::uint64_t native_socket_token[2]{};
    // Per-joint finger hinge turns (rad over the bind pose; finger_ik.hpp finger_turns): thumb 01-03, other fingers 02-04. An empty hand
    // with them closes by contact and by what it holds instead of grip/trigger; the metacarpals stay on the bind pose (the contact
    // planned the fingers from it: a metacarpal that moved would move their knuckles and hinges).
    float finger_turn[2][5][3]{};bool has_finger[2]{};
    // The hinge each of those joints turns about, unit, in the arm-model space of the bind pose (finger_ik.hpp hinge_axes: the axes the
    // contact was tested with, so the bones bend exactly as tested, never sideways). Without them the solver's own bind-pose axes apply.
    float finger_axis[2][5][3][3]{};bool has_finger_axis[2]{};
    // Held item: rotate the stable socket local 180 degrees about the palm normal (blade points the other way).
    bool reverse_grip[2]{};
    // Optional native animated arm pose (69 joints, arm-model space, unscaled). When present the
    // whole hand subtree keeps the game's authored fingers, helpers and accessories (ring).
    const std::vector<Xf>* native_arm[2]{};
    // Optional native pose plus the ownership the current native state requests (see native_ownership).
    const NativePose* native{};
    Ownership own{};
    double dt=1/90.;
    Xf raw_hand[2]{};
    bool raw_hand_valid[2]{};
};
struct Output {
    std::vector<Xf> body,arm[2];Xf arm_frame[2];
    double scale=1;bool hand_reached[2]{};Vec3 feet[2];bool stepping[2]{};
    double owned[4]{}; // eased native weights: legs, torso, left arm, right arm
    double phase[2]{};Vec3 swing_from[2]{},swing_to[2]{};
    bool dragged[2]{}; // planted foot was beyond reach and dragged with the leg this frame
    // How each hand's fingers were posed this solve: 0 not posed, 1 the contact turns (finger_ik.hpp), 2 grip and trigger alone, 3 the game's own
    // fingers (a hand that holds an authored item: a sword, a pistol).
    int finger_mode[2]{};
    double thumb_applied[2][3]{}; // degrees each thumb joint was turned by this solve (what finger_local applied of the contact's turn)
    std::vector<Xf> raw_arm[2];
    bool raw_hand_reached[2]{};
};

class BodySolver {
    struct ArmState {
        Vec3 pole{};
        double roll{},hold_steady{},thumb_applied[3]{};
        Xf hold{},hold_prev{};
        std::uint64_t socket_token{};
        bool has_pole{},has_roll{},has_hold{};
        int finger_mode{};
    };
public:
    class FrozenArm {
        friend class BodySolver;
        ArmState state_{};
        Inputs inputs_{};
        Xf chest_{};
        unsigned side_{};
    };
    std::optional<FrozenArm> freeze_arm(unsigned side,const Inputs& inputs,const Xf& chest) const {
        if(side>1 || !inputs.hand_valid[side] || !inputs.hand_empty[side] || inputs.own.arm[side]!=0 || !valid_pose(chest))return {};
        FrozenArm result;result.side_=side;result.state_=arm_state_[side];result.inputs_=inputs;result.chest_=chest;
        result.inputs_.native=nullptr;
        result.inputs_.native_arm[0]=result.inputs_.native_arm[1]=nullptr;
        return result;
    }
    std::vector<Xf> rearm(const FrozenArm& frozen,const Xf& attachment,bool& reached) const {
        if(frozen.side_>1 || !valid_pose(attachment))throw std::invalid_argument("Invalid frozen arm target");
        auto state=frozen.state_;auto inputs=frozen.inputs_;inputs.hand[frozen.side_]=attachment;
        const auto delta=mul(frozen.chest_.q,conj(sk_[0].bind[spine_[2]].q));
        return pose_arm(static_cast<int>(frozen.side_),frozen.chest_,delta,inputs,0,reached,state);
    }
    BodySolver(Skeleton body,Skeleton left,Skeleton right,Calibration calibration):cal_(calibration) {
        if(!std::isfinite(cal_.eye_height) || cal_.eye_height<=0 || cal_.eye_height>3 ||
           !std::isfinite(cal_.hand_span) || cal_.hand_span<0 || cal_.hand_span>3 ||
           !std::isfinite(cal_.shoulder_span) || cal_.shoulder_span<0 || cal_.shoulder_span>.95 ||
           !std::isfinite(cal_.eye_to_shoulder) || cal_.eye_to_shoulder<0 || cal_.eye_to_shoulder>.4 ||
           !std::isfinite(cal_.torso_scale) || cal_.torso_scale<.6 || cal_.torso_scale>2 ||
           !std::isfinite(cal_.arm_reach[0]) || !std::isfinite(cal_.arm_reach[1]) ||
           cal_.arm_reach[0]<0 || cal_.arm_reach[0]>1.2 || cal_.arm_reach[1]<0 || cal_.arm_reach[1]>1.2)
            throw std::invalid_argument("Invalid body calibration");
        sk_[0]=std::move(body);sk_[1]=std::move(left);sk_[2]=std::move(right);
        for(auto& s:sk_) s.finish();
        auto& b=sk_[0];
        root_=b.find("center_root_jnt");spine_[0]=b.find("center_spine01_jnt");spine_[1]=b.find("center_spine02_jnt");
        spine_[2]=b.find("center_spine03_jnt");neck_=b.find("center_neck_jnt");head_=b.find("center_head_jnt");
        camera_=b.find("camera_jnt");anchor_=b.find("anchor_jnt");
        const char* side[2]={"left","right"};
        for(int i=0;i<2;++i) {
            const std::string s=side[i];
            femur_[i]=b.find(s+"_femur_jnt");tibia_[i]=b.find(s+"_tibia_jnt");ankle_[i]=b.find(s+"_ankle_jnt");
            auto& a=sk_[1+i];
            clavicle_[i]=a.find(s+"_shoulder_jnt");upper_[i]=a.find(s+"_arm_jnt");elbow_[i]=a.find(s+"_elbow_jnt");
            wrist_[i]=a.find(s+"_wrist_jnt");attach_[i]=a.find(s+"_hand_attachment_jnt");
            twist_[i][0]=a.find(s+"_lower_armhelper1_jnt");twist_[i][1]=a.find(s+"_lower_armhelper2_jnt");
            twist_[i][2]=a.find(s+"_lower_armhelper3_jnt");twist_[i][3]=a.find(s+"_wrist_rotation_jnt");
            const char* fingers[5]={"thumb","index","medium","ring","pinky"};
            for(int f=0;f<5;++f) for(int k=0;k<4;++k) {
                const std::string n=s+"_"+fingers[f]+"_0"+char('1'+k)+"_jnt";
                finger_[i][f][k]=f==0 && k==3?-1:a.find(n);
            }
        }
        for(auto& s:sk_) s.finish();
        for(int i=0;i<2;++i) init_finger_axes(i);
        // Uniform avatar scale from eye height; arm segments additionally fit the measured span.
        scale_=std::clamp(cal_.eye_height/sk_[0].bind[camera_].p.z,.6,1.4);
        for(auto& s:sk_) {for(auto& l:s.local) l.p=l.p*scale_;s.finish();}
        // Carry the sleeve roots with the fitted chest surface; independently
        // measured upper-arm centres remain a separate shoulder-span constraint.
        if(cal_.torso_scale!=1) for(int i=0;i<2;++i) {
            auto& a=sk_[1+i];const auto chest=sk_[0].bind[spine_[2]];
            const auto shoulder=mul(chest,a.bind[clavicle_[i]]).p;
            const Vec3 delta{0,(shoulder.y-chest.p.y)*(cal_.torso_scale-1),0};
            const auto parent=mul(chest,a.bind[a.parent[clavicle_[i]]]);
            a.local[clavicle_[i]].p=a.local[clavicle_[i]].p+rotated(conj(parent.q),delta);a.finish();
        }
        if(cal_.shoulder_span>0) {
            const auto chest=sk_[0].bind[spine_[2]];
            const auto left_shoulder=mul(chest,sk_[1].bind[upper_[0]]).p;
            const auto right_shoulder=mul(chest,sk_[2].bind[upper_[1]]).p;
            const double span=std::abs(left_shoulder.y-right_shoulder.y);
            if(span<1e-6) throw std::invalid_argument("Invalid native shoulder span");
            for(int i=0;i<2;++i) {
                auto& a=sk_[1+i];
                const auto shoulder=mul(chest,a.bind[upper_[i]]).p;
                const Vec3 delta{0,(shoulder.y-(left_shoulder.y+right_shoulder.y)*.5)*(cal_.shoulder_span/span-1),0};
                const auto parent=mul(chest,a.bind[a.parent[upper_[i]]]);
                a.local[upper_[i]].p=a.local[upper_[i]].p+rotated(conj(parent.q),delta);
                a.finish();
            }
        }
        if(cal_.eye_to_shoulder>0) {
            auto& bfit=sk_[0];
            const auto chest=bfit.bind[spine_[2]];
            const double shoulder_z=(mul(chest,sk_[1].bind[upper_[0]]).p.z+mul(chest,sk_[2].bind[upper_[1]]).p.z)*.5;
            const Vec3 delta{0,0,bfit.bind[camera_].p.z-shoulder_z-cal_.eye_to_shoulder};
            // Local joint axes are rotated in the native bind: world Z is not local Z.
            bfit.local[spine_[2]].p=bfit.local[spine_[2]].p+rotated(conj(bfit.bind[bfit.parent[spine_[2]]].q),delta);
            bfit.local[neck_].p=bfit.local[neck_].p-rotated(conj(chest.q),delta);
            bfit.finish();
        }
        if(cal_.hand_span>0) {
            const auto arm_bind=[&](int i,int j) {return mul(sk_[0].bind[spine_[2]],sk_[1+i].bind[j]).p;};
            const auto span=arm_bind(0,attach_[0])-arm_bind(1,attach_[1]);
            const auto limbs=(arm_bind(0,wrist_[0])-arm_bind(0,upper_[0]))-
                             (arm_bind(1,wrist_[1])-arm_bind(1,upper_[1]));
            const double quadratic=dot(limbs,limbs),linear=2*dot(span,limbs),constant=dot(span,span)-cal_.hand_span*cal_.hand_span;
            if(quadratic<1e-9 || linear*linear-4*quadratic*constant<0) throw std::invalid_argument("Unsupported fitted hand span");
            // Only upper/forearm links grow; the hand attachment offset stays fixed.
            arm_scale_[0]=arm_scale_[1]=1+(-linear+std::sqrt(linear*linear-4*quadratic*constant))/(2*quadratic);
            if(arm_scale_[0]<.7 || arm_scale_[0]>1.45) throw std::invalid_argument("Unsupported fitted arm length");
        }
        for(int i=0;i<2;++i) {
            auto& a=sk_[1+i];
            if(cal_.arm_reach[i]>0) {
                const auto limbs=a.bind[wrist_[i]].p-a.bind[upper_[i]].p;
                const auto fixed=a.bind[attach_[i]].p-a.bind[wrist_[i]].p;
                const double quadratic=dot(limbs,limbs),linear=2*dot(limbs,fixed);
                const double constant=dot(fixed,fixed)-cal_.arm_reach[i]*cal_.arm_reach[i];
                if(quadratic<1e-9 || linear*linear-4*quadratic*constant<0) throw std::invalid_argument("Unsupported independent arm reach");
                arm_scale_[i]=(-linear+std::sqrt(linear*linear-4*quadratic*constant))/(2*quadratic);
                if(arm_scale_[i]<.7 || arm_scale_[i]>1.45) throw std::invalid_argument("Unsupported independent arm length");
            }
            for(size_t j=0;j<a.local.size();++j)
                if(a.parent[j]==upper_[i] || a.parent[j]==elbow_[i]) a.local[j].p=a.local[j].p*arm_scale_[i];
            a.finish();
        }
        const auto& bb=sk_[0].bind;
        stand_neck_=bb[neck_].p.z;stand_pelvis_=bb[root_].p.z;
        ankle_height_=bb[ankle_[0]].p.z;
    }
    double scale() const {return scale_;}
    double arm_scale() const {return (arm_scale_[0]+arm_scale_[1])*.5;}
    double arm_scale(unsigned side) const {return side<2?arm_scale_[side]:1;}
    double torso_scale() const {return cal_.torso_scale;}
    const Skeleton& skeleton(int i) const {return sk_[i];}

    // Call on reference-space epoch, native owner replacement or teleport.
    // Old planted feet and native blend sources cannot cross those boundaries.
    void reset_hold(int i) {arm_state_[i].has_hold=false;}
    void reset() {
        chest_={};feet_valid_=false;has_eye_=false;walk_=0;walk_on_=false;velocity_={};last_native_={};
        for(int i=0;i<2;++i) {foot_[i]={};arm_state_[i].has_pole=false;arm_state_[i].has_roll=false;dragged_[i]=false;arm_state_[i].has_hold=false;arm_state_[i].socket_token=0;}
        for(auto& p:progress_) p=0;
        for(auto& p:path_) p.clear();
    }

    Output solve(const Inputs& in) {
        if(!valid_pose(in.eye) || !std::isfinite(in.ground) || !std::isfinite(in.dt) || in.dt<0 ||
           !std::isfinite(in.own.legs) || !std::isfinite(in.own.torso) ||
           !std::isfinite(in.own.in_seconds) || in.own.in_seconds<=0 ||
           !std::isfinite(in.own.out_seconds) || in.own.out_seconds<=0)
            throw std::invalid_argument("Invalid body tracking input");
        const auto palette_valid=[](const std::vector<Xf>& pose,size_t expected) {
            return pose.size()==expected && std::all_of(pose.begin(),pose.end(),valid_pose);
        };
        for(int i=0;i<2;++i) {
            if((in.hand_valid[i] && !valid_pose(in.hand[i])) || !std::isfinite(in.grip[i]) ||
               !std::isfinite(in.trigger[i]) || !std::isfinite(in.own.arm[i]) ||
               (in.native_arm[i] && !palette_valid(*in.native_arm[i],sk_[1+i].local.size())))
                throw std::invalid_argument("Invalid body hand input");
            if(in.has_finger[i]) for(const auto& f:in.finger_turn[i]) for(float t:f) if(!std::isfinite(t) || t<-1.6f || t>3.2f) throw std::invalid_argument("Invalid body finger turn");
            if(in.has_finger[i] && in.has_finger_axis[i]) for(const auto& f:in.finger_axis[i]) for(const auto& a:f) {
                const double n=std::sqrt(static_cast<double>(a[0])*a[0]+static_cast<double>(a[1])*a[1]+static_cast<double>(a[2])*a[2]);
                if(!std::isfinite(n) || std::abs(n-1)>.05) throw std::invalid_argument("Invalid body finger axis");
            }
        }
        if(in.native && (!valid_pose(in.native->model) || !palette_valid(in.native->body,sk_[0].local.size()) ||
           (!in.native->arm[0].empty() && !palette_valid(in.native->arm[0],sk_[1].local.size())) ||
           (!in.native->arm[1].empty() && !palette_valid(in.native->arm[1],sk_[2].local.size()))))
            throw std::invalid_argument("Invalid owned native body palette");
        const auto& owned_source=in.native?*in.native:last_native_;
        if((in.own.legs>0 || in.own.torso>0 || in.own.arm[0]>0 || in.own.arm[1]>0) &&
           owned_source.body.size()!=sk_[0].local.size())
            throw std::invalid_argument("Authored ownership needs a native body pose");
        for(int i=0;i<2;++i) if(in.own.arm[i]>0 && owned_source.arm[i].size()!=sk_[1+i].local.size())
            throw std::invalid_argument("Authored ownership needs its native arm pose");
        const double dt=std::clamp(in.dt,0.,.1);
        const auto& b=sk_[0];
        Output out;out.scale=scale_;
        std::vector<Xf> world(b.local.size());
        // 1. Head is exact: camera_jnt = HMD eye; head joint follows rigidly.
        const Xf eye{normalized(in.eye.q),in.eye.p};
        world[camera_]=eye;
        world[head_]=mul(eye,inverse(b.local[camera_]));
        const auto head_delta=mul(world[head_].q,conj(b.bind[head_].q));
        // 2. Torso yaw: dead-zoned follow of the head, pulled by both hands.
        const auto fx=rotated(eye.q,{1,0,0}),fz=rotated(eye.q,{0,0,1});
        const auto flat=fx+fz*(-fx.z);
        const double head_yaw=std::atan2(flat.y,flat.x);
        double hand_yaw{};
        const bool hands=in.hand_valid[0] && in.hand_valid[1] &&
            rig::both_hands_yaw(eye.p,in.hand[0].p,in.hand[1].p,{std::cos(head_yaw),std::sin(head_yaw),0},scale_,hand_yaw);
        const double yaw=chest_.step(head_yaw,hands?&hand_yaw:nullptr,dt);
        const auto yaw_q=rot_z(yaw);
        const Vec3 forward{std::cos(yaw),std::sin(yaw),0},left{-std::sin(yaw),std::cos(yaw),0};
        if(!feet_valid_) init_feet(eye.p,yaw,in.ground);
        // 3. Neck target from the head, with the neck carrying half the head rotation.
        Quaternion spine_delta[4]{yaw_q,yaw_q,yaw_q,yaw_q};
        Vec3 neck_target{};Quaternion neck_delta{};
        const auto head_from_neck=b.bind[head_].p-b.bind[neck_].p;
        // Head velocity (input-derived, smoothed) drives lean and step lead; never the pelvis.
        if(has_eye_ && dt>0) {
            const auto v=(eye.p-last_eye_)*(1/dt);
            // Quick attack (a stick push must lead the feet at once), slow release (no stutter on stops).
            const Vec3 horizontal{v.x,v.y,0};
            velocity_=velocity_+(horizontal-velocity_)*(1-std::exp(-dt/(length(horizontal)>length(velocity_)?.04:.15)));
        }
        if(dt>0 || !has_eye_) {last_eye_=eye.p;has_eye_=true;}
        const double speed=std::hypot(velocity_.x,velocity_.y);
        // Walking/running: the game's own cycle drives the legs (re-rooted under our pelvis) while the
        // player moves; hysteresis on the smoothed speed and a 0.2 s ease keep it continuous.
        if(!(in.native && in.native->body.size()==b.local.size())) walk_on_=false;
        else if(speed>.25) walk_on_=true;
        else if(speed<.15) walk_on_=false;
        walk_=walk_on_?std::min(1.,walk_+dt/.2):std::max(0.,walk_-dt/.2);
        const double walk_w=smooth01(walk_);
        const double leg_w=std::max(weight_legs_(),walk_w);
        // Actual (continuous) foot positions: a step's landing point would jump at step start.
        const Vec3 feet_mid=(foot_[0].at+foot_[1].at)*.5;
        double crouch_back=0;
        for(int iteration=0;iteration<2;++iteration) {
            neck_delta=nlerp(spine_delta[3],head_delta,.5);
            neck_target=world[head_].p-rotated(neck_delta,head_from_neck);
            // 4. Pelvis: over the feet at standing height, lowered and pushed back by crouch.
            const double drop=std::max(0.,in.ground+stand_neck_-neck_target.z);
            // Standing still with a small offset, the pelvis stays over the feet (the torso leans);
            // when moving or reaching far it follows the neck and the feet step after it.
            const auto under=neck_target-rotated(yaw_q,Vec3{b.bind[neck_].p.x-b.bind[root_].p.x,b.bind[neck_].p.y-b.bind[root_].p.y,0});
            const auto offset=Vec3{under.x-feet_mid.x,under.y-feet_mid.y,0};
            const double lean=(1-smooth01(speed/.5))*(1-smooth01((length(offset)-.12*scale_)/(.2*scale_)));
            Vec3 pelvis_goal=Vec3{under.x,under.y,0}-offset*lean;
            pelvis_goal.z=in.ground+stand_pelvis_-.62*drop;
            crouch_back=.35*drop;
            pelvis_goal=pelvis_goal-forward*crouch_back;
            auto torso=neck_target-pelvis_goal;
            if(length(torso)<1e-6) torso={0,0,1};
            // Lean toward the head, limited (less when moving so walking moves the pelvis instead).
            const double max_tilt=(45+35*smooth01(drop/(.6*scale_)))*kPi/180;
            auto dir=unit(torso);
            const double tilt=std::acos(std::clamp(dir.z,-1.,1.));
            if(tilt>max_tilt) {
                const auto h=unit(Vec3{dir.x,dir.y,0});
                dir=Vec3{0,0,std::cos(max_tilt)}+h*std::sin(max_tilt);
            }
            // Bind torso direction (pelvis->neck) rotated by yaw, then swung to dir.
            const auto bind_dir=rotated(yaw_q,unit(b.bind[neck_].p-b.bind[root_].p));
            const auto swing=between(bind_dir,dir);
            static constexpr double share[4]={.2,.45,.7,1.};
            for(int k=0;k<4;++k) spine_delta[k]=mul(nlerp({},swing,share[k]),yaw_q);
        }
        // 5. Spine FK from a provisional pelvis, then translate so the neck lands exactly.
        const int chain[4]={root_,spine_[0],spine_[1],spine_[2]};
        world[root_]={mul(spine_delta[0],b.bind[root_].q),{}};
        for(int k=1;k<4;++k) {
            const auto& parent=world[b.parent[chain[k]]];
            world[chain[k]]={mul(spine_delta[k],b.bind[chain[k]].q),parent.p+rotated(parent.q,b.local[chain[k]].p)};
        }
        world[neck_]={mul(neck_delta,b.bind[neck_].q),world[spine_[2]].p+rotated(world[spine_[2]].q,b.local[neck_].p)};
        auto shift=neck_target-world[neck_].p;
        // Keep the calibrated upper-body anchor. Leg feasibility must not pull
        // the neck/chest away from the tracked head to satisfy stale plants.
        for(int j:{root_,spine_[0],spine_[1],spine_[2],neck_}) world[j].p=world[j].p+shift;
        // Native ownership: eased weights, native pose anchored so its camera_jnt is the HMD eye.
        std::vector<Xf> nb;
        // Keep the last native pose so a blend-out completes even if the source disappears.
        if(in.native && in.native->body.size()==b.local.size()) last_native_=*in.native;
        const NativePose* source=in.native?in.native:(last_native_.body.empty()?nullptr:&last_native_);
        {
            // Blend time grows with how far apart the tracked and authored poses are, so the
            // transition never moves the pelvis or a hand faster than a speed limit.
            double gap[6]{},turn[6]{};
            if(source && source->body.size()==b.local.size()) {
                const auto n=native_world(*source,eye.p);
                gap[0]=gap[1]=length(n[root_].p-world[root_].p);
                const auto angle=[](Quaternion a,Quaternion c) {return 2*std::acos(std::min(1.,std::abs(a.x*c.x+a.y*c.y+a.z*c.z+a.w*c.w)));};
                turn[0]=angle(n[root_].q,world[root_].q);turn[1]=std::max(turn[0],angle(n[spine_[2]].q,world[spine_[2]].q));
                for(int i=0;i<2;++i) if(source->arm[i].size()==sk_[1+i].local.size()) {
                    const auto hand=mul(n[spine_[2]],Xf{source->arm[i][attach_[i]].q,source->arm[i][attach_[i]].p*scale_});
                    gap[2+i]=gap[4+i]=length(hand.p-in.hand[i].p);
                    turn[2+i]=turn[4+i]=2*std::acos(std::min(1.,std::abs(hand.q.x*in.hand[i].q.x+hand.q.y*in.hand[i].q.y+
                                                                        hand.q.z*in.hand[i].q.z+hand.q.w*in.hand[i].q.w)));
                }
            }
            ease_ownership(in,dt,gap,turn);
        }
        for(int k=0;k<4;++k) out.owned[k]=weight(k);
        out.owned[0]=leg_w;
        if(leg_w==0 && weight(1)==0) reset_paths(0);
        for(int i=0;i<2;++i) if(weight(2+i)==0 && weight(4+i)==0) reset_paths(1+i);
        const bool native=source && source->body.size()==b.local.size() &&
            (leg_w>0 || weight(1)>0 || weight(2)>0 || weight(3)>0 || weight(4)>0 || weight(5)>0);
        if(native) {
            nb=native_world(*source,eye.p);
            const double wt=weight(1);
            if(wt>0) {
                std::vector<Xf> vr(world);
                world[root_]=blend(vr[root_],nb[root_],wt,0,root_);
                for(int j:{spine_[0],spine_[1],spine_[2],neck_})
                    world[j]=mul(world[b.parent[j]],blend(local_of(vr,b,j),local_of(nb,b,j),wt,0,j));
            }
        }
        // 6. Legs: stepping feet and two-bone knees.
        // Step relative to the support point: a squat pushes the hips back without moving the feet.
        if(dt>0) step_feet(world[root_].p+forward*crouch_back,yaw,in.ground,dt);
        // Only the pelvis moves for leg reach; the tracked spine/neck stay fixed.
        // The .15*scale bound admits the captured floor mismatch without an
        // unbounded pelvis-to-spine stretch. Larger conflicts retain the leg
        // reach clamp below rather than pretending every fit is feasible.
        double excess=0;
        for(int i=0;i<2;++i) {
            const auto hip=world[root_].p+rotated(world[root_].q,b.local[femur_[i]].p);
            const double reach=.995*(length(b.bind[tibia_[i]].p-b.bind[femur_[i]].p)+length(b.bind[ankle_[i]].p-b.bind[tibia_[i]].p));
            const double across=std::hypot(hip.x-foot_[i].at.x,hip.y-foot_[i].at.y);
            excess=std::max(excess,hip.z-(in.ground+ankle_height_+std::sqrt(std::max(0.,reach*reach-across*across))));
        }
        world[root_].p.z-=std::clamp(excess,0.,.15*scale_)*(1-std::max(leg_w,weight(1)));
        for(int i=0;i<2;++i) solve_leg(world,i,forward,left,leg_w);
        if(native && leg_w>0) {
            const double wl=leg_w;
            std::vector<Xf> vr(world);
            for(size_t j=0;j<world.size();++j) if(in_leg(static_cast<int>(j))) {
                const auto mine=is_leg(static_cast<int>(j))?local_of(vr,b,static_cast<int>(j)):b.local[j];
                world[j]=mul(world[b.parent[j]],blend(mine,local_of(nb,b,static_cast<int>(j)),wl,0,j));
            }
            // While native owns the legs the planted feet follow its feet, so VR stepping
            // resumes from where the authored move left them (no slide after landing).
            // Migrate in proportion to the weight so the VR legs never jump at blend start.
            // Walking-only native legs leave the plants alone: procedural stepping keeps running unseen,
            // so blending back out is already where the feet belong.
            const double wa=weight(0);
            if(dt>0 && wa>0) for(int i=0;i<2;++i) {
                const auto a=nb[ankle_[i]].p,f=rotated(nb[ankle_[i]].q,rotated(conj(b.bind[ankle_[i]].q),{1,0,0}));
                auto& foot=foot_[i];
                // A swinging foot finishes its step; only its landing point migrates.
                auto& p=foot.stepping?foot.to:foot.at;
                p=p+(Vec3{a.x,a.y,0}-Vec3{p.x,p.y,0})*wa;p.z=in.ground+ankle_height_;
                auto& yaw_ref=foot.stepping?foot.to_yaw:foot.yaw;
                yaw_ref=yaw_ref+wrap_pi(std::atan2(f.y,f.x)-yaw_ref)*wa;
            }
        }
        // Remaining body joints follow their parents rigidly (origin/anchor sit under the pelvis).
        world[0]={yaw_q,{world[root_].p.x,world[root_].p.y,in.ground}};
        for(size_t j=0;j<world.size();++j) {
            const bool set=static_cast<int>(j)==camera_||static_cast<int>(j)==head_||static_cast<int>(j)==root_||
                static_cast<int>(j)==neck_||j==0||static_cast<int>(j)==spine_[0]||static_cast<int>(j)==spine_[1]||static_cast<int>(j)==spine_[2]||
                is_leg(static_cast<int>(j)) || (native && leg_w>0 && in_leg(static_cast<int>(j)));
            if(!set) world[j]=b.parent[j]<0?b.local[j]:mul(world[b.parent[j]],b.local[j]);
        }
        world[anchor_]=world[0];
        // 7. Arms hang from spine03 (identity attach tags).
        const Xf chest=world[spine_[2]];
        const auto chest_delta=mul(chest.q,conj(b.bind[spine_[2]].q));
        for(int i=0;i<2;++i) {
            out.arm_frame[i]=chest;
            auto candidate_state=arm_state_[i];
            out.arm[i]=pose_arm(i,chest,chest_delta,in,dt,out.hand_reached[i],arm_state_[i]);
            out.finger_mode[i]=arm_state_[i].finger_mode;for(int s2=0;s2<3;++s2) out.thumb_applied[i][s2]=arm_state_[i].thumb_applied[s2];
            const auto& na=native && source->arm[i].size()==out.arm[i].size()?source->arm[i]:std::vector<Xf>{};
            if(!na.empty() && (weight(2+i)>0 || weight(4+i)>0)) {
                const auto& a=sk_[1+i];
                const double wa=weight(2+i);
                std::vector<Xf> vr(out.arm[i]);
                const double wh=weight(4+i);
                for(size_t j=0;j<vr.size();++j) {
                    const auto parent=j?vr[a.parent[j]]:chest;
                    const auto mine=mul(inverse(parent),vr[j]);
                    auto theirs=j?mul(inverse(na[a.parent[j]]),na[j]):na[0];
                    theirs.p=theirs.p*scale_;
                    out.arm[i][j]=mul(j?out.arm[i][a.parent[j]]:chest,blend(mine,theirs,under_wrist(i,static_cast<int>(j))?wh:wa,1+i,j));
                }
                out.hand_reached[i]=out.hand_reached[i] && wa<1e-6 && weight(4+i)<1e-6;
            }
            if(in.raw_hand_valid[i] && in.hand_valid[i] && in.hand_empty[i] && !in.native_socket_token[i] && valid_pose(in.raw_hand[i]) &&
               in.own.arm[i]==0 && weight(2+i)==0 && weight(4+i)==0)try {
                auto candidate=in;candidate.hand[i]=in.raw_hand[i];
                out.raw_arm[i]=pose_arm(i,chest,chest_delta,candidate,dt,out.raw_hand_reached[i],candidate_state);
            } catch(...) {out.raw_arm[i].clear();out.raw_hand_reached[i]=false;}
            out.phase[i]=foot_[i].t;out.swing_from[i]=foot_[i].from;out.swing_to[i]=foot_[i].to;
            out.feet[i]=foot_[i].at;out.stepping[i]=foot_[i].stepping;out.dragged[i]=dragged_[i];
        }
        out.body=std::move(world);
        return out;
    }

private:
    struct Foot {Vec3 at{},from{},to{};double yaw{},from_yaw{},to_yaw{},t{};bool stepping{};};
    Skeleton sk_[3];Calibration cal_;
    int root_{},spine_[3]{},neck_{},head_{},camera_{},anchor_{};
    int femur_[2]{},tibia_[2]{},ankle_[2]{};
    int clavicle_[2]{},upper_[2]{},elbow_[2]{},wrist_[2]{},attach_[2]{},twist_[2][4]{};
    double arm_scale_[2]{1,1};
    double scale_=1,stand_neck_{},stand_pelvis_{},ankle_height_{};
    rig::ChestYaw chest_;
    Foot foot_[2];bool feet_valid_{};
    Vec3 velocity_{},last_eye_{};bool has_eye_{};
    ArmState arm_state_[2]{};
    bool dragged_[2]{};
    // Stable item-hold local (attachment relative to wrist): follows the native local only once it has been steady.
    Vec3 palm_attach_[2]{};
    // Fingers: [side][thumb,index,medium,ring,pinky][01..04]; axes are in each joint's local frame.
    int finger_[2][5][4]{};Vec3 finger_axis_[2][5][4]{};

    // Flexion axis per finger joint from the T-pose bind: palms face down (-Z), so a finger
    // segment d curls about cross(d, palm); the thumb folds toward the middle knuckle.
    void init_finger_axes(int i) {
        const auto& a=sk_[1+i];
        const Vec3 palm=rotated(conj(sk_[0].bind[spine_[2]].q),{0,0,-1}); // arm-model space
        const auto at=[&](int j) {return a.bind[j].p;};
        const Vec3 across=at(finger_[i][2][1]);
        palm_attach_[i]=unit(rotated(conj(a.bind[attach_[i]].q),palm));
        for(int f=0;f<5;++f) for(int k=0;k<4;++k) {
            const int j=finger_[i][f][k];if(j<0) continue;
            int child=-1;for(size_t c=0;c<a.parent.size();++c) if(a.parent[c]==j && a.names[c].find("helper")==std::string::npos) {child=static_cast<int>(c);break;}
            if(child<0) continue;
            const auto d=unit(at(child)-at(j));
            auto axis=f==0?cross(d,unit(across-at(j))):cross(d,palm);
            if(length(axis)<1e-6) continue;
            finger_axis_[i][f][k]=rotated(conj(a.bind[j].q),unit(axis));
        }
    }
    // Degrees per joint (01 metacarpal, 02 MCP, 03 PIP, 04 DIP) for relaxed and closed hands.
    Xf finger_local(int i,int j,double grip,double trigger,ArmState& state,const float (*turn)[3]=nullptr,const float (*axes)[3][3]=nullptr) const {
        const auto& a=sk_[1+i];
        static constexpr double relaxed[5][4]={{8,10,12,0},{0,12,18,8},{0,18,24,12},{2,22,28,14},{4,26,32,16}};
        static constexpr double closed[5][4]={{25,30,45,0},{0,80,95,55},{0,85,100,60},{6,88,100,60},{12,90,100,62}};
        for(int f=0;f<5;++f) for(int k=0;k<4;++k) if(finger_[i][f][k]==j) {
            const double amount=std::clamp(f==1?trigger*.85:f==0?std::max(grip,trigger)*.8:grip,0.,1.);
            const int s=f?k-1:k; // chain joint of finger_turns (fingers 02-04, thumb 01-03); a finger's metacarpal has none
            if(turn && s<0) return a.local[j]; // a metacarpal under contact turns: on the pose the contact planned from
            const double degrees=turn && s>=0?turn[f][s]*180/kPi:relaxed[f][k]+(closed[f][k]-relaxed[f][k])*amount;
            if(f==0 && s>=0 && s<3) state.thumb_applied[s]=turn?degrees:0;
            auto axis=finger_axis_[i][f][k];
            if(turn && axes && s>=0 && s<3) axis=rotated(conj(a.bind[j].q),unit({axes[f][s][0],axes[f][s][1],axes[f][s][2]})); // the tested hinge
            if(length(axis)<.5) return a.local[j];
            return {mul(a.local[j].q,axis_angle(axis,degrees*kPi/180)),a.local[j].p};
        }
        return a.local[j];
    }

    // Regions: legs, torso, left arm, right arm, left hand, right hand (fingers).
    double progress_[6]{};NativePose last_native_;double walk_{};bool walk_on_{};
    double weight_legs_() const {return weight(0);}
    void ease_ownership(const Inputs& in,double dt,const double gap[6],const double turn[6]) {
        const double target[6]={in.own.legs,in.own.torso,in.own.arm[0],in.own.arm[1],in.own.arm[0],in.own.arm[1]};
        for(int k=0;k<6;++k) {
            const double t=std::clamp(target[k],0.,1.);
            // Arms swing farther between a tracked and an authored pose, and fingers can flip from
            // a grip to an open authored hand: blend them more gently than legs and torso.
            const double slow=k>=4?2.6:k>=2?1.6:1.;
            const bool rising=t>progress_[k];
            const double limit=k<2?(rising?2.5:1.2):(rising?3.:1.5); // m/s for the pelvis / a hand
            // Smoothstep peaks at 1.5x the mean rate, hence the factor on the distance term.
            const double spin=(rising?420.:400.)*kPi/180; // rad/s for a hand, pelvis or chest orientation
            const double seconds=std::max({slow*(rising?in.own.in_seconds:in.own.out_seconds),1.5*gap[k]/limit,1.5*turn[k]/spin});
            const double rate=dt/std::max(1e-3,seconds);
            progress_[k]=t>progress_[k]?std::min(t,progress_[k]+rate):std::max(t,progress_[k]-rate);
        }
    }
    double weight(int k) const {return smooth01(progress_[k]);}
    bool under_wrist(int i,int j) const {
        const auto& a=sk_[1+i];
        for(int p=a.parent[j];p>=0;p=a.parent[p]) if(p==wrist_[i]) return true;
        return false;
    }
    // Blend a->b along a path that stays continuous over time: when a joint's two poses are
    // nearly opposite (~180 deg), the shortest path flips sides frame to frame, so the delta
    // keeps the hemisphere it had last frame (per joint slot; reset when the region is idle).
    std::vector<Quaternion> path_[3];
    Xf blend(const Xf& a,const Xf& b,double w,int part,size_t joint) {
        auto& slots=path_[part];
        if(slots.size()<=joint) slots.resize(sk_[part].local.size(),Quaternion{0,0,0,0});
        auto d=mul(conj(a.q),b.q);
        auto& last=slots[joint];
        const bool fresh=last.x==0 && last.y==0 && last.z==0 && last.w==0;
        if(fresh? d.w<0 : d.x*last.x+d.y*last.y+d.z*last.z+d.w*last.w<0) d={-d.x,-d.y,-d.z,-d.w};
        // Angle-axis (slerp) of the chosen path. The path never switches branch mid-blend (that
        // would pop by up to 2*pi*min(w,1-w)); it resets only once the region is fully VR.
        const double v=std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z),angle=2*std::atan2(v,d.w);
        last=d;
        if(v<1e-12) return {a.q,a.p+(b.p-a.p)*w};
        const Quaternion part_d=axis_angle({d.x/v,d.y/v,d.z/v},angle*w);
        return {mul(a.q,part_d),a.p+(b.p-a.p)*w};
    }
    void reset_paths(int part) {path_[part].clear();}
    static Xf local_of(const std::vector<Xf>& w,const Skeleton& s,int j) {
        return s.parent[j]<0?w[j]:mul(inverse(w[s.parent[j]]),w[j]);
    }
    // Native body in world, scaled to the avatar and anchored so its camera_jnt sits at the eye.
    std::vector<Xf> native_world(const NativePose& n,Vec3 eye) const {
        std::vector<Xf> w(n.body.size());
        for(size_t j=0;j<w.size();++j) w[j]=mul(n.model,Xf{n.body[j].q,n.body[j].p*scale_});
        const auto shift=eye-w[camera_].p;
        for(auto& x:w) x.p=x.p+shift;
        return w;
    }
    // Leg joints including toes (everything under a femur).
    bool in_leg(int j) const {
        const auto& s=sk_[0];
        for(int a=j;a>=0;a=s.parent[a]) if(a==femur_[0] || a==femur_[1]) return true;
        return false;
    }
    bool is_leg(int j) const {
        for(int i=0;i<2;++i) if(j==femur_[i]||j==tibia_[i]||j==ankle_[i]) return true;
        return false;
    }
    Vec3 foot_home(int i,Vec3 pelvis,double yaw) const {
        const auto& b=sk_[0].bind;
        const auto offset=rotated(rot_z(yaw),Vec3{b[ankle_[i]].p.x-b[root_].p.x,b[ankle_[i]].p.y-b[root_].p.y,0});
        return {pelvis.x+offset.x,pelvis.y+offset.y,0};
    }
    void init_feet(Vec3 eye,double yaw,double ground) {
        const auto& b=sk_[0].bind;
        const auto pelvis=eye-rotated(rot_z(yaw),b[camera_].p-b[root_].p);
        for(int i=0;i<2;++i) {
            auto h=foot_home(i,pelvis,yaw);h.z=ground+ankle_height_;
            foot_[i]={h,h,h,yaw,yaw,yaw,0,false};
        }
        feet_valid_=true;
    }
    void step_feet(Vec3 pelvis,double yaw,double ground,double dt) {
        const double speed=std::hypot(velocity_.x,velocity_.y);
        const bool fast=speed>3*scale_;
        const double duration=fast?std::clamp(.6*scale_/(speed*1.2),.065,.2):std::clamp(.34-.06*speed,.2,.34);
        double carried_time[2]{};
        for(int i=0;i<2;++i) {auto& f=foot_[i];if(f.stepping) {
            const double next=f.t+dt/duration;
            if(fast && next>1) carried_time[i]=(next-1)*duration;
            f.t=std::min(1.,next);
            const double s=smooth01(f.t);
            const double stride=std::hypot(f.to.x-f.from.x,f.to.y-f.from.y);
            // Bump with zero vertical velocity at lift-off and touchdown (soft plant, no knee pop).
            const double lift=std::min(.12,.03+.3*stride)*scale_*16*f.t*f.t*(1-f.t)*(1-f.t);
            f.at=f.from+(f.to-f.from)*s;f.at.z=ground+ankle_height_+lift;
            f.yaw=f.from_yaw+wrap_pi(f.to_yaw-f.from_yaw)*s;
            if(f.t>=1) {f.stepping=false;f.at.z=ground+ankle_height_;}
        } else f.at.z=ground+ankle_height_;}
        const auto& b=sk_[0].bind;
        const double full=length(b[tibia_[0]].p-b[femur_[0]].p)+length(b[ankle_[0]].p-b[tibia_[0]].p);
        // Rebase an unreachable swing coherently, preserving phase and lift.
        // Its at/from/to must not accumulate metres behind a fast-moving body.
        double corridor[2]{};
        for(int i=0;i<2;++i) {
            const auto home=foot_home(i,pelvis,yaw);
            const auto hip=pelvis+rotated(rot_z(yaw),sk_[0].local[femur_[i]].p);
            const double height=std::max(0.,hip.z-ground-ankle_height_-.15*scale_);
            const double horizontal=std::sqrt(std::max(0.,(.995*full)*(.995*full)-height*height));
            corridor[i]=std::max(.03*scale_,std::min(.45*full,.8*horizontal));
            // Running: a foot may not trail the pelvis far (legs behind the body/camera).
            corridor[i]*=1-.5*smooth01((speed-1.5*scale_)/(2*scale_));
            auto& f=foot_[i];const Vec3 away{f.at.x-home.x,f.at.y-home.y,0};
            if(length(away)>corridor[i]) {
                const auto correction=home+unit(away)*corridor[i]-Vec3{f.at.x,f.at.y,0};
                f.at=f.at+correction;f.from=f.from+correction;f.to=f.to+correction;
                f.from.z=f.to.z=ground+ankle_height_;
            }
        }
        // One foot at a time, except the trailing foot may lift late in the other's swing, and a
        // foot left beyond leg reach (e.g. after an authored move) steps at once.
        const double reach=.95*(length(sk_[0].bind[tibia_[0]].p-sk_[0].bind[femur_[0]].p)+length(sk_[0].bind[ankle_[0]].p-sk_[0].bind[tibia_[0]].p));
        bool urgent=false;
        for(int i=0;i<2;++i) {
            const auto home=foot_home(i,pelvis,yaw);
            if(!foot_[i].stepping && std::hypot(foot_[i].at.x-home.x,foot_[i].at.y-home.y)>reach) urgent=true;
        }
        if(!urgent && !fast) for(const auto& f:foot_) if(f.stepping && !(f.t>.6 && speed>.4)) return;
        // Land ahead of where the pelvis will be: swing time plus half a stance. Home travels
        // speed*duration during the swing, so the cap only bounds the lead beyond that travel;
        // a foot planted within that lead is where it belongs, not a reason to step again.
        const auto lead_of=[&](int i) {
            auto lead=velocity_*(duration*1.5);
            const double lead_length=length(lead),max_lead=(fast?corridor[i]:.6*scale_)+speed*duration;
            return lead_length>max_lead?lead*(max_lead/lead_length):lead;
        };
        // Trigger the foot farthest from its home; lead it by the pelvis velocity.
        double worst=0;int pick=-1;
        for(int i=0;i<2;++i) {
            if(foot_[i].stepping) continue;
            const auto home=foot_home(i,pelvis,yaw);
            Vec3 off{foot_[i].at.x-home.x,foot_[i].at.y-home.y,0};
            if(speed>1e-3) {
                const auto dir=velocity_*(1/speed);const double along=dot(off,dir);
                if(along>0) off=off-dir*std::min(along,std::max(0.,length(lead_of(i))-speed*duration));
            }
            const double d=length(off);
            const double turn=std::abs(wrap_pi(yaw-foot_[i].yaw));
            const double threshold=(.16-.06*smooth01(speed/1.))*scale_;
            const double score=std::max(d/threshold,turn/(35*kPi/180));
            if(score>1 && score>worst) {worst=score;pick=i;}
        }
        if(pick<0) return;
        auto& f=foot_[pick];
        auto target=foot_home(pick,pelvis,yaw);
        const auto lead=lead_of(pick);
        target=target+Vec3{lead.x,lead.y,0};target.z=ground+ankle_height_;
        // Natural stride: a far foot comes back in several steps, not one leap.
        const Vec3 stride{target.x-f.at.x,target.y-f.at.y,0};
        const double max_stride=std::max(.6*scale_,2*speed*duration);
        if(length(stride)>max_stride) target=Vec3{f.at.x,f.at.y,0}+stride*(max_stride/length(stride))+Vec3{0,0,target.z};
        f.from=f.at;f.from.z=ground+ankle_height_;f.to=target;f.from_yaw=f.yaw;f.to_yaw=yaw;f.t=0;f.stepping=true;
        // Retain elapsed time past a fast landing. Discarding it can lock every
        // low-rate render sample to the same phase despite substepped walking.
        if(fast && carried_time[pick]>0) {
            f.t=std::min(.999,carried_time[pick]/duration);
            const double s=smooth01(f.t),carried_stride=std::hypot(f.to.x-f.from.x,f.to.y-f.from.y);
            f.at=f.from+(f.to-f.from)*s;
            f.at.z=ground+ankle_height_+std::min(.12,.03+.3*carried_stride)*scale_*16*f.t*f.t*(1-f.t)*(1-f.t);
            f.yaw=f.from_yaw+wrap_pi(f.to_yaw-f.from_yaw)*s;
        }
    }
    // Two-bone chain rotations: bind (direction, hinge) frames mapped to solved ones.
    static void place_chain(std::vector<Xf>& w,const Skeleton& s,int j0,int j1,int j2,Vec3 a,Vec3 bpos,Vec3 c,
                            Vec3 bind_hinge,Vec3 hinge) {
        const auto &b0=s.bind[j0],&b1=s.bind[j1],&b2=s.bind[j2];
        const auto qa=frame_map(b1.p-b0.p,bind_hinge,bpos-a,hinge);
        const auto qb=frame_map(b2.p-b1.p,bind_hinge,c-bpos,hinge);
        w[j0]={mul(qa,b0.q),a};w[j1]={mul(qb,b1.q),bpos};
    }
    // Hinge axis = cross(bend direction, root->end direction); pole decides a straight limb.
    static Vec3 hinge_of(Vec3 a,Vec3 b,Vec3 c,Vec3 pole) {
        const auto dir=unit(c-a);
        auto bend=b-(a+dir*dot(b-a,dir));
        if(length(bend)<1e-6) bend=pole-dir*dot(pole,dir);
        return unit(cross(unit(bend),dir));
    }
    void solve_leg(std::vector<Xf>& w,int i,Vec3 forward,Vec3 left,double native_w) {
        const auto& s=sk_[0];
        const auto hip=w[root_].p+rotated(w[root_].q,s.local[femur_[i]].p);
        const double la=length(s.bind[tibia_[i]].p-s.bind[femur_[i]].p),lb=length(s.bind[ankle_[i]].p-s.bind[tibia_[i]].p);
        auto target=foot_[i].at;
        // Knees forward along the foot heading, slightly outward.
        const auto knee_fwd=rotated(rot_z(foot_[i].yaw),{1,0,0})*.7+forward*.3+left*(i==0?.15:-.15);
        auto delta=target-hip;
        const double d=length(delta),full=la+lb;
        // Hard reach clamp (a foot beyond reach hangs) blended with soft IK for a swinging foot:
        // near full extension the knee then eases instead of snapping straight-to-bent. The soft
        // share follows the lift bump (zero at lift-off and touchdown), so it never switches.
        auto hard=d>full-1e-4?hip+delta*((full-1e-4)/d):target;
        if(foot_[i].stepping && d>.94*full) {
            const double soft=.06*full,t=foot_[i].t,bump=16*t*t*(1-t)*(1-t);
            const auto eased=hip+delta*((full-soft*std::exp(-(d-(full-soft))/soft))/d);
            hard=hard+(eased-hard)*bump;
        }
        target=hard;
        const auto solved=solve_arm(hip,target,knee_fwd,la,lb);
        // Bind knee points forward (+X) along a downward leg; solved hinge uses the same rule.
        place_chain(w,s,femur_[i],tibia_[i],ankle_[i],hip,solved.elbow,solved.wrist,
                    cross(Vec3{1,0,0},Vec3{0,0,-1}),hinge_of(hip,solved.elbow,solved.wrist,knee_fwd));
        // Foot flat on the ground with its own heading.
        w[ankle_[i]]={mul(rot_z(foot_[i].yaw),s.bind[ankle_[i]].q),solved.wrist};
        // A planted foot left beyond reach (only after an authored move put it there) is dragged
        // with the leg instead of hanging: the planted point stays reachable and continuous.
        dragged_[i]=false;
        if(!foot_[i].stepping && native_w==0 && length(target-foot_[i].at)>1e-4) {
            foot_[i].at={solved.wrist.x,solved.wrist.y,foot_[i].at.z};dragged_[i]=true;
        }
    }
    std::vector<Xf> pose_arm(int i,const Xf& chest,Quaternion chest_delta,const Inputs& in,double dt,bool& reached,ArmState& state) const {
        const auto& s=sk_[1+i];
        std::vector<Xf> w(s.local.size());
        // Bind joints in world, carried by the chest.
        std::vector<Xf> carried(s.local.size());
        for(size_t j=0;j<s.local.size();++j) carried[j]=mul(chest,s.bind[j]);
        for(size_t j=0;j<s.local.size();++j) w[j]=carried[j];
        const auto up=rotated(chest_delta,{0,0,1}),fwd=rotated(chest_delta,{1,0,0});
        const auto outward=rotated(chest_delta,{0,i==0?1.:-1.,0});
        Xf hand=in.hand[i];
        if(!in.hand_valid[i]) {
            // Untracked: relaxed hang beside the hip.
            hand={mul(axis_angle(fwd,i==0?-kPi/2:kPi/2),carried[attach_[i]].q),
                  carried[upper_[i]].p+outward*(.05*scale_)-up*(.55*scale_)};
        }
        const auto* native=in.native_arm[i] && in.native_arm[i]->size()==s.local.size()?in.native_arm[i]:nullptr;
        const bool procedural=!native || in.hand_empty[i];
        if(state.socket_token!=in.native_socket_token[i]) {state.has_hold=false;state.socket_token=in.native_socket_token[i];}
        const bool native_socket=native && (!procedural || in.native_socket_token[i]);
        state.finger_mode=procedural?(in.has_finger[i]?1:2):3;
        // Held item: the native attachment local is rotated by authored attack/block/equip clips, so
        // the socket keeps the last steady value (<20 deg/s, <5 cm/s for 0.25 s) and the player owns the grip.
        if(!native_socket) state.has_hold=false;
        else {
            auto n=mul(inverse((*native)[s.parent[attach_[i]]]),(*native)[attach_[i]]);n.p=n.p*scale_;
            // Sneak stance holds the sword reversed: attachment Y along -wrist Z in every sword sneak clip,
            // +Z in every forward clip (2026-10-12 clip scan). The player keeps the forward grip: turned back and moved to
            // the standing clips' socket (sword_idle T -.0148 -.0916 .0262 vs sneak .0305 -.0654 .025, native units;
            // keeping the sneak offset put the fist on the blade above the handle, headset v346).
            if(i==1 && rotated(n.q,{0,1,0}).z<-.5) {n.q=mul(n.q,axis_angle({0,0,1},kPi));n.p=n.p+Vec3{-.0453,-.0262,.0012}*scale_;}
            if(!state.has_hold) {state.hold=state.hold_prev=n;state.hold_steady=0;state.has_hold=true;}
            else if(dt>0) {
                const double ang=2*std::acos(std::min(1.,std::abs(n.q.x*state.hold_prev.q.x+n.q.y*state.hold_prev.q.y+n.q.z*state.hold_prev.q.z+n.q.w*state.hold_prev.q.w)));
                const bool calm=ang/dt<20*kPi/180 && length(n.p-state.hold_prev.p)/dt<.05;
                state.hold_steady=calm?state.hold_steady+dt:0;state.hold_prev=n;
                if(state.hold_steady>=.25) {const double k=std::min(1.,dt*8);state.hold={nlerp(state.hold.q,n.q,k),state.hold.p+(n.p-state.hold.p)*k};}
            }
        }
        const auto hand_local=[&](int j) {
            if(j==attach_[i] && native_socket) {
                auto h=state.hold;
                if(in.reverse_grip[i]) h.q=mul(h.q,axis_angle(palm_attach_[i],kPi));
                return h;
            }
            if(procedural) return finger_local(i,j,in.grip[i],in.trigger[i],state,in.has_finger[i]?in.finger_turn[i]:nullptr,in.has_finger[i] && in.has_finger_axis[i]?in.finger_axis[i]:nullptr);
            auto l=mul(inverse((*native)[s.parent[j]]),(*native)[j]);
            l.p=l.p*scale_;
            return l;
        };
        // Pre-avatar contract (user-accepted weapon direction): the controller fixes the WRIST
        // through the bind wrist->attachment offset; the native animated attachment local then
        // carries the weapon. Using the animated local here rotated the hand to keep the socket
        // at its bind relation, so swords/guns pointed off whenever a weapon pose was authored.
        auto bind_attach=mul(inverse(s.bind[wrist_[i]]),s.bind[attach_[i]]);bind_attach.p=bind_attach.p*scale_;
        const auto wrist_target=mul(hand,inverse(native?bind_attach:hand_local(attach_[i])));
        const double la=length(s.bind[elbow_[i]].p-s.bind[upper_[i]].p),lb=length(s.bind[wrist_[i]].p-s.bind[elbow_[i]].p);
        // Clavicle: shrug toward far or high reaches (bounded).
        {
            const auto c=carried[clavicle_[i]].p,u=carried[upper_[i]].p;
            const auto reach=wrist_target.p-u;
            const double r=length(reach);
            const double reaching=smooth01((r/(la+lb)-.75)/.3),high=smooth01((dot(unit(reach),up)-.2)/.7);
            const double amount=std::max(reaching*.6,high)*22*kPi/180;
            const auto from=u-c;auto axis=cross(from,reach);
            if(length(axis)>1e-9 && r>1e-6) {
                const double gap=std::acos(std::clamp(dot(unit(from),unit(reach)),-1.,1.));
                const auto q=axis_angle(axis,std::min(amount,gap));
                for(size_t j=0;j<s.local.size();++j) if(j!=0) {
                    w[j]={mul(q,carried[j].q),c+rotated(q,carried[j].p-c)};
                }
            }
        }
        const auto shoulder=w[upper_[i]].p;
        // Elbow pole: Boneworks policy (down/out, rises for high or rear reaches, palm-up turns it in).
        const auto palm=rotated(mul(wrist_target.q,conj(carried[wrist_[i]].q)),rotated(chest_delta,{0,0,-1}));
        const auto reach=wrist_target.p-shoulder;
        Vec3 pole=rig::boneworks_elbow_pole(length(reach)>1e-4?reach:fwd,fwd,outward,up,la+lb,dot(palm,up),
                                            state.has_pole?&state.pole:nullptr,dt);
        state.pole=pole;state.has_pole=true;
        // Soft IK: near full reach the elbow straightens along an exponential instead of popping
        // (2-bone IK has infinite d(elbow)/d(reach) at L); the hand stays exact through a small
        // bone stretch, at most 6%, beyond which the hand stops short.
        const double full=la+lb,soft=.08*full,d=length(reach);
        double extension=1;
        // Elbow straightness (softened distance / length) is continuous in d; when the stretch
        // cap engages, the same arm shape is kept and the wrist stops short along the reach.
        double target_distance=d;
        if(d>full-soft) {
            const double softened=full-soft*std::exp(-(d-(full-soft))/soft);
            extension=std::min(1.06,d/softened);
            target_distance=softened*extension;
        }
        const auto toward=d>1e-9?shoulder+reach*(target_distance/d):wrist_target.p;
        const auto solved=solve_arm(shoulder,toward,pole,la*extension,lb*extension);
        reached=length(solved.wrist-wrist_target.p)<1e-5;
        const auto hinge=hinge_of(shoulder,solved.elbow,solved.wrist,pole);
        // Bind hinge: T-pose arm flexes forward, so the elbow points back (-X).
        const auto bind_dir=unit(w[wrist_[i]].p-w[upper_[i]].p);
        const auto bind_hinge=unit(cross(rotated(chest_delta,{-1,0,0}),bind_dir));
        const auto bu=w[upper_[i]],be=w[elbow_[i]],bw=w[wrist_[i]];
        const auto qa=frame_map(be.p-bu.p,bind_hinge,solved.elbow-shoulder,hinge);
        const auto qb=frame_map(bw.p-be.p,bind_hinge,solved.wrist-solved.elbow,hinge);
        // Upper-arm subtree (incl. arm_02 twist helper) rigid with the upper arm.
        for(size_t j=0;j<s.local.size();++j) if(s.parent[j]==upper_[i] && static_cast<int>(j)!=elbow_[i])
            w[j]={mul(qa,w[j].q),shoulder+rotated(qa,w[j].p-bu.p)};
        w[upper_[i]]={mul(qa,bu.q),shoulder};
        const Xf elbow{mul(qb,be.q),solved.elbow};
        // Forearm twist toward the wrist roll, spread along the helpers.
        const auto wrist_carried=mul(qb,bw.q);
        const auto axis=unit(solved.wrist-solved.elbow);
        // Unwrap against the previous frame so the roll never jumps across +-180 degrees.
        double raw=twist_angle(mul(wrist_target.q,conj(wrist_carried)),axis);
        if(state.has_roll) raw=state.roll+wrap_pi(raw-state.roll);
        state.roll=raw;state.has_roll=true;
        const double roll=std::clamp(raw,-2.3,2.3);
        for(size_t j=0;j<s.local.size();++j) if(s.parent[j]==elbow_[i] && static_cast<int>(j)!=wrist_[i]) {
            const double along=std::clamp(dot(w[j].p-be.p,bw.p-be.p)/dot(bw.p-be.p,bw.p-be.p),0.,1.);
            const auto twist=axis_angle(axis,roll*along);
            w[j]={mul(twist,mul(qb,w[j].q)),solved.elbow+rotated(qb,w[j].p-be.p)*extension};
        }
        w[elbow_[i]]=elbow;
        w[wrist_[i]]={wrist_target.q,solved.wrist};
        // Hand subtree follows the wrist; finger locals are curled by grip/trigger.
        for(size_t j=0;j<s.local.size();++j) {
            int a=static_cast<int>(j);bool under=false;
            while(a>=0) {if(a==wrist_[i] && static_cast<int>(j)!=wrist_[i]) {under=true;break;}a=s.parent[a];}
            if(under) w[j]=mul(w[s.parent[j]],hand_local(static_cast<int>(j)));
        }
        return w;
    }
};
} // namespace d2vr::body
