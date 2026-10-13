#include "../src/vr_gestures.hpp"
#include <cstdio>

using namespace d2vr;

static int failures=0;
static void require(bool condition,const char* what) {
    if(condition) return;
    ++failures;std::printf("FAIL: %s\n",what);
}
static GV3 V(float x,float y,float z) {return {x,y,z};}

struct Rig {
    GestureFrame f{};
    Rig() {
        f.head_pos=V(0,1.6f,0);f.head_fwd=V(0,0,-1);f.head_right=V(1,0,0);
        f.allowed=true;f.tracked[0]=f.tracked[1]=true;
        f.pos[0]=V(0,1.4f,-0.3f);f.pos[1]=V(0,1.4f,-0.5f);
        for(int i=0;i<2;++i) {f.aim[i]=V(0,0,-1);f.up[i]=V(0,1,0);}
    }
};

int main() {
    {
        // Block: dwell, entry, hysteresis band, release dwell.
        SwordBlockPose b;
        Rig r;r.f.sword_equipped=true;
        r.f.pos[1]=V(0,1.6f,-0.4f);r.f.aim[1]=V(1,0,0);r.f.vel[1]=V(0,0,0);
        auto step=[&](SwordBlockPose& p,const GestureFrame& f) {
            return p.update(f.head_pos,f.head_fwd,f.head_right,f.pos[1],f.aim[1],
                len(f.vel[1]),f.sword_equipped,f.allowed,f.tracked[1],f.now_ms);
        };
        bool r1=false;
        for(int i=0;i<5;++i) {r.f.now_ms=static_cast<std::uint64_t>(i*11);r1=step(b,r.f);}
        require(!r1,"block waits for the 80 ms dwell");
        for(int i=5;i<=8;++i) {r.f.now_ms=static_cast<std::uint64_t>(i*11);r1=step(b,r.f);}
        require(r1,"block enters after the dwell");
        r.f.aim[1]=V(0,0,-1); // pointing forward: any orientation in front still blocks (headset v349)
        for(int i=9;i<20;++i) {r.f.now_ms=static_cast<std::uint64_t>(i*11);r1=step(b,r.f);}
        require(r1,"a forward-pointing sword in front keeps the block");
        r.f.pos[1]=V(0.8f,1.6f,-0.4f); // hand out at the side
        for(int i=20;i<=33;++i) {r.f.now_ms=static_cast<std::uint64_t>(i*11);r1=step(b,r.f);}
        require(r1,"block holds through the 150 ms release");
        r.f.now_ms=374;r1=step(b,r.f);
        require(!r1,"block leaves after 150 ms with the hand at the side");
        SwordBlockPose b2;r.f.pos[1]=V(0.25f,0.6f,-0.2f); // arm hanging down
        for(int i=0;i<30;++i) {r.f.now_ms=static_cast<std::uint64_t>(i*11);r1=step(b2,r.f);}
        require(!r1,"a lowered sword never blocks");
        r.f.pos[1]=V(0,1.6f,-0.4f);
        r.f.aim[1]=V(1,0,0);SwordBlockPose b3;
        for(int i=0;i<20;++i) {r.f.now_ms=static_cast<std::uint64_t>(i*11);r1=step(b3,r.f);}
        require(r1,"the block is on before the cancel checks");
        r.f.allowed=false;r.f.now_ms=500;r1=step(b3,r.f);
        require(!r1,"block drops immediately when allowed is lost");
        r.f.allowed=true;r.f.sword_equipped=false;r.f.now_ms=600;
        for(int i=0;i<30;++i) {r.f.now_ms=600+static_cast<std::uint64_t>(i*11);r1=step(b3,r.f);}
        require(!r1,"block never enters while the sword is not equipped");
    }
    {
        // Throat slash: fast lateral swipe near the head.
        ThroatSlash t;
        Rig r;r.f.takedown_available=true;r.f.vel[1]=V(2.5f,0,0);
        r.f.pos[1]=V(0,1.6f,-0.4f);
        auto step=[&](ThroatSlash& p,const GestureFrame& f) {
            return p.update(f.head_pos,f.head_fwd,f.head_right,f.pos[1],f.vel[1],
                f.takedown_available,f.allowed,f.tracked[1],f.now_ms);
        };
        const bool a1=step(t,r.f);
        r.f.now_ms=11;const bool a2=step(t,r.f);
        require(!a1 && a2,"the lateral swipe near the head fires on the second frame");
        bool again=false;
        for(int i=2;i<8;++i) {r.f.now_ms=static_cast<std::uint64_t>(i*11);again|=step(t,r.f);}
        require(!again,"a continuing swipe does not fire again");
        ThroatSlash tv;r.f.takedown_available=true;r.f.vel[1]=V(0,2.5f,0);
        for(int i=0;i<4;++i) {r.f.now_ms=static_cast<std::uint64_t>(i*11);require(!step(tv,r.f),"a vertical chop does not fire");}
        ThroatSlash tf;r.f.vel[1]=V(0,0,-2.5f);r.f.pos[1]=V(0,1.6f,-0.4f);
        for(int i=0;i<4;++i) {r.f.now_ms=static_cast<std::uint64_t>(i*11);require(!step(tf,r.f),"a forward thrust does not fire");}
        ThroatSlash tn;r.f.vel[1]=V(2.5f,0,0);r.f.takedown_available=false;
        for(int i=0;i<4;++i) {r.f.now_ms=static_cast<std::uint64_t>(i*11);require(!step(tn,r.f),"no takedown prompt means no slash");}
    }
    {
        // Throat slash: one pulse per motion and the 800 ms cooldown.
        Rig r;r.f.takedown_available=true;r.f.pos[1]=V(0,1.6f,-0.4f);
        auto step=[&](ThroatSlash& p,const GestureFrame& f) {
            return p.update(f.head_pos,f.head_fwd,f.head_right,f.pos[1],f.vel[1],
                f.takedown_available,f.allowed,f.tracked[1],f.now_ms);
        };
        ThroatSlash t;std::uint64_t now=0;bool fired=false;
        r.f.vel[1]=V(2.5f,0,0);r.f.now_ms=now;step(t,r.f);now+=11;
        r.f.now_ms=now;fired=step(t,r.f);
        require(fired,"the first swipe fires");
        for(int i=0;i<16;++i) {now+=11;r.f.vel[1]=V(0,0,0);r.f.now_ms=now;step(t,r.f);}
        r.f.vel[1]=V(2.5f,0,0);r.f.now_ms=now;step(t,r.f);now+=11;r.f.now_ms=now;
        require(!step(t,r.f),"the 800 ms cooldown blocks a second swipe");
        for(int i=0;i<16;++i) {now+=11;r.f.vel[1]=V(0,0,0);r.f.now_ms=now;step(t,r.f);}
        while(now<850) {now+=11;r.f.vel[1]=V(0,0,0);r.f.now_ms=now;step(t,r.f);}
        r.f.vel[1]=V(2.5f,0,0);r.f.now_ms=now;step(t,r.f);now+=11;r.f.now_ms=now;
        require(step(t,r.f),"a swipe after the cooldown fires");
    }
    {
        // Push: open hand, one pulse; closed fist never fires.
        Rig r;r.f.grip[0]=0;r.f.pos[0]=V(0,1.4f,-0.3f);r.f.vel[0]=V(0,0,-1.5f);
        r.f.pos[1]=V(0,1.4f,0.8f);
        PowerGestures p;std::uint64_t now=0;int pulses=0;
        for(int i=0;i<20;++i) {r.f.now_ms=now;if(p.update(r.f)==PowerGesture::cast_push)++pulses;
            r.f.pos[0].z-=1.5f*0.011111f;now+=11;}
        require(pulses==1,"the open-hand push fires exactly once");
        Rig r2;r2.f.grip[0]=0.8f;r2.f.pos[0]=V(0,1.4f,-0.3f);r2.f.vel[0]=V(0,0,-1.5f);
        PowerGestures p2;now=0;pulses=0;
        for(int i=0;i<20;++i) {r2.f.now_ms=now;if(p2.update(r2.f)==PowerGesture::cast_push)++pulses;
            r2.f.pos[0].z-=1.5f*0.011111f;now+=11;}
        require(pulses==0,"a closed fist never pushes");
    }
    {
        // Yoink: only after the LT hold, on a flick toward the head.
        Rig r;r.f.pos[0]=V(0,1.4f,-0.4f);
        PowerGestures p;std::uint64_t now=0;bool early=false,got=false;
        for(int i=0;i<13;++i) {r.f.now_ms=now;r.f.trigger[0]=0.7f;r.f.vel[0]=V(0,0,0);
            if(p.update(r.f)==PowerGesture::cast_yoink) early=true;now+=11;}
        require(!early,"yoink waits for the 150 ms trigger hold");
        r.f.vel[0]=V(0,1.0f,2.0f);
        for(int i=0;i<3;++i) {r.f.now_ms=now;
            if(p.update(r.f)==PowerGesture::cast_yoink) got=true;now+=11;}
        require(got,"yoink fires on a flick toward the head after the hold");
    }
    {
        // Temple toggle: dwell, latch, leave, re-arm.
        Rig r;const GV3 anchor=V(-0.09f,1.64f,0);r.f.pos[0]=anchor;r.f.vel[0]=V(0,0,0);
        PowerGestures p;std::uint64_t now=0;int pulses=0;
        for(int i=0;i<40;++i) {r.f.now_ms=now;if(p.update(r.f)==PowerGesture::temple_toggle)++pulses;now+=11;}
        require(pulses==1,"the temple dwell toggles once");
        r.f.pos[0]=V(-0.09f,1.64f,-0.5f);
        for(int i=0;i<5;++i) {r.f.now_ms=now;p.update(r.f);now+=11;}
        r.f.pos[0]=anchor;
        for(int i=0;i<40;++i) {r.f.now_ms=now;if(p.update(r.f)==PowerGesture::temple_toggle)++pulses;now+=11;}
        require(pulses==2,"leaving the temple re-arms the toggle");
    }
    {
        // Clap.
        Rig r;r.f.pos[0]=V(0,1.5f,-0.05f);r.f.pos[1]=V(0,1.5f,0.05f);
        r.f.vel[0]=V(0,0,1);r.f.vel[1]=V(0,0,-1);r.f.now_ms=0;
        PowerGestures p;
        require(p.update(r.f)==PowerGesture::clap,"converging hands fire the clap");
    }
    {
        // Global cooldown: a clap pulse swallows a push inside 350 ms.
        Rig r;r.f.pos[0]=V(0,1.5f,-0.05f);r.f.pos[1]=V(0,1.5f,0.05f);
        r.f.vel[0]=V(0,0,1);r.f.vel[1]=V(0,0,-1);r.f.now_ms=0;
        PowerGestures p;
        require(p.update(r.f)==PowerGesture::clap,"the clap starts the global cooldown");
        r.f.pos[1]=V(0,1.4f,0.8f);r.f.vel[1]=V(0,0,0);
        r.f.pos[0]=V(0,1.4f,-0.3f);r.f.vel[0]=V(0,0,-1.5f);r.f.grip[0]=0;
        std::uint64_t now=100;int early=0;
        for(int i=0;i<12;++i) {r.f.now_ms=now;if(p.update(r.f)==PowerGesture::cast_push)++early;
            r.f.pos[0].z-=1.5f*0.011111f;now+=11;}
        require(early==0,"the global cooldown swallows the push");
        r.f.pos[0]=V(0,1.4f,-0.3f);r.f.vel[0]=V(0,0,0);
        for(int i=0;i<20;++i) {r.f.now_ms=now;p.update(r.f);now+=11;}
        r.f.vel[0]=V(0,0,-1.5f);int late=0;
        for(int i=0;i<12;++i) {r.f.now_ms=now;if(p.update(r.f)==PowerGesture::cast_push)++late;
            r.f.pos[0].z-=1.5f*0.011111f;now+=11;}
        require(late==1,"the push fires after the global cooldown");
    }
    {
        // No gestures while disallowed, and no late fire for a disallowed motion.
        Rig r;r.f.allowed=false;r.f.grip[0]=0;
        r.f.pos[0]=V(0,1.4f,-0.3f);r.f.vel[0]=V(0,0,-1.5f);
        PowerGestures p;std::uint64_t now=0;int pulses=0;
        for(int i=0;i<6;++i) {r.f.now_ms=now;if(p.update(r.f)!=PowerGesture::none)++pulses;
            r.f.pos[0].z-=1.5f*0.011111f;now+=11;}
        require(pulses==0,"no gesture fires while disallowed");
        r.f.allowed=true;
        for(int i=0;i<8;++i) {r.f.now_ms=now;if(p.update(r.f)==PowerGesture::cast_push)++pulses;
            r.f.pos[0].z-=1.5f*0.011111f;now+=11;}
        require(pulses==0,"a motion begun disallowed never fires late");
    }
    {
        // Primitives: SpeedFlick, PoseHold, ConvergeDiverge.
        SpeedFlick sf(0.9f,0.4f);
        require(!sf.update(0.5f,true),"speed below the high threshold does not fire");
        require(sf.update(1.0f,true),"the high threshold fires once");
        require(!sf.update(1.2f,true),"a held speed stays spent");
        require(!sf.update(0.3f,true),"the hysteresis band does not re-arm");
        require(sf.update(1.0f,true),"dropping below the re-arm threshold allows a new pulse");
        SpeedFlick sf2(0.9f,0.4f);
        require(!sf2.update(1.0f,false),"a disallowed motion fires nothing");
        require(!sf2.update(1.0f,true),"and stays spent when allowed returns");
        require(sf2.update(1.0f,true)==false && sf2.update(0.2f,true)==false
            && sf2.update(1.0f,true),"a disallowed motion re-arms only after stopping");

        PoseHold ph(0.14f,0.25f,300,0.3f);
        std::uint64_t now=0;int n=0;std::uint64_t first=0;
        for(int i=0;i<60;++i) {if(ph.update(0.10f,1.f,0.1f,true,now)) {++n;if(!first) first=now;} now+=11;}
        require(n==1 && first>=300,"the pose hold pulses once after the dwell");
        require(!ph.update(0.30f,1.f,0.1f,true,now),"the pose hold resets outside the leave radius");
        n=0;
        for(int i=0;i<40;++i) {if(ph.update(0.10f,1.f,0.1f,true,now)) ++n;now+=11;}
        require(n==1,"the pose hold re-arms after leaving");

        ConvergeDiverge cd(0.10f,0.8f,600);
        require(cd.update(0.09f,1.0f,true,0),"the converge primitive fires on closure");
        require(!cd.update(0.09f,1.0f,true,100),"and stays spent inside the cooldown");
        cd.update(0.30f,0.f,true,700);
        require(cd.update(0.09f,1.0f,true,800),"the converge primitive re-arms after the cooldown");
        ConvergeDiverge cd2(0.10f,0.8f,600);
        require(!cd2.update(0.09f,1.0f,false,0),"converge needs allowed");
    }
    if(failures) {std::printf("%d requirement(s) failed\n",failures);return 1;}
    std::printf("PASS: block dwell/hysteresis/cancel, throat slash lateral/once/cooldown, "
        "push, yoink, temple, clap, global cooldown, disallowed spend, primitives\n");
    return 0;
}
