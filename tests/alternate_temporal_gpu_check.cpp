#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/fsr_taa.hpp"
#include <DirectXPackedVector.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
constexpr UINT width=1024,height=768;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void checked(HRESULT value,const char* message){if(FAILED(value)){std::cerr<<message<<" HRESULT=0x"<<std::hex<<static_cast<unsigned>(value)<<std::dec<<'\n';throw std::runtime_error(message);}}
Ptr<ID3D11Texture2D> texture(ID3D11Device* device,DXGI_FORMAT format,UINT binds,const void* pixels=nullptr,UINT pitch=0,bool staging=false){
    D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=1;d.Format=format;d.SampleDesc.Count=1;
    d.Usage=staging?D3D11_USAGE_STAGING:D3D11_USAGE_DEFAULT;d.BindFlags=staging?0:binds;d.CPUAccessFlags=staging?D3D11_CPU_ACCESS_READ:0;
    const D3D11_SUBRESOURCE_DATA initial{pixels,pitch,0};Ptr<ID3D11Texture2D> result;
    checked(device->CreateTexture2D(&d,pixels?&initial:nullptr,&result),"synthetic texture");return result;
}
HRESULT STDMETHODCALLTYPE finish(ID3D11DeviceContext* context,BOOL restore,ID3D11CommandList** list){return context->FinishCommandList(restore,list);}
void STDMETHODCALLTYPE execute(ID3D11DeviceContext* context,ID3D11CommandList* list,BOOL restore){context->ExecuteCommandList(list,restore);}
void drain(ID3D11Device* device,ID3D11DeviceContext* context){
    D3D11_QUERY_DESC desc{};desc.Query=D3D11_QUERY_EVENT;Ptr<ID3D11Query> query;checked(device->CreateQuery(&desc,&query),"completion query");
    context->End(query.Get());context->Flush();const auto deadline=GetTickCount64()+10000;
    for(;;){const auto hr=context->GetData(query.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);if(hr==S_OK)return;
        if(hr!=S_FALSE || GetTickCount64()>=deadline){std::cerr<<"FAIL: GPU completion unproved; retaining owners until process exit\n";ExitProcess(2);}Sleep(1);}
}
struct Eye {
    std::array<Ptr<ID3D11Texture2D>,2> hdr,tone;
    std::array<Ptr<ID3D11ShaderResourceView>,2> tone_srv;
    Ptr<ID3D11Texture2D> history,result,output,readback;
    Ptr<ID3D11RenderTargetView> rtv;
};
class Fixture {
    Ptr<ID3D11Device> device_;Ptr<ID3D11DeviceContext> immediate_,deferred_;
    Ptr<ID3D11Texture2D> motion_,depth_;Ptr<ID3D11DepthStencilView> dsv_;
    std::array<Eye,2> eyes_;std::array<std::array<d2vr::TaaBindings,2>,2> bindings_;
    std::unique_ptr<d2vr::FsrTaa> seam_=std::make_unique<d2vr::FsrTaa>();
    std::array<std::vector<unsigned char>,2> retained_;
    bool quality_{},dlss_{};unsigned frame_{};
    std::uint64_t bootstrap_fallbacks_{},bootstrap_features_{};bool optimal_rebuild_{};
    void resources(){
        std::vector<std::uint16_t> zero(size_t(width)*height*2,0);
        motion_=texture(device_.Get(),DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE,zero.data(),width*4);
        depth_=texture(device_.Get(),DXGI_FORMAT_R32_TYPELESS,D3D11_BIND_DEPTH_STENCIL|D3D11_BIND_SHADER_RESOURCE);
        D3D11_DEPTH_STENCIL_VIEW_DESC dv{};dv.Format=DXGI_FORMAT_D32_FLOAT;dv.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        checked(device_->CreateDepthStencilView(depth_.Get(),&dv,&dsv_),"depth view");
        for(unsigned eye=0;eye<2;++eye){auto& e=eyes_[eye];
            for(unsigned phase=0;phase<2;++phase){
                std::vector<std::uint16_t> hdr(size_t(width)*height*4);std::vector<unsigned char> ldr(hdr.size());
                for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)for(unsigned channel=0;channel<4;++channel){
                    const bool bright=(x<width/2)!=bool(phase);
                    const float value=channel==3?1.f:channel==eye?(bright?.9f:.45f):channel==2?.0625f:.125f;
                    const size_t at=(size_t(y)*width+x)*4+channel;
                    hdr[at]=DirectX::PackedVector::XMConvertFloatToHalf(value);ldr[at]=static_cast<unsigned char>(std::lround(value*255.f));
                }
                e.hdr[phase]=texture(device_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE,hdr.data(),width*8);
                e.tone[phase]=texture(device_.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE,ldr.data(),width*4);
                checked(device_->CreateShaderResourceView(e.tone[phase].Get(),nullptr,&e.tone_srv[phase]),"tonemap SRV");
            }
            e.history=texture(device_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
            e.result=texture(device_.Get(),DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
            const auto format=quality_?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R16G16B16A16_FLOAT;
            e.output=quality_?texture(device_.Get(),format,D3D11_BIND_RENDER_TARGET):e.result;
            e.readback=texture(device_.Get(),format,0,nullptr,0,true);
            if(quality_)checked(device_->CreateRenderTargetView(e.output.Get(),nullptr,&e.rtv),"eye RTV");
            else {Ptr<ID3D11UnorderedAccessView> view;checked(device_->CreateUnorderedAccessView(e.output.Get(),nullptr,&view),"initial output UAV");const float sentinel[]{-8,-8,-8,-8};immediate_->ClearUnorderedAccessViewFloat(view.Get(),sentinel);}
            if(quality_){const float sentinel[]{.02f,.02f,.02f,1};immediate_->ClearRenderTargetView(e.rtv.Get(),sentinel);}
            for(unsigned phase=0;phase<2;++phase){auto& b=bindings_[eye][phase];
                ID3D11Texture2D* inputs[]{e.history.Get(),motion_.Get(),e.hdr[phase].Get(),nullptr,e.history.Get(),e.result.Get()};
                for(unsigned i=0;i<6;++i)if(inputs[i]){b.res[i]=inputs[i];b.res[i]->AddRef();inputs[i]->GetDesc(&b.tex[i]);b.is_tex[i]=true;}
                for(unsigned i=0;i<3;++i)checked(device_->CreateShaderResourceView(inputs[i],nullptr,&b.srv[i]),"TAA SRV");
                D3D11_BUFFER_DESC exposure_desc{};exposure_desc.ByteWidth=16;exposure_desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
                exposure_desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;exposure_desc.StructureByteStride=4;
                Ptr<ID3D11Buffer> exposure;checked(device_->CreateBuffer(&exposure_desc,nullptr,&exposure),"exposure buffer");
                checked(device_->CreateShaderResourceView(exposure.Get(),nullptr,&b.srv[3]),"exposure SRV");
                for(unsigned i=0;i<2;++i)checked(device_->CreateUnorderedAccessView(inputs[i+4],nullptr,&b.uav[i]),"TAA UAV");
                D3D11_BUFFER_DESC cb{};cb.ByteWidth=800;cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER;checked(device_->CreateBuffer(&cb,nullptr,&b.cb[0]),"view buffer");
            }
        }
    }
    std::vector<unsigned char> read(unsigned eye){
        auto& e=eyes_[eye];immediate_->CopyResource(e.readback.Get(),e.output.Get());drain(device_.Get(),immediate_.Get());
        // DXVK may finish the EVENT before the staging allocation is host-map-ready. Keep the
        // standalone readback bounded, retrying only WAS_STILL_DRAWING; never accept old pixels.
        D3D11_MAPPED_SUBRESOURCE map{};const auto deadline=GetTickCount64()+10000;unsigned retries=0;
        for(;;){const auto hr=immediate_->Map(e.readback.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);
            if(hr!=DXGI_ERROR_WAS_STILL_DRAWING){checked(hr,"completed readback");break;}
            if(GetTickCount64()>=deadline){std::cerr<<"FAIL: staging Map timed out; retaining owners until process exit\n";ExitProcess(2);}
            if(!retries++)immediate_->Flush();Sleep(1);
        }
        if(retries)std::cout<<"readback eye="<<eye<<" map_retries="<<retries<<'\n';
        const UINT bytes=width*(quality_?4:8);require(map.pData && map.RowPitch>=bytes,"invalid staging layout");
        std::vector<unsigned char> result(size_t(bytes)*height);
        for(UINT y=0;y<height;++y)std::memcpy(result.data()+size_t(y)*bytes,static_cast<const unsigned char*>(map.pData)+size_t(y)*map.RowPitch,bytes);
        immediate_->Unmap(e.readback.Get(),0);return result;
    }
    void verify(unsigned eye,const std::vector<unsigned char>& pixels,unsigned phase,bool reset){
        std::array<double,3> sums{};std::array<double,2> bands{};size_t count=0;
        for(UINT y=32;y<height-32;++y)for(UINT x=32;x<width-32;++x){
            for(unsigned c=0;c<3;++c){const auto index=(size_t(y)*width+x)*4+c;float value{};
                if(quality_)value=float(pixels[index])/255.f;
                else {std::uint16_t half{};std::memcpy(&half,pixels.data()+index*2,2);value=DirectX::PackedVector::XMConvertHalfToFloat(half);}
                require(std::isfinite(value),"nonfinite temporal output");sums[c]+=value;if(c==eye)bands[x<width/2?0:1]+=value;
            }++count;
        }
        for(auto& value:sums)value/=double(count);
        require(sums[eye]>.25 && sums[eye]>sums[1-eye]*1.8 && sums[eye]>sums[2]*1.8,"empty/stale/wrong-eye temporal output");
        if(reset)require((phase?bands[1]-bands[0]:bands[0]-bands[1])/double(count)>.06,"reset image retained the previous spatial pattern");
        std::cout<<"frame="<<frame_<<" eye="<<eye<<" mean_rgb="<<sums[0]<<','<<sums[1]<<','<<sums[2]<<" reset="<<reset<<'\n';
    }
    Ptr<ID3D11CommandList> record(unsigned eye,unsigned phase,bool duplicate=false){
        auto& b=bindings_[eye][phase];deferred_->ClearDepthStencilView(dsv_.Get(),D3D11_CLEAR_DEPTH,.5f,0);
        require(seam_->replace(deferred_.Get(),static_cast<int>(eye),b,1.5f,0,0),"production TAA seam declined");
        if(duplicate)require(!seam_->replace(deferred_.Get(),static_cast<int>(1-eye),bindings_[1-eye][phase],1.5f,0,0),"duplicate native split must decline");
        if(quality_){auto* rtv=eyes_[eye].rtv.Get();deferred_->OMSetRenderTargets(1,&rtv,nullptr);auto* srv=eyes_[eye].tone_srv[phase].Get();
            deferred_->PSSetShaderResources(0,1,&srv);seam_->last_vfov[eye]=1.5f;seam_->last_jitter_x[eye]=seam_->last_jitter_y[eye]=0;
            seam_->observe_post_tonemap_bind(deferred_.Get(),&srv,1);seam_->post_tonemap_end(static_cast<int>(eye),deferred_.Get());}
        Ptr<ID3D11CommandList> list;checked(deferred_->FinishCommandList(TRUE,&list),"finish temporal remainder");seam_->finished(deferred_.Get(),list.Get());return list;
    }
    void diagnostics(const char* stage){
        std::cout<<"seam stage="<<stage<<" output="<<width<<'x'<<height<<" fraction="<<seam_->fraction
            <<" FL=0x"<<std::hex<<static_cast<unsigned>(device_->GetFeatureLevel())<<std::dec
            <<" enabled="<<seam_->enabled<<" ready="<<seam_->ready<<" context_builds="<<seam_->context_builds
            <<" not_ready="<<seam_->not_ready<<" failures="<<seam_->failures<<" create_failures="<<seam_->create_failures
            <<" dlss_state="<<seam_->dlss_state<<" dlss_reason="<<d2vr::DlssEyeUpscaler::reason_name(static_cast<d2vr::DlssEyeUpscaler::Reason>(seam_->dlss_last_reason.load()))
            <<" dlss_result="<<seam_->dlss_last_result<<" dlss_features="<<seam_->dlss_features_created<<'\n';
        for(const unsigned input:{1u,2u,5u}){const auto& d=bindings_[0][0].tex[input];
            std::cout<<"input="<<input<<" size="<<d.Width<<'x'<<d.Height<<" format="<<d.Format<<" binds="<<d.BindFlags<<" samples="<<d.SampleDesc.Count<<'\n';}
        std::cout.flush();
    }
    void submit(ID3D11CommandList* list){require(seam_->execute(immediate_.Get(),list,FALSE,&execute),"production split execution");drain(device_.Get(),immediate_.Get());}
    void fresh(unsigned eye,unsigned phase,bool reset=false){
        const auto mine=seam_->eye_dispatches[eye].load(),other=seam_->eye_dispatches[1-eye].load(),evaluations=seam_->dlss_evaluations.load();
        auto list=record(eye,phase);submit(list.Get());
        require(seam_->eye_dispatches[eye]==mine+1 && seam_->eye_dispatches[1-eye]==other,"alternating dispatch touched other eye history");
        require(seam_->eye_upscaler_generation[eye]==(dlss_?d2vr::dlss_mode::generation:3u),"wrong backend / silent fallback");
        if(dlss_)require(seam_->dlss_evaluations==evaluations+1 && seam_->dlss_fallbacks==bootstrap_fallbacks_,"requested DLSS was not evaluated");
        if(dlss_ && quality_){
            require(seam_->eye_render_width[eye]==static_cast<UINT>(std::lround(width*seam_->fraction.load())) &&
                seam_->eye_render_height[eye]==static_cast<UINT>(std::lround(height*seam_->fraction.load())),"Quality ignored NGX render dimensions");
            require(seam_->dlss_features_created>0 && (!optimal_rebuild_ || seam_->dlss_features_created>bootstrap_features_),"Quality did not rebuild features for adopted optimal size");
        }
        auto current=read(eye);verify(eye,current,phase,reset);
        require(read(1-eye)==retained_[1-eye],"retained opposite-eye output changed");retained_[eye]=std::move(current);++frame_;
    }
public:
    Fixture(ID3D11Device* device,ID3D11DeviceContext* context,bool quality,bool dlss,const std::wstring& provider,const std::wstring& cache)
        :device_(device),immediate_(context),quality_(quality),dlss_(dlss){
        checked(device_->CreateDeferredContext(0,&deferred_),"deferred context");resources();drain(device_.Get(),immediate_.Get());
        for(unsigned eye=0;eye<2;++eye)retained_[eye]=read(eye);
        seam_->enabled=true;seam_->fraction=quality_?.67f:1.f;seam_->fsr4_active=false;
        if(dlss_){seam_->dlss_active=true;seam_->dlss_quality=quality_;seam_->dlss_provider_directory=provider;seam_->dlss_cache_directory=cache;}
        seam_->observe_depth(dsv_.Get());require(!seam_->replace(deferred_.Get(),0,bindings_[0][0],1.5f,0,0),"cold seam must defer until prepared");
        diagnostics("before-prepare");seam_->prepare(device_.Get());diagnostics("after-prepare");
        require(seam_->ready,"temporal contexts unavailable");d2vr::TaaDispatchHook::finish_original=&finish;
        if(dlss_ && quality_){
            // The first real production dispatch queries NGX. Adopt its answer exactly as the renderer does,
            // then require subsequent feature creation/recreation and every measured eye to use that size.
            auto bootstrap=record(0,0);submit(bootstrap.Get());diagnostics("NGX-bootstrap");
            require(seam_->dlss_optimal_queries>0 && seam_->dlss_optimal_width>0 && seam_->dlss_optimal_height>0,"NGX optimal settings unavailable");
            const float optimal=seam_->dlss_quality_fraction.load();require(std::isfinite(optimal) && optimal>=.5f && optimal<.999f,"NGX Quality fraction invalid");
            bootstrap_features_=seam_->dlss_features_created.load();optimal_rebuild_=seam_->fraction.load()!=optimal && bootstrap_features_>0;
            seam_->fraction=optimal;seam_->prepare(device_.Get());require(seam_->ready,"optimal render dimensions not prepared");
            bootstrap_fallbacks_=seam_->dlss_fallbacks.load();retained_[0]=read(0);
            std::cout<<"NGX optimal="<<seam_->dlss_optimal_width<<'x'<<seam_->dlss_optimal_height<<" adopted_fraction="<<optimal
                <<" bootstrap_features="<<bootstrap_features_<<" bootstrap_fallbacks="<<bootstrap_fallbacks_<<'\n';
        }
    }
    void run(){
        for(unsigned frame=0;frame<12;++frame)fresh(frame&1u,(frame/2)&1u,frame<2);
        const auto dispatches=seam_->replaced.load();seam_->request_reset();
        require(read(0)==retained_[0] && read(1)==retained_[1] && seam_->replaced==dispatches,"skipped frame altered kept eyes / dispatched work");
        const std::array<std::uint64_t,2> resets{seam_->eye_resets[0].load(),seam_->eye_resets[1].load()};
        auto queued=record(0,0,!quality_);seam_->request_reset();const auto stale=seam_->stale_jobs.load();submit(queued.Get());
        require(seam_->stale_jobs==stale+1 && seam_->replaced==dispatches,"queued old generation evaluated after reset");
        require(read(1)==retained_[1],"stale queued job touched retained eye");
        if(quality_)require(read(0)==retained_[0],"revoked upscale job changed retained output");else {retained_[0]=read(0);verify(0,retained_[0],0,true);}
        fresh(1,0,true);require(seam_->eye_resets[1]==resets[1]+1 && seam_->eye_resets[0]==resets[0],"first resumed eye consumed other eye reset");
        fresh(0,0,true);require(seam_->eye_resets[0]==resets[0]+1,"second resumed eye lost pending reset");
        require(seam_->failures==0 && seam_->create_failures==0 && seam_->dlss_evaluate_failures==0,"temporal GPU failure");
        std::cout<<"PASS: alternating production seam; divergent GPU pixels, unchanged kept eye, skipped frame, stale queued job, independent reset recovery; synthetic only\n";
        drain(device_.Get(),immediate_.Get());seam_->clear();
    }
};
} // namespace
int wmain(int argc,wchar_t** argv){
    try {
        std::wstring mode,provider,cache;
        for(int i=1;i<argc;++i){require(i+1<argc,"argument requires value");const std::wstring arg=argv[i];
            if(arg==L"--mode")mode=argv[++i];else if(arg==L"--provider-directory")provider=argv[++i];else if(arg==L"--cache-directory")cache=argv[++i];else require(false,"unknown argument");}
        const bool dlss=mode==L"dlaa" || mode==L"dlss-quality",quality=mode==L"fsr3-quality" || mode==L"dlss-quality";
        require(dlss || mode==L"fsr3-native" || mode==L"fsr3-quality","mode: fsr3-native / fsr3-quality / dlaa / dlss-quality");
        require(!dlss || (std::filesystem::path(provider).is_absolute() && std::filesystem::path(cache).is_absolute()),"DLSS requires explicit absolute provider/cache directories (normal production validation applies)");
        Ptr<IDXGIFactory1> factory;checked(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");Ptr<IDXGIAdapter1> adapter;
        for(UINT i=0;;++i){Ptr<IDXGIAdapter1> candidate;const auto hr=factory->EnumAdapters1(i,&candidate);if(hr==DXGI_ERROR_NOT_FOUND)break;checked(hr,"adapter enumeration");
            DXGI_ADAPTER_DESC1 desc{};checked(candidate->GetDesc1(&desc),"adapter description");if(desc.VendorId==0x10de && !(desc.Flags&(DXGI_ADAPTER_FLAG_SOFTWARE|DXGI_ADAPTER_FLAG_REMOTE))){adapter=candidate;std::wcout<<L"adapter="<<desc.Description<<L" mode="<<mode<<L'\n';break;}}
        require(bool(adapter),"NVIDIA hardware unavailable; no software fallback");Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;
        const D3D_FEATURE_LEVEL wanted=D3D_FEATURE_LEVEL_11_1;D3D_FEATURE_LEVEL actual{};
        checked(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,&wanted,1,D3D11_SDK_VERSION,&device,&actual,&context),"NVIDIA hardware FL11.1 device");
        require(actual>=D3D_FEATURE_LEVEL_11_1,"production FSR seam requires FL11.1");
        Fixture fixture(device.Get(),context.Get(),quality,dlss,provider,cache);fixture.run();return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL/unavailable: "<<error.what()<<'\n';return 1;}
}
