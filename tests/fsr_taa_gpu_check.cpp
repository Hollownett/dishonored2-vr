#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef D2VR_FSR_BASELINE
#include "../artifacts/build-source/20261010T092851.440911Z-e450f903/source/src/fsr_taa.hpp"
#else
#include "../src/fsr_taa.hpp"
#endif
#include "../src/gpu_pass_timer.hpp"
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <cstdio>
#include <stdexcept>
#include <filesystem>
#include <chrono>
#include <fstream>

template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
static void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
static Ptr<ID3D11Texture2D> texture(ID3D11Device* device,UINT w,UINT h,DXGI_FORMAT format,UINT flags,const void* data=nullptr,UINT bpp=4,UINT mips=1) {
    D3D11_TEXTURE2D_DESC d{};d.Width=w;d.Height=h;d.MipLevels=mips;d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;d.BindFlags=flags;
    const D3D11_SUBRESOURCE_DATA initial{data,w*bpp,0};Ptr<ID3D11Texture2D> out;
    check(SUCCEEDED(device->CreateTexture2D(&d,data && mips==1?&initial:nullptr,&out)),"texture allocation");
    if(data && mips>1) {Ptr<ID3D11DeviceContext> context;device->GetImmediateContext(&context);context->UpdateSubresource(out.Get(),0,nullptr,data,w*bpp,0);}
    return out;
}
static HRESULT STDMETHODCALLTYPE finish(ID3D11DeviceContext* c,BOOL restore,ID3D11CommandList** list) {return c->FinishCommandList(restore,list);}
static void STDMETHODCALLTYPE execute(ID3D11DeviceContext* c,ID3D11CommandList* list,BOOL restore) {c->ExecuteCommandList(list,restore);}
static void settle(d2vr::GpuPassTimer& timer,ID3D11DeviceContext* context,std::uint64_t count) {
    context->Flush();const auto deadline=GetTickCount64()+5000;
    while(timer.stats().samples+timer.stats().disjoint<count && GetTickCount64()<deadline) {timer.poll(context);Sleep(1);}
    check(timer.stats().samples+timer.stats().disjoint>=count,"GPU query completion timed out");
}
// dlss: a directory holding the user's nvngx_dlss.dll. The production seam then runs the NGX backend (DLAA at
// fraction 1, DLSS Quality below it) with the same synthetic inputs; every eye dispatch must be an NGX evaluation.
static void run(ID3D11Device* device,ID3D11DeviceContext* context,float fraction,const std::wstring& provider={},bool expect_fsr4=false,
    DXGI_FORMAT native_colour=DXGI_FORMAT_R8G8B8A8_UNORM,UINT native_mips=1,bool actual100=false,const std::string& output_path={},
    DXGI_FORMAT output_format=DXGI_FORMAT_R8G8B8A8_UNORM,const std::wstring& dlss={}) {
    const UINT w=actual100?2112:2496,h=actual100?2304:2688;
    std::vector<DirectX::PackedVector::HALF> hdr(size_t(w)*h*4),motion(size_t(w)*h*2,0);
    std::vector<unsigned char> ldr(size_t(w)*h*4);
    for(size_t i=0;i<size_t(w)*h;++i) for(unsigned c=0;c<4;++c) {
        float v=c==3?1.f:c==0?.5f:c==1?.25f:.1f;
        if(actual100 && c<3) v+=.04f*static_cast<float>((i%w/47+i/w/53+c)%3)-.04f;
        hdr[i*4+c]=DirectX::PackedVector::XMConvertFloatToHalf(v);ldr[i*4+c]=static_cast<unsigned char>(std::lround(v*255.f));
    }
    auto colour=texture(device,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE,hdr.data(),8);
    auto vectors=texture(device,w,h,DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE,motion.data(),4);
    auto history=texture(device,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
    auto result=texture(device,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE);
    auto tonemapped=texture(device,w,h,native_colour,D3D11_BIND_SHADER_RESOURCE,ldr.data(),4,native_mips);
    auto eye=texture(device,w,h,output_format,D3D11_BIND_RENDER_TARGET);
    auto depth=texture(device,w,h,DXGI_FORMAT_R32_TYPELESS,D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE);
    Ptr<ID3D11DepthStencilView> dsv;D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    check(SUCCEEDED(device->CreateDepthStencilView(depth.Get(),&dv,&dsv)),"depth view");
    Ptr<ID3D11RenderTargetView> eye_rtv;Ptr<ID3D11ShaderResourceView> tone_srv;
    D3D11_RENDER_TARGET_VIEW_DESC eye_view{};eye_view.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
    eye_view.Format=output_format==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM:output_format;
    check(SUCCEEDED(device->CreateRenderTargetView(eye.Get(),&eye_view,&eye_rtv)),"eye view");
    D3D11_SHADER_RESOURCE_VIEW_DESC tone_desc{};tone_desc.Format=native_colour==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM:native_colour;
    tone_desc.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;tone_desc.Texture2D.MipLevels=1;
    check(SUCCEEDED(device->CreateShaderResourceView(tonemapped.Get(),&tone_desc,&tone_srv)),"tonemap view");
    d2vr::TaaBindings b;
    ID3D11Texture2D* textures[6]{history.Get(),vectors.Get(),colour.Get(),nullptr,history.Get(),result.Get()};
    for(unsigned i=0;i<6;++i) if(textures[i]) {b.res[i]=textures[i];b.res[i]->AddRef();textures[i]->GetDesc(&b.tex[i]);b.is_tex[i]=true;}
    for(unsigned i=0;i<3;++i) check(SUCCEEDED(device->CreateShaderResourceView(textures[i],nullptr,&b.srv[i])),"TAA input view");
    D3D11_BUFFER_DESC exposure_desc{};exposure_desc.ByteWidth=16;exposure_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;exposure_desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;exposure_desc.StructureByteStride=4;
    Ptr<ID3D11Buffer> exposure;check(SUCCEEDED(device->CreateBuffer(&exposure_desc,nullptr,&exposure)),"exposure buffer");
    check(SUCCEEDED(device->CreateShaderResourceView(exposure.Get(),nullptr,&b.srv[3])),"exposure view");
    for(unsigned i=0;i<2;++i) check(SUCCEEDED(device->CreateUnorderedAccessView(textures[i+4],nullptr,&b.uav[i])),"TAA output view");
    D3D11_BUFFER_DESC cb{};cb.ByteWidth=800;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(SUCCEEDED(device->CreateBuffer(&cb,nullptr,&b.cb[0])),"per-view constants");
    Ptr<ID3D11DeviceContext> deferred;check(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"deferred context");
    auto fsr=std::make_unique<d2vr::FsrTaa>();fsr->enabled=true;fsr->fraction=fraction;
#if defined(D2VR_FSR4_COMPUTE_TIMING) && !defined(D2VR_FSR_BASELINE)
    fsr->fsr4_compute_timing_enabled=true;
#endif
#ifndef D2VR_FSR_BASELINE
    fsr->fsr4_provider_path=provider;
    if(!dlss.empty()) {
        fsr->dlss_provider_directory=dlss;fsr->dlss_cache_directory=std::filesystem::absolute(L"build/ngx-cache").wstring();
        fsr->dlss_active=true;fsr->dlss_quality=fraction<.999f;
    }
#else
    check(provider.empty() && dlss.empty(),"The archived baseline has no FSR4 or DLSS backend");
#endif
    std::printf("fraction %.2f resources created\n",fraction);
    fsr->observe_depth(dsv.Get());check(!fsr->replace(deferred.Get(),0,b,1.6f,0,0),"unprepared pass declines temporal replacement");
#ifndef D2VR_FSR_BASELINE
    for(int side=0;side<2;++side) check(fsr->fallback_current(deferred.Get(),side,b),"first eye passes copy current colour until contexts are ready");
    check(fsr->fallback_copies==2,"both initial eyes avoid native shared TAA");
#endif
    fsr->prepare(device);
    check(fsr->ready,"FSR prepared");d2vr::TaaDispatchHook::finish_original=&finish;std::puts("contexts prepared");

    d2vr::GpuPassTimer timer;check(timer.prepare(device),"GPU timestamp queries");
    Ptr<ID3D11Query> completed;
    if(!provider.empty() || !dlss.empty()) {D3D11_QUERY_DESC q{};q.Query=D3D11_QUERY_EVENT;check(SUCCEEDED(device->CreateQuery(&q,&completed)),"Diagnostic completion query");}
    BOOL execute_restore=FALSE; // TRUE while the state pin below measures the whole prefix/DLSS/remainder split
    const auto pair=[&](unsigned frame) {
        for(int side=0;side<2;++side) {
            deferred->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.05f,0);
            const float jx=(frame%2)?.25f:-.25f,jy=(frame%2)?-.25f:.25f;
            check(fsr->replace(deferred.Get(),side,b,1.6f,jx,jy),"TAA replacement");
            if(fraction<.999f) {
                auto* rtv=eye_rtv.Get();deferred->OMSetRenderTargets(1,&rtv,nullptr);
                auto* srv=tone_srv.Get();deferred->PSSetShaderResources(0,1,&srv);
                fsr->last_vfov[side]=1.6f;fsr->last_jitter_x[side]=jx;fsr->last_jitter_y[side]=jy;
                fsr->observe_post_tonemap_bind(deferred.Get(),&srv,1);fsr->post_tonemap_end(side,deferred.Get());
            }
            Ptr<ID3D11CommandList> list;check(SUCCEEDED(deferred->FinishCommandList(TRUE,&list)),"finish split remainder");
            fsr->finished(deferred.Get(),list.Get());check(fsr->execute(context,list.Get(),execute_restore,&execute),"execute native-prefix/FSR/remainder");
        }
    };
    const auto settle_pair=[&]() {
        // Standalone test only, outside timestamps: prevent CPU bursts exhausting the
        // production bridge's nonblocking slot ring before checking ML routing.
        if(completed) {context->End(completed.Get());context->Flush();const auto deadline=GetTickCount64()+5000;
            while(context->GetData(completed.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_FALSE && GetTickCount64()<deadline) Sleep(1);
            check(context->GetData(completed.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK,"Diagnostic GPU completion timeout");}
    };
#ifndef D2VR_FSR_BASELINE
    // State pin: distinctive immediate-context state must survive the split that creates and evaluates the NGX
    // features (ExecuteCommandList(TRUE) restores around each list, so anything left over came from the mod's dispatch).
    D3D11_VIEWPORT pin_viewport{7,11,123,45,.25f,.75f};Ptr<ID3D11Buffer> pin_cb;Ptr<ID3D11ShaderResourceView> pin_srv;Ptr<ID3D11BlendState> pin_blend;
    const auto pin_state=[&]() {
        if(!pin_cb) {D3D11_BUFFER_DESC bd{};bd.ByteWidth=64;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;check(SUCCEEDED(device->CreateBuffer(&bd,nullptr,&pin_cb)),"pin constant buffer");}
        if(!pin_srv) check(SUCCEEDED(device->CreateShaderResourceView(tonemapped.Get(),&tone_desc,&pin_srv)),"pin shader resource");
        if(!pin_blend) {D3D11_BLEND_DESC bl{};bl.RenderTarget[0].BlendEnable=TRUE;bl.RenderTarget[0].SrcBlend=D3D11_BLEND_SRC_ALPHA;bl.RenderTarget[0].DestBlend=D3D11_BLEND_INV_SRC_ALPHA;bl.RenderTarget[0].BlendOp=D3D11_BLEND_OP_ADD;
            bl.RenderTarget[0].SrcBlendAlpha=D3D11_BLEND_ONE;bl.RenderTarget[0].DestBlendAlpha=D3D11_BLEND_ZERO;bl.RenderTarget[0].BlendOpAlpha=D3D11_BLEND_OP_ADD;bl.RenderTarget[0].RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
            check(SUCCEEDED(device->CreateBlendState(&bl,&pin_blend)),"pin blend state");}
        context->RSSetViewports(1,&pin_viewport);
        auto* cb=pin_cb.Get();context->CSSetConstantBuffers(7,1,&cb);context->VSSetConstantBuffers(5,1,&cb);
        auto* srv=pin_srv.Get();context->PSSetShaderResources(3,1,&srv);
        const float factor[4]{.1f,.2f,.3f,.4f};context->OMSetBlendState(pin_blend.Get(),factor,0x5);
    };
    const auto check_pin=[&](const char* phase) {
        UINT n=1;D3D11_VIEWPORT vp{};context->RSGetViewports(&n,&vp);
        ID3D11Buffer* cb{};context->CSGetConstantBuffers(7,1,&cb);ID3D11Buffer* vcb{};context->VSGetConstantBuffers(5,1,&vcb);
        ID3D11ShaderResourceView* srv{};context->PSGetShaderResources(3,1,&srv);
        ID3D11BlendState* blend{};float factor[4]{};UINT mask{};context->OMGetBlendState(&blend,factor,&mask);
        const bool same=n==1 && vp.TopLeftX==7 && vp.TopLeftY==11 && vp.Width==123 && vp.Height==45 && vp.MinDepth==.25f && vp.MaxDepth==.75f &&
            cb==pin_cb.Get() && vcb==pin_cb.Get() && srv==pin_srv.Get() && blend==pin_blend.Get() && factor[0]==.1f && factor[3]==.4f && mask==0x5;
        if(cb) cb->Release();if(vcb) vcb->Release();if(srv) srv->Release();if(blend) blend->Release();
        std::printf("state pin %s: %s\n",phase,same?"preserved":"CHANGED");
        check(same,"immediate-context state leaked through the DLSS split");
    };
    if(!dlss.empty()) {execute_restore=TRUE;pin_state();}
#endif
    for(unsigned i=0;i<8;++i) {pair(i);settle_pair();if(!i) std::puts("first eye pair executed");
#ifndef D2VR_FSR_BASELINE
        if(!dlss.empty() && i<2) check_pin(i?"second pair (steady evaluate)":"first pair (NGX init/create/evaluate)");
#endif
    }
#ifndef D2VR_FSR_BASELINE
    if(!dlss.empty()) {execute_restore=FALSE;ID3D11ShaderResourceView* none{};context->PSSetShaderResources(3,1,&none);ID3D11Buffer* nocb{};context->CSSetConstantBuffers(7,1,&nocb);context->VSSetConstantBuffers(5,1,&nocb);context->OMSetBlendState(nullptr,nullptr,0xffffffff);}
    if(!dlss.empty()) {
        // The first dispatch on the executing thread created the features (Quality: after NGX's optimal settings) and
        // every warm-up eye already went through NGX; the EVENT tickets are polled on that same thread.
        std::printf("DLSS state=%u features_created=%llu optimal=%ux%u quality_fraction=%.4f create_failures=%llu reason=%s result=%d owner_status=%u rebuilds=%llu\n",
            fsr->dlss_state.load(),static_cast<unsigned long long>(fsr->dlss_features_created.load()),fsr->dlss_optimal_width.load(),fsr->dlss_optimal_height.load(),
            fsr->dlss_quality_fraction.load(),static_cast<unsigned long long>(fsr->dlss_create_failures.load()),
            d2vr::DlssEyeUpscaler::reason_name(static_cast<d2vr::DlssEyeUpscaler::Reason>(fsr->dlss_last_reason.load())),fsr->dlss_last_result.load(),fsr->dlss_owner_status.load(),
            static_cast<unsigned long long>(fsr->dlss_owner_rebuilds.load()));
        std::printf("DLSS warm-up evaluations=%llu failures=%llu fallbacks=%llu owner_refusals=%llu generation=[%u,%u]\n",
            static_cast<unsigned long long>(fsr->dlss_evaluations.load()),static_cast<unsigned long long>(fsr->dlss_evaluate_failures.load()),
            static_cast<unsigned long long>(fsr->dlss_fallbacks.load()),static_cast<unsigned long long>(fsr->dlss_owner_refusals.load()),
            fsr->eye_upscaler_generation[0].load(),fsr->eye_upscaler_generation[1].load());
        check(fsr->dlss_state.load()==static_cast<unsigned>(d2vr::DlssFallback::State::active),"NGX features created for the production seam");
        if(fraction<.999f) check(fsr->dlss_optimal_width.load()<w && fsr->dlss_optimal_height.load()<h,"DLSS Quality optimal render size below the output");
        check(fsr->dlss_evaluations==16 && fsr->dlss_evaluate_failures==0 && fsr->dlss_fallbacks==0,"every warm-up eye was an NGX evaluation");
        // The motion texture is the engine's full (display-sized) target; in Quality its content is the render subrect
        // the feature reads as low-resolution motion with the render-size scale (the FSR contract).
        std::printf("DLSS motion texture=%ux%u colour=%ux%u render=%ux%u display=%ux%u\n",fsr->eye_motion_width[0].load(),fsr->eye_motion_height[0].load(),
            fsr->eye_colour_width[0].load(),fsr->eye_colour_height[0].load(),fsr->eye_render_width[0].load(),fsr->eye_render_height[0].load(),w,h);
        check(fsr->eye_motion_width[0]==w && fsr->eye_motion_height[0]==h && fsr->eye_motion_width[1]==w,"DLSS consumed the display-sized motion texture");
        if(fraction<.999f) check(fsr->eye_render_width[0]<w && fsr->eye_render_height[0]<h,"Quality rendered the subrect below the display size");
    }
#endif
#if defined(D2VR_FSR4_COMPUTE_TIMING) && !defined(D2VR_FSR_BASELINE)
    fsr->poll_fsr4_gpu_timing();
    std::array<std::uint64_t,2> compute_start_samples{};std::array<double,2> compute_start_ms{};
    for(unsigned side=0;side<2;++side) {
        compute_start_samples[side]=fsr->eye_fsr4_compute_gpu_samples[side].load();
        compute_start_ms[side]=fsr->eye_fsr4_compute_gpu_total_ms[side].load();
    }
#endif
    const unsigned samples=actual100?128:32;
    double cpu_total_ms=0;
    std::array<double,2> cpu_record_ms{},cpu_handoff_ms{};
    for(unsigned i=0;i<samples;++i) {
        const int ticket=timer.begin(context);check(ticket>=0,"reserve performance sample");
        const auto cpu_start=std::chrono::steady_clock::now();pair(i+8);
        cpu_total_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-cpu_start).count();
#ifndef D2VR_FSR_BASELINE
        if(actual100) for(unsigned side=0;side<2;++side) {
            cpu_record_ms[side]+=fsr->eye_fsr4_record_ms[side].load();
            cpu_handoff_ms[side]+=fsr->eye_fsr4_handoff_ms[side].load();
        }
#endif
        timer.end(context,ticket);settle_pair();
        if(i%4==3) settle(timer,context,i+1);
    }
    settle(timer,context,samples);const auto measured=timer.stats();
#if defined(D2VR_FSR4_COMPUTE_TIMING) && !defined(D2VR_FSR_BASELINE)
    fsr->poll_fsr4_gpu_timing();
    if(actual100 && expect_fsr4) for(unsigned side=0;side<2;++side) {
        const auto count=fsr->eye_fsr4_compute_gpu_samples[side].load()-compute_start_samples[side];
        const double total=fsr->eye_fsr4_compute_gpu_total_ms[side].load()-compute_start_ms[side];
        check(count==samples,"All measured FSR4 compute-only GPU samples must complete");
        std::printf("FSR4_compute_eye=%u completed_samples=%llu total_ms=%.6f mean_ms=%.6f includes_warmup=0\n",
            side,static_cast<unsigned long long>(count),total,total/static_cast<double>(count));
    }
#endif
    const auto dispatches=2ull*(8+samples);
    check(measured.samples==samples && measured.disjoint==0 && fsr->failures==0 && fsr->create_failures==0 && fsr->replaced==dispatches,"successful measured eye dispatches");
#ifndef D2VR_FSR_BASELINE
    if(!dlss.empty()) {
        check(fsr->dlss_evaluations==dispatches && fsr->dlss_evaluate_failures==0 && fsr->dlss_fallbacks==0 && fsr->dlss_create_failures==0,"every measured eye was an NGX evaluation");
        check(fsr->eye_upscaler_generation[0]==d2vr::dlss_mode::generation && fsr->eye_upscaler_generation[1]==d2vr::dlss_mode::generation,"both eyes report the DLSS backend");
        std::printf("DLSS per-eye evaluations=%llu over %llu dispatches, owner_refusals=%llu, features_created=%llu\n",
            static_cast<unsigned long long>(fsr->dlss_evaluations.load()),static_cast<unsigned long long>(dispatches),
            static_cast<unsigned long long>(fsr->dlss_owner_refusals.load()),static_cast<unsigned long long>(fsr->dlss_features_created.load()));
    }
    if(expect_fsr4) {
        check(fsr->fsr4_create_failures==0 && fsr->fsr4_fallbacks==0 && fsr->fsr4_dispatches==dispatches,"All per-eye jobs must use the genuine FSR4 provider");
        check(fsr->eye_upscaler_generation[0]==4 && fsr->eye_upscaler_generation[1]==4 && fsr->eye_fsr4_version[0] && fsr->eye_fsr4_version[1],"Both eyes must report actual FSR4 provider identities");
        std::printf("FSR4 per-eye dispatches=%llu provider_ids=[%llu,%llu]\n",static_cast<unsigned long long>(fsr->fsr4_dispatches.load()),static_cast<unsigned long long>(fsr->eye_fsr4_version[0].load()),static_cast<unsigned long long>(fsr->eye_fsr4_version[1].load()));
    } else if(!provider.empty()) {
        check(fsr->fsr4_create_failures==2 && fsr->fsr4_dispatches==0 && fsr->fsr4_fallbacks==dispatches,"Unavailable FSR4 must retain the FSR3 route");
        check(fsr->eye_upscaler_generation[0]==3 && fsr->eye_upscaler_generation[1]==3 && !fsr->eye_fsr4_version[0] && !fsr->eye_fsr4_version[1],"Fallback must not report an FSR4 provider");
        std::puts("PASS: missing FSR4 provider preserves FSR3 for both eyes");
    }
#endif
#ifndef D2VR_FSR_BASELINE
    if(fraction<.999f && !actual100 && dlss.empty()) {
        // Governor steps: a smaller (then the original) fraction reuses the contexts, a larger one rebuilds once.
        const auto builds=fsr->context_builds.load(),replaced=fsr->replaced.load(),ml_before=fsr->fsr4_dispatches.load();
        for(const float f:{fraction-.07f,fraction}) {fsr->fraction=f;fsr->prepare(device);check(fsr->ready,"resized FSR ready");pair(40);settle_pair();pair(41);settle_pair();}
        check(fsr->context_builds==builds && fsr->replaced==replaced+8 && fsr->failures==0,"smaller render size reuses the FSR contexts");
        if(expect_fsr4) check(fsr->fsr4_dispatches==ml_before+8 && fsr->fsr4_fallbacks==0,"FSR4 accepts a smaller render size");
        fsr->fraction=fraction+.05f;fsr->prepare(device);check(fsr->context_builds==builds+1,"larger render size rebuilds");
        fsr->fraction=fraction;fsr->prepare(device);check(fsr->context_builds==builds+1,"back below the new maximum reuses");
        pair(42);settle_pair();check(fsr->failures==0,"dispatch after grow");
        std::printf("resize: builds=%llu (reused for %.2f/%.2f, rebuilt for %.2f)\n",static_cast<unsigned long long>(fsr->context_builds.load()),fraction-.07f,fraction,fraction+.05f);
    }
#endif
    // Native results are HDR; upscale results are byte RGBA. Confirm the tested output contains the expected colour.
    auto* output=fraction<.999f?eye.Get():result.Get();D3D11_TEXTURE2D_DESC od{};output->GetDesc(&od);od.Usage=D3D11_USAGE_STAGING;od.BindFlags=0;od.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Ptr<ID3D11Texture2D> readback;check(SUCCEEDED(device->CreateTexture2D(&od,nullptr,&readback)),"output readback");context->CopyResource(readback.Get(),output);
    D3D11_MAPPED_SUBRESOURCE mapped{};check(SUCCEEDED(context->Map(readback.Get(),0,D3D11_MAP_READ,0,&mapped)),"map result");
    const auto* middle=static_cast<const unsigned char*>(mapped.pData)+(h/2)*mapped.RowPitch+(w/2)*(fraction<.999f?4:8);
    const float red=fraction<.999f?middle[0]/255.f:DirectX::PackedVector::XMConvertHalfToFloat(reinterpret_cast<const DirectX::PackedVector::HALF*>(middle)[0]);
    std::uint64_t pixel_hash=14695981039346656037ull;
    if(actual100) {
        std::ofstream pixels;
        if(!output_path.empty()) {pixels.open(output_path,std::ios::binary);check(pixels.is_open(),"open output pixel dump");}
        for(UINT y=0;y<h;++y) {
            const auto* row=static_cast<const unsigned char*>(mapped.pData)+size_t(y)*mapped.RowPitch;
            for(UINT x=0;x<w*4;++x) {pixel_hash^=row[x];pixel_hash*=1099511628211ull;}
            if(pixels.is_open()) pixels.write(reinterpret_cast<const char*>(row),static_cast<std::streamsize>(w)*4);
        }
        if(pixels.is_open()) {pixels.close();check(!pixels.fail(),"write output pixel dump");}
    }
    context->Unmap(readback.Get(),0);
    check(std::isfinite(red) && red>.4f && red<.6f,"FSR colour result valid");
#ifndef D2VR_FSR_BASELINE
    if(!dlss.empty()) {
        // Deselecting retires the pair; a resize and a mode switch must recreate cleanly and the fallback must stay FSR 3.
        const auto created=fsr->dlss_features_created.load();
        execute_restore=TRUE;pin_state();
        fsr->dlss_active=false;pair(60);settle_pair();check_pin("deselect (feature release)");
        check(fsr->dlss_state.load()==static_cast<unsigned>(d2vr::DlssFallback::State::off) && fsr->eye_upscaler_generation[0]==3,"deselected DLSS hands the eye back to FSR 3");
        fsr->dlss_active=true;pair(61);settle_pair();check_pin("reselect (feature create)");execute_restore=FALSE;
        check(fsr->dlss_features_created==created+1 && fsr->eye_upscaler_generation[0]==d2vr::dlss_mode::generation && fsr->dlss_evaluate_failures==0,"re-selected DLSS recreates its features");
        std::printf("DLSS reselect: features_created=%llu evaluations=%llu\n",static_cast<unsigned long long>(fsr->dlss_features_created.load()),static_cast<unsigned long long>(fsr->dlss_evaluations.load()));
    }
#endif
    std::printf("fraction=%.2f render=%ux%u display=%ux%u samples=%llu GPU_pair_mean_ms=%.6f max_ms=%.6f output_red=%.4f\n",fraction,static_cast<UINT>(std::lround(w*fraction)),static_cast<UINT>(std::lround(h*fraction)),w,h,static_cast<unsigned long long>(measured.samples),measured.total_ms/static_cast<double>(measured.samples),measured.max_ms,red);
    if(actual100) std::printf("GPU_pair_total_ms=%.6f CPU_pair_submit_total_ms=%.6f CPU_pair_submit_mean_ms=%.6f pixel_fnv1a64=%016llx pixel_bytes=%llu\n",
        measured.total_ms,cpu_total_ms,cpu_total_ms/samples,static_cast<unsigned long long>(pixel_hash),static_cast<unsigned long long>(w)*h*4);
    if(actual100) std::printf("output_format=%u CPU_FSR4_record_eye_mean_ms=[%.6f,%.6f] CPU_FSR4_handoff_eye_mean_ms=[%.6f,%.6f]\n",
        static_cast<unsigned>(output_format),cpu_record_ms[0]/samples,cpu_record_ms[1]/samples,cpu_handoff_ms[0]/samples,cpu_handoff_ms[1]/samples);
#ifndef D2VR_FSR_BASELINE
    const auto resets_left=fsr->eye_resets[0].load(),resets_right=fsr->eye_resets[1].load();
    fsr->request_reset();pair(100);settle_pair();
    check(fsr->eye_resets[0]==resets_left+1 && fsr->eye_resets[1]==resets_right+1,"scene/XR discontinuity resets both eye histories");
    if(fraction>=.999f) {
        // A is already sealed for execution. The same context's second TAA
        // dispatch B cannot split again and records a spatial fallback in A's
        // remainder. A must not consume/clear B's reset when executed later.
        const auto stale_before=fsr->stale_jobs.load(),dispatch_before=fsr->eye_dispatches[0].load();
        const auto reset0=fsr->eye_resets[0].load(),reset1=fsr->eye_resets[1].load();
        deferred->ClearDepthStencilView(dsv.Get(),D3D11_CLEAR_DEPTH,.05f,0);
        check(fsr->replace(deferred.Get(),0,b,1.6f,0,0),"queue first native temporal split");
        check(!fsr->replace(deferred.Get(),0,b,1.6f,0,0),"duplicate split must decline without losing its prefix");
        check(fsr->fallback_current(deferred.Get(),0,b),"duplicate dispatch records current-eye fallback");
        Ptr<ID3D11CommandList> remainder;
        check(SUCCEEDED(deferred->FinishCommandList(TRUE,&remainder)),"finish fallback remainder");
        fsr->finished(deferred.Get(),remainder.Get());
        check(fsr->execute(context,remainder.Get(),FALSE,&execute),"execute queued prefix and fallback remainder");
        check(fsr->stale_jobs==stale_before+1 && fsr->eye_dispatches[0]==dispatch_before,
            "queued temporal dispatch must not consume a later spatial fallback's reset");
        pair(101);settle_pair();
        check(fsr->eye_resets[0]==reset0+1 && fsr->eye_resets[1]==reset1+1,
            "first accepted pair after queued fallback resets both invalidated histories");
    }
#endif
    fsr->clear();
}
int main(int argc,char** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    try {
        Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL wanted=D3D_FEATURE_LEVEL_11_1;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&wanted,1,D3D11_SDK_VERSION,&device,nullptr,&context)),"hardware FL11.1 device");
        Ptr<IDXGIDevice> dxgi;Ptr<IDXGIAdapter> adapter;DXGI_ADAPTER_DESC adapter_desc{};
        check(SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&adapter_desc)),"hardware adapter identity");
        std::printf("adapter=%ls vendor=%04x device=%04x LUID=%08x:%08x dedicated_vram_bytes=%llu\n",
            adapter_desc.Description,adapter_desc.VendorId,adapter_desc.DeviceId,static_cast<unsigned>(adapter_desc.AdapterLuid.HighPart),
            static_cast<unsigned>(adapter_desc.AdapterLuid.LowPart),static_cast<unsigned long long>(adapter_desc.DedicatedVideoMemory));
