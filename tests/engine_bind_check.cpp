// v1319 check of weapon_draw::engine_bind_g (src/weapon_draw_place.hpp): the weapon's place on the drawn joint 59 from the engine's own bind
// (tag rotation q = x,y,z,w, tag origin o, model scales s, and for a crossed item the joint correction C applied as columns * C).
// Checks: the identity tag is diag(s); the right arm's weapon tag (0,1,0,0) is a half turn about Y; C = y180 on a y180 tag cancels; and for a
// random joint pose J the engine's own composition F = (J * C) * [R(q) | o] * diag(s), built column by column as cross_joint_both and the
// model frame do, gives inverse(J) * F == engine_bind_g to 1e-9; a bad quaternion or scale is refused.
//   Build/run: build\tmp-check-run.cmd engine_bind_check /DNOMINMAX
#include "../src/weapon_draw_place.hpp"
#include <cstdio>
#include <cmath>
using namespace d2vr::weapon_draw;
static int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)
static double worst(const Mat& a,const Mat& b) {double w=0;for(int i=0;i<12;++i) w=std::max(w,std::abs(a[static_cast<std::size_t>(i)]-b[static_cast<std::size_t>(i)]));return w;}
static Mat rot(double ax,double ay,double az,double angle) {
    const double n=std::sqrt(ax*ax+ay*ay+az*az);ax/=n;ay/=n;az/=n;const double c=std::cos(angle),s=std::sin(angle),t=1-c;
    Mat m=identity();
    m[0]=t*ax*ax+c;m[1]=t*ax*ay-s*az;m[2]=t*ax*az+s*ay;m[4]=t*ax*ay+s*az;m[5]=t*ay*ay+c;m[6]=t*ay*az-s*ax;m[8]=t*ax*az-s*ay;m[9]=t*ay*az+s*ax;m[10]=t*az*az+c;
    return m;
}
int main() {
    const float one[3]{1,1,1},zero[3]{0,0,0},id_q[4]{0,0,0,1},y180_q[4]{0,1,0,0};
    const float y180[9]{-1,0,0, 0,1,0, 0,0,-1};
    Mat g{};
    CHECK(engine_bind_g(id_q,zero,one,nullptr,g) && worst(g,identity())<1e-12);
    Mat ry=identity();ry[0]=-1;ry[10]=-1;
    CHECK(engine_bind_g(y180_q,zero,one,nullptr,g) && worst(g,ry)<1e-12);
    CHECK(engine_bind_g(y180_q,zero,one,y180,g) && worst(g,identity())<1e-12);   // a crossed right-tag item: the correction undoes the tag
    // random joint, tag and correction: the engine's composition column by column
    const float q[4]{.1826f,-.3651f,.5477f,.7303f};   // normalized (1,-2,3,4)/sqrt(30)
    const float o[3]{.012f,-.034f,.051f},s[3]{1.02f,.98f,1.01f};
    const float heart_c[9]{-1,0,0, 0,0,1, 0,1,0};
    Mat J=rot(.3,-.5,.8,1.1);J[3]=-91.7;J[7]=-92.4;J[11]=4.9;
    Mat JC=J;   // columns * C
    for(int r=0;r<3;++r) for(int i=0;i<3;++i) {double t=0;for(int k=0;k<3;++k) t+=J[static_cast<std::size_t>(r*4+k)]*heart_c[k*3+i];JC[static_cast<std::size_t>(r*4+i)]=t;}
    Mat L{};CHECK(engine_bind_g(q,o,one,nullptr,L));   // [R(q) | o]
    Mat F=mul(JC,L);
    for(int r=0;r<3;++r) for(int c=0;c<3;++c) F[static_cast<std::size_t>(r*4+c)]*=s[c];   // the model frame's scales on its axis columns (model_of)
    Mat inv_j{};CHECK(inverse(J,inv_j));
    CHECK(engine_bind_g(q,o,s,heart_c,g));
    const double err=worst(mul(inv_j,F),g);
    std::printf("engine bind: inverse(J) * F vs G %.2e\n",err);
    CHECK(err<1e-9);
    const float bad_q[4]{0,0,0,2},bad_s[3]{1,0,1};
    CHECK(!engine_bind_g(bad_q,zero,one,nullptr,g) && !engine_bind_g(id_q,zero,bad_s,nullptr,g));
    std::printf("%s\n",failures?"FAILED":"ok");
    return failures?1:0;
}
