#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/command_list_pass_map.hpp"
#include "../src/render_pass_table.hpp"
#include "../src/gpu_pass_window.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

static void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
static bool close(double a,double b) {return std::fabs(a-b)<1e-4;}
int main() {
    try {
        // Static pass table: names, callback RVAs, and the two callbacks the existing hooks already use.
        check(d2vr::render_pass_count==52,"52 ARK_RENDERPASS_* registrations");
        unsigned duplicates=0;
        for(unsigned i=0;i<d2vr::render_pass_count;++i) {
            const std::string name=d2vr::render_passes[i].name;
            check(name.rfind("ARK_RENDERPASS_",0)==0,"pass name prefix");
            check(d2vr::render_passes[i].callback_rva!=0,"pass callback present");
            for(unsigned j=0;j<i;++j) if(d2vr::render_passes[j].callback_rva==d2vr::render_passes[i].callback_rva) ++duplicates;
        }
        check(duplicates==1,"only ASYNC_KICKOFF/WAIT_FOR_ASYNC share a callback");
        check(d2vr::render_pass_for_callback_rva(0x327270)==42,"POSTPROC callback maps to id 42");
        check(d2vr::render_pass_for_callback_rva(0x323E70)==43,"POST_TONEMAP callback maps to id 43");
        check(d2vr::render_pass_for_callback_rva(0x123456)==-1,"unknown callback is untagged");
        check(std::string(d2vr::render_pass_name(d2vr::render_pass_untagged))=="untagged","untagged name");

        // List -> pass map: finish on one thread, execute on another; reuse; never executed; untagged.
        d2vr::CommandListPassMap lists;
        int first=0,second=0,third=0,fourth=0,fifth=0;
        check(lists.record(&first,7),"record first list");
        check(lists.find(&first)==7,"same-thread lookup");
        std::thread worker([&] {
            check(lists.record(&second,8),"record second list on worker");
            check(lists.record(&third,9),"record third list on worker");
        });
        worker.join();
        check(lists.find(&second)==8 && lists.find(&third)==9,"lookup after finish thread ended");
        check(lists.find(&fourth)==-1,"never recorded list is untagged");
        check(lists.record(&first,11) && lists.find(&first)==11,"reused list takes the new pass");
        check(lists.record(&first,3) && lists.record(&second,3),"one pass recorded across two lists");
        check(lists.find(&first)==3 && lists.find(&second)==3,"both lists of a pass resolve");
        check(lists.record(&fifth,4) && lists.find(&fifth)==4,"recorded but never executed still resolves");
        check(!lists.record(nullptr,1) && lists.find(nullptr)==-1,"null list rejected");
        check(!lists.record(&first,-1) && lists.find(&first)==3,"negative pass rejected without clobbering");

        // Bounded table under many unique pointers: never blocks, never grows, evictions are counted.
        {
            lists.clear();
            std::vector<int> many(d2vr::CommandListPassMap::capacity+256);
            for(std::size_t i=0;i<many.size();++i) lists.record(&many[i],static_cast<int>(i%d2vr::render_pass_count));
            const auto stats=lists.stats();
            check(stats.recorded+stats.evicted==many.size(),"every unique list is recorded or explicitly evicted");
            check(stats.evicted>0,"capacity pressure is reported");
            lists.clear();
            check(lists.find(&many.back())==-1 && lists.stats().recorded==0,"clear releases every slot");
        }
        // Concurrent writers on distinct pointers.
        {
            std::vector<int> many(8000);
            std::vector<std::thread> writers;
            for(unsigned t=0;t<4;++t) writers.emplace_back([&,t] {
                for(unsigned i=0;i<2000;++i) lists.record(&many[t*2000+i],static_cast<int>((t*2000+i)%d2vr::render_pass_count));
            });
            for(auto& writer_thread:writers) writer_thread.join();
            const auto stats=lists.stats();
            check(stats.recorded+stats.evicted==8000,"concurrent records are all accounted for");
            int found=0;
            for(auto& value:many) if(lists.find(&value)>=0) ++found;
            check(found>0,"concurrent records remain readable");
            lists.clear();
        }

        // Per-pass 900-frame window and nearest-rank percentiles (zeros included).
        auto window=std::make_unique<d2vr::GpuPassWindow>();
        const unsigned decals=3,gather_umbra=5,cull_ocean=4;
        for(unsigned frame=0;frame<100;++frame) {
            d2vr::NativeListGpuTimer::PassFrame pass{};
            pass.valid=true;
            pass.ms[decals][static_cast<unsigned>(d2vr::Phase::eye0)]=1.0+frame;
            pass.ms[decals][static_cast<unsigned>(d2vr::Phase::eye1)]=2.0+frame;
            pass.lists[decals][static_cast<unsigned>(d2vr::Phase::eye0)]=1;
            pass.lists[decals][static_cast<unsigned>(d2vr::Phase::eye1)]=1;
            pass.ms[gather_umbra][static_cast<unsigned>(d2vr::Phase::eye0)]=0.5;
            pass.lists[gather_umbra][static_cast<unsigned>(d2vr::Phase::eye0)]=1;
            pass.ms[cull_ocean][static_cast<unsigned>(d2vr::Phase::eye1)]=0.25;
            pass.lists[cull_ocean][static_cast<unsigned>(d2vr::Phase::eye1)]=1;
            pass.ms[d2vr::render_pass_untagged][static_cast<unsigned>(d2vr::Phase::eye0)]=0.1;
            pass.lists[d2vr::render_pass_untagged][static_cast<unsigned>(d2vr::Phase::eye0)]=1;
            d2vr::ModGpu mod{};mod.valid=true;
            mod.ms[static_cast<unsigned>(d2vr::ModSpan::fsr_eye0)]=1.5;
            mod.ms[static_cast<unsigned>(d2vr::ModSpan::aa_sharpen)]=0.2;
            check(window->feed(frame+1,pass,&mod),"new timer sequence enters the window");
        }
        check(window->frames()==100,"window holds 100 unique timer frames");
        d2vr::NativeListGpuTimer::PassFrame duplicate{};
        duplicate.valid=true;
        check(!window->feed(100,duplicate,nullptr),"duplicate sequence refused");
        check(window->frames()==100,"duplicate sequence does not add a frame");
        check(close(window->pass_ms_percentile(decals,0,.50),50.0),"eye0 p50");
        check(close(window->pass_ms_percentile(decals,0,.90),90.0),"eye0 p90");
        check(close(window->pass_ms_percentile(decals,1,.50),51.0),"eye1 p50");
        check(close(window->pass_ms_percentile(decals,1,.90),91.0),"eye1 p90");
        check(close(window->pass_total_ms(decals),102.0),"pass total is the per-frame mean");
        check(close(window->pass_lists_percentile(decals,.50),2.0),"lists_p50 across both eyes");
        check(close(window->mod_ms_percentile(static_cast<unsigned>(d2vr::ModSpan::fsr_eye0),.50),1.5),"mod FSR p50");
        check(close(window->mod_ms_percentile(static_cast<unsigned>(d2vr::ModSpan::aa_sharpen),.50),0.2),"mod sharpen p50");
        const std::string json=window->json(true);
        const auto decals_at=json.find("ARK_RENDERPASS_UPDATE_NPC_DECALS");
        const auto umbra_at=json.find("ARK_RENDERPASS_GATHER_UMBRA");
        const auto ocean_at=json.find("ARK_RENDERPASS_CULL_OCEAN");
        check(decals_at!=std::string::npos && umbra_at!=std::string::npos && ocean_at!=std::string::npos,"JSON names every seen pass");
        check(decals_at<umbra_at && umbra_at<ocean_at,"passes sorted by total cost");
        check(json.find("\"untagged\":{")!=std::string::npos && json.find("\"frames\":100")!=std::string::npos,"untagged and frame count in JSON");
        check(json.find("\"mod_gpu_ms_p50\":{")!=std::string::npos,"mod spans next to the pass table");
        check(!window->feed(50,duplicate,nullptr),"sequence regression clears the stale window");
        check(window->frames()==0,"timer epoch reset discards old samples");
        window->clear();
        check(window->frames()==0 && window->last_sequence()==0,"clear resets the window");
        const std::string empty=window->json(false);
        check(empty.find("\"frames\":0")!=std::string::npos && empty.find("\"enabled\":false")!=std::string::npos,"empty window JSON");
        std::printf("PASS: 52-pass table, thread-crossing list map (reuse, never executed, untagged, bounded, concurrent), "
            "per-pass 900-frame window percentiles and mod spans\n");
        return 0;
    } catch(const std::exception& error) {
        std::fprintf(stderr,"FAIL: %s\n",error.what());
        return 1;
    }
}