#ifdef D2VR_FSR_BASELINE
        std::puts("FSR wrapper baseline: padded/full-size input copy and depth work");
#else
        std::puts("FSR wrapper candidate: cropped input copy and active-size depth work");
#endif
        std::wstring provider,dlss;
        bool ml=false,missing=false,actual100=false;std::string output_path;
        DXGI_FORMAT output_format=DXGI_FORMAT_R8G8B8A8_UNORM;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--fsr4") ml=true;
            else if(arg=="--fsr4-missing") missing=true;
            else if(arg=="--100-percent") actual100=true;
            else if(arg=="--output-srgb" || arg=="--output-typeless") {
                check(output_format==DXGI_FORMAT_R8G8B8A8_UNORM,"Select one output format");
                output_format=arg=="--output-srgb"?DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:DXGI_FORMAT_R8G8B8A8_TYPELESS;
            }
            else if(arg=="--output" && i+1<argc) output_path=argv[++i];
            else if(arg=="--dlss" && i+1<argc) {const std::string narrow=argv[++i];dlss.assign(narrow.begin(),narrow.end());}
            else if(arg=="--dlss-env") {
                // The harness lane: the provider directory comes from D2VR_DLSS_PROVIDER_DIR; without it the lane is a skip.
                wchar_t value[1024]{};const auto n=GetEnvironmentVariableW(L"D2VR_DLSS_PROVIDER_DIR",value,1024);
                if(!n || n>=1024) {std::puts("DLSS provider not configured (D2VR_DLSS_PROVIDER_DIR): skipped");return 0;}
                dlss.assign(value,n);
            }
            else check(false,"Unknown GPU check argument");
        }
        if(!dlss.empty()) {
            check(!ml && !missing && !actual100,"The DLSS lane runs alone");
            std::wprintf(L"DLSS provider directory %ls\n",dlss.c_str());
            run(device.Get(),context.Get(),1.f,{},false,DXGI_FORMAT_R8G8B8A8_UNORM,1,false,{},DXGI_FORMAT_R8G8B8A8_UNORM,dlss);
            run(device.Get(),context.Get(),.67f,{},false,DXGI_FORMAT_R8G8B8A8_UNORM,1,false,{},DXGI_FORMAT_R8G8B8A8_UNORM,dlss);
            std::puts("PASS: DLAA and DLSS Quality NGX evaluations through the production FsrTaa seam; synthetic scene, not game-frame quality");return 0;
        }
        check(!(ml && missing),"Select one FSR provider");
        check(output_path.empty() || actual100,"Pixel dump requires --100-percent");
        check(output_format==DXGI_FORMAT_R8G8B8A8_UNORM || actual100,"Output format requires --100-percent");
        if(ml) provider=std::filesystem::absolute(L"third_party/fsr4/bin/amd_fidelityfx_upscaler_dx12.dll").wstring();
        else if(missing) provider=std::filesystem::absolute(L"third_party/fsr4/missing/amd_fidelityfx_upscaler_dx12.dll").wstring();
        if(actual100) {
            run(device.Get(),context.Get(),.77f,provider,ml,DXGI_FORMAT_R8G8B8A8_UNORM,1,true,output_path,output_format);
            std::puts("PASS: actual100 output geometry, patterned synthetic scene; not game-frame performance");return 0;
        }
        run(device.Get(),context.Get(),1.f,provider,ml);run(device.Get(),context.Get(),.77f,provider,ml);run(device.Get(),context.Get(),.67f,provider,ml);
        if(ml) for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_TYPELESS}) {
            run(device.Get(),context.Get(),.77f,provider,true,format,2);
            run(device.Get(),context.Get(),.67f,provider,true,format,2);
        }
        std::puts("PASS: native/quality/balanced real GPU wrapper dispatches and output checks; synthetic scene, not game-frame performance");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
