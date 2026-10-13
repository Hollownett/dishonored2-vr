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
static void run(ID3D11Device* device,ID3D11DeviceContext* context,float fraction,const std::wstring& provider={},bool expect_fsr4=false,
    DXGI_FORMAT native_colour=DXGI_FORMAT_R8G8B8A8_UNORM,UINT native_mips=1) {
    constexpr UINT w=2496,h=2688;
    std::vector<DirectX::PackedVector::HALF> hdr(size_t(w)*h*4),motion(size_t(w)*h*2,0);
    std::vector<unsigned char> ldr(size_t(w)*h*4);
    for(size_t i=0;i<size_t(w)*h;++i) for(unsigned c=0;c<4;++c) {
        const float v=c==3?1.f:c==0?.5f:c==1?.25f:.1f;
        hdr[i*4+c]=DirectX::PackedVector::XMConvertFloatToHalf(v);ldr[i*4+c]=static_cast<unsigned char>(std::lround(v*255.f));
    }
    auto colour=texture(device,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE,hdr.data(),8);
    auto vectors=texture(device,w,h,DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE,motion.data(),4);
    auto history=texture(device,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
    auto result=texture(device,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_UNORDERED_ACCESS|D3D11_BIND_SHADER_RESOURCE);
    auto tonemapped=texture(device,w,h,native_colour,D3D11_BIND_SHADER_RESOURCE,ldr.data(),4,native_mips);
    auto eye=texture(device,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET);
    auto depth=texture(device,w,h,DXGI_FORMAT_R32_TYPELESS,D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE);
    Ptr<ID3D11DepthStencilView> dsv;D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
    check(SUCCEEDED(device->CreateDepthStencilView(depth.Get(),&dv,&dsv)),"depth view");
    Ptr<ID3D11RenderTargetView> eye_rtv;Ptr<ID3D11ShaderResourceView> tone_srv;
    check(SUCCEEDED(device->CreateRenderTargetView(eye.Get(),nullptr,&eye_rtv)),"eye view");
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
#ifndef D2VR_FSR_BASELINE
    fsr->fsr4_provider_path=provider;
#else
    check(provider.empty(),"The archived baseline has no FSR4 backend");
#endif
    std::printf("fraction %.2f resources created\n",fraction);
    fsr->observe_depth(dsv.Get());check(!fsr->replace(deferred.Get(),0,b,1.6f,0,0),"unprepared pass keeps native TAA");fsr->prepare(device);
    check(fsr->ready,"FSR prepared");d2vr::TaaDispatchHook::finish_original=&finish;std::puts("contexts prepared");
    d2vr::GpuPassTimer timer;check(timer.prepare(device),"GPU timestamp queries");
    Ptr<ID3D11Query> completed;
    if(!provider.empty()) {D3D11_QUERY_DESC q{};q.Query=D3D11_QUERY_EVENT;check(SUCCEEDED(device->CreateQuery(&q,&completed)),"Diagnostic completion query");}
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
            fsr->finished(deferred.Get(),list.Get());check(fsr->execute(context,list.Get(),FALSE,&execute),"execute native-prefix/FSR/remainder");
        }
    };
    const auto settle_pair=[&]() {
        // Standalone test only, outside timestamps: prevent CPU bursts exhausting the
        // production bridge's nonblocking slot ring before checking ML routing.
        if(completed) {context->End(completed.Get());context->Flush();const auto deadline=GetTickCount64()+5000;
            while(context->GetData(completed.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_FALSE && GetTickCount64()<deadline) Sleep(1);
            check(context->GetData(completed.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH)==S_OK,"Diagnostic GPU completion timeout");}
    };
    for(unsigned i=0;i<8;++i) {pair(i);settle_pair();if(!i) std::puts("first eye pair executed");}
    constexpr unsigned samples=32;
    for(unsigned i=0;i<samples;++i) {
        const int ticket=timer.begin(context);check(ticket>=0,"reserve performance sample");pair(i+8);timer.end(context,ticket);settle_pair();
        if(i%4==3) settle(timer,context,i+1);
    }
    settle(timer,context,samples);const auto measured=timer.stats();
    check(measured.samples==samples && measured.disjoint==0 && fsr->failures==0 && fsr->create_failures==0 && fsr->replaced==80,"successful measured eye dispatches");
#ifndef D2VR_FSR_BASELINE
    if(expect_fsr4) {
        check(fsr->fsr4_create_failures==0 && fsr->fsr4_fallbacks==0 && fsr->fsr4_dispatches==80,"All per-eye jobs must use the genuine FSR4 provider");
        check(fsr->eye_upscaler_generation[0]==4 && fsr->eye_upscaler_generation[1]==4 && fsr->eye_fsr4_version[0] && fsr->eye_fsr4_version[1],"Both eyes must report actual FSR4 provider identities");
        std::printf("FSR4 per-eye dispatches=%llu provider_ids=[%llu,%llu]\n",static_cast<unsigned long long>(fsr->fsr4_dispatches.load()),static_cast<unsigned long long>(fsr->eye_fsr4_version[0].load()),static_cast<unsigned long long>(fsr->eye_fsr4_version[1].load()));
    } else if(!provider.empty()) {
        check(fsr->fsr4_create_failures==2 && fsr->fsr4_dispatches==0 && fsr->fsr4_fallbacks==80,"Unavailable FSR4 must retain the FSR3 route");
        check(fsr->eye_upscaler_generation[0]==3 && fsr->eye_upscaler_generation[1]==3 && !fsr->eye_fsr4_version[0] && !fsr->eye_fsr4_version[1],"Fallback must not report an FSR4 provider");
        std::puts("PASS: missing FSR4 provider preserves FSR3 for both eyes");
    }
#endif
#ifndef D2VR_FSR_BASELINE
    if(fraction<.999f) {
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
    const float red=fraction<.999f?middle[0]/255.f:DirectX::PackedVector::XMConvertHalfToFloat(reinterpret_cast<const DirectX::PackedVector::HALF*>(middle)[0]);context->Unmap(readback.Get(),0);
    check(std::isfinite(red) && red>.4f && red<.6f,"FSR colour result valid");
    std::printf("fraction=%.2f render=%ux%u display=%ux%u samples=%llu GPU_pair_mean_ms=%.6f max_ms=%.6f output_red=%.4f\n",fraction,static_cast<UINT>(std::lround(w*fraction)),static_cast<UINT>(std::lround(h*fraction)),w,h,static_cast<unsigned long long>(measured.samples),measured.total_ms/static_cast<double>(measured.samples),measured.max_ms,red);
    fsr->clear();
}
int main(int argc,char** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    try {
        Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL wanted=D3D_FEATURE_LEVEL_11_1;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&wanted,1,D3D11_SDK_VERSION,&device,nullptr,&context)),"hardware FL11.1 device");
