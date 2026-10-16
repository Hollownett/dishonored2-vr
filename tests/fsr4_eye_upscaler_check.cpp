#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/fsr4_eye_upscaler.hpp"
#include <cstdio>
#include <vector>

template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
static void check(bool ok,const std::string& reason) {if(!ok) throw std::runtime_error(reason);}
using Fsr4=d2vr::Fsr4EyeUpscaler;
struct Ledger {
    static_assert(static_cast<unsigned>(Fsr4::Phase::inputs_begin)==0 && static_cast<unsigned>(Fsr4::Phase::inputs_ready)==1 &&
        static_cast<unsigned>(Fsr4::Phase::output_ready)==2 && static_cast<unsigned>(Fsr4::Phase::output_copied)==3 &&
        static_cast<unsigned>(Fsr4::Phase::cpu_flush)==4 && static_cast<unsigned>(Fsr4::Phase::ffx_gpu)==5 &&
        static_cast<unsigned>(Fsr4::Phase::ffx_dropped)==6 && static_cast<unsigned>(Fsr4::Phase::cpu_record)==7 &&
        static_cast<unsigned>(Fsr4::Phase::cpu_handoff)==8 && static_cast<unsigned>(Fsr4::Phase::cpu_dispatch)==9);
    struct Event {Fsr4::Id id{};Fsr4::Phase phase{};ID3D11DeviceContext* context{};double duration{};};
    std::array<Event,64> events{};std::size_t count{};bool overflow{};
    static void observe(void* user,const Fsr4::Id& id,Fsr4::Phase phase,ID3D11DeviceContext* context,double duration) noexcept {
        auto& ledger=*static_cast<Ledger*>(user);
        if(ledger.count==ledger.events.size()) {ledger.overflow=true;return;}
        ledger.events[ledger.count++]={id,phase,context,duration};
    }
    void expect(const Fsr4::Id& id,ID3D11DeviceContext* context,bool submitted,bool dropped) const {
        check(!overflow,"Diagnostic ledger capacity exceeded");
        std::array<std::size_t,10> phases{};
        for(std::size_t event_index=0;event_index<count;++event_index) {
            const auto& event=events[event_index];
            check(event.id.epoch==id.epoch && event.id.frame==id.frame && event.id.dispatch==id.dispatch && event.id.eye==id.eye,
                "Diagnostic callback must preserve the selected identity");
            check(event.context==context,"Diagnostic callback must preserve the context");
            check(std::isfinite(event.duration) && event.duration>=0,"Diagnostic duration must be finite and nonnegative");
            const auto phase=static_cast<std::size_t>(event.phase);
            check(phase<phases.size(),"Unexpected diagnostic phase");++phases[phase];
        }
        for(const auto phase:{Fsr4::Phase::inputs_begin,Fsr4::Phase::inputs_ready,Fsr4::Phase::output_ready,
                Fsr4::Phase::output_copied,Fsr4::Phase::cpu_flush,Fsr4::Phase::cpu_record,Fsr4::Phase::cpu_handoff})
            check(phases[static_cast<std::size_t>(phase)]==(submitted?1u:0u),"Submitted diagnostics require exactly four GPU markers and CPU Flush/record/handoff samples");
        check(phases[static_cast<std::size_t>(Fsr4::Phase::cpu_dispatch)]==0,"Direct FSR4 dispatch must not fabricate the enclosing FsrTaa CPU interval");
        check(phases[static_cast<std::size_t>(Fsr4::Phase::ffx_gpu)]==(dropped?0u:1u),"Diagnostic GPU result must occur exactly once unless explicitly dropped");
        check(phases[static_cast<std::size_t>(Fsr4::Phase::ffx_dropped)]==(dropped?1u:0u),"Diagnostic drop must occur exactly once without a GPU result");
    }
};
static Ptr<ID3D11Texture2D> texture(ID3D11Device* device,UINT w,UINT h,DXGI_FORMAT format,const void* data,UINT pitch) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.ArraySize=desc.MipLevels=1;
    desc.Format=format;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    D3D11_SUBRESOURCE_DATA initial{};initial.pSysMem=data;initial.SysMemPitch=pitch;
    Ptr<ID3D11Texture2D> result;
    check(SUCCEEDED(device->CreateTexture2D(&desc,data?&initial:nullptr,&result)),"Create test texture");return result;
}
static float half(unsigned value) {
    const unsigned exponent=(value>>10)&31,sign=value>>15,fraction=value&1023;
    if(exponent==31) return INFINITY;
    const float result=exponent?std::ldexp(1.f+static_cast<float>(fraction)/1024.f,static_cast<int>(exponent)-15):
        std::ldexp(static_cast<float>(fraction),-24);
    return sign?-result:result;
}
int wmain(int argc,wchar_t** argv) {
    try {
        const auto provider=std::filesystem::absolute(argc>1?std::filesystem::path(argv[1]):
            std::filesystem::path(L"third_party/fsr4/bin/amd_fidelityfx_upscaler_dx12.dll"));
        Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context,deferred;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
            &device,nullptr,&context)),"Create standalone hardware device");
        check(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"Create rejection context");
        Ledger completed_ledger,rejected_ledger,retired_ledger;
        d2vr::Fsr4EyeUpscaler eye;
        d2vr::Fsr4EyeUpscaler::Desc desc{};desc.render_w=512;desc.render_h=384;desc.display_w=768;desc.display_h=576;
        desc.auto_exposure=false;
        check(!eye.create(device.Get(),desc,L"relative.dll"),"Unsigned/relative provider path must be rejected");
        check(eye.create(device.Get(),desc,provider.c_str()),eye.last_error());
        check(eye.valid() && eye.version_id()!=0 && eye.version_name().find("4.")!=std::string::npos,"Created context must identify actual FSR4");
        check(eye.create(device.Get(),desc,provider.c_str()),"Repeated prepare preserves the existing FSR4 context");
        std::vector<unsigned short> colour(static_cast<std::size_t>(desc.render_w)*desc.render_h*4),mv(colour.size()/2,0);
        const unsigned short channels[]{0x3400,0x3800,0x3a00,0x3c00};
        for(std::size_t i=0;i<colour.size();++i) colour[i]=channels[i%4];
        std::vector<float> depth(static_cast<std::size_t>(desc.render_w)*desc.render_h,.5f);
        std::vector<unsigned char> reactive(depth.size(),0);
        auto c=texture(device.Get(),desc.render_w,desc.render_h,DXGI_FORMAT_R16G16B16A16_FLOAT,colour.data(),desc.render_w*8);
        std::vector<unsigned short> full_mv(static_cast<std::size_t>(desc.display_w)*desc.display_h*2,0);
        auto m=texture(device.Get(),desc.display_w,desc.display_h,DXGI_FORMAT_R16G16_FLOAT,full_mv.data(),desc.display_w*4);
        auto d=texture(device.Get(),desc.render_w,desc.render_h,DXGI_FORMAT_R32_FLOAT,depth.data(),desc.render_w*4);
        auto r=texture(device.Get(),desc.render_w,desc.render_h,DXGI_FORMAT_R8_UNORM,reactive.data(),desc.render_w);
        auto output=texture(device.Get(),desc.display_w,desc.display_h,DXGI_FORMAT_R16G16B16A16_FLOAT,nullptr,0);
        d2vr::Fsr4EyeUpscaler::Inputs in{};in.colour=c.Get();in.depth=d.Get();in.motion=m.Get();in.reactive=r.Get();in.output=output.Get();
        in.render_w=desc.render_w;in.render_h=desc.render_h;in.mv_scale_x=static_cast<float>(desc.render_w);
        in.mv_scale_y=static_cast<float>(desc.render_h);in.reset=true;
        const Fsr4::Diagnostics rejected_trace{{11,22,31,1},&Ledger::observe,&rejected_ledger};
        check(!eye.dispatch(deferred.Get(),in,true,&rejected_trace),"Deferred dispatch must be rejected");
        rejected_ledger.expect(rejected_trace.id,deferred.Get(),false,true);
        auto invalid=in;invalid.render_w=desc.render_w+1;check(!eye.dispatch(context.Get(),invalid),"Mismatched dimensions rejected");
        const Fsr4::Diagnostics completed_trace{{11,22,32,1},&Ledger::observe,&completed_ledger};
        check(eye.dispatch(context.Get(),in,true,&completed_trace),eye.last_error());
        check(eye.record_ms()>=0 && eye.handoff_ms()>=0,"FSR4 phase timings");
        for(std::size_t event_index=0;event_index<completed_ledger.count;++event_index) {
            const auto& event=completed_ledger.events[event_index];
            if(event.phase==Fsr4::Phase::cpu_record)check(event.duration==eye.record_ms(),"CPU record callback must report this dispatch's existing duration");
            if(event.phase==Fsr4::Phase::cpu_handoff)check(event.duration==eye.handoff_ms(),"CPU handoff callback must report this dispatch's existing duration");
        }
        context->Flush(); // Standalone validation only, sends final consumption signal.
        const auto deadline=GetTickCount64()+10000;
        while(eye.completed_gpu_value()<eye.submitted_gpu_value() && GetTickCount64()<deadline) Sleep(1);
        check(eye.completed_gpu_value()>=eye.submitted_gpu_value(),"FSR4 GPU completion timeout");
        D3D11_TEXTURE2D_DESC staging_desc{};output->GetDesc(&staging_desc);
        staging_desc.BindFlags=staging_desc.MiscFlags=0;staging_desc.Usage=D3D11_USAGE_STAGING;staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        Ptr<ID3D11Texture2D> staging;check(SUCCEEDED(device->CreateTexture2D(&staging_desc,nullptr,&staging)),"Create completed output readback");
        context->CopyResource(staging.Get(),output.Get());D3D11_MAPPED_SUBRESOURCE mapped{};
        check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Map standalone output");
        const auto sample=reinterpret_cast<const unsigned short*>(static_cast<const unsigned char*>(mapped.pData)+
            static_cast<std::size_t>(desc.display_h/2)*mapped.RowPitch)+desc.display_w/2*4;
        const std::array<float,3> measured{half(sample[0]),half(sample[1]),half(sample[2])};context->Unmap(staging.Get(),0);
        for(unsigned i=0;i<measured.size();++i) check(std::isfinite(measured[i]) && std::abs(measured[i]-(.25f+.25f*i))<.08f,"FSR4 output must preserve constant scene colour");
        eye.poll_gpu_timing();
        completed_ledger.expect(completed_trace.id,context.Get(),true,false);
        eye.poll_gpu_timing();
        completed_ledger.expect(completed_trace.id,context.Get(),true,false);
        std::fill(reactive.begin(),reactive.end(),static_cast<unsigned char>(255));
        context->UpdateSubresource(r.Get(),0,nullptr,reactive.data(),desc.render_w,0);
        for(unsigned iteration=0;iteration<6;++iteration) {
            // Reuse the completed slot with supplied/non-supplied mask transitions.
            // Cached constants must not retain the preceding nonzero reactive mask.
            in.reactive=iteration%3==0?r.Get():nullptr;in.reset=true;
            check(eye.dispatch(context.Get(),in),eye.last_error());context->Flush();
            const auto finish=GetTickCount64()+10000;
            while(eye.completed_gpu_value()<eye.submitted_gpu_value() && GetTickCount64()<finish) Sleep(1);
            check(eye.completed_gpu_value()>=eye.submitted_gpu_value(),"Cached slot GPU completion timeout");
            context->CopyResource(staging.Get(),output.Get());
            check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Map cached output");
            const auto pixel=reinterpret_cast<const unsigned short*>(static_cast<const unsigned char*>(mapped.pData)+
                static_cast<std::size_t>(desc.display_h/2)*mapped.RowPitch)+desc.display_w/2*4;
            bool same=true;for(unsigned i=0;i<3;++i)same=same && std::isfinite(half(pixel[i])) && std::abs(half(pixel[i])-measured[i])<.02f;
            context->Unmap(staging.Get(),0);check(same,"Cached constants preserve rendered content");
        }
        const Fsr4::Diagnostics retired_trace{{11,23,33,1},&Ledger::observe,&retired_ledger};
        in.reset=false;check(eye.dispatch(context.Get(),in,true,&retired_trace),eye.last_error());
        // Destroy with work in flight: bounded registry owns queued resources.
        const auto name=eye.version_name();const auto id=eye.version_id();eye.destroy();context->Flush();
        retired_ledger.expect(retired_trace.id,context.Get(),true,true);
        check(!eye.valid(),"Retired context unavailable for new dispatch");d2vr::Fsr4EyeUpscaler::poll_retired();
        for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R11G11B10_FLOAT}) {
            auto variant=desc;variant.hdr=format!=DXGI_FORMAT_R8G8B8A8_UNORM;
            check(eye.create(device.Get(),variant,provider.c_str(),format,format),eye.last_error());
            auto scene=texture(device.Get(),variant.display_w,variant.display_h,format,nullptr,0);
            // Larger native source with an active upper-left render subrect.
            Ptr<ID3D11RenderTargetView> scene_rtv;
            D3D11_TEXTURE2D_DESC scene_desc{};scene->GetDesc(&scene_desc);scene.Reset();scene_desc.BindFlags|=D3D11_BIND_RENDER_TARGET;
            check(SUCCEEDED(device->CreateTexture2D(&scene_desc,nullptr,&scene)),"Create crop/format colour source");
            check(SUCCEEDED(device->CreateRenderTargetView(scene.Get(),nullptr,&scene_rtv)),"Create crop/format RTV");
            const float clear[4]{.25f,.5f,.75f,1};context->ClearRenderTargetView(scene_rtv.Get(),clear);
            auto result=texture(device.Get(),variant.display_w,variant.display_h,format,nullptr,0);
            auto sample_input=in;sample_input.colour=scene.Get();sample_input.output=result.Get();sample_input.reset=true;
            check(eye.dispatch(context.Get(),sample_input),eye.last_error());context->Flush();
            const auto finish=GetTickCount64()+10000;
            while(eye.completed_gpu_value()<eye.submitted_gpu_value() && GetTickCount64()<finish) Sleep(1);
            check(eye.completed_gpu_value()>=eye.submitted_gpu_value(),"LDR/native HDR crop dispatch completion timeout");
            result->GetDesc(&staging_desc);staging_desc.BindFlags=staging_desc.MiscFlags=0;staging_desc.Usage=D3D11_USAGE_STAGING;
            staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;staging.Reset();
            check(SUCCEEDED(device->CreateTexture2D(&staging_desc,nullptr,&staging)),"Create variant output validation");
            context->CopyResource(staging.Get(),result.Get());check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Read variant output");
            const auto pixels=static_cast<const unsigned char*>(mapped.pData)+static_cast<std::size_t>(variant.display_h/2)*mapped.RowPitch+variant.display_w/2*4;
            const bool pixels_valid=format==DXGI_FORMAT_R8G8B8A8_UNORM?
                std::abs(static_cast<int>(pixels[0])-64)<20 && std::abs(static_cast<int>(pixels[1])-128)<20 && std::abs(static_cast<int>(pixels[2])-191)<20:
                pixels[0]!=0 || pixels[1]!=0 || pixels[2]!=0 || pixels[3]!=0;
            context->Unmap(staging.Get(),0);check(pixels_valid,"Variant output retains native colour content");eye.destroy();
        }
        std::printf("{\"fsr4_dispatch_proven\":true,\"provider_version_id\":%llu,\"provider_version_name\":\"%s\","
            "\"constant_rgb\":[%.6f,%.6f,%.6f],\"game_integration_proven\":false,\"headset_acceptance\":false}\n",
            static_cast<unsigned long long>(id),name.c_str(),measured[0],measured[1],measured[2]);return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FSR4 CHECK FAILED: %s\n",error.what());return 1;}
}
