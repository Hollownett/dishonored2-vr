#include "../src/gadget_aim.hpp"
#include "../src/selection_policy.hpp"
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
    expect(!gadget_aim_wanted(p,true),"placement requires a valid palm");
    p.placement_valid=true;
    expect(gadget_aim_wanted(p,true) && !gadget_aim_wanted(p,false),"placement follows the palm without the trigger; throw sensor stays idle");
    {GadgetAimPose t=p;t.trigger=true;expect(gadget_aim_wanted(t,false) && gadget_aim_wanted(t,true),"trigger held feeds both sensors");}
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
    {   // v776 (headset v775: a mine could never be placed with the right hand): the placement ray leaves the PALM of the hand that holds the mine. A grip frame's
        // left axis points out of the back of the left hand and out of the right palm, so a left and a right hand with the palm to the same wall both get the ray to it.
        const auto reaches=[](const GadgetAimPose& g,double wall_x) { // the ray (origin, dir, length) crosses the plane x = wall_x
            if(g.place_dir[0]<=.01f) return false;
            const double t=(wall_x-g.place_origin[0])/g.place_dir[0];return t>=0 && t<=g.place_length;
        };
        const auto palm_to_plus_x=[](unsigned side) { // palm toward +X: left = -X for the left hand, +X for the right; forward along +Y / -Y; up +Z (right-handed)
            WorldTrackedPose h{};h.position={2,3,1};
            h.basis={side?Vec3{0,-1,0}:Vec3{0,1,0},side?Vec3{1,0,0}:Vec3{-1,0,0},Vec3{0,0,1}};return h;
        };
        for(unsigned side=0;side<2;++side) {
            GadgetAimPose g;gadget_palm_ray(g,side,palm_to_plus_x(side));
            expect(std::fabs(g.place_dir[0]-1.f)<1e-6f && std::fabs(g.place_dir[1])<1e-6f && std::fabs(g.place_dir[2])<1e-6f,"the ray runs along the palm normal (+X)");
            expect(std::fabs(g.place_origin[0]-1.9f)<1e-5f && g.place_origin[1]==3.f && g.place_origin[2]==1.f,"it starts 10 cm behind the palm");
            expect(g.place_length==static_cast<float>(mine_palm_reach_m),"and reaches 60 cm (v779: 50 cm in front of the palm)");
            expect(reaches(g,2.45) && reaches(g,2.2) && reaches(g,2.0),"a wall 45 or 20 cm in front of the palm, or right at it, is reached");
            expect(!reaches(g,2.6) && !reaches(g,1.5),"a wall out of reach, or behind the hand, is not");
            const auto probe=palm_probe(side,palm_to_plus_x(side),PalmOffset{},std::nullopt,Vec3{}); // the hand selection's palm normal (the project's reference)
            expect(std::fabs(probe.direction.x-g.place_dir[0])<1e-6 && std::fabs(probe.direction.y-g.place_dir[1])<1e-6 && std::fabs(probe.direction.z-g.place_dir[2])<1e-6,"the same palm normal as palm_probe");
        }
        {   // end to end, as publish_left_aim builds the hand: an OpenXR grip pose through to_game_pose. A controller held with identity orientation has its -X (the frame's left,
            // the game's +Y here) out of the right palm and out of the back of the left hand: the right palm faces the game's left, the left palm its right.
            GameCameraPose anchor{};anchor.position={10,20,30};anchor.basis={1,0,0,0,1,0,0,0,1};
            const TrackedPose origin{{0,0,0},{0,0,0,1}};
            for(unsigned side=0;side<2;++side) {
                const float to_wall=side?1.f:-1.f;   // game +Y is left
                GadgetAimPose g;gadget_palm_ray(g,side,to_game_pose(TrackedPose{{0,0,0},{0,0,0,1}},origin,anchor,1.));
                const double t=(20+to_wall*.2-g.place_origin[1])/g.place_dir[1];   // a wall 20 cm in front of the palm
                expect(std::fabs(g.place_dir[1]-to_wall)<1e-6f && std::fabs(g.place_dir[0])<1e-6f && t>0 && t<=g.place_length,"an identity controller's palm ray reaches the wall in front of its palm, either hand");
            }
        }
        {   // the old left-only sign on a right hand: the ray ran out of the back of the palm, away from the wall in front of it
            GadgetAimPose g;gadget_palm_ray(g,0,palm_to_plus_x(1));
            expect(!reaches(g,2.2) && g.place_dir[0]<-.99f,"a right hand read as a left one points away from its wall");
        }
    }
    std::printf(failures?"gadget_aim_check FAILED\n":"gadget_aim_check ok\n");
    return failures?1:0;
}
