// v1221: a dual-wield copy is bound to the drawn hand at the copy service's own grip offset, also when the hand plays its kind's gun clip
// (headset v1216: the left crossbow copy was drawn 119 deg / 64 mm off where the copy service put it). Pure math + the real Binder.
#include "../src/copy_draw_grip.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace d2vr::weapon_draw;
static Mat rigid(double ax,double ay,double az,double deg,double tx,double ty,double tz) {   // rotation about a unit axis, then translation (column vectors)
    const double n=std::sqrt(ax*ax+ay*ay+az*az);ax/=n;ay/=n;az/=n;
    const double a=deg*3.14159265358979323846/180,c=std::cos(a),s=std::sin(a),t=1-c;
    Mat m=identity();
    m[0]=t*ax*ax+c;m[1]=t*ax*ay-s*az;m[2]=t*ax*az+s*ay;
    m[4]=t*ax*ay+s*az;m[5]=t*ay*ay+c;m[6]=t*ay*az-s*ax;
    m[8]=t*ax*az-s*ay;m[9]=t*ay*az+s*ax;m[10]=t*az*az+c;
    m[3]=tx;m[7]=ty;m[11]=tz;return m;
}
static Mat inv(const Mat& m) {Mat o{};const bool ok=inverse(m,o);assert(ok);return o;}
static bool same(const Mat& a,const Mat& b) {return distance(a,b)<1e-6 && angle(a,b)<1e-6;}
int main() {
    const Mat att=rigid(0,1,0,25,.01,-.02,.03);              // grip -> hand attachment joint, procedural (grip_to_attachment)
    const Mat clip=rigid(1,1,1,119,.02,.01,-.03);            // the gun clip's authored socket: grip -> drawn joint 59
    const Mat offset=rigid(0,1,0,-67,.049,-.038,-.043);      // the copy's grip offset (the crossbow's measured hold, v1216 status)
    const Mat grip=rigid(0,0,1,30,-48.6,-93.1,-15.4),grip2=rigid(1,0,0,40,-48.2,-93.0,-15.1);
    const Mat placed=mul(grip,offset);                        // where the copy service put the copy (its engine frame)
    {   // the logged case: a clip on the copy's hand. The old binding (joint * inverse(attachment) taken as the grip) is off by the clip.
        const Mat joint=mul(grip,clip);
        const Mat old_bound=mul(mul(joint,inv(att)),offset);
        assert(angle(old_bound,placed)*57.29577951308232>60 && distance(old_bound,placed)>.02);   // the v1216 report: drawn rotated in the hand
        const Mat g=copy_joint_grip(joint,grip,offset);
        assert(valid_grip(g) && same(mul(joint,g),placed));                                          // the fix: drawn where it is placed
        GripCandidate c{};assert(candidate_from_offset(g,c,0x1234,write_held,7,1));c.space=space_joint59;
        assert(Binder::observe(0,c));                                                               // exact: bound on the first render
        Mat b{};assert(Binder::frame(0,mul(grip2,clip),b) && same(b,mul(grip2,offset)));           // the next render's drawn joint: still in the grip
    }
    {   // no clip: joint 59 = grip * attachment, the binding is the old one exactly
        const Mat joint=mul(grip,att);
        const Mat g=copy_joint_grip(joint,grip,offset);
        assert(same(g,mul(inv(att),offset)) && same(mul(joint,g),placed));
    }
    Mat singular{};assert(!valid_grip(copy_joint_grip(singular,grip,offset)));
    std::cout<<"copy_draw_grip_check ok\n";
    return 0;
}
