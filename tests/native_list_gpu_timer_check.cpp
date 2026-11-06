#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_list_gpu_timer.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
static void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
static void retire(d2vr::NativeListGpuTimer& timer,ID3D11DeviceContext* context,std::uint64_t frames) {
    context->Flush(); // Test-only progress. Production timer never flushes or waits.
    const auto deadline=GetTickCount64()+3000;
    while(timer.stats().frames+timer.stats().disjoint<frames && GetTickCount64()<deadline) {timer.poll(context);Sleep(1);}
    check(timer.stats().frames==frames,"all expected frames completed without disjoint clocks");
}
int main(int argc,char** argv) {
    try {
        const bool hardware=argc>1 && std::strcmp(argv[1],"--hardware")==0;
        Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context,deferred;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,
            nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"create test GPU device");
        check(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"create deferred context");
        d2vr::NativeListGpuTimer timer;
        check(!timer.prepare(nullptr) && !timer.begin_list(context.Get()),"reject absent device");
        check(timer.prepare(device.Get()),"preallocate bounded query ring");
        check(!timer.begin_list(deferred.Get()),"reject unexecuted deferred context");
        Ptr<ID3D11Device> foreign;Ptr<ID3D11DeviceContext> foreign_context;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&foreign,nullptr,&foreign_context)),"create foreign owner");
        check(!timer.begin_list(foreign_context.Get()),"reject foreign immediate context");

        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=1024;desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        Ptr<ID3D11Texture2D> texture;Ptr<ID3D11RenderTargetView> target;
        check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&texture)) &&
              SUCCEEDED(device->CreateRenderTargetView(texture.Get(),nullptr,&target)),"create native-list target");
        const float colour[4]{.25f,.5f,.75f,1};
        deferred->ClearRenderTargetView(target.Get(),colour);
        Ptr<ID3D11CommandList> list;
        check(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)),"finish native list");
        const auto first=timer.begin_list(context.Get());check(first!=0,"begin first native list");
        check(!timer.begin_list(context.Get()),"reject overlapping list timestamp");
        context->ExecuteCommandList(list.Get(),TRUE);timer.end_list(context.Get(),first);
        for(unsigned i=0;i<16;++i) context->ClearRenderTargetView(target.Get(),colour); // FSR-like work outside native intervals
        const auto second=timer.begin_list(context.Get());check(second!=0 && second!=first,"distinct list ticket");
        timer.end_list(context.Get(),first); // stale ticket must not end the current list
        context->ExecuteCommandList(list.Get(),TRUE);timer.end_list(context.Get(),second);
        timer.finish_frame(context.Get());
        check(timer.stats().frames==0,"encoding alone does not publish GPU results");
        retire(timer,context.Get(),1);
        const auto measured=timer.stats();
        check(measured.last_lists==2 && measured.lists==2 && measured.last_dropped_lists==1,"multilist coverage and overlap reported");
        check(std::isfinite(measured.last_list_sum_ms) && measured.last_list_sum_ms>=0 &&
              measured.last_elapsed_ms>=measured.last_list_sum_ms,"separate native sum and broader elapsed span");
        check(timer.prepare(device.Get()) && timer.stats().frames==1,"same device retains history");

        for(unsigned i=0;i<d2vr::NativeListGpuTimer::max_lists+2;++i) {
            const auto ticket=timer.begin_list(context.Get());
            check((ticket!=0)==(i<d2vr::NativeListGpuTimer::max_lists),"bounded per-frame list capacity");
            context->ExecuteCommandList(list.Get(),FALSE);timer.end_list(context.Get(),ticket);
        }
        timer.finish_frame(context.Get());retire(timer,context.Get(),2);
        check(timer.stats().last_lists==d2vr::NativeListGpuTimer::max_lists && timer.stats().last_dropped_lists==2 &&
              timer.stats().lists==d2vr::NativeListGpuTimer::max_lists+2,
              "overflow remains a clearly partial sample");
        const auto unfinished=timer.begin_list(context.Get());check(unfinished!=0,"begin incomplete frame");
        timer.finish_frame(context.Get());timer.end_list(context.Get(),unfinished);
        check(timer.stats().dropped_frames==1,"unfinished interval drops whole frame");
        context->Flush();timer.poll(context.Get());

        timer.clear();check(timer.stats().frames==0 && !timer.begin_list(context.Get()),"release all query/device ownership");
        check(timer.prepare(device.Get()),"reprepare after cleanup");
        const auto fresh=timer.begin_list(context.Get());check(fresh && fresh!=unfinished,"stale identity cannot alias after cleanup");
        timer.end_list(context.Get(),unfinished);context->ExecuteCommandList(list.Get(),TRUE);timer.end_list(context.Get(),fresh);
        timer.finish_frame(context.Get());retire(timer,context.Get(),1);
        check(timer.stats().rejected_calls==1 && timer.stats().last_lists==1,"stale ticket rejected while new list completes");

        // More than one ring rotation; completed slots must safely be reusable.
        for(unsigned i=0;i<20;++i) {
            const auto ticket=timer.begin_list(context.Get());
            context->ExecuteCommandList(list.Get(),TRUE);timer.end_list(context.Get(),ticket);timer.finish_frame(context.Get());
            context->Flush();timer.poll(context.Get()); // test-only submission
        }
        const auto deadline=GetTickCount64()+3000;
        while(timer.stats().frames+timer.stats().dropped_frames<21 && GetTickCount64()<deadline) {timer.poll(context.Get());Sleep(1);}
        check(timer.stats().frames+timer.stats().dropped_frames==21,"bounded ring retires or explicitly drops every frame");
        check(timer.stats().query_failures==0 && timer.stats().disjoint==0,"no GPU query errors");
        check(timer.begin_list(context.Get())!=0,"open a bracket before teardown");
        timer.clear();check(timer.prepare(device.Get()),"active bracket cleanup permits safe reprepare");
        const auto teardown=timer.begin_list(context.Get());context->ExecuteCommandList(list.Get(),TRUE);
        timer.end_list(context.Get(),teardown);timer.finish_frame(context.Get());retire(timer,context.Get(),1);
        std::printf("PASS (%s): native sum %.6f ms, elapsed %.6f ms; multilist, capacity, owner, stale ticket, async, cleanup, phase and pass tags\n",
            hardware?"hardware":"WARP",measured.last_list_sum_ms,measured.last_elapsed_ms);
        { // phase tags (task 3b): lists executed in different phases are summed and counted per phase
            timer.clear();check(timer.prepare(device.Get()),"reprepare for phase tags");
            const d2vr::Phase tags[]{d2vr::Phase::pre,d2vr::Phase::pre,d2vr::Phase::eye0,d2vr::Phase::eye1,d2vr::Phase::eye1,d2vr::Phase::eye1};
            for(auto tag:tags) {const auto t=timer.begin_list(context.Get(),tag);check(t!=0,"phase list begins");context->ExecuteCommandList(list.Get(),TRUE);timer.end_list(context.Get(),t);}
            timer.finish_frame(context.Get());retire(timer,context.Get(),1);
            const auto ph=timer.stats().last_phase;
            check(ph.valid && ph.lists[0]==2 && ph.lists[1]==1 && ph.lists[2]==3 && ph.lists[3]==0,"per-phase list counts");
            double total=0;for(double v:ph.ms) {check(v>=0,"phase ms non-negative");total+=v;}
            check(std::fabs(total-timer.stats().last_list_sum_ms)<1e-6,"phase sums add up to the list sum");
        }
        { // v999 pass tags: the same executed lists are summed and counted per (pass, phase)
            timer.clear();check(timer.prepare(device.Get()),"reprepare for pass tags");
            struct Tag {d2vr::Phase phase;std::uint8_t pass;};
            const Tag tags[]{{d2vr::Phase::eye0,3},{d2vr::Phase::eye0,3},{d2vr::Phase::eye1,4},{d2vr::Phase::eye1,3},{d2vr::Phase::eye1,7}};
            for(const auto& tag:tags) {
                const auto t=timer.begin_list(context.Get(),tag.phase,tag.pass);
                check(t!=0,"pass-tagged list begins");
                context->ExecuteCommandList(list.Get(),TRUE);timer.end_list(context.Get(),t);
            }
            timer.finish_frame(context.Get());retire(timer,context.Get(),1);
            const auto pf=timer.stats().last_pass;
            check(pf.valid,"pass frame valid");
            check(pf.lists[3][1]==2 && pf.lists[3][2]==1 && pf.lists[4][2]==1 && pf.lists[7][2]==1,"per-pass/per-phase list counts");
            check(pf.lists[d2vr::NativeListGpuTimer::untagged_pass][0]==0 && pf.lists[d2vr::NativeListGpuTimer::untagged_pass][1]==0,"no untagged samples");
            double pass_total=0;
            for(unsigned p=0;p<d2vr::NativeListGpuTimer::max_pass;++p) for(unsigned phase=0;phase<d2vr::phase_count;++phase) {
                check(pf.ms[p][phase]>=0,"pass ms non-negative");
                pass_total+=pf.ms[p][phase];
            }
            check(std::fabs(pass_total-timer.stats().last_list_sum_ms)<1e-6,"pass sums add up to the list sum");
        }
        timer.clear();return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
