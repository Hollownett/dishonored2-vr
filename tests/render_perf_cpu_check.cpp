#define NOMINMAX
#include "../src/render_perf_cpu.hpp"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

int main() {
    const auto storage=std::make_unique<d2vr::RenderPerfCpu>();auto& counters=*storage;
    counters.record(1,2,d2vr::RenderPerfCpu::hud_validate,100);
    counters.record(1,2,d2vr::RenderPerfCpu::hud_validate,300);
    counters.record(1,3,d2vr::RenderPerfCpu::hud_begin,500);
    counters.record(1,3,d2vr::RenderPerfCpu::fsr_record,1000);
    counters.record(1,3,d2vr::RenderPerfCpu::fsr_handoff,2000);
    counters.record(1,3,d2vr::RenderPerfCpu::fsr_dispatch,4000);
    {d2vr::RenderPerfCpu::Scope disabled(nullptr,1,3,0);disabled.stop();}
    std::ostringstream first;counters.drain(first);
    assert(first.str().find(",0,2,400,300\n")!=std::string::npos);
    assert(first.str().find(",1,1,500,500\n")!=std::string::npos);
    assert(first.str().find(",6,1,1000,1000\n")!=std::string::npos);
    assert(first.str().find(",7,1,2000,2000\n")!=std::string::npos);
    assert(first.str().find(",8,1,4000,4000\n")!=std::string::npos);
    std::ostringstream empty;counters.drain(empty);assert(empty.str().empty());
    std::atomic<bool> done{};
    std::ostringstream all;
    std::thread drainer([&]{while(!done.load()){counters.drain(all);std::this_thread::yield();}counters.drain(all);});
    std::vector<std::thread> workers;
    for(unsigned worker=0;worker<4;++worker)workers.emplace_back([&]{for(unsigned sample_index=0;sample_index<1000;++sample_index)counters.record(4,5,d2vr::RenderPerfCpu::hud_end,7);});
    for(auto& worker:workers)worker.join();
    done=true;drainer.join();counters.drain(all);
    std::uint64_t count{},ns{};std::string line;std::istringstream rows(all.str());
    while(std::getline(rows,line)){
        std::replace(line.begin(),line.end(),',',' ');std::istringstream row(line);
        d2vr::RenderPerfCpu::Row row_record;row>>row_record.epoch>>row_record.frame>>row_record.thread>>row_record.category>>row_record.count>>row_record.ns>>row_record.maximum;
        assert(row_record.epoch==4 && row_record.frame==5 && row_record.category==d2vr::RenderPerfCpu::hud_end && row_record.maximum==7);
        count+=row_record.count;ns+=row_record.ns;
    }
    assert(count==4000 && ns==28000 && counters.dropped()==0);
}
