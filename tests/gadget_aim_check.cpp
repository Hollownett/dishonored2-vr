#include "../src/gadget_aim.hpp"
#include <cstdio>
static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}
int main() {
    using namespace d2vr;
    GadgetAimPose p;p.origin={1,2,3};p.forward={0,1,0};p.tick=1000;
    expect(gadget_aim_fresh(p,1000) && gadget_aim_fresh(p,1100),"fresh within 100 ms");
    expect(!gadget_aim_fresh(p,1101) && !gadget_aim_fresh(p,999),"stale or from the future");
    GadgetAimPose z=p;z.tick=0;expect(!gadget_aim_fresh(z,10),"never published");
    GadgetAimPose n=p;n.forward={0,0,0};expect(!gadget_aim_fresh(n,1000),"zero forward");
    n=p;n.origin[1]=NAN;expect(!gadget_aim_fresh(n,1000),"NaN origin");
    n=p;n.forward={0,3,0};expect(!gadget_aim_fresh(n,1000),"non-unit forward");
    float start[3]{10,20,30},dir[3]{0,0,-1},ent[3]{5,6,7};
    {
        FieldSwap s;s.put(start,p.origin);s.put(dir,p.forward);s.put(ent,p.forward,true);s.put(nullptr,p.forward);
        expect(s.size()==3,"null skipped");
        expect(start[0]==1 && start[1]==2 && start[2]==3,"start swapped");
        expect(dir[0]==0 && dir[1]==1 && dir[2]==0,"direction swapped");
        expect(ent[0]==0 && ent[1]==1 && ent[2]==7,"xy-only keeps z");
    }
    expect(start[0]==10 && start[2]==30 && dir[2]==-1 && ent[0]==5 && ent[1]==6 && ent[2]==7,"scope restores every field");
    {
        FieldSwap s;s.put(start,p.origin);s.restore();s.restore();
        expect(start[0]==10 && start[1]==20,"explicit restore is idempotent");
    }
    {   // an engine write-back during the native call is kept; untouched fields still restore; mismatches are counted
        float a[3]{1,1,1},b[3]{2,2,2},c[3]{3,3,3};
        FieldSwap s;s.put(a,p.origin);s.put(b,p.forward);s.put(c,p.forward,true);
        b[1]=9.f;c[2]=8.f;   // engine rewrote b (counts) and c's z (not ours: no mismatch, kept)
        s.restore();
        expect(a[0]==1 && a[1]==1 && a[2]==1,"unchanged field restored");
        expect(b[1]==9.f && b[0]==0.f,"rewritten field kept as the engine left it");
        expect(c[0]==3 && c[1]==3 && c[2]==8.f,"xy_only field restores x,y and never touches z");
        expect(s.skipped()==1,"one mismatch counted");
    }
    std::printf(failures?"gadget_aim_check FAILED\n":"gadget_aim_check ok\n");
    return failures?1:0;
}
