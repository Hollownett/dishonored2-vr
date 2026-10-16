#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/render_perf_gpu.hpp"
#include <cstdio>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>

template<class InterfaceType> using Ptr=Microsoft::WRL::ComPtr<InterfaceType>;
static void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
static void retire(d2vr::RenderPerfGpu& gpu,ID3D11DeviceContext* immediate_context,std::ostringstream& csv,unsigned frame) {
    immediate_context->Flush();
    const auto deadline=GetTickCount64()+5000;
    const auto match=","+std::to_string(frame)+",5,-1,0,0,";
    while(GetTickCount64()<deadline) {
        gpu.poll(immediate_context);gpu.drain(csv);if(csv.str().find(match)!=std::string::npos) return;Sleep(1);
    }
    check(false,"frame queries did not complete");
}
static void ownership_roles(d2vr::RenderPerfGpu& profiler,ID3D11Device* device,ID3D11DeviceContext* immediate_context,ID3D11DeviceContext* deferred) {
    check(profiler.prepare(device),"prepare callback-role regression resources");
    check(!profiler.begin_frame(immediate_context,300000,9),"GPU frame requires admitted operation");
    std::uint64_t active_present=111,active_epoch=222;
    check(!profiler.active_frame(immediate_context,active_present,active_epoch) && active_present==111 && active_epoch==222,"rejected identity read does not publish requested identity");
    check(profiler.begin_operation(immediate_context),"first callback role admitted");
    check(profiler.begin_operation(immediate_context),"same-thread operation nesting admitted");
    check(profiler.begin_frame(immediate_context,300000,9),"admitted owner opens frame");
    check(profiler.active_frame(immediate_context,active_present,active_epoch) && active_present==300000 && active_epoch==9,"active identity comes from accepted frame");
    profiler.end_operation(immediate_context);
    bool concurrent_accepted=true;std::thread concurrent_role([&] {concurrent_accepted=profiler.begin_operation(immediate_context);});concurrent_role.join();
    check(!concurrent_accepted,"concurrent callback role rejected without engine serialization");
    check(profiler.stats().loss_reasons[static_cast<unsigned>(d2vr::RenderPerfGpu::LossReason::operation_owner_conflict)]==1,"concurrent operation loss recorded separately");
    check(!profiler.begin_operation(deferred),"deferred context cannot claim immediate callback role");
    check(!profiler.begin_span(deferred,12),"publication category is immediate-only");
    profiler.end_operation(immediate_context);
    std::ostringstream role_csv;std::string role_failure;
    std::thread present_role([&] {
        try {
            check(profiler.begin_operation(immediate_context),"later thread role admitted after previous callback returns");
            std::uint64_t present_identity{},epoch_identity{};
            check(profiler.active_frame(immediate_context,present_identity,epoch_identity) && present_identity==300000 && epoch_identity==9,"frame envelope survives callback thread handoff");
            const auto native_span=profiler.begin_span(immediate_context,0);check(native_span!=0,"later role records native span");profiler.end_span(immediate_context,native_span);
            const auto publication_span=profiler.begin_span(immediate_context,12);check(publication_span!=0,"inclusive immediate publication category accepted");
            const auto bounds_span=profiler.begin_span(immediate_context,2);check(bounds_span!=0,"publication can contain separately labeled bounds");
            profiler.end_span(immediate_context,bounds_span);profiler.end_span(immediate_context,publication_span);
            profiler.end_frame(immediate_context);retire(profiler,immediate_context,role_csv,300000);profiler.end_operation(immediate_context);
        } catch(const std::exception& failure) {role_failure=failure.what();}
    });present_role.join();check(role_failure.empty(),role_failure.c_str());
    check(role_csv.str().find(",300000,12,-1,0,0,")!=std::string::npos,"publication span remains separately labeled");
    check(profiler.begin_operation(immediate_context),"next native callback role admitted");
    check(profiler.begin_frame(immediate_context,400000,9),"open frame before deliberately missed Present closure");
    profiler.end_operation(immediate_context);
    std::thread recovery_role([&] {
        try {
            check(profiler.begin_operation(immediate_context),"recovery callback role admitted");
            check(profiler.begin_frame(immediate_context,400001,9),"new frame retires stale envelope and advances");
            const auto recovered_span=profiler.begin_span(immediate_context,0);check(recovered_span!=0,"stale frame does not poison later native spans");
            profiler.end_span(immediate_context,recovered_span);profiler.end_frame(immediate_context);
            retire(profiler,immediate_context,role_csv,400001);profiler.end_operation(immediate_context);
        } catch(const std::exception& failure) {role_failure=failure.what();}
    });recovery_role.join();check(role_failure.empty(),role_failure.c_str());
    check(role_csv.str().find(",400000,5,-1,0,0,")==std::string::npos,"stale incomplete elapsed never publishes");
    check(role_csv.str().find(",400000,8,-1,")!=std::string::npos,"stale frame retirement remains explicit");
    check(profiler.begin_operation(immediate_context),"clear callback admitted");const auto failures_before_clear=profiler.stats().failures;
    profiler.clear();profiler.end_operation(immediate_context);
    check(profiler.stats().failures==failures_before_clear,"clear releases GPU resources while operation destructor can balance");
    check(!profiler.begin_operation(immediate_context),"cleared profiler has no prepared context authority");
    check(profiler.prepare(device) && profiler.begin_operation(immediate_context),"fresh prepare after balanced cleared operation");
    profiler.set_recording_frame(600000,9);
    for(unsigned category=1;category<=2;++category) {
        for(unsigned sample_index=0;sample_index<32;++sample_index) {
            const auto sample_ticket=profiler.begin_span(deferred,category);check(sample_ticket!=0,"quota32 admits a bounded complete category burst");profiler.end_span(deferred,sample_ticket);
        }
        check(!profiler.begin_span(deferred,category),"33rd category callback is explicitly truncated");
    }
    profiler.set_recording_frame(600001,9);profiler.drain(role_csv);
    check(role_csv.str().find("9,600000,14,-1,33,1,32,")!=std::string::npos,"capture admission reports eligible versus admitted");
    check(role_csv.str().find("9,600000,14,-1,33,2,32,")!=std::string::npos,"bounds admission reports eligible versus admitted");
    profiler.set_recording_frame(600120,9);
    for(unsigned category=1;category<=2;++category) for(unsigned sample_index=0;sample_index<13;++sample_index) {
        const auto sample_ticket=profiler.begin_span(deferred,category);check(sample_ticket!=0,"13-slot category sample fits within bounded quota");profiler.end_span(deferred,sample_ticket);
    }
    profiler.set_recording_frame(600121,9);profiler.drain(role_csv);
    check(role_csv.str().find("9,600120,14,-1,13,1,13,")!=std::string::npos,"admission equality remains recording-frame metadata");
    check(role_csv.str().find(",11,-1,32,1,1,")!=std::string::npos,"global sampling policy remains partial even at equal admission");
    profiler.clear();profiler.end_operation(immediate_context);
}
int main() {
    try {
        Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> immediate_context,deferred;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
            &device,nullptr,&immediate_context)),"create isolated WARP device");
        check(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"create deferred recorder");
        auto gpu=std::make_unique<d2vr::RenderPerfGpu>();
        check(!gpu->begin_span(deferred.Get(),1),"startup recording ignored");
        check(gpu->prepare(device.Get()),"preallocate queries");
        check(gpu->begin_operation(immediate_context.Get()),"admit original fixture owner operation");
        check(gpu->stats().hud_per_category_quota==32,"bounded per-category sample quota");
        check(gpu->stats().hud_stride==120 && gpu->stats().hud_capacity==2048,"default HUD sampling budget");gpu->set_hud_stride(30);
        gpu->set_recording_frame(29);check(!gpu->begin_span(deferred.Get(),1),"unsampled HUD recording ignored");
        gpu->set_recording_frame(30);
        const auto hud=gpu->begin_span(deferred.Get(),1,0,999);check(hud!=0,"HUD records before first immediate frame");
        gpu->end_span(deferred.Get(),hud);Ptr<ID3D11CommandList> list;
        check(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)),"finish HUD list");gpu->finished(deferred.Get(),list.Get());
        gpu->begin_frame(immediate_context.Get(),30,7);gpu->begin_frame(immediate_context.Get(),30,7);
        const auto native=gpu->begin_span(immediate_context.Get(),0);check(native!=0,"native list span");
        gpu->executing(immediate_context.Get(),list.Get());immediate_context->ExecuteCommandList(list.Get(),FALSE);gpu->end_span(immediate_context.Get(),native);
        gpu->mark(immediate_context.Get(),3,0,123,1);gpu->end_frame(immediate_context.Get());
        std::ostringstream csv;retire(*gpu,immediate_context.Get(),csv,30);
        check(csv.str().find(",30,1,0,"+std::to_string(hud)+",0,")!=std::string::npos,"HUD output uses unique ticket");
        gpu->begin_frame(immediate_context.Get(),31,7);gpu->executing(immediate_context.Get(),list.Get());immediate_context->ExecuteCommandList(list.Get(),FALSE);gpu->end_frame(immediate_context.Get());
        retire(*gpu,immediate_context.Get(),csv,31);
        check(csv.str().find(",30,6,0,"+std::to_string(hud)+",1,")!=std::string::npos,"late replay explicitly invalidates published HUD row");
        gpu->set_recording_frame(60);const auto replay=gpu->begin_span(deferred.Get(),2,1);gpu->end_span(deferred.Get(),replay);
        Ptr<ID3D11CommandList> replay_list;check(SUCCEEDED(deferred->FinishCommandList(FALSE,&replay_list)),"finish replay list");
        gpu->finished(deferred.Get(),replay_list.Get());gpu->begin_frame(immediate_context.Get(),60,7);
        for(unsigned sample_index=0;sample_index<2;++sample_index) {gpu->executing(immediate_context.Get(),replay_list.Get());immediate_context->ExecuteCommandList(replay_list.Get(),FALSE);}
        gpu->end_frame(immediate_context.Get());retire(*gpu,immediate_context.Get(),csv,60);
        check(csv.str().find(",60,2,1,"+std::to_string(replay)+",0,")==std::string::npos,"pre-poll replay cannot publish HUD span");
        gpu->begin_frame(immediate_context.Get(),61,7);
        for(unsigned sample_index=0;sample_index<128;++sample_index) {const auto span=gpu->begin_span(immediate_context.Get(),0);check(span!=0,"native span capacity");gpu->end_span(immediate_context.Get(),span);}
        check(!gpu->begin_span(immediate_context.Get(),0),"exhausted native diagnostics drop without fallback");gpu->end_frame(immediate_context.Get());retire(*gpu,immediate_context.Get(),csv,61);
        gpu->begin_frame(immediate_context.Get(),62,7);gpu->mark(immediate_context.Get(),3,0,555,1);gpu->abort_frame(immediate_context.Get());gpu->abort_frame(immediate_context.Get());
        gpu->begin_frame(immediate_context.Get(),63,7);gpu->end_frame(immediate_context.Get());retire(*gpu,immediate_context.Get(),csv,63);
        check(csv.str().find(",62,5,-1,0,0,")==std::string::npos,"aborted frame never publishes");
        gpu->begin_frame(immediate_context.Get(),64,7);const auto owned=gpu->begin_span(immediate_context.Get(),0);
        check(owned!=0,"owner frame remains available for thread-rejection checks");
        const auto failures_before=gpu->stats().failures;
        bool foreign_accepted=true;std::thread foreign([&] {
            foreign_accepted=gpu->prepare(device.Get());gpu->end_frame(immediate_context.Get());gpu->poll(immediate_context.Get());
        });foreign.join();
        check(!foreign_accepted,"prepare rejects another owner thread");
        check(gpu->stats().failures==failures_before+3,"foreign end and poll reject without consuming the owner frame");
        gpu->end_span(immediate_context.Get(),owned);gpu->end_frame(immediate_context.Get());retire(*gpu,immediate_context.Get(),csv,64);
        const auto drops_before_ring=gpu->stats().dropped;
        for(unsigned frame=100;frame<108;++frame) {
            gpu->begin_frame(immediate_context.Get(),frame,7);const auto span=gpu->begin_span(immediate_context.Get(),0);
            check(span!=0,"all eight pending frame slots are usable without polling");gpu->end_span(immediate_context.Get(),span);gpu->end_frame(immediate_context.Get());
        }
        gpu->begin_frame(immediate_context.Get(),108,7);check(!gpu->begin_span(immediate_context.Get(),0),"ninth unretired frame cannot overwrite the ring");gpu->end_frame(immediate_context.Get());
        check(gpu->stats().dropped>=drops_before_ring+2,"full ring reports dropped frame and span diagnostics");
        retire(*gpu,immediate_context.Get(),csv,107);
        for(unsigned frame=100;frame<108;++frame)
            check(csv.str().find(","+std::to_string(frame)+",5,-1,0,0,")!=std::string::npos,"all eight pending frames retire intact");
        check(csv.str().find(",108,5,-1,0,0,")==std::string::npos,"ring overflow frame never publishes");
        gpu->begin_frame(immediate_context.Get(),109,7);const auto recovered=gpu->begin_span(immediate_context.Get(),0);check(recovered!=0,"ring recovers after retirement");
        gpu->end_span(immediate_context.Get(),recovered);gpu->end_frame(immediate_context.Get());retire(*gpu,immediate_context.Get(),csv,109);
        gpu->observe(7,61,3,0,123,101,.25);gpu->drain(csv);
        check(csv.str().find("7,61,3,0,123,101,0.25,")!=std::string::npos,"scalar sample retains explicit frame identity");
        gpu->clear();check(gpu->prepare(device.Get()),"fresh query objects after clear");
        gpu->set_recording_frame(90);const auto fresh=gpu->begin_span(deferred.Get(),1);check(fresh>replay,"tickets survive reprepare");
        gpu->end_span(deferred.Get(),fresh);
        gpu->set_hud_stride(120);const auto used_before_unsampled=gpu->stats().hud_used;
        gpu->set_recording_frame(119);check(!gpu->begin_span(deferred.Get(),1),"default stride excludes frame 119");
        check(gpu->stats().hud_used==used_before_unsampled,"unsampled recording consumes no query pair");
        gpu->set_recording_frame(120);
        const auto capacity=gpu->stats().hud_capacity;
        const auto capture_quota_index=static_cast<unsigned>(d2vr::RenderPerfGpu::LossReason::hud_capture_quota);
        const auto bounds_quota_index=static_cast<unsigned>(d2vr::RenderPerfGpu::LossReason::hud_bounds_quota);
        const auto capacity_reason_index=static_cast<unsigned>(d2vr::RenderPerfGpu::LossReason::hud_capacity);
        const auto before_category_quota=gpu->stats();
        const auto quota_capture=gpu->begin_span(deferred.Get(),1);check(quota_capture!=0,"sampled frame admits first capture pair");
        gpu->end_span(deferred.Get(),quota_capture);
        for(unsigned category_sample=1;category_sample<32;++category_sample) {
            const auto capture_sample=gpu->begin_span(deferred.Get(),1);check(capture_sample!=0,"capture quota admits every bounded sample");gpu->end_span(deferred.Get(),capture_sample);
        }
        check(!gpu->begin_span(deferred.Get(),1),"same-frame repeated capture is an intentional quota skip");
        const auto quota_bounds=gpu->begin_span(deferred.Get(),2);check(quota_bounds!=0,"bounds admission has an independent category quota");
        gpu->end_span(deferred.Get(),quota_bounds);
        for(unsigned category_sample=1;category_sample<32;++category_sample) {
            const auto bounds_sample=gpu->begin_span(deferred.Get(),2);check(bounds_sample!=0,"bounds quota admits every bounded sample");gpu->end_span(deferred.Get(),bounds_sample);
        }
        check(!gpu->begin_span(deferred.Get(),2),"same-frame repeated bounds is an intentional quota skip");
        const auto after_category_quota=gpu->stats();
        check(after_category_quota.hud_used==before_category_quota.hud_used+64,"category quotas admit exactly 64 pairs");
        check(after_category_quota.dropped==before_category_quota.dropped && after_category_quota.failures==before_category_quota.failures,"intentional category skips do not change loss counters");
        check(after_category_quota.loss_reasons[capture_quota_index]==before_category_quota.loss_reasons[capture_quota_index]+1 &&
              after_category_quota.loss_reasons[bounds_quota_index]==before_category_quota.loss_reasons[bounds_quota_index]+1,"quota skips remain separate by category");
        std::uint64_t sampled_record_frame=120;unsigned admitted_pairs=after_category_quota.hud_used;
        while(admitted_pairs<capacity) {
            sampled_record_frame+=120;gpu->set_recording_frame(sampled_record_frame);
            for(unsigned category=1;category<=2 && admitted_pairs<capacity;++category) {
                for(unsigned category_sample=0;category_sample<32 && admitted_pairs<capacity;++category_sample) {
                    const auto span=gpu->begin_span(deferred.Get(),category);check(span!=0,"all bounded HUD pairs are usable once across sampled frames");
                    gpu->end_span(deferred.Get(),span);++admitted_pairs;
                }
            }
        }
        check(gpu->stats().hud_used==2048 && gpu->stats().sampled_record_frames==33,"coverage records all quota-limited sampled frames and full pair budget");
        const auto before_full_pool_quota=gpu->stats();
        check(!gpu->begin_span(deferred.Get(),1),"final admitted category still reports quota skip at full pool");
        check(gpu->stats().loss_reasons[capture_quota_index]==before_full_pool_quota.loss_reasons[capture_quota_index]+1 &&
              gpu->stats().loss_reasons[capacity_reason_index]==before_full_pool_quota.loss_reasons[capacity_reason_index] &&
              gpu->stats().dropped==before_full_pool_quota.dropped,"full-pool intentional skip is distinct from a new capacity loss");
        const auto drops_before_hud=gpu->stats().dropped;
        check(!gpu->begin_span(deferred.Get(),2),"unadmitted category encounters the exhausted hard HUD pool");
        check(gpu->stats().loss_reasons[capacity_reason_index]==before_full_pool_quota.loss_reasons[capacity_reason_index]+1,"hard capacity has an explicit loss reason");
        Ptr<ID3D11CommandList> capacity_list;check(SUCCEEDED(deferred->FinishCommandList(FALSE,&capacity_list)),"finish capacity recording without GPU execution");
        gpu->finished(deferred.Get(),capacity_list.Get());capacity_list.Reset();
        gpu->set_recording_frame(sampled_record_frame+120);check(!gpu->begin_span(deferred.Get(),2),"released lists never recycle HUD query pairs");
        check(gpu->stats().dropped==drops_before_hud+2 && gpu->stats().hud_used==capacity,"HUD exhaustion stays bounded and observable");
        check(gpu->stats().loss_reasons[capacity_reason_index]==before_full_pool_quota.loss_reasons[capacity_reason_index]+2,"new sampled frame after list release still reports hard capacity loss");
        gpu->drain(csv);check(csv.str().find("0,33,7,-1,2048,120,2048,")!=std::string::npos,"coverage summary exposes used pairs, stride and sampled frames");
        gpu->clear();
        gpu->end_operation(immediate_context.Get());
        ownership_roles(*gpu,device.Get(),immediate_context.Get(),deferred.Get());
        std::puts("PASS: isolated WARP profiler bookkeeping; not native gameplay performance");return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
