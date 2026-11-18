#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dlss_eye_upscaler.hpp"
#include "d3d11_temporal_owner.hpp"
#include "foveated_upscale.hpp"
#include "canted_fov.hpp"
#include "visibility_mask.hpp"
#include <dxgi1_2.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
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
const char* reason_name(Upscaler::Reason reason) noexcept {return Upscaler::reason_name(reason);}
void report(const char* phase,const Upscaler::Status& status,const char* requested="DLAA") {
    std::cout<<phase<<" requested="<<requested<<" reason="<<reason_name(status.reason)<<" reason_id="<<static_cast<unsigned>(status.reason)
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
// R11G11B10_FLOAT words from per-channel exponents, zero mantissa (11-bit and 10-bit floats, bias 15: 15 = 1.0, 14 = .5).
constexpr std::uint32_t r11g11b10(unsigned r_exp,unsigned g_exp,unsigned b_exp) {return (r_exp<<6)|((g_exp<<6)<<11)|((b_exp<<5)<<22);}
// Per-eye textures shaped like the mod's FsrTaa eye state (src/fsr_taa.hpp prepare(), max_fraction 0 so the context is
// the render size): upscale = Eye::colour_in (render-size RGBA8 tonemapped copy), Eye::depth (render-size R32F copy),
// the engine's display-size R16G16F motion target, Eye::upscaled (display-size RGBA8 UAV); native DLAA = the
// display-size R11G11B10 pre-TAA colour and output.
struct ModSurfaces {
    UINT w{},h{},rw{},rh{};bool upscale{};DXGI_FORMAT format{};
    std::array<ComPtr<ID3D11Texture2D>,2> colour,output,readback;
    std::array<ComPtr<ID3D11UnorderedAccessView>,2> output_uav;
    ComPtr<ID3D11Texture2D> depth,motion;
    static ComPtr<ID3D11Texture2D> texture(ID3D11Device* device,UINT w,UINT h,DXGI_FORMAT format,UINT binds,const void* data,UINT pitch,bool staging=false) {
        D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;
        desc.Format=format;desc.SampleDesc.Count=1;desc.Usage=staging?D3D11_USAGE_STAGING:D3D11_USAGE_DEFAULT;
        desc.BindFlags=staging?0:binds;desc.CPUAccessFlags=staging?D3D11_CPU_ACCESS_READ:0;
        const D3D11_SUBRESOURCE_DATA initial{data,pitch,0};ComPtr<ID3D11Texture2D> result;
        check(device->CreateTexture2D(&desc,data?&initial:nullptr,&result),"mod-shaped texture");return result;
    }
    static constexpr std::uint32_t r11g11b10_magenta=r11g11b10(15,0,15); // (1,0,1): the cleared sentinel
    ModSurfaces(ID3D11Device* device,UINT w_,UINT h_,UINT rw_,UINT rh_,bool upscale_):w(w_),h(h_),rw(rw_),rh(rh_),upscale(upscale_) {
        format=upscale?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R11G11B10_FLOAT;
        const UINT cw=upscale?rw:w,ch=upscale?rh:h;
        for(unsigned eye=0;eye<2;++eye) {
            std::vector<std::uint32_t> pixels(static_cast<std::size_t>(cw)*ch);
            for(UINT y=0;y<ch;++y)for(UINT x=0;x<cw;++x) {
                const bool odd=((x/32+y/32+eye)&1u)!=0;
                pixels[static_cast<std::size_t>(y)*cw+x]=upscale?(odd?0xff1e3cc8u:0xff1ec83cu):(odd?r11g11b10(14,15,13):r11g11b10(15,14,13));
            }
            colour[eye]=texture(device,cw,ch,format,D3D11_BIND_SHADER_RESOURCE,pixels.data(),cw*4);
            output[eye]=texture(device,w,h,format,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,nullptr,0);
            check(device->CreateUnorderedAccessView(output[eye].Get(),nullptr,&output_uav[eye]),"mod-shaped output UAV");
            readback[eye]=texture(device,w,h,format,0,nullptr,0,true);
        }
        std::vector<float> depths(static_cast<std::size_t>(cw)*ch,.5f);
        std::vector<std::array<std::uint16_t,2>> vectors(static_cast<std::size_t>(w)*h,{0,0});
        depth=texture(device,cw,ch,DXGI_FORMAT_R32_FLOAT,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS,depths.data(),cw*4);
        motion=texture(device,w,h,DXGI_FORMAT_R16G16_FLOAT,D3D11_BIND_SHADER_RESOURCE,vectors.data(),w*4);
    }
    bool sentinel(std::uint32_t word) const noexcept {return upscale?word==0xffff00ffu:word==r11g11b10_magenta;}
    // Counts sentinel (never written) pixels inside and outside the expected written rectangle.
    std::pair<std::size_t,std::size_t> untouched(ID3D11DeviceContext* context,unsigned eye,UINT x0,UINT y0,UINT x1,UINT y1) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(readback[eye].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped),"proven-complete mod readback");
        struct Unmap {ID3D11DeviceContext* context;ID3D11Resource* resource;~Unmap(){context->Unmap(resource,0);}} unmap{context,readback[eye].Get()};
        std::size_t inside=0,outside=0;
        for(UINT y=0;y<h;++y) {
            const auto* row=reinterpret_cast<const std::uint32_t*>(static_cast<const unsigned char*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch);
            for(UINT x=0;x<w;++x) if(sentinel(row[x])) {if(x>=x0 && x<x1 && y>=y0 && y<y1) ++inside;else ++outside;}
        }
        return {inside,outside};
    }
};
struct Payload {std::shared_ptr<Surfaces> surfaces;Upscaler::Lease feature;std::shared_ptr<ModSurfaces> mod;};
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

