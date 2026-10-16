#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/render_perf_gpu.hpp"
#include <cstdio>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>

template<class ValueType> using Ptr=Microsoft::WRL::ComPtr<ValueType>;
static void check(bool accepted,const char* description) {if(!accepted) throw std::runtime_error(description);}
static std::uint64_t reason_count(d2vr::RenderPerfGpu& profiler,d2vr::RenderPerfGpu::LossReason reason) {
    return profiler.stats().loss_reasons[static_cast<unsigned>(reason)];
}
int main() {
    try {
        Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> immediate,deferred;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
            &device,nullptr,&immediate)),"create standalone WARP device");
        check(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"create standalone deferred context");
        auto profiler=std::make_unique<d2vr::RenderPerfGpu>();check(profiler->prepare(device.Get()),"prepare query inventory");
        check(profiler->begin_operation(immediate.Get()),"admit loss fixture owner operation");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::query_create_failed)==0,"successful creation has no CreateQuery failure");
        profiler->set_recording_frame(120);
        const auto capture_ticket=profiler->begin_span(deferred.Get(),1,0,100);
        const auto bounds_ticket=profiler->begin_span(deferred.Get(),2,0,101);
        check(capture_ticket && bounds_ticket,"first category samples admitted");
        profiler->end_span(deferred.Get(),bounds_ticket);profiler->end_span(deferred.Get(),capture_ticket);
        for(unsigned category_sample=1;category_sample<32;++category_sample) {
            for(unsigned category=1;category<=2;++category) {
                const auto sample_ticket=profiler->begin_span(deferred.Get(),category);check(sample_ticket!=0,"each category admits 32 bounded samples");profiler->end_span(deferred.Get(),sample_ticket);
            }
        }
        const auto drop_count_before_quota=profiler->stats().dropped;
        for(unsigned child_index=0;child_index<300;++child_index) {
            check(!profiler->begin_span(deferred.Get(),1,1,child_index),"capture quota rejects repeated child before query allocation");
            check(!profiler->begin_span(deferred.Get(),2,1,child_index),"bounds quota rejects repeated child before query allocation");
        }
        check(profiler->stats().hud_used==64,"hundreds of children consume only 64 bounded query pairs");
        check(profiler->stats().sampled_record_frames==1,"one recording frame counted once");
        check(profiler->stats().dropped==drop_count_before_quota,"intentional quota skips do not increment legacy drop count");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::hud_capture_quota)==300,"capture quota count exact");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::hud_bounds_quota)==300,"bounds quota count exact");
        profiler->set_recording_frame(121);check(!profiler->begin_span(deferred.Get(),1),"stride still rejects unsampled recording frames");
        check(profiler->stats().hud_used==64,"unsampled frame consumes no pair");
        for(unsigned recording_index=2;recording_index<=32;++recording_index) {
            profiler->set_recording_frame(static_cast<std::uint64_t>(recording_index)*120);
            for(unsigned category=1;category<=2;++category) {
                for(unsigned category_sample=0;category_sample<32;++category_sample) {
                    const auto sample_ticket=profiler->begin_span(deferred.Get(),category);
                    check(sample_ticket!=0,"quota resets for next sampled frame and each category");profiler->end_span(deferred.Get(),sample_ticket);
                }
            }
        }
        check(profiler->stats().hud_used==2048,"query pool remains finite and never recycled");
        check(profiler->stats().sampled_record_frames==32,"two bounded categories per sampled frame budget");
        check(!profiler->begin_span(deferred.Get(),1),"already admitted category remains a quota skip when final pair fills the pool");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::hud_capture_quota)==301,"full pool does not change intentional quota classification");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::hud_capacity)==0,"same-frame intentional skip is not a pool drop");
        profiler->set_recording_frame(33ull*120);check(!profiler->begin_span(deferred.Get(),1),"hard pool exhaustion remains explicit");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::hud_capacity)==1,"capacity distinguished from intentional quota");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::query_create_failed)==0,"pool exhaustion does not claim CreateQuery failure");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::query_read_failed)==0,"no read failure invented before polling");
        profiler->begin_frame(immediate.Get(),200000,7);const auto native_ticket=profiler->begin_span(immediate.Get(),0);
        check(native_ticket!=0,"owner can open native span");
        check(profiler->begin_frame(immediate.Get(),200001,7),"stale unclosed frame retires and next frame begins");
        const auto replacement_native_ticket=profiler->begin_span(immediate.Get(),0);check(replacement_native_ticket!=0,"replacement frame has independent native span");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::active_frame_conflict)==1,"unclosed frame conflict has its own reason");
        bool foreign_prepare_accepted=true;
        std::thread rejected_owner([&] {
            foreign_prepare_accepted=profiler->prepare(device.Get());
            profiler->end_frame(immediate.Get());profiler->poll(immediate.Get());profiler->end_span(immediate.Get(),replacement_native_ticket);
        });rejected_owner.join();
        check(!foreign_prepare_accepted,"foreign prepare rejected");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::prepare_owner)==1,"prepare thread loss distinguished");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::end_owner)==1,"end thread loss distinguished");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::poll_owner)==1,"poll thread loss distinguished");
        check(reason_count(*profiler,d2vr::RenderPerfGpu::LossReason::span_end_owner)==1,"span end thread loss distinguished");
        const auto health=profiler->stats();
        check(health.owner_thread!=health.last_rejected_thread,"thread identities preserve ownership mismatch");
        check(health.owner_context_identity==health.last_rejected_context,"same context rejected on another thread is explicit");
        profiler->end_span(immediate.Get(),replacement_native_ticket);profiler->abort_frame(immediate.Get());
        std::ostringstream csv;profiler->drain(csv);
        check(csv.str().find(",9,-1,15,0,1,")!=std::string::npos,"capacity reason reaches worker CSV");
        check(csv.str().find(",11,-1,32,1,1,")!=std::string::npos,"partial capture quota policy reaches CSV");
        profiler->clear();
        profiler->end_operation(immediate.Get());
        std::puts("PASS: standalone diagnostic loss and admission checks; not native game timing");return 0;
    } catch(const std::exception& failure) {std::fprintf(stderr,"FAIL: %s\n",failure.what());return 1;}
}
