#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "d3d11_temporal_owner.hpp"
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using Owner=d2vr::D3D11TemporalOwner;
using Status=Owner::Status;
namespace {
void require(bool yes,const char* message) {if(!yes) {std::cerr<<"check failed: "<<message<<'\n';throw std::runtime_error(message);}}
void check(HRESULT hr,const char* message) {if(FAILED(hr)) {std::cerr<<message<<" hr=0x"<<std::hex<<static_cast<unsigned>(hr)<<std::dec<<'\n';throw std::runtime_error(message);}}
const char shader_source[]=R"(
struct CP {float4 p:SV_Position;};
CP vs(float4 p:POSITION) {CP o;o.p=p;return o;}
float4 ps(CP i):SV_Target {return i.p;}
[maxvertexcount(1)] void gs(point CP i[1],inout PointStream<CP> o) {o.Append(i[0]);}
struct Factors {float edge[3]:SV_TessFactor;float inside:SV_InsideTessFactor;};
Factors constants(InputPatch<CP,3> p,uint patch:SV_PrimitiveID) {Factors f;f.edge[0]=1;f.edge[1]=1;f.edge[2]=1;f.inside=1;return f;}
[domain("tri")][partitioning("integer")][outputtopology("triangle_cw")][outputcontrolpoints(3)][patchconstantfunc("constants")]
CP hs(InputPatch<CP,3> p,uint n:SV_OutputControlPointID) {return p[n];}
[domain("tri")] CP ds(Factors f,float3 uvw:SV_DomainLocation,const OutputPatch<CP,3> p) {CP o;o.p=p[0].p*uvw.x+p[1].p*uvw.y+p[2].p*uvw.z;return o;}
[numthreads(1,1,1)] void cs(uint3 id:SV_DispatchThreadID) {}
)";
ComPtr<ID3DBlob> compile(const char* entry,const char* profile) {
    ComPtr<ID3DBlob> code,error;
    const auto hr=D3DCompile(shader_source,sizeof shader_source,nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&error);
    if(FAILED(hr) && error)std::cerr.write(static_cast<const char*>(error->GetBufferPointer()),static_cast<std::streamsize>(error->GetBufferSize()));
    check(hr,"shader compile");return code;
}
struct Bindings {
    ComPtr<ID3D11VertexShader> vs;ComPtr<ID3D11HullShader> hs;ComPtr<ID3D11DomainShader> ds;
    ComPtr<ID3D11GeometryShader> gs;ComPtr<ID3D11PixelShader> ps;ComPtr<ID3D11ComputeShader> cs;
    ComPtr<ID3D11Buffer> cb,vertex,index,stream,cs_output,om_output;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11SamplerState> sampler;
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11UnorderedAccessView> cs_uav,om_uav;
    ComPtr<ID3D11BlendState> blend;
    ComPtr<ID3D11DepthStencilState> depth;
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11Predicate> predicate;
    bool offsets{};
    void create(ID3D11Device* d) {
        auto code=compile("vs","vs_5_0");check(d->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs),"VS");
        const D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};
        check(d->CreateInputLayout(&element,1,code->GetBufferPointer(),code->GetBufferSize(),&layout),"input layout");
#define CREATE_SHADER(member,entry,profile,method) code=compile(entry,profile);check(d->method(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&member),entry)
        CREATE_SHADER(hs,"hs","hs_5_0",CreateHullShader);CREATE_SHADER(ds,"ds","ds_5_0",CreateDomainShader);
        CREATE_SHADER(gs,"gs","gs_5_0",CreateGeometryShader);CREATE_SHADER(ps,"ps","ps_5_0",CreatePixelShader);CREATE_SHADER(cs,"cs","cs_5_0",CreateComputeShader);
