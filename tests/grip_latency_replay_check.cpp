// Replays a real headset controller-trace.csv (arg 1 or D2VR_GRIP_TRACE) through GripIntent with the old fixed 100 ms window and
// the quick commit, and reports how many ms a grip-alone press saves and how many fists the new rule would lose. No trace: skipped.
#define _CRT_SECURE_NO_WARNINGS
#include "../src/vr_controls.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
struct Row {unsigned long long tick;float grip[2],trigger[2];};
int main(int argc,char** argv) {
    const char* path=argc>1?argv[1]:std::getenv("D2VR_GRIP_TRACE");
    std::ifstream in(path?path:"");
    if(!in) {std::printf("grip_latency_replay_check: no trace, skipped\n");return 0;}
    std::string line;std::getline(in,line);
    std::vector<std::string> names;{std::stringstream h(line);std::string c;while(std::getline(h,c,','))names.push_back(c);}
    const auto col=[&](const char* n){for(std::size_t i=0;i<names.size();++i)if(names[i]==n)return static_cast<int>(i);return -1;};
    const int ct=col("tick_ms"),lg=col("left_grip"),rg=col("right_grip"),lt=col("left_trigger"),rt=col("right_trigger");
    if(ct<0||lg<0||rg<0||lt<0||rt<0) {std::printf("grip_latency_replay_check: unexpected columns\n");return 1;}
    std::vector<Row> rows;
    while(std::getline(in,line)) {
        std::vector<std::string> f;std::stringstream s(line);std::string c;while(std::getline(s,c,','))f.push_back(c);
        if(f.size()<=static_cast<std::size_t>(std::max({ct,lg,rg,lt,rt}))) continue;
        try {rows.push_back({std::stoull(f[ct]),{std::stof(f[lg]),std::stof(f[rg])},{std::stof(f[lt]),std::stof(f[rt])}});} catch(...) {}
    }
    std::vector<long long> saved;int fists_old=0,fists_new=0,uses_old=0,uses_new=0;
    for(unsigned h=0;h<2;++h) {
        d2vr::GripIntent old_rule,new_rule;long long old_at=-1,new_at=-1;
        for(const auto& r:rows) {
            const auto o=old_rule.update(r.grip[h],r.trigger[h],r.tick,true,100,false,400,1000000);
            const auto n=new_rule.update(r.grip[h],r.trigger[h],r.tick,true);
            if(o.fist_started) ++fists_old;
            if(n.fist_started) ++fists_new;
            if(o.use_started) {++uses_old;old_at=static_cast<long long>(r.tick);}
            if(n.use_started) {++uses_new;new_at=static_cast<long long>(r.tick);}
            if(r.grip[h]<.35f) {if(old_at>=0 && new_at>=0) saved.push_back(old_at-new_at);old_at=new_at=-1;}
        }
    }
    std::sort(saved.begin(),saved.end());
    long long sum=0;for(const auto v:saved) sum+=v;
    const long long med=saved.empty()?0:saved[saved.size()/2];
    std::printf("grip_latency_replay_check: %zu rows, Use presses old %d new %d, fists old %d new %d, median saved %lld ms, mean %.1f ms\n",
        rows.size(),uses_old,uses_new,fists_old,fists_new,med,saved.empty()?0.:static_cast<double>(sum)/static_cast<double>(saved.size()));
    if(fists_new*10<fists_old*9 || (!saved.empty() && med<30)) {std::printf("FAIL\n");return 1;}
    return 0;
}
