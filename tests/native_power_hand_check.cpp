#define NOMINMAX
#include "../src/native_attachment_pose.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
// Dual wield, power hand: the frame conversion (native_power_hand.hpp) and the cross read of NativeAttachmentPose.
namespace {
using d2vr::rig::Transform;
using d2vr::rig::ModelFrame;
namespace ph=d2vr::power_hand;
// Joint index, left bind, right bind (model_bind_3x4 of artifacts/fullbody-assets/pack/skeleton_{left,right}_arm.json, column-major).
struct Bind {unsigned joint;std::array<float,16> left,right;};
const Bind binds[]={
    {1,{0.20122283f,-0.13824309f,0.96974132f,0,0.97945235f,0.01474392f,-0.20113604f,0,0.01350788f,0.99028858f,0.13836933f,0,0.02941416f,0.13158979f,0.02222769f,1},{0.20122283f,0.13824309f,-0.96974132f,0,0.97945235f,-0.01474392f,0.20113604f,0,0.01350788f,-0.99028858f,-0.13836933f,0,-0.0294142f,0.13158734f,0.02222739f,1}},
    {4,{-0.07382943f,-0.14074467f,0.98728929f,0,0.99727088f,-0.01041118f,0.07309167f,0,-8.41e-06f,0.98999118f,0.14112922f,0,0.68217053f,0.13189071f,0.00752802f,1},{-0.07382943f,0.14074467f,-0.98728929f,0,0.99727088f,0.01041118f,-0.07309167f,0,-8.41e-06f,-0.98999118f,-0.14112922f,0,-0.68217596f,0.13189482f,0.00752895f,1}},
    {27,{0.04300406f,-0.14099927f,0.98907525f,0,0.99905609f,-5.57e-06f,-0.04343881f,0,0.00613035f,0.9900097f,0.14086594f,0,0.79142908f,0.14010608f,0.01779558f,1},{0.04300406f,0.14099927f,-0.98907525f,0,0.99905609f,5.57e-06f,0.04343881f,0,0.00613035f,-0.9900097f,-0.14086594f,0,-0.79143396f,0.140108f,0.01779624f,1}},
    {40,{0.32744114f,-0.12903758f,0.93601902f,0,0.94477738f,0.05870093f,-0.32241263f,0,-0.01334185f,0.98990075f,0.14113288f,0,0.85852278f,0.14540033f,-0.03501711f,1},{0.32744114f,0.12903758f,-0.93601902f,0,0.94477738f,-0.05870093f,0.32241263f,0,-0.01334185f,-0.98990075f,-0.14113288f,0,-0.85852796f,0.14540442f,-0.03501617f,1}},
    {59,{-2e-08f,-0.14112986f,0.98999109f,0,1,1e-08f,2e-08f,0,-1e-08f,0.98999109f,0.14112986f,0,0.74655732f,0.08101759f,0.00027571f,1},{-2e-08f,0.14112986f,-0.98999109f,0,1,-1e-08f,-2e-08f,0,-1e-08f,-0.98999109f,-0.14112986f,0,-0.74656196f,0.08101918f,0.00027629f,1}},
    {66,{-0.05832744f,-0.14088956f,0.98830564f,0,0.99829751f,-0.00823173f,0.05774365f,0,-1e-08f,0.9899911f,0.14112983f,0,0.64300707f,0.13229956f,0.00465766f,1},{-0.05832744f,0.14088956f,-0.98830564f,0,0.99829751f,0.00823173f,-0.05774365f,0,-1e-08f,-0.9899911f,-0.14112983f,0,-0.63436096f,0.13231493f,0.00452394f,1}},
};
bool nearf(float a,float b,float tol) {return std::abs(a-b)<=tol;}
bool same(const Transform& a,const Transform& b,float tol) {
    for(unsigned i=0;i<16;++i) if(!nearf(a.f[i],b.f[i],tol)) return false;
    return true;
}
Transform make(const std::array<float,16>& f) {Transform t;t.f=f;return t;}
std::array<float,15> frame(float x,float y,float z,float yaw,float scale=1.f) {
    const float c=std::cos(yaw),s=std::sin(yaw);
    return {x,y,z, c,s,0, -s,c,0, 0,0,1, scale,scale,scale};
}
// The world position/axes a joint has under a model frame.
d2vr::Vec3 world_point(const std::array<float,15>& f,const Transform& t) {return ModelFrame(f).world_point(d2vr::rig::position(t));}
d2vr::Vec3 world_axis(const std::array<float,15>& f,const Transform& t,unsigned i) {return ModelFrame(f).world_direction(d2vr::rig::column(t,i));}
bool close(d2vr::Vec3 a,d2vr::Vec3 b,double tol=1e-4) {return d2vr::length(a-b)<=tol;}
void pure() {
    // The mirror rule holds on the real skeletons: left bind -> right bind, and back (it is its own inverse).
    for(const auto& b:binds) {
        const auto mirrored=ph::mirror_joint(make(b.left));
        assert(same(mirrored,make(b.right),b.joint==66?1e-2f:1e-4f));
        assert(same(ph::mirror_joint(mirrored),make(b.left),1e-6f));
        assert(d2vr::rig::valid(mirrored)); // still a rigid joint (the cuff validity and the engine's own checks)
    }
    std::array<Transform,69> pose{};
    for(unsigned j=0;j<69;++j) {pose[j]=make(binds[j%6].left);pose[j].f[15]=1;}
    pose[0]=make({1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1});
    const auto right=ph::mirror_pose(pose);
    assert(same(right[0],pose[0],0)); // origin is the identity in both skeletons
    for(unsigned j=1;j<69;++j) assert(same(right[j],ph::mirror_joint(pose[j]),0));
    // Frame conversion: the same world pose, the destination's coordinates.
    const auto right_frame=frame(1.f,2.f,3.f,.4f),left_frame=frame(1.f,2.f,3.f,.4f);
    const auto joint=make(binds[4].right);
    // Identical frames: nothing changes (this is what the game has: both arms hang on the same spine joint).
    assert(same(ph::cross_joint(joint,ModelFrame(right_frame),ModelFrame(left_frame),joint),joint,1e-5f));
    // Different frames (and scale): the world pose is kept exactly.
    const auto other=frame(-.5f,.25f,1.5f,-1.1f,1.f);
    const auto moved=ph::cross_joint(joint,ModelFrame(right_frame),ModelFrame(other),make(binds[4].left));
    assert(close(world_point(other,moved),world_point(right_frame,joint)));
    for(unsigned i=0;i<3;++i) assert(close(world_axis(other,moved,i),world_axis(right_frame,joint,i)));
    const auto scaled=frame(.3f,.1f,-.2f,2.f,1.25f);
    const auto scaled_move=ph::cross_joint(joint,ModelFrame(right_frame),ModelFrame(scaled),joint);
    assert(close(world_point(scaled,scaled_move),world_point(right_frame,joint),1e-3));
    for(unsigned i=0;i<3;++i) assert(close(world_axis(scaled,scaled_move,i),world_axis(right_frame,joint,i),1e-4));
    assert(d2vr::rig::valid(scaled_move));
    // The non-spatial lanes come from the destination slot.
    auto lanes=joint;lanes.f[3]=.5f;lanes.f[7]=.25f;lanes.f[11]=.125f;lanes.f[15]=2.f;
    const auto laned=ph::cross_joint(joint,ModelFrame(right_frame),ModelFrame(other),lanes);
    assert(laned.f[3]==.5f && laned.f[7]==.25f && laned.f[11]==.125f && laned.f[15]==2.f);
    // The attachment correction: 180 degrees about the joint's local Y; the position is untouched.
    const auto weapon=make(binds[4].right);
    const auto flipped=ph::cross_joint(weapon,ModelFrame(right_frame),ModelFrame(right_frame),weapon,&ph::y180_rot);
    for(unsigned k=0;k<3;++k) {
        assert(nearf(flipped.f[k],-weapon.f[k],1e-5f));       // local X reversed
        assert(nearf(flipped.f[4+k],weapon.f[4+k],1e-5f));    // local Y kept
        assert(nearf(flipped.f[8+k],-weapon.f[8+k],1e-5f));   // local Z reversed
        assert(nearf(flipped.f[12+k],weapon.f[12+k],1e-5f));
    }
    assert(d2vr::rig::valid(flipped));
    const auto kept=ph::cross_joint(weapon,ModelFrame(right_frame),ModelFrame(right_frame),weapon,&ph::identity_rot);
    assert(same(kept,weapon,1e-5f));
    assert(ph::valid_rot(ph::y180_rot) && ph::valid_rot(ph::identity_rot));
    ph::Rot3 bad=ph::y180_rot;bad[0]=-2;assert(!ph::valid_rot(bad));
    ph::Rot3 reflection{-1,0,0, 0,1,0, 0,0,1};assert(!ph::valid_rot(reflection)); // det -1 is a mirror, not a rotation
    // v776: the mirror correction is a half turn about the item's sagittal normal (tag*Y) in joint coordinates. The weapon tag gives the right arm's own
    // y180 (and the right sword's tag, y180, gives y180 back: the other direction is unchanged); the Heart's tag gives z180.
    assert(ph::mirror_correction(ph::weapon_tag_rot)==ph::y180_rot);
    assert(ph::mirror_correction(ph::y180_rot)==ph::y180_rot);
    const ph::Rot3 z180{-1,0,0, 0,-1,0, 0,0,1};
    assert(ph::mirror_correction(ph::heart_tag_rot)==z180);
    assert(ph::valid_rot(ph::heart_tag_rot) && ph::valid_rot(ph::mirror_correction(ph::heart_tag_rot)));
}
// The Heart's axes in its own mesh frame (models/weapons/heart/heart.bmd6model: bounds z -.072..+.183, the lens_fx tag on +x, the vessels on top).
const d2vr::Vec3 heart_lens{1,0,0},heart_side{0,1,0},heart_up{0,0,1};
// An item axis `u` (item-local) in model space for a joint and the tag the game binds the item with: J * tag * u.
d2vr::Vec3 item_axis(const Transform& joint,const ph::Rot3& tag,d2vr::Vec3 u) {
    const d2vr::Vec3 j{tag[0]*u.x+tag[1]*u.y+tag[2]*u.z,tag[3]*u.x+tag[4]*u.y+tag[5]*u.z,tag[6]*u.x+tag[7]*u.y+tag[8]*u.z};
    return d2vr::rig::column(joint,0)*j.x+d2vr::rig::column(joint,1)*j.y+d2vr::rig::column(joint,2)*j.z;
}
d2vr::Vec3 mirror_x(d2vr::Vec3 v) {return {-v.x,v.y,v.z};} // the arm models' body-plane mirror: model x is lateral
Transform joint_pose(d2vr::Vec3 x,d2vr::Vec3 y,d2vr::Vec3 z,d2vr::Vec3 p) {
    Transform t;t.f={float(x.x),float(x.y),float(x.z),0,float(y.x),float(y.y),float(y.z),0,float(z.x),float(z.y),float(z.z),0,float(p.x),float(p.y),float(p.z),1};return t;
}
// Rotate a joint pose about a model axis (angle in radians): a different way of holding the same thing.
Transform turned(const Transform& t,d2vr::Vec3 axis,double angle) {
    const double c=std::cos(angle),s=std::sin(angle);
    const auto rot=[&](d2vr::Vec3 v) {return v*c+d2vr::cross(axis,v)*s+axis*(d2vr::dot(axis,v)*(1-c));};
    return joint_pose(rot(d2vr::rig::column(t,0)),rot(d2vr::rig::column(t,1)),rot(d2vr::rig::column(t,2)),rot(d2vr::rig::position(t)));
}
// v776 (user, headset v775: "the Heart in the right hand is upside down"): the Heart binds to heart_attach_tag, a half turn about (0,1,1)/sqrt2, not to the
// identity weapon_attach_tag. The right hand plays the left clip mirrored (right joint = mirror_joint(left joint)) and the crossed item must be the mirror
// image of the left one: lens and vessels mirrored across the body plane, only the sagittal axis (item Y) reversed. y180 (the weapon tag's mirror) kept the lens
// but turned the vessels down.
void heart_in_the_right_hand() {
    const auto frame_id=frame(0,0,0,0);
    for(const auto& b:binds) if(b.joint==ph::attachment_joint) {
        std::vector<Transform> holds{make(b.left)};
        for(const double a:{.3,1.1,-2.0,2.9}) for(const auto axis:{d2vr::Vec3{1,0,0},d2vr::Vec3{0,1,0},d2vr::Vec3{0,0,1},d2vr::unit(d2vr::Vec3{1,2,-1})})
            holds.push_back(turned(make(b.left),axis,a));
        // Palm up, the Heart upright: the arm models have x left, y up, z forward (receipt arm_model_frame), so joint X forward, Y up (the Heart's vessels
        // run along joint Y through heart_attach_tag), Z to the player's right.
        holds.push_back(joint_pose({0,0,1},{0,1,0},{-1,0,0},{.3,-.3,.5}));
        for(const auto& left:holds) {
            const auto right=ph::mirror_joint(left);
            for(const auto* tag:{&ph::weapon_tag_rot,&ph::heart_tag_rot}) { // the three axes are generic item axes: lens/up in the item's sagittal plane, side its normal
                const auto correction=ph::mirror_correction(*tag);
                const auto crossed=ph::cross_joint(right,ModelFrame(frame_id),ModelFrame(frame_id),right,&correction);
                assert(d2vr::rig::valid(crossed));
                for(const auto u:{heart_lens,heart_up}) assert(close(item_axis(crossed,*tag,u),mirror_x(item_axis(left,*tag,u)),1e-5));
                assert(close(item_axis(crossed,*tag,heart_side),mirror_x(item_axis(left,*tag,heart_side))*-1.,1e-5)); // the one reversed axis
            }
            // The headset's bug: y180 on the Heart keeps the lens and reverses the vessels.
            const auto old=ph::cross_joint(right,ModelFrame(frame_id),ModelFrame(frame_id),right,&ph::y180_rot);
            assert(close(item_axis(old,ph::heart_tag_rot,heart_lens),mirror_x(item_axis(left,ph::heart_tag_rot,heart_lens)),1e-5));
            assert(close(item_axis(old,ph::heart_tag_rot,heart_up),mirror_x(item_axis(left,ph::heart_tag_rot,heart_up))*-1.,1e-5));
        }
    }
    // The held-upright case in the world, with the real arm model frame (receipt: x left, y up, z forward): up stays up, and the lens faces the player.
    const float yaw_c=static_cast<float>(std::cos(1.15)),yaw_s=static_cast<float>(std::sin(1.15));
    const std::array<float,15> real{4.f,-16.f,-3.f, -yaw_s,yaw_c,0, 0,0,1, yaw_c,yaw_s,0, 1,1,1};
    const ModelFrame world(real);
    const auto left=joint_pose({0,0,1},{0,1,0},{-1,0,0},{.3,-.3,.5}),right=ph::mirror_joint(left);
    const auto fixed=ph::mirror_correction(ph::heart_tag_rot);
    const auto good=ph::cross_joint(right,world,world,right,&fixed),bad=ph::cross_joint(right,world,world,right,&ph::y180_rot);
    const auto up=[&](const Transform& j) {return world.world_direction(item_axis(j,ph::heart_tag_rot,heart_up));};
    const auto lens=[&](const Transform& j) {return world.world_direction(item_axis(j,ph::heart_tag_rot,heart_lens));};
    assert(close(up(left),d2vr::Vec3{0,0,1},1e-4) && close(up(good),d2vr::Vec3{0,0,1},1e-4)); // upright in both hands
    assert(close(up(bad),d2vr::Vec3{0,0,-1},1e-4));                                         // y180: upside down
    const auto toward_player=world.world_direction(d2vr::Vec3{0,0,-1}); // model -z = back, toward the eyes
    assert(close(lens(left),toward_player,1e-4) && close(lens(good),toward_player,1e-4) && close(lens(bad),toward_player,1e-4));
}
}
namespace d2vr {
struct NativeAttachmentPoseCheck {
    alignas(16) inline static std::array<std::byte,0x300> animation[2]{};
    alignas(16) inline static std::array<std::byte,64> states[2]{};
    alignas(16) inline static std::array<std::byte,0x1900> model[2]{};
    alignas(16) inline static std::array<rig::Transform,69> native[2]{};
    inline static std::array<short,69> remap{};
    static bool read(std::uintptr_t address,void* out,std::size_t size){SIZE_T n{};return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),out,size,&n) && n==size;}
    template<class T>static void put(std::byte* address,const T& value){std::memcpy(address,&value,sizeof(value));}
    static std::uintptr_t addr(const void* p){return reinterpret_cast<std::uintptr_t>(p);}
    static std::array<rig::Transform,69> palette(float x,float y,float z) {
        std::array<rig::Transform,69> p{};
        for(auto& j:p) j.f[0]=j.f[5]=j.f[10]=j.f[15]=1;
        for(unsigned j=2;j<69;++j) {p[j].f[12]=x+j*.001f;p[j].f[13]=y;p[j].f[14]=z;}
        return p;
    }
    static void run() {
        NativeAttachmentPose pose;pose.read_=read;pose.active_=true;
        for(unsigned i=0;i<69;++i) remap[i]=static_cast<short>(i);
        const rig::Identity identity{1,10};
        const std::array<float,15> frames[2]={{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1},{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1}};
        for(unsigned s=0;s<2;++s) {
            for(auto& j:native[s]) j.f[0]=j.f[5]=j.f[10]=j.f[15]=1;
            put(animation[s].data()+0x2d8,addr(native[s].data()));put(animation[s].data()+0x2e8,addr(states[s].data()));
            put(states[s].data()+48,addr(remap.data()));put(states[s].data()+56,std::uint32_t{69});
            put(animation[s].data()+0x18,identity);put(animation[s].data()+0x38,addr(model[s].data()));
            put(model[s].data()+0x1860,frames[s]);
        }
        const auto left=palette(1.f,2.f,3.f),right=palette(-1.f,5.f,.5f);
        const auto now=GetTickCount64()-2;
        const auto policy=[&](unsigned mask) {pose.set_allowed(mask,1,now,7,2,3);};
        const auto source=[&](unsigned s,std::array<float,15> f) {
            return NativeAttachmentSource{now,7,1,2,3,addr(model[s].data()),identity,f};
        };
        std::array<float,15> left_frame=frames[0]; // the frame the left model currently has
        const auto publish_both=[&] {
            return pose.publish(0,addr(animation[0].data()),addr(native[0].data()),left,source(0,left_frame)) &&
                   pose.publish(1,addr(animation[1].data()),addr(native[1].data()),right,source(1,frames[1]));
        };
        const auto read_left=[&](short joint,float expect_x) {
            bool seen{};
            pose.around(addr(animation[0].data()),3,joint,[&]()->std::uint64_t {
                assert(nearf(native[0][static_cast<unsigned>(joint)].f[12],expect_x,1e-4f));seen=true;return 5;});
            assert(seen);
            assert(native[0][static_cast<unsigned>(joint)].f[12]==0.f); // restored after the call
        };
        policy(3);assert(publish_both());
        assert(pose.cross_correction_axis(0)==1 && pose.cross_correction_axis(1)==1 && pose.cross_correction_axis(2)==3); // y180 for both arms by default
        // Knob off (the default): the left read returns the left palette, exactly as before.
        assert(!pose.cross() && pose.cross_substitutions()==0);
        read_left(10,1.f+10*.001f);
        assert(pose.cross_substitutions()==0 && pose.cross_skipped()==0);
        // Mirrored: the same read returns the right hand's joint (identical model frames).
        pose.set_cross(true);
        read_left(10,-1.f+10*.001f);
        assert(pose.cross_substitutions()==1);
        // Cuff joints keep the left palette.
        read_left(1,0.f);assert(pose.cross_skipped()==1);
        // The right arm's own reads are untouched by the knob.
        pose.around(addr(animation[1].data()),3,10,[&]()->std::uint64_t {assert(nearf(native[1][10].f[12],-1.f+10*.001f,1e-4f));return 5;});
        // Joint 59 carries the attachment correction (y180: local X and Z reversed).
        {
            auto marked=right;marked[59].f[0]=0;marked[59].f[1]=0;marked[59].f[2]=1;marked[59].f[4]=1;marked[59].f[5]=0;marked[59].f[6]=0;
            marked[59].f[8]=0;marked[59].f[9]=1;marked[59].f[10]=0; // columns (0,0,1),(1,0,0),(0,1,0): a proper rotation
            assert(pose.publish(1,addr(animation[1].data()),addr(native[1].data()),marked,source(1,frames[1])));
            pose.around(addr(animation[0].data()),3,59,[&]()->std::uint64_t {
                const auto& t=native[0][59];
                assert(nearf(t.f[0],0,1e-5f) && nearf(t.f[1],0,1e-5f) && nearf(t.f[2],-1,1e-5f)); // X was (0,0,1)
                assert(nearf(t.f[4],1,1e-5f) && nearf(t.f[5],0,1e-5f) && nearf(t.f[6],0,1e-5f));   // Y kept
                assert(nearf(t.f[8],0,1e-5f) && nearf(t.f[9],-1,1e-5f) && nearf(t.f[10],0,1e-5f)); // Z was (0,1,0)
                return 5;});
            assert(pose.set_cross_attachment(ph::identity_rot));
            pose.around(addr(animation[0].data()),3,59,[&]()->std::uint64_t {
                assert(nearf(native[0][59].f[2],1,1e-5f));return 5;});
            ph::Rot3 reflection{-1,0,0, 0,1,0, 0,0,1};assert(!pose.set_cross_attachment(reflection));
            assert(pose.set_cross_attachment(ph::y180_rot));
            assert(publish_both());
        }
        // v776: the correction is per destination arm. The slot-1 item (left arm reads) takes the Heart's z180 while the sword (right arm reads) keeps y180.
        {
            const auto turn=[](rig::Transform& t) {t.f[0]=0;t.f[1]=0;t.f[2]=1;t.f[4]=1;t.f[5]=0;t.f[6]=0;t.f[8]=0;t.f[9]=1;t.f[10]=0;}; // columns (0,0,1),(1,0,0),(0,1,0)
            auto left_marked=left,right_marked=right;turn(left_marked[59]);turn(right_marked[59]);
            assert(pose.publish(0,addr(animation[0].data()),addr(native[0].data()),left_marked,source(0,left_frame)));
            assert(pose.publish(1,addr(animation[1].data()),addr(native[1].data()),right_marked,source(1,frames[1])));
            pose.set_cross_mask(3);
            assert(pose.set_cross_attachment(ph::mirror_correction(ph::heart_tag_rot)) && !pose.set_cross_attachment(ph::y180_rot,2));
            assert(pose.cross_correction_axis(0)==2 && pose.cross_correction_axis(1)==1); // status dual_wield.cross[4..5]: the Heart half-turns about Z, the sword about Y
            const auto axes=[&](unsigned arm,d2vr::Vec3 x,d2vr::Vec3 y,d2vr::Vec3 z) {
                pose.around(addr(animation[arm].data()),3,59,[&]()->std::uint64_t {
                    const auto& t=native[arm][59];
                    assert(close(d2vr::rig::column(t,0),x,1e-5) && close(d2vr::rig::column(t,1),y,1e-5) && close(d2vr::rig::column(t,2),z,1e-5));return 5;});
            };
            axes(0,{0,0,-1},{-1,0,0},{0,1,0}); // the left arm reads the right hand: columns * z180 (X and Y reversed, Z kept)
            axes(1,{0,0,-1},{1,0,0},{0,-1,0}); // the right arm reads the left hand: columns * y180 (X and Z reversed, Y kept), whatever the left arm's correction is
            assert(pose.set_cross_attachment(ph::y180_rot) && pose.cross_correction_axis(0)==1);
            axes(0,{0,0,-1},{1,0,0},{0,-1,0}); // back to the weapon tag's y180
            assert(publish_both());
        }
        // v776 (headset v775: a crossed item trailed the hand when running): the conversion uses the frames the palettes were PUBLISHED with. The origin arm's
        // live frame (not yet updated for this game frame when the destination's item is evaluated) must not move the item: it rides the destination's
        // live frame, as the destination's own palette does, which is the frame the origin arm is drawn in.
        {
            pose.set_cross_mask(3);
            const auto skewed=pose.cross_skewed();
            const std::array<float,15> stale{.1f,0,0, 1,0,0, 0,1,0, 0,0,1, 1,1,1};   // the right model's live frame 10 cm off its published one
            put(model[1].data()+0x1860,stale);
            read_left(10,-1.f+10*.001f);                                              // the right palette's joint, unmoved (live-frame conversion gave x - 0.1)
            assert(pose.cross_skewed()==skewed+1 && std::abs(pose.cross_skew_peak_mm()-100.)<.01);
            pose.set_cross_live_frames(true);read_left(10,-1.f+10*.001f+.1f);pose.set_cross_live_frames(false); // the A/B switch: the live (pre-v776) conversion moves it
            read_left(10,-1.f+10*.001f);
            put(model[1].data()+0x1860,frames[1]);
            read_left(10,-1.f+10*.001f);assert(pose.cross_skewed()==skewed+3);       // frames agree again: no skew counted (three skewed reads above)
            // Both live frames moved by the same game frame while the palettes were published with the old ones: the item takes the destination's live frame
            // (the engine composes with it), so the coordinates handed back are still the published ones.
            const std::array<float,15> moved{.1f,0,0, 1,0,0, 0,1,0, 0,0,1, 1,1,1};
            put(model[1].data()+0x1860,moved);put(model[0].data()+0x1860,moved);
            read_left(10,-1.f+10*.001f);
            put(model[1].data()+0x1860,frames[1]);put(model[0].data()+0x1860,frames[0]);
            assert(publish_both());
        }
        // The other direction (the native sword on the right arm riding the left hand), and both at once.
        {
            const auto right_read=[&](short joint,float expect_x) {
                bool seen{};
                pose.around(addr(animation[1].data()),3,joint,[&]()->std::uint64_t {
                    assert(nearf(native[1][static_cast<unsigned>(joint)].f[12],expect_x,1e-4f));seen=true;return 5;});
                assert(seen && native[1][static_cast<unsigned>(joint)].f[12]==0.f);
            };
            pose.set_cross_mask(2);assert(!pose.cross() && pose.cross_mask()==2);
            read_left(10,1.f+10*.001f);                 // the left arm keeps its own hand
            right_read(10,1.f+10*.001f);                // the right arm reads the left hand
            pose.set_cross_mask(3);
            read_left(10,-1.f+10*.001f);right_read(10,1.f+10*.001f);
            pose.set_cross_mask(7);assert(pose.cross_mask()==3);
            pose.set_cross(true);assert(pose.cross() && pose.cross_mask()==1);
            right_read(10,-1.f+10*.001f);               // back to the right arm's own hand
        }
        // Different model frames: the world pose of the right joint is kept in the left model's coordinates.
        {
            const std::array<float,15> moved_left{.5f,0,0, 0,1,0, -1,0,0, 0,0,1, 1,1,1}; // rotated a quarter turn, shifted
            put(model[0].data()+0x1860,moved_left);left_frame=moved_left;
            assert(publish_both());
            const auto expect=ModelFrame(moved_left).model_point(ModelFrame(frames[1]).world_point(rig::position(right[10])));
            pose.around(addr(animation[0].data()),3,10,[&]()->std::uint64_t {
                assert(nearf(native[0][10].f[12],static_cast<float>(expect.x),1e-4f) && nearf(native[0][10].f[13],static_cast<float>(expect.y),1e-4f) &&
                       nearf(native[0][10].f[14],static_cast<float>(expect.z),1e-4f));return 5;});
            put(model[0].data()+0x1860,frames[0]);left_frame=frames[0];assert(publish_both());
        }
        // The right arm unavailable (revoked): the left read falls back to its own palette instead of reading nothing.
        pose.clear(1);
        {
            const auto before=pose.cross_skipped();
            read_left(10,1.f+10*.001f);assert(pose.cross_skipped()==before+1);
        }
        assert(publish_both());
        // A receipt on either arm (the socket and contact protocols) keeps the left palette.
        {
            struct Ctx {static bool yes(void*,std::uint64_t) noexcept {return true;} static void no(void*,std::uint64_t) noexcept {}};
            int dummy{};
            auto guarded=source(1,frames[1]);guarded.admission={&dummy,9,&Ctx::yes,&Ctx::no};
            assert(pose.publish(1,addr(animation[1].data()),addr(native[1].data()),right,guarded));
            const auto before=pose.cross_substitutions();
            read_left(10,1.f+10*.001f);assert(pose.cross_substitutions()==before);
            assert(publish_both());
        }
        // Policy: the right arm owned natively (mask 1): no cross read.
        policy(1);assert(pose.publish(0,addr(animation[0].data()),addr(native[0].data()),left,source(0,frames[0])));
        assert(!pose.publish(1,addr(animation[1].data()),addr(native[1].data()),right,source(1,frames[1])));
        {
            const auto before=pose.cross_substitutions();
            read_left(10,1.f+10*.001f);assert(pose.cross_substitutions()==before);
        }
        // Back to the knob off: the left palette again, no counter change.
        policy(3);assert(publish_both());
        pose.set_cross(false);
        {
            const auto before=pose.cross_substitutions();
            read_left(10,1.f+10*.001f);assert(pose.cross_substitutions()==before);
        }
    }
};
}
// v763 (user, headset v762: "the right hand holds every weapon exactly as the left"): the holding hand plays the other arm's clip
// mirrored (FullBodyDriver mirror_native) and the cross applies y180 at joint 59; the item frame is joint 59 itself (bind receipts,
// headset v762). The item then sits at the exact mirror image of the other hand's hold: mirrored position, the lateral axis flipped back.
void mirrored_hold() {
    const auto S=[](d2vr::Vec3 v){return d2vr::Vec3{-v.x,v.y,v.z};}; // the arm models' mirror: x is lateral (T-pose space)
    for(const auto& b:binds) if(b.joint==ph::attachment_joint) {
        const auto left=make(b.left),right=ph::mirror_joint(left); // the right hand playing the left clip, mirrored
        const auto& C=ph::y180_rot;d2vr::Vec3 item[3];
        for(unsigned i=0;i<3;++i) item[i]=d2vr::rig::column(right,0)*C[0*3+i]+d2vr::rig::column(right,1)*C[1*3+i]+d2vr::rig::column(right,2)*C[2*3+i];
        assert(close(d2vr::rig::position(right),S(d2vr::rig::position(left)),1e-5));
        assert(close(item[0],S(d2vr::rig::column(left,0)),1e-5) && close(item[1],S(d2vr::rig::column(left,1))*-1.,1e-5) &&
               close(item[2],S(d2vr::rig::column(left,2)),1e-5));
    }
}
int main() {
    pure();
    heart_in_the_right_hand();
    mirrored_hold();
    d2vr::NativeAttachmentPoseCheck::run();
    std::cout << "native power hand check passed\n";
}
