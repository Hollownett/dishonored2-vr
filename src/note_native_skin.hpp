#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <memory>
#include <mutex>
#include <cstdint>
#include <cmath>
#include <stdexcept>

namespace d2vr {
// Pinned b671f824...6180: 25f930 copies 48-byte matrices to the two buffer
// wrappers at model+2008/+2040. 25e4d0 supplies their views to native draws.
// Only owned D3D resources change here; native addresses are equality tokens.
class NoteNativeSkin {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    static void check(HRESULT hr) {if(FAILED(hr))throw std::runtime_error("Native note skin resource failed");}
public:
    using Rows=std::array<float,12>;
    struct Model {std::uintptr_t current_buffer{},previous_buffer{};std::vector<Rows> palette;};
    struct Snapshot {std::uint64_t generation{},frame{},tick{};std::vector<Model> models;};
    // Same animated-affine * inverse-bind order as native 24c340/12e1d00.
    static std::vector<Rows> pack(const std::vector<Rows>& animated,const std::vector<Rows>& inverse) {
        if((animated.size()!=20 && animated.size()!=69) || inverse.size()!=animated.size())
            throw std::invalid_argument("Native note skin joint count differs");
        std::vector<Rows> result((animated.size()+7)&~std::size_t(7));
        for(auto& row:result)row={1,0,0,0,0,1,0,0,0,0,1,0};
        for(std::size_t i=0;i<animated.size();++i) {
            for(float f:animated[i])if(!std::isfinite(f))throw std::invalid_argument("Nonfinite animated joint");
            for(float f:inverse[i])if(!std::isfinite(f))throw std::invalid_argument("Nonfinite inverse bind");
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<4;++c) {
                float value=c==3?animated[i][r*4+3]:0;
                for(unsigned k=0;k<3;++k)value+=animated[i][r*4+k]*inverse[i][k*4+c];
                if(!std::isfinite(value))throw std::invalid_argument("Nonfinite packed joint");
                result[i][r*4+c]=value;
            }
        }
        return result;
    }
    struct Prepared {
        Snapshot snapshot;
    private:
        friend class NoteNativeSkin;
        struct View {std::size_t model{};D3D11_SHADER_RESOURCE_VIEW_DESC desc{};Ptr<ID3D11ShaderResourceView> resource;};
        Ptr<ID3D11Device> device;
        std::vector<Ptr<ID3D11Buffer>> buffers;
        std::vector<View> views;
        std::mutex mutex;
        static bool same(const D3D11_SHADER_RESOURCE_VIEW_DESC& a,const D3D11_SHADER_RESOURCE_VIEW_DESC& b) {
            if(a.Format!=b.Format || a.ViewDimension!=b.ViewDimension)return false;
            if(a.ViewDimension==D3D11_SRV_DIMENSION_BUFFER)
                return a.Buffer.FirstElement==b.Buffer.FirstElement && a.Buffer.NumElements==b.Buffer.NumElements;
            return a.ViewDimension==D3D11_SRV_DIMENSION_BUFFEREX &&
                a.BufferEx.FirstElement==b.BufferEx.FirstElement && a.BufferEx.NumElements==b.BufferEx.NumElements && a.BufferEx.Flags==b.BufferEx.Flags;
        }
    public:
        Ptr<ID3D11ShaderResourceView> replacement(ID3D11ShaderResourceView* original,std::uint32_t* model_bit=nullptr,std::uint32_t allowed_models=~0u) {
            if(!original)return {};
            Ptr<ID3D11Resource> source;original->GetResource(&source);Ptr<ID3D11Buffer> source_buffer;
            if(FAILED(source.As(&source_buffer)))return {};
            const auto address=reinterpret_cast<std::uintptr_t>(source_buffer.Get());
            std::size_t model=0;
            for(;model<snapshot.models.size();++model)if(address==snapshot.models[model].current_buffer || address==snapshot.models[model].previous_buffer)break;
            if(model==snapshot.models.size() || !(allowed_models&(1u<<model)))return {};
            D3D11_SHADER_RESOURCE_VIEW_DESC desc{};original->GetDesc(&desc);
            if(desc.ViewDimension!=D3D11_SRV_DIMENSION_BUFFER && desc.ViewDimension!=D3D11_SRV_DIMENSION_BUFFEREX)return {};
            std::lock_guard<std::mutex> lock(mutex);
            for(const auto& view:views)if(view.model==model && same(view.desc,desc)){if(model_bit)*model_bit=1u<<model;return view.resource;}
            Ptr<ID3D11ShaderResourceView> view;
            check(device->CreateShaderResourceView(buffers[model].Get(),&desc,&view));
            views.push_back({model,desc,view});if(model_bit)*model_bit=1u<<model;return view;
        }
    };
    static std::shared_ptr<Prepared> prepare(ID3D11Device* device,Snapshot snapshot) {
        if(!device || !snapshot.generation || !snapshot.frame || !snapshot.tick || snapshot.models.empty() || snapshot.models.size()>3)
            throw std::invalid_argument("Invalid native note skin snapshot");
        auto result=std::make_shared<Prepared>();result->device=device;result->snapshot=std::move(snapshot);
        for(std::size_t i=0;i<result->snapshot.models.size();++i) {
            const auto& model=result->snapshot.models[i];
            if(!model.current_buffer || !model.previous_buffer || (model.palette.size()!=24 && model.palette.size()!=72))
                throw std::invalid_argument("Invalid native note skin model");
            for(std::size_t j=0;j<i;++j) {
                const auto& prior=result->snapshot.models[j];
                if(model.current_buffer==prior.current_buffer || model.current_buffer==prior.previous_buffer ||
                   model.previous_buffer==prior.current_buffer || model.previous_buffer==prior.previous_buffer)
                    throw std::invalid_argument("Ambiguous native note skin buffers");
            }
            for(const auto& row:model.palette)for(float f:row)if(!std::isfinite(f))throw std::invalid_argument("Nonfinite native note skin palette");
            D3D11_BUFFER_DESC desc{};desc.ByteWidth=static_cast<UINT>(model.palette.size()*sizeof(Rows));
            desc.Usage=D3D11_USAGE_IMMUTABLE;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            D3D11_SUBRESOURCE_DATA data{};data.pSysMem=model.palette.data();Ptr<ID3D11Buffer> buffer;
            check(device->CreateBuffer(&desc,&data,&buffer));result->buffers.push_back(buffer);
        }
        return result;
    }
    class Scope {
        using Get=void (STDMETHODCALLTYPE ID3D11DeviceContext::*)(UINT,UINT,ID3D11ShaderResourceView**);
        using Set=void (STDMETHODCALLTYPE ID3D11DeviceContext::*)(UINT,UINT,ID3D11ShaderResourceView* const*);
        struct Binding {Set set{};UINT slot{};Ptr<ID3D11ShaderResourceView> original;};
        ID3D11DeviceContext* context_{};std::shared_ptr<Prepared> snapshot_;std::vector<Binding> bindings_;std::uint32_t models_{},allowed_models_{};
        void stage(Get get,Set set) {
            constexpr UINT count=D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
            std::array<ID3D11ShaderResourceView*,count> raw{};(context_->*get)(0,count,raw.data());
            std::array<Ptr<ID3D11ShaderResourceView>,count> owned;
            for(UINT i=0;i<count;++i)owned[i].Attach(raw[i]);
            for(UINT i=0;i<count;++i) {
                std::uint32_t model_bit{};auto replacement=snapshot_->replacement(raw[i],&model_bit,allowed_models_);if(!replacement)continue;
                bindings_.push_back({set,i,owned[i]});auto* view=replacement.Get();(context_->*set)(i,1,&view);models_|=model_bit;
            }
        }
        void restore() noexcept {for(auto i=bindings_.rbegin();i!=bindings_.rend();++i){auto* view=i->original.Get();(context_->*i->set)(i->slot,1,&view);}bindings_.clear();}
    public:
        // Caller validates note generation, source ownership and tracking freshness.
        Scope(ID3D11DeviceContext* context,std::shared_ptr<Prepared> snapshot,bool active=true,std::uint32_t allowed_models=~0u):context_(context),snapshot_(std::move(snapshot)),allowed_models_(allowed_models) {
            if(!active || !context_ || !snapshot_ || !allowed_models_)return;
            bindings_.reserve(10);
            try {
                Ptr<ID3D11VertexShader> vs;context_->VSGetShader(&vs,nullptr,nullptr);if(vs)stage(&ID3D11DeviceContext::VSGetShaderResources,&ID3D11DeviceContext::VSSetShaderResources);
                Ptr<ID3D11HullShader> hs;context_->HSGetShader(&hs,nullptr,nullptr);if(hs)stage(&ID3D11DeviceContext::HSGetShaderResources,&ID3D11DeviceContext::HSSetShaderResources);
                Ptr<ID3D11DomainShader> ds;context_->DSGetShader(&ds,nullptr,nullptr);if(ds)stage(&ID3D11DeviceContext::DSGetShaderResources,&ID3D11DeviceContext::DSSetShaderResources);
                Ptr<ID3D11GeometryShader> gs;context_->GSGetShader(&gs,nullptr,nullptr);if(gs)stage(&ID3D11DeviceContext::GSGetShaderResources,&ID3D11DeviceContext::GSSetShaderResources);
                Ptr<ID3D11PixelShader> ps;context_->PSGetShader(&ps,nullptr,nullptr);if(ps)stage(&ID3D11DeviceContext::PSGetShaderResources,&ID3D11DeviceContext::PSSetShaderResources);
            } catch(...) {restore();throw;}
        }
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
        ~Scope(){restore();}
        std::size_t replaced()const noexcept{return bindings_.size();}
        std::uint32_t models()const noexcept{return models_;}
    };
};
}
