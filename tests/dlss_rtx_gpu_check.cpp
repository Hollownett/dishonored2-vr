#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dlss_eye_upscaler.hpp"
#include "d3d11_temporal_owner.hpp"
#include <dxgi1_2.h>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using Upscaler=d2vr::DlssEyeUpscaler;
using Owner=d2vr::D3D11TemporalOwner;
namespace {
constexpr UINT width=1024,height=768;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
void check(HRESULT result,const char* message) {
    if(FAILED(result)) {std::cerr<<message<<" HRESULT=0x"<<std::hex<<static_cast<unsigned>(result)<<std::dec<<'\n';throw std::runtime_error(message);}
}
const char* reason_name(Upscaler::Reason reason) noexcept {
#define REASON(name) case Upscaler::Reason::name:return #name
    switch(reason) {
        REASON(none);REASON(disabled);REASON(invalid_calls);REASON(invalid_paths);REASON(missing_provider);REASON(provider_signature);
        REASON(cache_unwritable);REASON(provider_selection_unverified);REASON(coexistence_unsupported);REASON(invalid_context);
        REASON(wrong_device);REASON(invalid_dimensions);REASON(invalid_conventions);REASON(registry_full);REASON(owner_conflict);
        REASON(retry_suppressed);REASON(init_failed);REASON(capability_failed);REASON(capability_query_failed);REASON(unavailable);
        REASON(driver_update);REASON(allocate_failed);REASON(create_failed);REASON(duplicate_history);REASON(invalid_resource);
        REASON(stale_generation);REASON(retired);REASON(evaluate_failed);
    }
#undef REASON
    return "unknown";
}
void report(const char* phase,const Upscaler::Status& status) {
    std::cout<<phase<<" requested=DLAA reason="<<reason_name(status.reason)<<" reason_id="<<static_cast<unsigned>(status.reason)
        <<" eye="<<status.eye<<" result=0x"<<std::hex<<static_cast<unsigned>(status.result)
        <<" cleanup=0x"<<static_cast<unsigned>(status.cleanup_result)<<" feature_init=0x"<<static_cast<unsigned>(status.feature_init_result)
        <<" feature_init_query=0x"<<static_cast<unsigned>(status.feature_init_query_result)
        <<" driver_major_query=0x"<<static_cast<unsigned>(status.driver_major_result)
        <<" driver_minor_query=0x"<<static_cast<unsigned>(status.driver_minor_result)<<std::dec
        <<" min_driver="<<status.driver_major<<'.'<<status.driver_minor<<" provider_verified="<<status.observed_provider_verified<<'\n';
    std::wcout<<L"expected_provider="<<status.expected_provider<<L" observed_provider="<<status.observed_provider<<L'\n';
}
// Standalone-test last resort only. Preserve every outstanding strong owner until
// the OS tears down THIS process; never pretend timeout/query error is completion.
[[noreturn]] void unproved_exit(const char* message) noexcept {
    std::cerr<<"FAIL: unproved GPU retirement: "<<message<<"; exiting standalone test without C++ owner teardown\n"<<std::flush;
    ExitProcess(2);
}
float half_float(std::uint16_t value) noexcept {
    const unsigned exponent=(value>>10)&31u,mantissa=value&1023u;
    const float sign=(value&0x8000u)?-1.f:1.f;
    if(exponent==31)return mantissa?std::numeric_limits<float>::quiet_NaN():sign*std::numeric_limits<float>::infinity();
    if(!exponent)return sign*std::ldexp(static_cast<float>(mantissa),-24);
    return sign*std::ldexp(1.f+static_cast<float>(mantissa)/1024.f,static_cast<int>(exponent)-15);
}
struct Surfaces {
    std::array<std::array<ComPtr<ID3D11Texture2D>,2>,2> colour;
    std::array<ComPtr<ID3D11Texture2D>,2> output,readback;
    std::array<ComPtr<ID3D11UnorderedAccessView>,2> output_uav;
    ComPtr<ID3D11Texture2D> depth,motion;
    static ComPtr<ID3D11Texture2D> texture(ID3D11Device* device,DXGI_FORMAT format,UINT binds,const void* data,UINT pitch,bool staging=false) {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=format;desc.SampleDesc.Count=1;desc.Usage=staging?D3D11_USAGE_STAGING:D3D11_USAGE_DEFAULT;
        desc.BindFlags=staging?0:binds;desc.CPUAccessFlags=staging?D3D11_CPU_ACCESS_READ:0;
        const D3D11_SUBRESOURCE_DATA initial{data,pitch,0};ComPtr<ID3D11Texture2D> result;
        check(device->CreateTexture2D(&desc,data?&initial:nullptr,&result),"synthetic texture");return result;
    }
    explicit Surfaces(ID3D11Device* device) {
        for(unsigned eye=0;eye<2;++eye) {
            for(unsigned source=0;source<2;++source) {
                std::vector<std::array<std::uint16_t,4>> pixels(static_cast<std::size_t>(width)*height);
                for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
                    // Exactly representable half values: dominant 1 or .75; other .125; blue .0625.
                    const std::uint16_t dominant=(((x/32+y/32+source)&1u)!=0)?std::uint16_t(0x3a00):std::uint16_t(0x3c00);
                    pixels[static_cast<std::size_t>(y)*width+x]=eye==0?
                        std::array<std::uint16_t,4>{dominant,0x3000,0x2c00,0x3c00}:
                        std::array<std::uint16_t,4>{0x3000,dominant,0x2c00,0x3c00};
                }
                colour[eye][source]=texture(device,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE,pixels.data(),width*8);
            }
            output[eye]=texture(device,DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,nullptr,0);
            check(device->CreateUnorderedAccessView(output[eye].Get(),nullptr,&output_uav[eye]),"synthetic output UAV");
            readback[eye]=texture(device,DXGI_FORMAT_R16G16B16A16_FLOAT,0,nullptr,0,true);
        }
        std::vector<float> depths(static_cast<std::size_t>(width)*height,.5f);
        std::vector<std::array<std::uint16_t,2>> vectors(static_cast<std::size_t>(width)*height,{0,0});
        depth=texture(device,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_SHADER_RESOURCE,depths.data(),width*4);
        motion=texture(device,DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE,vectors.data(),width*4);
    }
    void verify(ID3D11DeviceContext* context,unsigned eye,unsigned frame) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(readback[eye].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped),"proven-complete synthetic readback");
        struct Unmap {ID3D11DeviceContext* context;ID3D11Resource* resource;~Unmap(){context->Unmap(resource,0);}} unmap{context,readback[eye].Get()};
        require(mapped.pData && mapped.RowPitch>=width*8,"readback row pitch");
        std::array<double,3> sums{};std::size_t untouched=0;
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
            std::array<std::uint16_t,4> pixel{};
            const auto* row=static_cast<const unsigned char*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch;
            std::memcpy(pixel.data(),row+static_cast<std::size_t>(x)*8,8);
            bool sentinel=true;
            for(unsigned channel=0;channel<4;++channel) {
                const float v=half_float(pixel[channel]);require(std::isfinite(v),"nonfinite NGX output pixel");
                sentinel=sentinel && v==-8.f;
                if(channel<3)sums[channel]+=v;
            }
            if(sentinel)++untouched;
        }
        const double pixels=static_cast<double>(width)*height;
        for(auto& value:sums)value/=pixels;
        require(untouched==0,"untouched output sentinel pixels");
        require(sums[eye]>.2 && sums[eye]>sums[1-eye]*1.5 && sums[eye]>sums[2]*1.5,"wrong-eye or empty synthetic output");
        std::cout<<"SYNTHETIC frame="<<frame<<" eye="<<eye<<" finite_pixels="<<width*height<<" untouched="<<untouched
            <<" mean_rgb="<<sums[0]<<','<<sums[1]<<','<<sums[2]<<'\n';
    }
};
struct Payload {std::shared_ptr<Surfaces> surfaces;Upscaler::Lease feature;};
class Session {
    std::optional<Owner> owner_;
    Owner::DrainOwner drain_;
    std::optional<Owner::Ticket> pending_;
    bool closed_{};
    void complete() {
        if(!pending_)return;
        context->Flush(); // Standalone test submission only; production poll never flushes.
        const auto start=GetTickCount64();
        for(;;) {
            if(drain_.poll()!=Owner::Status::ok)unproved_exit("owner poll refused");
            if(auto done=drain_.take_completed()) {
                if(done->ticket.owner!=pending_->owner || done->ticket.serial!=pending_->serial)unproved_exit("unexpected completion ticket");
                pending_.reset();
                std::cout<<"EVENT completion query=0x"<<std::hex<<static_cast<unsigned>(done->query_result)
                    <<" device=0x"<<static_cast<unsigned>(done->device_result)<<std::dec<<" abandoned="<<done->device_abandoned<<'\n';
                require(!done->device_abandoned,"actual device removal; synthetic hardware test failed");return;
            }
            if(drain_.pending().quarantined)unproved_exit("query error on still-unproven device");
            if(GetTickCount64()-start>=15000)unproved_exit("EVENT timeout is not completion");
            Sleep(1);
        }
    }
public:
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    Upscaler backend; // Default production constructor ONLY: real NativeValidation and official SDK calls.
    std::shared_ptr<Payload> payload;
    Session(ID3D11Device* d,ID3D11DeviceContext* c):device(d),context(c) {
        Owner::Result result;owner_=Owner::prepare(d,c,result);
        require(owner_ && result.status==Owner::Status::ok,"actual Device1/Context1 state/query owner unavailable");
        drain_=owner_->drain_owner();
        payload=std::make_shared<Payload>();payload->surfaces=std::make_shared<Surfaces>(d);
        std::cout<<"temporal_owner feature_level=0x"<<std::hex<<owner_->feature_level()<<" state_flags=0x"<<owner_->context_state_flags()<<std::dec<<'\n';
    }
    ~Session() noexcept {try {finish();}catch(...) {unproved_exit("exception in standalone cleanup");}}
    template<class Work> Owner::Status submit(Work&& work) {
        require(!pending_,"serial test must drain every earlier use");
        Owner::Status status;
        const Owner::Lease generation=payload->feature?payload->feature.retained_owner():Owner::Lease(payload);
        const auto ticket=owner_->reserve(payload,generation,status);
        require(ticket && status==Owner::Status::ok,"reserve before synthetic GPU work");pending_=ticket;
        const auto result=owner_->record(*ticket,std::forward<Work>(work));
        if(result!=Owner::Status::ok && result!=Owner::Status::callback_failed && result!=Owner::Status::callback_exception) {
            if(owner_->cancel(*ticket)!=Owner::Status::ok)unproved_exit("refused recording could not cancel reservation");
            pending_.reset();throw std::runtime_error("recording admission refused");
        }
        complete();return result;
    }
    void finish() {
        if(closed_)return;
        complete(); // All success/failure submissions must prove complete before generation retirement.
        backend.disable();
        if(payload && payload->feature) {
            require(Upscaler::confirm_gpu_retirement(payload->feature),"all-submission generation retirement refused");
            payload->feature={};Upscaler::collect_retired();
        }
        report("shutdown",backend.status());
        if(owner_)require(owner_->close()==Owner::Status::ok,"close drained primitive");
        require(drain_.can_destroy() && drain_.can_unload(),"standalone drain is not empty");
        payload.reset();owner_.reset();drain_={};closed_=true;
        require(backend.status().cleanup_result==NVSDK_NGX_Result_Success,"SDK cleanup reported a failure");
    }
};
Upscaler::Desc description() {
    Upscaler::Desc desc;desc.width=width;desc.height=height;
    desc.device_generation=desc.dimension_generation=desc.feature_generation=1;
    desc.colour=desc.output=Upscaler::Format::rgba16_float;
    desc.motion={1,1,false,temporal_eye::MotionDirection::current_to_previous,temporal_eye::MotionUnits::pixels,temporal_eye::MotionResolution::active_render};
    desc.colour_contract={temporal_eye::ColourDomain::native_scene,temporal_eye::ColourEncoding::linear,
        temporal_eye::ExposureSource::fixed,true,false,1,1,{},{}};
    desc.inverted_depth=true;desc.mv_low_resolution=false;desc.jitter={0,0,0,0,0,32,true};return desc;
}
void suite(ID3D11Device* device,ID3D11DeviceContext* context,const Upscaler::Paths& paths) {
    Session session(device,context);const auto desc=description();bool prepared=false;
    const auto init=session.submit([&](Owner::Recording& recording) {
        return recording.isolate([&](ID3D11DeviceContext1*) {
            prepared=session.backend.prepare(device,context,desc,paths);
            if(prepared)session.payload->feature=session.backend.lease();
            return prepared;
        })==Owner::Status::ok;
    });
    report("prepare",session.backend.status());
    require(init==Owner::Status::ok && prepared && session.backend.ready() && session.payload->feature,"production paired DLAA prepare unavailable");
    for(unsigned frame=1;frame<=4;++frame) {
        const bool changed=frame==3;const unsigned source=frame>=3?1u:0u;
        if(changed)Upscaler::reset_histories(session.payload->feature);
        std::array<Upscaler::Status,2> statuses;std::array<bool,2> evaluated{};
        const auto recorded=session.submit([&](Owner::Recording& recording) {
            return recording.isolate([&](ID3D11DeviceContext1* isolated) {
                const FLOAT sentinel[]{-8,-8,-8,-8};
                for(unsigned eye=0;eye<2;++eye) {
                    auto& resources=*session.payload->surfaces;
                    isolated->ClearUnorderedAccessViewFloat(resources.output_uav[eye].Get(),sentinel);
                    Upscaler::Eval eval;eval.eye=eye;eval.frame=eval.pair=frame;eval.device_generation=eval.dimension_generation=eval.feature_generation=1;
                    eval.context=context;eval.colour=resources.colour[eye][source].Get();eval.depth=resources.depth.Get();
                    eval.motion=resources.motion.Get();eval.output=resources.output[eye].Get();eval.jitter={0,0,0,0,frame%32,32,frame==1 || changed};
                    eval.motion_contract=desc.motion;eval.colour_contract=desc.colour_contract;eval.frame_time_ms=16.6667f;eval.discontinuity=changed;
                    eval.scratch_owner=session.payload->surfaces;
                    evaluated[eye]=Upscaler::evaluate(session.payload->feature,eval,&statuses[eye]);
                    // Readback is queued even on a failed SDK return; marker still protects partial work/resources.
                    isolated->CopyResource(resources.readback[eye].Get(),resources.output[eye].Get());
                }
                return evaluated[0] && evaluated[1];
            })==Owner::Status::ok;
        });
        for(unsigned eye=0;eye<2;++eye)report(eye==0?"evaluate-left":"evaluate-right",statuses[eye]);
        require(recorded==Owner::Status::ok && evaluated[0] && evaluated[1],"real NGX evaluate did not succeed for both eyes");
        if(changed)std::cout<<"SYNTHETIC paired source textures switched; both histories explicitly reset/discontinuous\n";
        for(unsigned eye=0;eye<2;++eye)session.payload->surfaces->verify(context,eye,frame);
    }
    session.finish();
}
Upscaler::Paths arguments(int argc,wchar_t** argv) {
    Upscaler::Paths paths;
    for(int i=1;i<argc;++i) {
        const std::wstring option=argv[i];require(i+1<argc,"option requires an explicit absolute directory");
        if(option==L"--provider-directory" && paths.provider_directory.empty())paths.provider_directory=argv[++i];
        else if(option==L"--cache-directory" && paths.cache_directory.empty())paths.cache_directory=argv[++i];
        else throw std::runtime_error("unknown or duplicate CLI option");
    }
    require(!paths.provider_directory.empty() && !paths.cache_directory.empty(),"required: --provider-directory ABSOLUTE --cache-directory ABSOLUTE");
    return paths;
}
} // namespace
int wmain(int argc,wchar_t** argv) {
    try {
        const auto paths=arguments(argc,argv);
        std::cout<<"SYNTHETIC paired DLAA/NGX check only; no native D2 input, stereo presentation or headset proof\n";
        std::wcout<<L"requested_provider_directory="<<paths.provider_directory<<L" cache_directory="<<paths.cache_directory<<L'\n';
        ComPtr<IDXGIFactory1> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");
        ComPtr<IDXGIAdapter1> adapter;DXGI_ADAPTER_DESC1 selected{};
        for(UINT index=0;;++index) {
            ComPtr<IDXGIAdapter1> candidate;const auto hr=factory->EnumAdapters1(index,&candidate);if(hr==DXGI_ERROR_NOT_FOUND)break;check(hr,"DXGI enumeration");
            DXGI_ADAPTER_DESC1 desc{};check(candidate->GetDesc1(&desc),"adapter desc");
            if(desc.VendorId==0x10de && !(desc.Flags&(DXGI_ADAPTER_FLAG_SOFTWARE|DXGI_ADAPTER_FLAG_REMOTE))) {adapter=candidate;selected=desc;break;}
        }
        require(bool(adapter),"explicit NVIDIA hardware unavailable; no WARP/software/remote fallback");
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL actual_level{};
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
        check(D3D11CreateDevice(adapter.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,0,levels,2,D3D11_SDK_VERSION,&device,&actual_level,&context),"explicit NVIDIA D3D11 device");
        ComPtr<IDXGIDevice> dxgi;ComPtr<IDXGIAdapter> actual_adapter;DXGI_ADAPTER_DESC actual{};
        check(device.As(&dxgi),"actual DXGI device");check(dxgi->GetAdapter(&actual_adapter),"actual adapter");check(actual_adapter->GetDesc(&actual),"actual adapter desc");
        require(actual.VendorId==0x10de && actual.DeviceId==selected.DeviceId && actual.AdapterLuid.HighPart==selected.AdapterLuid.HighPart && actual.AdapterLuid.LowPart==selected.AdapterLuid.LowPart,"created device differs from selected NVIDIA LUID");
        std::wcout<<L"adapter="<<actual.Description<<L" vendor=0x"<<std::hex<<actual.VendorId<<L" device=0x"<<actual.DeviceId
            <<L" LUID="<<actual.AdapterLuid.HighPart<<L":"<<actual.AdapterLuid.LowPart<<L" feature_level=0x"<<actual_level
            <<L" creation_flags=0x"<<device->GetCreationFlags()<<std::dec<<L" VRAM="<<actual.DedicatedVideoMemory<<L'\n';
        suite(device.Get(),context.Get(),paths);
        std::cout<<"PASS: four paired SYNTHETIC DLAA frames evaluated and read back on explicit NVIDIA hardware; all generation uses GPU-drained.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL/unavailable: "<<error.what()<<'\n';return 1;}
}
