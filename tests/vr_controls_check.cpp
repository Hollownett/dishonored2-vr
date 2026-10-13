#include "../src/vr_controls.hpp"
#include <cassert>
#include <cstdio>

static int failures=0;
static void require(bool condition,const char* what) {
    if(condition) return;
    ++failures;std::printf("FAIL: %s\n",what);
}
int main() {
    {
        d2vr::XWheelOwner x;
        x.update(false,true);
        require(!x.wheel() && !x.x_consumed(),"idle X stays closed");
        x.update(true,true);
        require(x.wheel() && x.x_consumed(),"a fresh allowed X opens the wheel");
        x.update(true,true);
        require(x.wheel() && x.x_consumed(),"held X keeps the wheel open");
        x.update(false,true);
        require(!x.wheel() && !x.x_consumed(),"release closes the wheel for the native equip");
        x.update(true,true);
        require(x.wheel(),"a released X can open the wheel again");
        x.update(true,false);
        require(!x.wheel() && x.x_consumed(),"lost allowed cancels at once and keeps the press claimed");
        x.update(true,true);
        require(!x.wheel() && x.x_consumed(),"allowed returning mid-press never reopens");
        x.update(false,true);
        require(!x.x_consumed(),"the cancelled claim ends on release");
        x.update(true,false);
        require(!x.wheel() && !x.x_consumed(),"a press that starts disallowed is ignored");
        x.update(true,true);
        require(!x.wheel() && !x.x_consumed(),"an ignored press never late-opens");
        x.update(false,true);x.update(true,true);
        require(x.wheel(),"release re-arms the fresh press edge");
        x.reset();
        require(!x.wheel() && !x.x_consumed(),"reset clears the owner");
        x.update(true,true);
        require(x.wheel(),"reset does not latch X");
    }
    {   // Two-handed wheel: left X at once, right A after 350 ms; first hand owns until release.
        d2vr::WheelHands w;
        auto u=[&](bool x,bool a,unsigned long long t,bool allowed=true) {w.update(x,a,allowed,t);};
        u(0,0,0);
        require(!w.wheel() && w.hand()==-1,"idle");
        u(1,0,100);
        require(w.wheel() && w.hand()==0 && !w.a_consumed(),"X opens the wheel for the left hand");
        u(1,1,200);u(1,1,900);
        require(w.hand()==0 && !w.a_consumed(),"A cannot steal or double-open while X owns it");
        u(0,1,950);u(0,1,1400);
        require(!w.wheel() && !w.a_consumed(),"X release closes; the A held through it never late-opens");
        u(0,0,1500);u(0,1,2000);u(0,0,2100);
        require(!w.wheel() && !w.a_consumed(),"A tap stays a native jump");
        u(0,1,3000);u(0,1,3300);
        require(!w.wheel() && !w.a_consumed(),"A before the hold stays native");
        u(0,1,3360);
        require(w.wheel() && w.hand()==1 && w.a_consumed(),"A hold opens for the right hand");
        u(1,1,3400);
        require(w.hand()==1,"X cannot take over while A owns it");
        u(1,0,3500);u(1,0,3600);
        require(!w.wheel() && !w.a_consumed(),"A release closes; X pressed during A stays ignored");
        u(0,0,3800);u(1,0,4000);u(1,0,4100,false);u(1,0,4200);
        require(!w.wheel(),"lost allowed cancels, no reopen");
        u(0,0,4300);u(0,1,5000);u(0,1,5100,false);u(0,1,5500);
        require(!w.wheel() && !w.a_consumed(),"A disallowed during the hold never opens");
        float tx=.1f,ty=0,mx=0,my=-1;
        d2vr::wheel_select(1,tx,ty,mx,my,true,1,0);
        require(tx==0 && ty==-1 && mx==0 && my==0,"right-owned: left stick selects, move dropped");
        tx=.9f;ty=0;mx=0;my=1;
        d2vr::wheel_select(0,tx,ty,mx,my,true,0,1);
        require(tx==.9f && ty==0 && my==0,"left-owned: right stick wins over dial and left stick is dropped");
        tx=ty=0;d2vr::wheel_select(1,tx,ty,mx,my,true,0,1);
        require(tx==0 && ty==1,"hand dial fills in when the stick is idle");
    }
    {   // Pointing cursor: either hand selects regardless of which button opened the wheel.
        using V=std::array<double,3>;
        d2vr::WheelCursor c;d2vr::WheelHands w;float x=0,y=0;
        const V rt{1,0,0};V L{-.2,1,-.3},R{.2,1,-.3};
        auto step=[&](bool tl=true,bool tr=true) {return c.update(true,{tl,tr},{L,R},rt,x,y);};
        w.update(true,false,true,0);
        require(w.hand()==0 && !step(),"left X opens; nothing selected at open");
        R={.2,1.01,-.3};step();R={.2,1.02,-.3};step();R={.2,1.05,-.3};
        require(step() && c.cursor()==1 && y>.99f && x==0,"right hand selects up while left X opened");
        R={.2,1.06,-.3};
        require(step() && c.cursor()==1,"cursor hand stays");
        require(!c.commit(true,{0,0}) && c.commit(true,{0,.9f}) && !c.commit(true,{0,.9f}),"fresh right trigger commits once");
        w.commit();
        require(!w.wheel(),"commit closes the wheel");
        w.update(false,false,true,10);
        w.update(false,true,true,20);w.update(false,true,true,400);
        require(w.hand()==1,"right A opens");
        c.update(false,{1,1},{L,R},rt,x,y);
        L={-.2,1,-.3};R={.2,1,-.3};step();
        L={-.25,1,-.3};step();L={-.3,1,-.3};
        require(step() && c.cursor()==0 && x<-.99f && y==0,"left hand selects left while right A opened");
        L={-.31,1,-.3};R={.21,1,-.3};
        require(step() && c.cursor()==0,"slower other hand does not steal");
        L={-.2,1,-.3};R={.2,1,-.3};c.update(false,{1,1},{L,R},rt,x,y);
        step();L={-.2,1.01,-.3};
        require(!step() && c.cursor()==0,"inside dead zone is neutral");
        float tx=0,ty=0,mx=0,my=1;
        d2vr::wheel_select(1,tx,ty,mx,my,true,1,0);
        require(tx==0 && ty==1,"stick deflection overrides the cursor");
        c.update(false,{1,1},{L,R},rt,x,y);
        require(!c.commit(true,{0,.9f}),"trigger held from before the press edge never commits without a cursor");
    }
    {
        d2vr::InteractOwner o;
        const std::array<float,2> idle{0,0};
        auto settle=[&](int hand,bool target,bool wheel,bool allowed) {o.update(idle,hand,target,wheel,allowed);};
        auto r=o.update(idle,1,true,false,true);
        require(!r.interact && !r.consume_trigger[0] && !r.consume_trigger[1],"idle triggers own nothing");
        r=o.update({0,0.5f},1,true,false,true);
        require(!r.interact && !r.consume_trigger[1],"a pull under the press threshold starts no role");
        r=o.update({0,0.6f},1,true,false,true);
        require(r.interact && r.consume_trigger[1] && !r.consume_trigger[0],"the active hand with a target interacts and consumes");
        r=o.update({0,0.4f},1,true,false,true);
        require(r.interact && r.consume_trigger[1],"the interact role survives the release hysteresis band");
        r=o.update({0,0.3f},1,true,false,true);
        require(!r.interact && !r.consume_trigger[1],"interact ends below the release threshold");
        settle(1,true,false,true);
        r=o.update({0,0.6f},1,false,false,true);
        require(!r.interact && !r.consume_trigger[1],"no target is native passthrough");
        r=o.update({0,0.7f},1,true,false,true);
        require(!r.interact && !r.consume_trigger[1],"a target appearing mid-press cannot upgrade a native role");
        require(!o.update(idle,1,true,false,true).consume_trigger[1],"the native role ends on release");
        settle(1,true,false,true);
        r=o.update({0.6f,0},1,true,false,true);
        require(!r.interact && !r.consume_trigger[0],"a non-active trigger is native");
        o.reset();settle(0,true,false,true);
        r=o.update({0.6f,0.6f},0,true,false,true);
        require(r.interact && r.consume_trigger[0] && !r.consume_trigger[1],"only the active side becomes an interact");
        settle(1,true,false,true);
        r=o.update({0,0.6f},1,true,false,false);
        require(!r.interact && !r.consume_trigger[1],"an interact press needs allowed at the press edge");
        o.reset();settle(1,true,false,true);
        r=o.update({0.6f,0.7f},1,true,true,true);
        require(!r.interact && r.consume_trigger[0] && r.consume_trigger[1],"an open wheel consumes either trigger");
        r=o.update({0.6f,0.7f},1,true,false,true);
        require(!r.interact && r.consume_trigger[0] && r.consume_trigger[1],"the wheel role latches past the wheel closing");
        settle(1,true,false,true);
        require(o.update({0,0.6f},1,true,false,true).interact,"an interact press starts");
        r=o.update({0,0.6f},1,true,false,false);
        require(!r.interact && r.consume_trigger[1],"allowed false cancels the interact but keeps consuming");
        r=o.update({0,0.6f},1,true,false,true);
        require(!r.interact && r.consume_trigger[1],"the cancelled press never resumes");
        require(!o.update(idle,1,true,false,true).consume_trigger[1],"the cancelled press frees on release");
        require(o.update({0,0.6f},1,true,false,true).interact,"a fresh press interacts again");
        settle(1,true,false,true);
        require(o.update({0,0.6f},1,true,false,true).interact,"interact starts with a target");
        r=o.update({0,0.6f},1,false,false,true);
        require(r.interact && r.consume_trigger[1],"the interact role latches when the target disappears");
        d2vr::InteractOwner held;
        r=held.update({0.8f,0},0,true,false,true);
        require(!r.interact && r.consume_trigger[0],"a trigger held at creation is consumed, not owned");
        for(int i=0;i<5;++i) r=held.update({0.8f,0},0,true,false,true);
        require(!r.interact && r.consume_trigger[0],"a trigger held at creation never starts a role");
        require(!held.update({0,0},0,true,false,true).consume_trigger[0],"release clears the created-hold drain");
        require(held.update({0.8f,0},0,true,false,true).interact,"a fresh press after release owns the interact");
        held.reset();
        r=held.update({0.8f,0},0,true,false,true);
        require(!r.interact && r.consume_trigger[0],"reset under a held trigger drains it");
        require(!held.update({0.4f,0},0,true,false,true).interact,"a partly released trigger stays in the drain");
        require(!held.update({0.3f,0},0,true,false,true).consume_trigger[0],"below the release threshold the drain ends");
        require(held.update({0.8f,0},0,true,false,true).interact,"a post-reset release allows a fresh interact");
    }
    if(failures) {std::printf("%d requirement(s) failed\n",failures);return 1;}
        {
        // A non-active trigger waits for its own ray, then interacts or fires.
        d2vr::InteractOwner owner;owner.update({0,0},1,false,false,true,1);
        auto r=owner.update({1,0},1,false,false,true,100);
        if(!r.consume_trigger[0] || r.interact) {std::puts("FAIL: pending press not consumed");return 1;}
        r=owner.update({1,0},0,true,false,true,170);
        if(!r.interact) {std::puts("FAIL: pending press did not become interact");return 1;}
        owner.update({0,0},0,false,false,true,200);
        r=owner.update({1,0},1,false,false,true,300);r=owner.update({1,0},1,false,false,true,560);
        if(r.consume_trigger[0]) {std::puts("FAIL: pending press did not fall back to native");return 1;}
    }
std::printf("PASS: X wheel open/close/cancel/no-reopen/late-press, "
        "interact role latch/cancel-consume/held-at-reset and native passthrough\n");
    {
        d2vr::InteractOwner owner;owner.update({0,0},1,false,false,true,1,false);
        auto r=owner.update({0,1},1,false,false,true,100,false);
        require(r.consume_trigger[1] && !r.interact,"selected hand waits for fresh query after locking");
        r=owner.update({0,1},1,true,false,true,170,false);
        require(r.consume_trigger[1] && !r.interact,"stale selected target cannot settle pending grip");
        r=owner.update({0,1},1,true,false,true,180,true);
        require(r.consume_trigger[1] && r.interact,"fresh locked-hand query settles into interact");
        owner.update({0,0},1,false,false,true,200,false);
        r=owner.update({0,1},1,false,false,true,300,false);
        r=owner.update({0,1},1,false,false,false,350,false);
        require(r.consume_trigger[1] && !r.interact,"focus loss drains pending grip");
        r=owner.update({0,1},1,true,false,true,400,true);
        require(r.consume_trigger[1] && !r.interact,"focus returning never replays pending grip");
    }
    {   // Laser pointer: head at origin facing -Z, plane 1 m ahead, radius .25 m.
        d2vr::WheelPointer p;float x=0,y=0;std::array<double,3> hit{};bool vis=false;
        const std::array<double,3> head{0,1.6,0},fwd{0,0,-1};
        std::array<std::array<double,3>,2> pos{{{.02,1.6,0},{.2,1.6,0}}},dir{{{0,0,-1},{0,0,-1}}};
        require(!p.update(true,{true,true},pos,dir,head,fwd,0,x,y,hit,vis) && vis,"centre hit is neutral but shows a dot");
        require(std::abs(hit[0]-.02)<1e-9 && std::abs(hit[1]-1.6)<1e-9 && std::abs(hit[2]+1)<1e-9,"dot is on the plane at the ray hit");
        pos[1]={.2,1.6,0};dir[1]={.25,0,-1}; // right hand aims +.25 m right at 1 m
        require(p.update(true,{true,true},pos,dir,head,fwd,0,x,y,hit,vis) && p.cursor()==1,"moving hand becomes the cursor");
        require(x>.99f && std::abs(y)<.01f,"right of centre selects the right wedge");
        dir[1]={0,.25,-1};pos[1]={.2,1.6,0};
        require(p.update(true,{true,true},pos,dir,head,fwd,0,x,y,hit,vis) && y>.3f && x>0,"upward ray gives positive y");
        {   // Stability: 2 mm tremor around the right/up-right wedge border never flips the sector or moves the dot much.
            d2vr::WheelPointer q;float qx=0,qy=0;std::array<double,3> h0{},h1{};bool v=false;
            std::array<std::array<double,3>,2> qp{{{0,1.6,0},{0,1.6,0}}},qd{{{.2,0,-1},{.2,0,-1}}};
            q.update(true,{true,true},qp,qd,head,fwd,0,qx,qy,h0,v);
            const float sx=qx,sy=qy;int flips=0;double drift=0;
            for(int i=0;i<60;++i) {
                const double j=(i&1?1:-1)*.002; // +-2 mm jitter at 1 m, and also across the 22.5 deg border
                qd[0]={.2+j,j*2,-1};qd[1]=qd[0];
                q.update(true,{true,true},qp,qd,head,fwd,0,qx,qy,h1,v);
                flips+=(qx!=sx||qy!=sy);drift=std::max(drift,std::abs(h1[0]-.2));
            }
            require(flips==0 && drift<.004,"tremor keeps the sector and the dot steady");
            // Hysteresis: at 25 deg (3 deg past the 22.5 edge) the wedge holds; at 35 deg it switches.
            qd[0]=qd[1]={.2,.2*std::tan(25*3.14159265/180),-1};
            q.update(true,{true,true},qp,qd,head,fwd,0,qx,qy,h1,v);
            require(qx==sx && qy==sy,"within 8 deg past the border the wedge holds");
            qd[0]=qd[1]={.2,.2*std::tan(35*3.14159265/180),-1};
            for(int i=0;i<30;++i) q.update(true,{true,true},qp,qd,head,fwd,0,qx,qy,h1,v);
            require(qy>.6f,"clearly inside the next wedge switches");
        }
        require(!p.update(true,{true,true},pos,{std::array<double,3>{0,0,1},std::array<double,3>{0,0,1}},head,fwd,0,x,y,hit,vis) && !vis,"rays pointing away show nothing");
        p.update(false,{true,true},pos,dir,head,fwd,0,x,y,hit,vis);
        require(!p.commit(true,{0,0}),"idle trigger does not commit");
        p.update(true,{true,true},pos,dir,head,fwd,0,x,y,hit,vis);
        require(p.commit(true,{1,0}) && !p.commit(true,{1,0}),"fresh trigger on the cursor hand commits once");
    }
    {   // Wheel face buttons: fresh presses pass while open; held-through open/close stay masked.
        d2vr::WheelFaceButtons f;
        auto o=f.update(false,false,true,true);
        require(!o.y && o.active,"Y held at wheel open is masked");
        o=f.update(false,true,true,true);
        require(o.b && !o.y && !o.a,"fresh B passes while Y stays masked");
        o=f.update(true,true,false,true);
        require(o.a && o.b && !o.y,"A and B pass natively in the wheel");
        o=f.update(true,false,false,false);
        require(!o.a && !o.b && o.active,"A held through close is masked");
        o=f.update(true,false,false,false);
        require(!o.a && o.active,"still masked while held");
        o=f.update(false,false,false,false);
        require(!o.active,"release clears the mask");
        o=f.update(true,false,false,false);
        require(!o.active && !o.a,"closed wheel is never forced to pass");
    }
    if(failures) return 1;
        {   // A tap jumps on release; a hold (wheel) never jumps.
        d2vr::WheelHands h;
        require(!h.a_passthrough(true,1000) && !h.a_passthrough(true,1100),"A press held back");
        require(h.a_passthrough(false,1150) && h.a_passthrough(false,1200) && !h.a_passthrough(false,1240),"A tap pulses on release");
        require(!h.a_passthrough(true,2000) && !h.a_passthrough(true,2400) && !h.a_passthrough(false,2500),"A hold never jumps");
    }
    { // Journal: a hand at the left temple + grip edge -> one 150 ms pulse; the squeeze is consumed until release.
        d2vr::TempleReach r;const std::array<double,3> head{0,1.7,0},fwd{0,0,-1},up{0,1,0};
        std::array<std::optional<std::array<double,3>>,2> front{std::array<double,3>{-.2,1.2,-.4},std::array<double,3>{.2,1.2,-.4}};
        std::array<std::optional<std::array<double,3>>,2> temple{std::array<double,3>{-.1,1.72,-.02},front[1]};
        std::array<std::optional<std::array<double,3>>,2> right_temple{front[0],std::array<double,3>{.1,1.72,-.02}};
        assert(!r.update(head,fwd,up,front,{1.f,0.f},true,0).journal);                 // squeezing in front: not the journal
        assert(!r.update(head,fwd,up,front,{0.f,0.f},true,10).journal);
        assert(!r.update(head,fwd,up,right_temple,{0.f,1.f},true,15).journal);        // right temple: not the journal
        assert(!r.update(head,fwd,up,front,{0.f,0.f},true,16).journal);
        const auto e=r.update(head,fwd,up,temple,{0.f,0.f},true,20);assert(e.entered==0 && !e.journal);
        const auto j=r.update(head,fwd,up,temple,{1.f,0.f},true,30);assert(j.journal && j.consume[0]);
        assert(r.update(head,fwd,up,front,{1.f,0.f},true,100).consume[0]);            // still held after leaving: still consumed
        assert(!r.update(head,fwd,up,front,{1.f,0.f},true,200).journal);
        assert(!r.update(head,fwd,up,front,{0.f,0.f},true,210).consume[0]);
        assert(!r.update(head,fwd,up,temple,{1.f,0.f},false,300).journal);            // not in gameplay
    }
    { // Wheel art zones: measured capture (553x306 sim, 0.8 m quad) and a square headset capture (2496x2688).
        const double h=.8*306/553;
        const auto at=[&](double u,double v,unsigned w,unsigned hh,double width){return *d2vr::wheel_stage_point((u-.5)*width,(.5-v)*width*hh/w,width,w,hh);};
        auto p=at(.212,.082,553,306,.8);assert(d2vr::wheel_zone(p[0],p[1])==d2vr::WheelZone::health);
        p=at(.081,.384,553,306,.8);assert(d2vr::wheel_zone(p[0],p[1])==d2vr::WheelZone::mana);
        p=at(.94,.082,553,306,.8);assert(d2vr::wheel_zone(p[0],p[1])==d2vr::WheelZone::hide);
        p=at(.5,.52,553,306,.8);assert(d2vr::wheel_zone(p[0],p[1])==d2vr::WheelZone::ring);
        // Headset: stage fitted by width, centred vertically. Stage (397,89) -> pixel (516,758).
        p=at(516./2496,(758.)/2688,2496,2688,.8);assert(d2vr::wheel_zone(p[0],p[1])==d2vr::WheelZone::health);
        assert(std::abs(d2vr::wheel_metres_per_stage(.8,2496,2688)*326-.1358)<1e-3); // ring radius on the headset quad
        (void)h;
    }
    return 0;
}
