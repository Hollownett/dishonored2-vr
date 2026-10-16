#pragma once
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <memory>

namespace d2vr {
// Present-thread-only asynchronous native alpha bounds. Retain the result lease
// while its texture is in use: a leased ring surface cannot be overwritten.
class NativeArtBounds {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
public:
    struct Observer {
        void* user{};
        std::uint64_t (*begin)(void*,ID3D11DeviceContext*,unsigned,int,std::uint64_t) noexcept{};
        void (*end)(void*,ID3D11DeviceContext*,std::uint64_t) noexcept{};
    } observer{};
    struct Metadata {std::uintptr_t owner{},file{},iggy{};UINT64 epoch{},tick{},serial{};};
    struct Result {
        Ptr<ID3D11Texture2D> texture;D3D11_BOX crop{};Metadata metadata{};bool empty{};
        bool fresh(std::uintptr_t owner,UINT64 epoch,UINT64 now) const noexcept {
            return !empty && texture && metadata.owner==owner && metadata.epoch==epoch &&
                now>=metadata.tick && now-metadata.tick<=250;
        }
    };
    using Lease=std::shared_ptr<const Result>;
private:
    struct Observation {
        Observer observer;ID3D11DeviceContext* context{};std::uint64_t ticket{};
        void finish() noexcept {const auto ended=ticket;ticket=0;if(ended && observer.end)observer.end(observer.user,context,ended);}
        ~Observation(){finish();}
    };
    struct Slot {
        Ptr<ID3D11Texture2D> texture;Ptr<ID3D11ShaderResourceView> srv;
        Ptr<ID3D11Buffer> bounds,staging;Ptr<ID3D11UnorderedAccessView> uav;Ptr<ID3D11Query> event;
        D3D11_TEXTURE2D_DESC desc{};Metadata metadata{};bool pending{};std::weak_ptr<const Result> lease;
    };
    Ptr<ID3D11DeviceContext> context_;Ptr<ID3D11Device> device_;Ptr<ID3D11ComputeShader> shader_;
    DWORD owner_thread_{};std::array<Slot,3> slots_{};
    bool owned() const noexcept {return context_ && owner_thread_ && owner_thread_==GetCurrentThreadId();}
    static bool rgba(DXGI_FORMAT format) noexcept {
        return format==DXGI_FORMAT_R8G8B8A8_TYPELESS || format==DXGI_FORMAT_R8G8B8A8_UNORM || format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    }
    bool resources(Slot& slot,const D3D11_TEXTURE2D_DESC& source) {
        if(slot.texture && slot.desc.Width==source.Width && slot.desc.Height==source.Height && slot.desc.Format==source.Format)return true;
        Slot replacement{};replacement.desc=source;
        auto desc=source;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.CPUAccessFlags=0;desc.MiscFlags=0;
        if(FAILED(device_->CreateTexture2D(&desc,nullptr,&replacement.texture)))return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=source.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS?DXGI_FORMAT_R8G8B8A8_UNORM:source.Format;
        srv.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;srv.Texture2D.MipLevels=1;
        if(FAILED(device_->CreateShaderResourceView(replacement.texture.Get(),&srv,&replacement.srv)))return false;
        D3D11_BUFFER_DESC buffer{};buffer.ByteWidth=16;buffer.Usage=D3D11_USAGE_DEFAULT;
        buffer.BindFlags=D3D11_BIND_UNORDERED_ACCESS;buffer.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
        if(FAILED(device_->CreateBuffer(&buffer,nullptr,&replacement.bounds)))return false;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav{};uav.Format=DXGI_FORMAT_R32_TYPELESS;uav.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;
        uav.Buffer.NumElements=4;uav.Buffer.Flags=D3D11_BUFFER_UAV_FLAG_RAW;
        if(FAILED(device_->CreateUnorderedAccessView(replacement.bounds.Get(),&uav,&replacement.uav)))return false;
        buffer.Usage=D3D11_USAGE_STAGING;buffer.BindFlags=0;buffer.MiscFlags=0;buffer.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        if(FAILED(device_->CreateBuffer(&buffer,nullptr,&replacement.staging)))return false;
        D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
        if(FAILED(device_->CreateQuery(&query,&replacement.event)))return false;
        slot=std::move(replacement);return true;
    }
public:
    bool configure(ID3D11DeviceContext* immediate,DWORD verified_owner_thread) {
        if(!immediate || !verified_owner_thread || verified_owner_thread!=GetCurrentThreadId() ||
           immediate->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)return false;
        // Changing context while commands/leases exist would discard ownership.
        if(context_)return context_.Get()==immediate && owner_thread_==verified_owner_thread;
        Ptr<ID3D11Device> device;immediate->GetDevice(&device);if(!device || device->GetFeatureLevel()<D3D_FEATURE_LEVEL_11_0)return false;
        constexpr char source[]=R"(
Texture2D<float4> art:register(t0); RWByteAddressBuffer bounds:register(u0);
groupshared uint x0,y0,x1,y1;
[numthreads(16,16,1)] void main(uint3 pixel:SV_DispatchThreadID,uint index:SV_GroupIndex) {
 uint width,height;art.GetDimensions(width,height);
 if(index==0){x0=width;y0=height;x1=0;y1=0;} GroupMemoryBarrierWithGroupSync();
 if(pixel.x<width && pixel.y<height && art.Load(int3(pixel.xy,0)).a>0){
  InterlockedMin(x0,pixel.x);InterlockedMin(y0,pixel.y);InterlockedMax(x1,pixel.x+1);InterlockedMax(y1,pixel.y+1);
 } GroupMemoryBarrierWithGroupSync();
 if(index==0 && x1>x0 && y1>y0){uint unused;
  bounds.InterlockedMin(0,x0,unused);bounds.InterlockedMin(4,y0,unused);
  bounds.InterlockedMax(8,x1,unused);bounds.InterlockedMax(12,y1,unused);
 }
})";
        Ptr<ID3DBlob> code;
        if(FAILED(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,nullptr)))return false;
        Ptr<ID3D11ComputeShader> shader;
        if(FAILED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader)))return false;
        context_=immediate;device_=device;shader_=shader;owner_thread_=verified_owner_thread;return true;
    }
    bool queue(ID3D11Texture2D* source,const Metadata& metadata) {
        if(!owned() || !source || !metadata.owner || !metadata.epoch || !metadata.tick || !metadata.serial)return false;
        Ptr<ID3D11Device> device;source->GetDevice(&device);D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
        if(device.Get()!=device_.Get() || !desc.Width || !desc.Height || desc.MipLevels!=1 || desc.ArraySize!=1 ||
           desc.SampleDesc.Count!=1 || !rgba(desc.Format))return false;
        Slot* selected{};
        for(auto& slot:slots_)if(!slot.pending && slot.lease.expired()){selected=&slot;break;}
        if(!selected || !resources(*selected,desc))return false;
        auto& slot=*selected;
        Ptr<ID3D11ComputeShader> old_shader;Ptr<ID3D11ShaderResourceView> old_srv;Ptr<ID3D11UnorderedAccessView> old_uav;
        std::array<ID3D11ClassInstance*,256> instances{};UINT count=static_cast<UINT>(instances.size());
        context_->CSGetShader(&old_shader,instances.data(),&count);context_->CSGetShaderResources(0,1,&old_srv);context_->CSGetUnorderedAccessViews(0,1,&old_uav);
        Observation observed{observer,context_.Get()};
        if(observed.observer.begin && observed.observer.end)
            observed.ticket=observed.observer.begin(observed.observer.user,context_.Get(),0,-1,metadata.serial);
        context_->CopyResource(slot.texture.Get(),source);
        // Raw-buffer UAV clears repeat component zero, not a uint4 tuple.
        // Upload the four distinct reduction seeds without any GPU readback.
        const UINT initial[]{desc.Width,desc.Height,0,0};context_->UpdateSubresource(slot.bounds.Get(),0,nullptr,initial,0,0);
        auto* srv=slot.srv.Get();auto* uav=slot.uav.Get();
        context_->CSSetShader(shader_.Get(),nullptr,0);context_->CSSetShaderResources(0,1,&srv);context_->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
        context_->Dispatch((desc.Width+15)/16,(desc.Height+15)/16,1);
        ID3D11ShaderResourceView* no_srv{};ID3D11UnorderedAccessView* no_uav{};
        context_->CSSetShaderResources(0,1,&no_srv);context_->CSSetUnorderedAccessViews(0,1,&no_uav,nullptr);
        context_->CopyResource(slot.staging.Get(),slot.bounds.Get());context_->End(slot.event.Get());
        observed.finish();
        srv=old_srv.Get();uav=old_uav.Get();context_->CSSetShader(old_shader.Get(),instances.data(),count);
        context_->CSSetShaderResources(0,1,&srv);context_->CSSetUnorderedAccessViews(0,1,&uav,nullptr);
        for(unsigned i=0;i<count;++i)if(instances[i])instances[i]->Release();
        slot.metadata=metadata;slot.pending=true;return true;
    }
    Lease poll() {
        if(!owned())return {};
        for(auto& slot:slots_)if(slot.pending){
            BOOL complete{};const auto ready=context_->GetData(slot.event.Get(),&complete,sizeof(complete),D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if(ready==S_FALSE || (ready==S_OK && !complete))continue;
            if(FAILED(ready)){slot.pending=false;continue;}
            D3D11_MAPPED_SUBRESOURCE map{};const auto mapped=context_->Map(slot.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);
            if(mapped==DXGI_ERROR_WAS_STILL_DRAWING)continue;
            if(FAILED(mapped)){slot.pending=false;continue;}
            std::array<UINT,4> bounds{};std::memcpy(bounds.data(),map.pData,sizeof(bounds));context_->Unmap(slot.staging.Get(),0);
            slot.pending=false;
            auto result=std::make_shared<Result>();result->texture=slot.texture;result->metadata=slot.metadata;
            result->empty=bounds[0]>=bounds[2] || bounds[1]>=bounds[3];
            if(!result->empty){
                if(bounds[2]>slot.desc.Width || bounds[3]>slot.desc.Height)continue;
                result->crop={bounds[0],bounds[1],0,bounds[2],bounds[3],1};
            }
            slot.lease=result;return result;
        }
        return {};
    }
};
} // namespace d2vr
