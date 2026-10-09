#include "../src/vr_turning.hpp"
#include "../src/finger_pose.hpp"
#include <iostream>
#include <limits>
#include <thread>
#include <atomic>
#include <fstream>
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
    try {
        d2vr::SnapTurn turns;turns.configure({true,90,.6,.3});
        require(turns.observe(.8,true,true,0,1) && turns.take(true,1,1)==0,"Held stick activated on enable");
        double total{};
        for(unsigned step=0;step<4;++step) {
            turns.observe(0,true,true,10+step*20,1);
            turns.observe(1,true,true,11+step*20,1);
            require(turns.take(false,12+step*20,1)==0,"Render replay consumed a body turn");
            const auto delta=turns.take(true,13+step*20,1);total+=delta;
            require(std::abs(delta-1.5707963267948966)<1e-12,"Snap angle is not sensitivity-independent");
            require(turns.take(true,14+step*20,1)==0,"Second eye/update duplicated a snap");
            turns.observe(1,true,true,15+step*20,1);require(turns.take(true,16+step*20,1)==0,"Held stick repeatedly snapped");
        }
        require(std::abs(total-6.283185307179586)<1e-12,"Four quarter turns did not return to the initial heading");
        turns.observe(0,true,true,100,1);turns.observe(1,true,true,101,1);
        turns.observe(1,false,true,102,1);require(turns.take(true,103,1)==0,"Menu transition retained a queued snap");
        turns.observe(1,true,true,104,1);require(turns.take(true,105,1)==0,"Resume fired a held stick");
        turns.observe(0,true,true,110,1);turns.observe(1,true,true,111,1);
        require(turns.take(true,612,1)==0,"Stale snap reached a later gameplay update");
        turns.observe(0,true,true,620,1);turns.observe(-1,true,true,621,1);
        require(turns.take(true,622,2)==0,"Recenter epoch retained an old turn");
        require(!turns.observe(std::numeric_limits<double>::quiet_NaN(),true,true,630,2),"Invalid axis claimed the native input");
        require(!turns.observe(0,true,false,640,2),"Settings/weapon wheel axis was stolen");
        turns.observe(0,true,true,650,2);turns.observe(-1,true,true,651,2);
        std::atomic<unsigned> consumers{};
        std::thread a([&]{if(turns.take(true,652,2)!=0)++consumers;}),b([&]{if(turns.take(true,652,2)!=0)++consumers;});
        a.join();b.join();require(consumers==1,"Concurrent engine/eye consumers duplicated the turn");
        namespace r=d2vr::rig;
        r::Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=identity.f[15]=1;
        std::array<r::Transform,69> open{},closed{},current{};open.fill(identity);closed.fill(identity);current.fill(identity);
        r::ArmHierarchy parents{};parents.fill(0);parents[0]=-1;
        // Deliberately reverse declaration order: wrist 4 -> 9 -> 7 -> 5.
        parents[9]=4;parents[7]=9;parents[5]=7;
        const std::array<unsigned,3> chain{9,7,5};
        auto base=identity;std::array<bool,69> fingers{};std::array<float,69> amounts{};
        for(auto joint:chain) {
            auto local=identity;local.f[12]=.1f;
            open[joint]=r::rigid_retarget(local,identity,base);base=open[joint];fingers[joint]=true;amounts[joint]=.5f;
        }
        base=identity;
        for(auto joint:chain) {
            auto local=identity;local.f[0]=local.f[5]=0;local.f[1]=1;local.f[4]=-1;local.f[12]=.1f;
            closed[joint]=r::rigid_retarget(local,identity,base);base=closed[joint];
        }
        current=open;const auto middle=r::blend_finger_palette(current,open,closed,parents,fingers,amounts);
        // Three local 45-degree curls must yield +135 degrees, rather than the
        // shortest-path -45 degrees from blending the global 270-degree tip.
        const auto tip=r::column(middle[5],0);
        require(std::abs(tip.x+std::sqrt(.5))<1e-5 && std::abs(tip.y-std::sqrt(.5))<1e-5,"Finger tip took the wrong global rotation path");
        for(auto joint:chain) require(std::abs(d2vr::length(r::position(middle[joint])-r::position(middle[parents[joint]]))-.1)<1e-5,"Finger segment shrank or opened a gap");
        for(unsigned joint=0;joint<69;++joint) if(!fingers[joint]) require(middle[joint].f==current[joint].f,"Finger blending changed a wrist/arm/body joint");
        amounts.fill(0);const auto first=r::blend_finger_palette(current,open,closed,parents,fingers,amounts);
        amounts.fill(1);const auto last=r::blend_finger_palette(current,open,closed,parents,fingers,amounts);
        for(auto joint:chain) for(unsigned i=0;i<16;++i) {
            require(std::abs(first[joint].f[i]-open[joint].f[i])<1e-5,"Open endpoint differs");
            require(std::abs(last[joint].f[i]-closed[joint].f[i])<1e-5,"Fist endpoint differs");
        }
        const auto stats=turns.stats();
        std::ofstream report("artifacts/vr-reuse-validation.json");
        report << "{\"snap_steps_fired\":" << stats.fired << ",\"snap_batches_consumed\":" << stats.consumed
               << ",\"snap_cancelled\":" << stats.cancelled << ",\"four_turns_degrees\":360,\"finger_chain_segments\":3,\"native_game_writes\":false,\"headset_validation\":false}\n";
        require(static_cast<bool>(report),"Cannot save reuse validation");
        std::cout<<"PASS: source-adapted snap replay/focus/recenter/expiry/concurrent consumption; upstream finger screw blend on parent-relative chains, preserved segment lengths, endpoints and untouched non-finger joints\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