#ifdef D2VR_FSR_BASELINE
        std::puts("FSR wrapper baseline: padded/full-size input copy and depth work");
#else
        std::puts("FSR wrapper candidate: cropped input copy and active-size depth work");
#endif
        std::wstring provider;
        const bool ml=argc>1 && std::string(argv[1])=="--fsr4";
        if(ml) provider=std::filesystem::absolute(L"third_party/fsr4/bin/amd_fidelityfx_upscaler_dx12.dll").wstring();
        else if(argc>1 && std::string(argv[1])=="--fsr4-missing") provider=std::filesystem::absolute(L"third_party/fsr4/missing/amd_fidelityfx_upscaler_dx12.dll").wstring();
        else check(argc==1,"Unknown GPU check argument");
        run(device.Get(),context.Get(),1.f,provider,ml);run(device.Get(),context.Get(),.77f,provider,ml);run(device.Get(),context.Get(),.67f,provider,ml);
        if(ml) for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,DXGI_FORMAT_R8G8B8A8_TYPELESS}) {
            run(device.Get(),context.Get(),.77f,provider,true,format,2);
            run(device.Get(),context.Get(),.67f,provider,true,format,2);
        }
        std::puts("PASS: native/quality/balanced real GPU wrapper dispatches and output checks; synthetic scene, not game-frame performance");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