#undef CREATE_SHADER
        auto buffer=[&](UINT flags,ComPtr<ID3D11Buffer>& out,UINT bytes=256) {
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=flags;check(d->CreateBuffer(&desc,nullptr,&out),"buffer");
        };
        buffer(D3D11_BIND_CONSTANT_BUFFER,cb,8192);buffer(D3D11_BIND_VERTEX_BUFFER,vertex);buffer(D3D11_BIND_INDEX_BUFFER,index);buffer(D3D11_BIND_STREAM_OUTPUT,stream);
        buffer(D3D11_BIND_UNORDERED_ACCESS,cs_output);buffer(D3D11_BIND_UNORDERED_ACCESS,om_output);
        D3D11_UNORDERED_ACCESS_VIEW_DESC u{};u.Format=DXGI_FORMAT_R32_UINT;u.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;u.Buffer.NumElements=64;
        check(d->CreateUnorderedAccessView(cs_output.Get(),&u,&cs_uav),"CS UAV");check(d->CreateUnorderedAccessView(om_output.Get(),&u,&om_uav),"OM UAV");
        D3D11_TEXTURE2D_DESC td{};td.Width=8;td.Height=8;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.SampleDesc.Count=1;td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> texture;check(d->CreateTexture2D(&td,nullptr,&texture),"SRV texture");check(d->CreateShaderResourceView(texture.Get(),nullptr,&srv),"SRV");
        td.BindFlags=D3D11_BIND_RENDER_TARGET;texture.Reset();check(d->CreateTexture2D(&td,nullptr,&texture),"RTV texture");check(d->CreateRenderTargetView(texture.Get(),nullptr,&rtv),"RTV");
        td.BindFlags=D3D11_BIND_DEPTH_STENCIL;td.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;texture.Reset();check(d->CreateTexture2D(&td,nullptr,&texture),"DSV texture");check(d->CreateDepthStencilView(texture.Get(),nullptr,&dsv),"DSV");
        D3D11_SAMPLER_DESC sd{};sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;sd.AddressU=D3D11_TEXTURE_ADDRESS_WRAP;sd.AddressV=D3D11_TEXTURE_ADDRESS_CLAMP;sd.AddressW=D3D11_TEXTURE_ADDRESS_MIRROR;sd.MaxLOD=16;
        check(d->CreateSamplerState(&sd,&sampler),"sampler");
        D3D11_BLEND_DESC bd{};auto& target=bd.RenderTarget[0];target.BlendEnable=TRUE;target.SrcBlend=D3D11_BLEND_BLEND_FACTOR;target.DestBlend=D3D11_BLEND_INV_BLEND_FACTOR;target.BlendOp=D3D11_BLEND_OP_ADD;target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlendAlpha=D3D11_BLEND_ZERO;target.BlendOpAlpha=D3D11_BLEND_OP_ADD;target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
        check(d->CreateBlendState(&bd,&blend),"blend");
        D3D11_DEPTH_STENCIL_DESC dd{};dd.DepthEnable=TRUE;dd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL;dd.DepthFunc=D3D11_COMPARISON_GREATER;dd.StencilEnable=FALSE;
        check(d->CreateDepthStencilState(&dd,&depth),"depth state");
        D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_FRONT;rd.DepthClipEnable=TRUE;rd.ScissorEnable=TRUE;
        check(d->CreateRasterizerState(&rd,&raster),"raster state");
        D3D11_QUERY_DESC q{D3D11_QUERY_OCCLUSION_PREDICATE,0};check(d->CreatePredicate(&q,&predicate),"predicate");
        D3D11_FEATURE_DATA_D3D11_OPTIONS options{};offsets=SUCCEEDED(d->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS,&options,sizeof options)) && options.ConstantBufferOffsetting;
    }
    void bind(ID3D11DeviceContext1* c) {
        c->VSSetShader(vs.Get(),nullptr,0);c->HSSetShader(hs.Get(),nullptr,0);c->DSSetShader(ds.Get(),nullptr,0);c->GSSetShader(gs.Get(),nullptr,0);c->PSSetShader(ps.Get(),nullptr,0);c->CSSetShader(cs.Get(),nullptr,0);
        auto* b=cb.Get();auto* resource=srv.Get();auto* sample=sampler.Get();const UINT first=16,count=16;
#define BIND_STAGE(stage) c->stage##SetConstantBuffers(3,1,&b);if(offsets)c->stage##SetConstantBuffers1(3,1,&b,&first,&count);c->stage##SetShaderResources(4,1,&resource);c->stage##SetSamplers(5,1,&sample)
        BIND_STAGE(VS);BIND_STAGE(HS);BIND_STAGE(DS);BIND_STAGE(GS);BIND_STAGE(PS);BIND_STAGE(CS);
#undef BIND_STAGE
        auto* vb=vertex.Get();const UINT stride=16,offset=16;c->IASetVertexBuffers(2,1,&vb,&stride,&offset);c->IASetIndexBuffer(index.Get(),DXGI_FORMAT_R16_UINT,4);c->IASetInputLayout(layout.Get());c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        auto* so=stream.Get();c->SOSetTargets(1,&so,&offset);
        auto* target=rtv.Get();auto* om=om_uav.Get();auto* compute=cs_uav.Get();const UINT preserve=UINT(-1);
        c->OMSetRenderTargetsAndUnorderedAccessViews(1,&target,dsv.Get(),1,1,&om,&preserve);c->CSSetUnorderedAccessViews(2,1,&compute,&preserve);
        const FLOAT factor[]{.1f,.2f,.3f,.4f};c->OMSetBlendState(blend.Get(),factor,0xaaaa5555);c->OMSetDepthStencilState(depth.Get(),7);c->RSSetState(raster.Get());
        const D3D11_VIEWPORT views[]{{11,13,320,240,.1f,.9f},{17,19,160,120,.2f,.8f}};c->RSSetViewports(2,views);
        const D3D11_RECT scissors[]{{3,4,200,210},{5,6,70,80}};c->RSSetScissorRects(2,scissors);
        c->Begin(predicate.Get());c->End(predicate.Get());c->SetPredication(predicate.Get(),FALSE);
    }
    void verify(ID3D11DeviceContext1* c) {
#define VERIFY_SHADER(stage,type,expected) {ComPtr<type> value;UINT n=0;c->stage##GetShader(&value,nullptr,&n);require(value.Get()==expected.Get() && n==0,#stage " shader restored");}
        VERIFY_SHADER(VS,ID3D11VertexShader,vs);VERIFY_SHADER(HS,ID3D11HullShader,hs);VERIFY_SHADER(DS,ID3D11DomainShader,ds);VERIFY_SHADER(GS,ID3D11GeometryShader,gs);VERIFY_SHADER(PS,ID3D11PixelShader,ps);VERIFY_SHADER(CS,ID3D11ComputeShader,cs);
#undef VERIFY_SHADER
#define VERIFY_STAGE(stage) {ComPtr<ID3D11Buffer> b;ComPtr<ID3D11ShaderResourceView> r;ComPtr<ID3D11SamplerState> s;c->stage##GetConstantBuffers(3,1,&b);c->stage##GetShaderResources(4,1,&r);c->stage##GetSamplers(5,1,&s);require(b.Get()==cb.Get() && r.Get()==srv.Get() && s.Get()==sampler.Get(),#stage " bindings restored");if(offsets){b.Reset();UINT first=0,count=0;c->stage##GetConstantBuffers1(3,1,&b,&first,&count);require(b.Get()==cb.Get() && first==16 && count==16,#stage " CB1 range restored");}}
        VERIFY_STAGE(VS);VERIFY_STAGE(HS);VERIFY_STAGE(DS);VERIFY_STAGE(GS);VERIFY_STAGE(PS);VERIFY_STAGE(CS);
#undef VERIFY_STAGE
        ComPtr<ID3D11Buffer> b;UINT stride=0,offset=0;c->IAGetVertexBuffers(2,1,&b,&stride,&offset);require(b.Get()==vertex.Get() && stride==16 && offset==16,"IA vertex restored");
        b.Reset();DXGI_FORMAT format{};c->IAGetIndexBuffer(&b,&format,&offset);require(b.Get()==index.Get() && format==DXGI_FORMAT_R16_UINT && offset==4,"IA index restored");
        ComPtr<ID3D11InputLayout> il;c->IAGetInputLayout(&il);require(il.Get()==layout.Get(),"IA layout restored");D3D11_PRIMITIVE_TOPOLOGY topology{};c->IAGetPrimitiveTopology(&topology);require(topology==D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,"IA topology restored");
        b.Reset();c->SOGetTargets(1,&b);require(b.Get()==stream.Get(),"SO binding restored (data cursor not claimed)");
        ComPtr<ID3D11RenderTargetView> target;ComPtr<ID3D11DepthStencilView> z;ComPtr<ID3D11UnorderedAccessView> om,compute;c->OMGetRenderTargetsAndUnorderedAccessViews(1,&target,&z,1,1,&om);c->CSGetUnorderedAccessViews(2,1,&compute);
        require(target.Get()==rtv.Get() && z.Get()==dsv.Get() && om.Get()==om_uav.Get() && compute.Get()==cs_uav.Get(),"OM/CS outputs restored");
        ComPtr<ID3D11BlendState> bs;FLOAT factor[4]{};UINT mask{};c->OMGetBlendState(&bs,factor,&mask);require(bs.Get()==blend.Get() && factor[0]==.1f && factor[1]==.2f && factor[2]==.3f && factor[3]==.4f && mask==0xaaaa5555,"blend restored");
        ComPtr<ID3D11DepthStencilState> dz;UINT stencil{};c->OMGetDepthStencilState(&dz,&stencil);require(dz.Get()==depth.Get() && stencil==7,"depth/stencil restored");
        ComPtr<ID3D11RasterizerState> rs;c->RSGetState(&rs);require(rs.Get()==raster.Get(),"rasterizer restored");
        UINT count=2;D3D11_VIEWPORT views[2]{};c->RSGetViewports(&count,views);require(count==2 && views[0].TopLeftX==11 && views[0].TopLeftY==13 && views[0].Width==320 && views[0].Height==240 && views[0].MinDepth==.1f && views[0].MaxDepth==.9f && views[1].TopLeftX==17 && views[1].TopLeftY==19 && views[1].Width==160 && views[1].Height==120 && views[1].MinDepth==.2f && views[1].MaxDepth==.8f,"viewports restored");
        count=2;D3D11_RECT rectangles[2]{};c->RSGetScissorRects(&count,rectangles);require(count==2 && rectangles[0].left==3 && rectangles[0].top==4 && rectangles[0].right==200 && rectangles[0].bottom==210 && rectangles[1].left==5 && rectangles[1].top==6 && rectangles[1].right==70 && rectangles[1].bottom==80,"scissors restored");
        ComPtr<ID3D11Predicate> p;BOOL value=TRUE;c->GetPredication(&p,&value);require(p.Get()==predicate.Get() && !value,"predication binding restored (query activity excluded)");
    }
};
void await(Owner::DrainOwner& drain,ID3D11DeviceContext* c,unsigned expected) {
    c->Flush(); // Standalone test submission ONLY. Production poll never flushes.
    const auto start=GetTickCount64();
    while(drain.pending().completed<expected) {
        require(drain.poll()==Status::ok,"owner poll");
        if(drain.pending().quarantined)throw std::runtime_error("actual hardware query error (not injected)");
        require(GetTickCount64()-start<10000,"hardware completion timeout; not a retirement proof");Sleep(1);
    }
}
struct Lifetime {unsigned* destroyed;explicit Lifetime(unsigned& n):destroyed(&n){}~Lifetime(){++*destroyed;}};
struct CopyPayload {
    ComPtr<ID3D11Buffer> source,readback;
    Lifetime lifetime;
    CopyPayload(ID3D11Buffer* from,ID3D11Buffer* to,unsigned& destroyed):source(from),readback(to),lifetime(destroyed) {}
};
void suite(IDXGIAdapter1* adapter,UINT flags) {
    ComPtr<ID3D11Device> d;ComPtr<ID3D11DeviceContext> base;D3D_FEATURE_LEVEL level{};
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,D3D_FEATURE_LEVEL_11_0};
    check(D3D11CreateDevice(adapter,D3D_DRIVER_TYPE_UNKNOWN,nullptr,flags,levels,2,D3D11_SDK_VERSION,&d,&level,&base),"explicit NVIDIA device");
    ComPtr<IDXGIDevice> actual;ComPtr<IDXGIAdapter> actual_adapter;DXGI_ADAPTER_DESC actual_desc{};DXGI_ADAPTER_DESC1 selected_desc{};
    check(d.As(&actual),"created DXGI device");check(actual->GetAdapter(&actual_adapter),"actual device adapter");check(actual_adapter->GetDesc(&actual_desc),"actual adapter identity");check(adapter->GetDesc1(&selected_desc),"selected identity");
    require(actual_desc.VendorId==0x10de && actual_desc.DeviceId==selected_desc.DeviceId &&
        actual_desc.AdapterLuid.HighPart==selected_desc.AdapterLuid.HighPart && actual_desc.AdapterLuid.LowPart==selected_desc.AdapterLuid.LowPart,"created device uses selected NVIDIA hardware LUID");
    ComPtr<ID3D11DeviceContext1> c;check(base.As(&c),"Context1");
    Owner::Result prepared;auto owner=Owner::prepare(d.Get(),base.Get(),prepared);require(owner && prepared.status==Status::ok,"prepare state/query owner");
    const auto actual_flags=d->GetCreationFlags();
    require((actual_flags&D3D11_CREATE_DEVICE_SINGLETHREADED)==(flags&D3D11_CREATE_DEVICE_SINGLETHREADED),"actual device creation flag");
    const UINT expected_flags=(actual_flags&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
    require(owner->feature_level()==level && owner->context_state_flags()==expected_flags,"actual feature level and state flags");
    std::cout<<"device_flags="<<flags<<" feature_level=0x"<<std::hex<<level<<" state_flags=0x"<<owner->context_state_flags()<<std::dec<<'\n';
    auto duplicate=Owner::prepare(d.Get(),base.Get(),prepared);require(!duplicate && prepared.status==Status::busy,"duplicate canonical stream refused");
    if(!(flags&D3D11_CREATE_DEVICE_SINGLETHREADED)) {
        ComPtr<ID3D11DeviceContext> deferred;check(d->CreateDeferredContext(0,&deferred),"deferred context fixture");
        auto wrong=Owner::prepare(d.Get(),deferred.Get(),prepared);require(!wrong && prepared.status==Status::wrong_context,"deferred context refused");
    }
    ComPtr<ID3D11Device> other;ComPtr<ID3D11DeviceContext> other_context;
    check(D3D11CreateDevice(adapter,D3D_DRIVER_TYPE_UNKNOWN,nullptr,flags,levels,2,D3D11_SDK_VERSION,&other,nullptr,&other_context),"second actual device");
    auto wrong=Owner::prepare(d.Get(),other_context.Get(),prepared);require(!wrong && prepared.status==Status::wrong_device,"cross-device context refused");
    auto drain=owner->drain_owner();require(!drain.can_destroy(),"open owner cannot unload");
    Bindings state;state.create(d.Get());state.bind(c.Get());state.verify(c.Get());
    std::cout<<"CB1_offsetting="<<state.offsets<<" (false means unavailable, not tested)\n";
    const auto generation=std::make_shared<int>(17);Status status;
    for(unsigned path=0;path<3;++path) {
        auto ticket=owner->reserve(std::make_shared<int>(1),generation,status);require(ticket && status==Status::ok,"reserve isolation path");
        const auto recorded=owner->record(*ticket,[&](Owner::Recording& recording) {
            const auto isolated=recording.isolate([&](ID3D11DeviceContext1* isolated_context) {
                require(recording.isolate([](ID3D11DeviceContext1*) {return true;})==Status::busy,"nested isolation refused");
                Status nested;require(!owner->reserve(generation,generation,nested) && nested==Status::busy,"callback reentry admission refused");
                require(owner->record(*ticket,[](Owner::Recording&) {return true;})==Status::busy,"callback record reentry refused");
                isolated_context->ClearState();isolated_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                if(path==2)throw std::runtime_error("injected callback exception (actual GPU state restoration)");
                return path==0;
            });
            require(isolated==(path==0?Status::ok:path==1?Status::callback_failed:Status::callback_exception),"isolation callback result");
            state.verify(recording.context()); // native remainder observes restored bindings
            return isolated==Status::ok;
        });
        require(recorded==(path==0?Status::ok:Status::callback_failed),"record callback outcome");
        require(!owner->take_completed(),"unsignaled observation never CPU-retires");
        await(drain,c.Get(),1);auto completed=drain.take_completed();require(completed && completed->ticket.serial==ticket->serial && !completed->device_abandoned,"actual query completion");
        state.verify(c.Get());require(owner->cancel(*ticket)==Status::stale_ticket,"completed ticket cannot cancel reused slot");
    }
    std::atomic<bool> foreign_ok{false};
    std::thread foreign([&] {Status foreign_status;auto t=owner->reserve(generation,generation,foreign_status);foreign_ok=!t && foreign_status==Status::wrong_thread && drain.poll()==Status::wrong_thread;});foreign.join();require(foreign_ok,"foreign thread refuses before D3D calls");
    std::array<unsigned,64> pattern{};for(unsigned i=0;i<pattern.size();++i)pattern[i]=0x13570000u+i*13;
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=sizeof pattern;bd.Usage=D3D11_USAGE_DEFAULT;D3D11_SUBRESOURCE_DATA data{};data.pSysMem=pattern.data();
    ComPtr<ID3D11Buffer> source,readback;check(d->CreateBuffer(&bd,&data,&source),"copy source");bd.Usage=D3D11_USAGE_STAGING;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(d->CreateBuffer(&bd,nullptr,&readback),"copy staging");
    // Explicitly end predication for this standalone copy; isolation tests above
    // already proved its binding. Query activity/data counters are not swapped.
    c->SetPredication(nullptr,FALSE);
    unsigned payload_destroyed=0,generation_destroyed=0;
    auto retained_generation=std::make_shared<Lifetime>(generation_destroyed);
    std::array<Owner::Ticket,Owner::capacity> tickets{};
    for(std::size_t i=0;i<tickets.size();++i) {
        auto payload=std::make_shared<CopyPayload>(source.Get(),readback.Get(),payload_destroyed);
        auto ticket=owner->reserve(payload,retained_generation,status);require(ticket && status==Status::ok,"reserve bounded ring");tickets[i]=*ticket;
        const auto result=owner->record(*ticket,[&](Owner::Recording& recording) {
            recording.context()->CopyResource(readback.Get(),source.Get());
            if(i==tickets.size()-1)throw std::runtime_error("partial copy failure still signals EVENT");
            return true;
        });
        require(result==(i==tickets.size()-1?Status::callback_exception:Status::ok),"partial command callback result");
    }
    source.Reset();readback.Reset(); // GPU resources now belong to retained opaque payloads
    retained_generation.reset();require(!payload_destroyed && !generation_destroyed,"GPU payloads retained before poll");
    require(!owner->reserve(generation,generation,status) && status==Status::full,"full ring refuses before GPU commands");
    require(owner->reset()==Status::ok && owner->close()==Status::ok,"reset/stop close admission");
    require(!owner->reserve(generation,generation,status) && status==Status::closed,"closed owner refuses admission");
    require(!drain.can_destroy() && !drain.can_unload(),"pending stop cannot destroy/unload");owner.reset();
    require(!payload_destroyed && !generation_destroyed,"strong drain survives facade destruction");
    await(drain,c.Get(),8);require(!payload_destroyed && !generation_destroyed,"completed tokens retain ownership until consumed");
    std::array<bool,8> seen{};
    while(auto completed=drain.take_completed()) {
        bool matched=false;for(std::size_t i=0;i<tickets.size();++i)if(completed->ticket.serial==tickets[i].serial) {require(!seen[i],"completion exactly once");seen[i]=true;matched=true;}
        require(matched && !completed->device_abandoned && completed->generation,"exact submission completion and generation lease");
        const auto payload=std::static_pointer_cast<const CopyPayload>(completed->payload);
        readback=payload->readback; // Keep readback only after completion proof; original refs were dropped.
    }
    for(bool value:seen)require(value,"every submitted ticket completed");
    require(payload_destroyed==8 && generation_destroyed==1 && drain.can_destroy() && drain.can_unload(),"drained leases released exactly once");
    D3D11_MAPPED_SUBRESOURCE mapped{};check(c->Map(readback.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped),"readback only after EVENT completion");
    const bool correct=std::memcmp(mapped.pData,pattern.data(),sizeof pattern)==0;c->Unmap(readback.Get(),0);require(correct,"actual GPU copied bytes");
    drain={}; // lifetime obligation completed; now replacement owner is legal
    auto replacement=Owner::prepare(d.Get(),base.Get(),prepared);require(bool(replacement),"canonical owner reusable after drain");
    unsigned cancelled=0;auto payload=std::make_shared<Lifetime>(cancelled);auto reserved=replacement->reserve(payload,generation,status);payload.reset();require(reserved && !cancelled,"reserved lease retained");
    require(replacement->cancel(*reserved)==Status::ok && cancelled==1,"unsubmitted reservation cancellable");
    auto old_ticket=tickets[0];
    auto fresh=replacement->reserve(generation,generation,status);require(bool(fresh),"replacement reservation");
    require(replacement->cancel(old_ticket)==Status::stale_ticket,"old owner ticket cannot cancel replacement work");
    auto r=replacement->drain_owner();require(replacement->close()==Status::ok && !r.can_destroy(),"reserved stop retains ownership");
    replacement.reset();require(r.cancel(*fresh)==Status::ok && r.can_destroy(),"strong drain can cancel never-submitted reservation after facade release");
    r={};
    auto destroyed_facade=Owner::prepare(d.Get(),base.Get(),prepared);require(bool(destroyed_facade),"callback teardown owner");
    auto teardown_drain=destroyed_facade->drain_owner();auto teardown_ticket=destroyed_facade->reserve(generation,generation,status);require(bool(teardown_ticket),"callback teardown reservation");
    require(destroyed_facade->record(*teardown_ticket,[&](Owner::Recording& recording) {
        destroyed_facade.reset(); // the method must retain State without dereferencing its dead facade
        require(teardown_drain.close()==Status::ok,"callback closes retained drain");
        require(recording.isolate([](ID3D11DeviceContext1* isolated) {isolated->ClearState();return true;})==Status::ok,"recording survives facade destruction");
        return true;
    })==Status::ok,"callback facade destruction safely signals marker");
    await(teardown_drain,c.Get(),1);require(bool(teardown_drain.take_completed()) && teardown_drain.can_destroy(),"destroyed facade drains actual completion");
    c->ClearState();std::cout<<"PASS NVIDIA actual state restoration, partial-failure EVENT, bounded leases/drain and copy bytes flags="<<flags<<'\n';
}
}
int main() {
    try {
        ComPtr<IDXGIFactory1> factory;check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)),"DXGI factory");ComPtr<IDXGIAdapter1> selected;DXGI_ADAPTER_DESC1 desc{};
        for(UINT i=0;;++i) {ComPtr<IDXGIAdapter1> candidate;const auto hr=factory->EnumAdapters1(i,&candidate);if(hr==DXGI_ERROR_NOT_FOUND)break;check(hr,"adapter enumeration");DXGI_ADAPTER_DESC1 current{};check(candidate->GetDesc1(&current),"adapter description");if(current.VendorId==0x10de && !(current.Flags&(DXGI_ADAPTER_FLAG_SOFTWARE|DXGI_ADAPTER_FLAG_REMOTE))) {selected=candidate;desc=current;break;}}
        require(selected && desc.VendorId==0x10de && !(desc.Flags&(DXGI_ADAPTER_FLAG_SOFTWARE|DXGI_ADAPTER_FLAG_REMOTE)),"NVIDIA hardware unavailable; WARP/software/remote is not a hardware pass");
        LARGE_INTEGER driver{};const auto driver_result=selected->CheckInterfaceSupport(__uuidof(IDXGIDevice),&driver);
        std::cout<<"CheckInterfaceSupport driver_hr=0x"<<std::hex<<static_cast<unsigned>(driver_result)<<std::dec<<" (failure means version unavailable)\n";
        std::wcout<<L"NVIDIA adapter="<<desc.Description<<L" vendor=0x"<<std::hex<<desc.VendorId<<L" device=0x"<<desc.DeviceId<<L" LUID="<<desc.AdapterLuid.HighPart<<L":"<<desc.AdapterLuid.LowPart<<L" driver="<<driver.QuadPart<<std::dec<<L" VRAM="<<desc.DedicatedVideoMemory<<L'\n';
        suite(selected.Get(),0);suite(selected.Get(),D3D11_CREATE_DEVICE_SINGLETHREADED);
        std::cout<<"PASS: both explicit NVIDIA device variants. No NGX/provider/game/headset used. Query error/device-loss injection not hardware-tested.\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL/unavailable: "<<e.what()<<'\n';return 1;}
}