// ---- Mod-shaped cases (plan item 1.1): the exact feature and evaluate parameters src/fsr_taa.hpp builds in the headset.
// Eye 0's runtime FOV tangents from the 2026-11-19 VDXR session status (left, right, up, down); eye 1 mirrors it.
constexpr double fov_tangents[2][4]{{-0.933,1.000,1.111,-1.192},{-1.000,0.933,1.111,-1.192}};
struct FoveaNdc {float cx{},cy{},hx{},hy{};};
// GameXr::update_fovea_ (src/game_xr.hpp): the optical centre and the half extents in the render frustum's NDC.
FoveaNdc fovea_ndc(unsigned eye,bool canted,double half_deg) {
    const float l=static_cast<float>(std::atan(fov_tangents[eye][0])),r=static_cast<float>(std::atan(fov_tangents[eye][1]));
    const float u=static_cast<float>(std::atan(fov_tangents[eye][2])),d=static_cast<float>(std::atan(fov_tangents[eye][3]));
    d2vr::RenderFrustum render{{0,0,0,1},std::tan(std::max(-l,r)),std::tan(std::max(u,-d))};
    if(canted) if(const auto c=d2vr::canted_fov(l,r,u,d)) render={c->rotation,std::tan(c->half_x),std::tan(c->half_y)};
    const auto centre=d2vr::visibility_mask_detail::project(render,0,0);
    const double t=std::tan(half_deg*0.017453292519943295);
    return {static_cast<float>(centre[0]/render.tan_x),static_cast<float>(centre[1]/render.tan_y),static_cast<float>(t/render.tan_x),static_cast<float>(t/render.tan_y)};
}
struct ModCase {
    std::string name;
    UINT w{},h{};double fraction{1};   // display size per eye; engine render fraction (1 = native DLAA)
    bool box{},subrects{},canted{true};// foveated box feature; InEnableOutputSubrects at create; canted render frusta
    UINT render_w{},render_h{};        // non-box override of the render size (0 = lround(w * fraction), as FsrTaa)
    bool expect_success{true};
};
struct ModGeometry {
    Upscaler::Desc desc;UINT rw{},rh{};
    std::array<Upscaler::Eval,2> offsets{}; // src/dst/output_region only
    std::array<d2vr::fovea::Box,2> boxes{};
};
// FsrTaa::prepare() (fovea boxes) + dlss_maintain_unguarded() (Desc) + dlss_dispatch_unguarded() (per-eye offsets).
ModGeometry mod_geometry(const ModCase& c) {
    ModGeometry g;const UINT w=c.w,h=c.h;const float f=static_cast<float>(c.fraction);
    const bool upscale=f<.999f;
    g.rw=upscale?(c.render_w?c.render_w:static_cast<UINT>(std::lround(w*f))):w;g.rh=upscale?(c.render_h?c.render_h:static_cast<UINT>(std::lround(h*f))):h;
    if(c.box) {
        const UINT frw=static_cast<UINT>(std::lround(w*f)),frh=static_cast<UINT>(std::lround(h*f));
        for(unsigned eye=0;eye<2;++eye) {
            const auto n=fovea_ndc(eye,c.canted,30.0);
            g.boxes[eye]=d2vr::fovea::box(w,h,frw,frh,n.cx,n.cy,n.hx,n.hy);
            require(g.boxes[eye].valid(),"mod fovea box invalid");
        }
    }
    auto& d=g.desc;
    d.width=w;d.height=h;d.render_width=g.rw;d.render_height=g.rh;
    if(c.box) {
        const auto& b=g.boxes[0];
        d.width=std::min(b.x.display,w);d.height=std::min(b.y.display,h);
        d.render_width=std::min<UINT>(static_cast<UINT>(std::lround(double(d.width)*g.rw/w)),g.rw);
        d.render_height=std::min<UINT>(static_cast<UINT>(std::lround(double(d.height)*g.rh/h)),g.rh);
        d.output_subrects=c.subrects;
    }
    d.quality=upscale?Upscaler::Quality::quality:Upscaler::Quality::dlaa;
    d.colour=d.output=upscale?Upscaler::Format::rgba8_unorm:Upscaler::Format::r11g11b10_float;
    const float mw=static_cast<float>(upscale?g.rw:w),mh=static_cast<float>(upscale?g.rh:h);
    d.motion={-1*mw,-1*mh,true,temporal_eye::MotionDirection::current_to_previous,temporal_eye::MotionUnits::uv,temporal_eye::MotionResolution::active_render};
    d.colour_contract=upscale?
        temporal_eye::ColourContract{temporal_eye::ColourDomain::post_tonemap,temporal_eye::ColourEncoding::perceptual_gamma,temporal_eye::ExposureSource::fixed,false,false,1,1,{},{}}:
        temporal_eye::ColourContract{temporal_eye::ColourDomain::native_scene,temporal_eye::ColourEncoding::linear,temporal_eye::ExposureSource::automatic,true,true,1,1,{},{}};
    d.inverted_depth=true;d.mv_low_resolution=upscale;
    d.jitter={0,0,0,0,0,32,false};
    d.device_generation=d.dimension_generation=d.feature_generation=1;
    for(unsigned eye=0;eye<2;++eye) if(c.box) {
        auto& ev=g.offsets[eye];const auto& e=g.boxes[eye];
        const double sx=double(g.rw)/w,sy=double(g.rh)/h;
        ev.dst_x=std::min(e.x.dst,w-d.width);ev.dst_y=std::min(e.y.dst,h-d.height);
        ev.src_x=std::min<unsigned>(static_cast<unsigned>(std::lround(ev.dst_x*sx)),g.rw-d.render_width);
        ev.src_y=std::min<unsigned>(static_cast<unsigned>(std::lround(ev.dst_y*sy)),g.rh-d.render_height);
        ev.output_region=true;
    }
    return g;
}
std::string hex(unsigned value) {std::ostringstream o;o<<"0x"<<std::hex<<value;return o.str();}
// One mod-shaped case: optimal settings for the display size (as FsrTaa queries them), the feature pair, then three
// paired frames. Returns true when every evaluate succeeded and the written region matches (box: only the box).
bool mod_case(ID3D11Device* device,ID3D11DeviceContext* context,const Upscaler::Paths& paths,const ModCase& c) {
    const auto g=mod_geometry(c);const auto& d=g.desc;
    const char* requested=d.quality==Upscaler::Quality::dlaa?"DLAA":"Quality";
    std::cout<<"\nCASE "<<c.name<<": display="<<c.w<<'x'<<c.h<<" fraction="<<c.fraction<<" render="<<g.rw<<'x'<<g.rh
        <<" feature render="<<d.render_width<<'x'<<d.render_height<<" target="<<d.width<<'x'<<d.height
        <<" quality="<<requested<<" box="<<c.box<<" output_subrects="<<d.output_subrects<<" canted="<<c.canted<<'\n';
    for(unsigned eye=0;eye<2 && c.box;++eye) {
        const auto& b=g.boxes[eye];const auto& o=g.offsets[eye];
        std::cout<<"  eye"<<eye<<" box dst="<<b.x.dst<<','<<b.y.dst<<" display="<<b.x.display<<'x'<<b.y.display<<" src="<<b.x.src<<','<<b.y.src
            <<" render="<<b.x.render<<'x'<<b.y.render<<" -> eval src="<<o.src_x<<','<<o.src_y<<" dst="<<o.dst_x<<','<<o.dst_y<<'\n';
    }
    Session session(device,context);
    session.payload->mod=std::make_shared<ModSurfaces>(device,c.w,c.h,g.rw,g.rh,d.quality!=Upscaler::Quality::dlaa);
    bool prepared=false;Upscaler::OptimalSettings full{},boxed{};Upscaler::Reason full_reason{},box_reason{};NVSDK_NGX_Result full_result{},box_result{};
    const auto init=session.submit([&](Owner::Recording& recording) {
        return recording.isolate([&](ID3D11DeviceContext1*) {
            full_reason=session.backend.optimal_settings(device,context,paths,1,c.w,c.h,Upscaler::Quality::quality,full,&full_result);
            if(c.box) box_reason=session.backend.optimal_settings(device,context,paths,1,d.width,d.height,Upscaler::Quality::quality,boxed,&box_result);
            prepared=session.backend.prepare(device,context,d,paths);
            if(prepared)session.payload->feature=session.backend.lease();
            return prepared;
        })==Owner::Status::ok;
    });
    std::cout<<"  optimal(display "<<c.w<<'x'<<c.h<<") reason="<<reason_name(full_reason)<<" result="<<hex(full_result)<<" optimal="<<full.optimal_width<<'x'<<full.optimal_height
        <<" min="<<full.min_width<<'x'<<full.min_height<<" max="<<full.max_width<<'x'<<full.max_height<<'\n';
    if(c.box) std::cout<<"  optimal(box "<<d.width<<'x'<<d.height<<") reason="<<reason_name(box_reason)<<" result="<<hex(box_result)<<" optimal="<<boxed.optimal_width<<'x'<<boxed.optimal_height
        <<" min="<<boxed.min_width<<'x'<<boxed.min_height<<" max="<<boxed.max_width<<'x'<<boxed.max_height<<'\n';
    report("  prepare",session.backend.status(),requested);
    if(init!=Owner::Status::ok || !prepared || !session.backend.ready() || !session.payload->feature) {
        std::cout<<"CASE "<<c.name<<" RESULT create_failed\n";session.finish();return false;
    }
    bool all=true;NVSDK_NGX_Result first_failure=NVSDK_NGX_Result_Success;
    for(unsigned frame=1;frame<=3 && all;++frame) {
        std::array<Upscaler::Status,2> statuses;std::array<bool,2> evaluated{};
        const auto recorded=session.submit([&](Owner::Recording& recording) {
            return recording.isolate([&](ID3D11DeviceContext1* isolated) {
                const FLOAT sentinel[]{1,0,1,1};
                auto& m=*session.payload->mod;
                for(unsigned eye=0;eye<2;++eye) {
                    isolated->ClearUnorderedAccessViewFloat(m.output_uav[eye].Get(),sentinel);
                    Upscaler::Eval eval=g.offsets[eye];eval.eye=eye;eval.frame=eval.pair=frame;
                    eval.device_generation=eval.dimension_generation=eval.feature_generation=1;
                    eval.context=context;eval.colour=m.colour[eye].Get();eval.depth=m.depth.Get();eval.motion=m.motion.Get();eval.output=m.output[eye].Get();
                    eval.jitter={0,0,0,0,(frame-1)%32,32,frame==1};
                    eval.motion_contract=d.motion;eval.colour_contract=d.colour_contract;eval.frame_time_ms=11.1f;eval.discontinuity=frame==1;
                    eval.scratch_owner=session.payload->mod;
                    evaluated[eye]=Upscaler::evaluate(session.payload->feature,eval,&statuses[eye]);
                    isolated->CopyResource(m.readback[eye].Get(),m.output[eye].Get());
                }
                return evaluated[0] && evaluated[1];
            })==Owner::Status::ok;
        });
        for(unsigned eye=0;eye<2;++eye) {
            std::cout<<"  frame "<<frame<<" eye"<<eye<<" evaluate reason="<<reason_name(statuses[eye].reason)<<" result="<<hex(statuses[eye].result);
            if(evaluated[eye]) {
                const auto& o=g.offsets[eye];
                const UINT x0=o.dst_x,y0=o.dst_y,x1=o.dst_x+d.width,y1=o.dst_y+d.height;
                const auto [inside,outside]=session.payload->mod->untouched(context,eye,x0,y0,x1,y1);
                const std::size_t expected_outside=static_cast<std::size_t>(c.w)*c.h-static_cast<std::size_t>(d.width)*d.height;
                std::cout<<" written region="<<x0<<','<<y0<<".."<<x1<<','<<y1<<" untouched_inside="<<inside<<" untouched_outside="<<outside<<'/'<<expected_outside;
                if(inside || outside!=expected_outside) {all=false;std::cout<<" REGION MISMATCH";}
            } else {all=false;if(first_failure==NVSDK_NGX_Result_Success) first_failure=statuses[eye].result;}
            std::cout<<'\n';
        }
        if(recorded!=Owner::Status::ok && recorded!=Owner::Status::callback_failed) all=false;
    }
    std::cout<<"CASE "<<c.name<<" RESULT "<<(all?"evaluated":"evaluate_failed")<<(all?"":" first_ngx="+hex(first_failure))<<'\n';
    session.finish();
    return all;
}
bool mod_suite(ID3D11Device* device,ID3D11DeviceContext* context,const Upscaler::Paths& paths) {
    std::vector<ModCase> cases;
    for(const auto& size:{std::array<UINT,2>{2246,2419},std::array<UINT,2>{2621,2822}}) {
        const std::string s=std::to_string(size[0])+'x'+std::to_string(size[1]);
        cases.push_back({"dlaa-"+s,size[0],size[1],1.0,false,false,true});
        for(const double f:{0.6665,0.70}) {
            const std::string fs=f<.68?"0.6665":"0.70";
            cases.push_back({"quality-whole-"+s+"-f"+fs,size[0],size[1],f,false,false,true});
            // v1108 foveated box exactly as v1393 creates it (no InEnableOutputSubrects): the headset failure.
            cases.push_back({"quality-box-"+s+"-f"+fs+"-no-subrects",size[0],size[1],f,true,false,true,0,0,false});
            cases.push_back({"quality-box-"+s+"-f"+fs+"-subrects",size[0],size[1],f,true,true,true});
        }
        cases.push_back({"quality-box-"+s+"-f0.70-subrects-uncanted",size[0],size[1],0.70,true,true,false});
    }
    // The non-box feature at the NGX optimal size and above it (the 2026-11-19 status: optimal 1497x1613, FSR render 1572x1693).
    cases.push_back({"quality-whole-2246x2419-optimal-1497x1613",2246,2419,0.6665,false,false,true,1497,1613});
    cases.push_back({"quality-whole-2246x2419-above-optimal-1572x1693",2246,2419,0.70,false,false,true,1572,1693});
    unsigned unexpected=0;std::vector<std::string> summary;
    for(const auto& c:cases) {
        const bool ok=mod_case(device,context,paths,c);
        const bool as_expected=ok==c.expect_success;
        if(!as_expected) ++unexpected;
        summary.push_back(c.name+": "+(ok?"evaluated":"FAILED")+(as_expected?" (as expected)":" (UNEXPECTED)"));
    }
    std::cout<<"\nMOD CASE SUMMARY\n";for(const auto& line:summary) std::cout<<"  "<<line<<'\n';
    return unexpected==0;
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
        require(mod_suite(device.Get(),context.Get(),paths),"a mod-shaped DLSS case did not behave as expected");
        std::cout<<"PASS: mod-shaped DLAA, DLSS Quality whole-eye and foveated-box cases behaved as expected on explicit NVIDIA hardware.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL/unavailable: "<<error.what()<<'\n';return 1;}
}
