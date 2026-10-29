#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_art_bounds.hpp"
#include <cassert>
#include <iostream>
#include <vector>

using Microsoft::WRL::ComPtr;
int main(){
    _set_error_mode(_OUT_TO_STDERR);
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    constexpr UINT width=67,height=49;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> source;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&source)));
    ComPtr<ID3D11ShaderResourceView> original_srv;assert(SUCCEEDED(device->CreateShaderResourceView(source.Get(),nullptr,&original_srv)));
    auto* bound=original_srv.Get();context->CSSetShaderResources(0,1,&bound);
    std::vector<unsigned char> pixels(width*height*4);
    const auto upload=[&](UINT left,UINT top,UINT right,UINT bottom,unsigned char alpha=255){
        std::fill(pixels.begin(),pixels.end(),static_cast<unsigned char>(0));
        for(UINT y=top;y<bottom;++y)for(UINT x=left;x<right;++x){const auto index=(y*width+x)*4;pixels[index]=77;pixels[index+3]=alpha;}
        context->UpdateSubresource(source.Get(),0,nullptr,pixels.data(),width*4,0);
    };
    d2vr::NativeArtBounds bounds;
    ComPtr<ID3D11DeviceContext> deferred;assert(SUCCEEDED(device->CreateDeferredContext(0,&deferred)));
    assert(!bounds.configure(deferred.Get(),GetCurrentThreadId()));
    assert(!bounds.configure(context.Get(),GetCurrentThreadId()+1));
    assert(bounds.configure(context.Get(),GetCurrentThreadId()));
    const auto tick=GetTickCount64();
    const d2vr::NativeArtBounds::Metadata a{100,200,300,4,tick,11},b{101,200,300,4,tick,12},c{102,200,300,4,tick,13};
    upload(2,3,9,12);assert(bounds.queue(source.Get(),a));
    upload(64,45,67,49);assert(bounds.queue(source.Get(),b));
    upload(0,0,0,0);assert(bounds.queue(source.Get(),c));
    assert(!bounds.queue(source.Get(),a)); // bounded pending ring
    ComPtr<ID3D11ShaderResourceView> restored;context->CSGetShaderResources(0,1,&restored);assert(restored.Get()==original_srv.Get());
    // Fixture driver submission substitutes for the application's normal
    // Present. The helper never Flushes, spins, or blocks on GPU readiness.
    context->Flush();
    std::array<d2vr::NativeArtBounds::Lease,3> results{};
    unsigned count{};
    for(unsigned attempts=0;attempts<5000 && count<3;++attempts){
        if(auto result=bounds.poll())results[count++]=std::move(result);else Sleep(1);
    }
    assert(count==3);
    for(const auto& result:results){
        if(result->metadata.serial==11){assert(result->metadata.owner==100 && result->crop.left==2 && result->crop.top==3 && result->crop.right==9 && result->crop.bottom==12);
            assert(result->fresh(100,4,tick) && !result->fresh(101,4,tick) && !result->fresh(100,5,tick) && !result->fresh(100,4,tick+251));}
        else if(result->metadata.serial==12){assert(result->metadata.owner==101 && result->crop.left==64 && result->crop.top==45 && result->crop.right==67 && result->crop.bottom==49);}
        else {assert(result->metadata.serial==13 && result->empty && !result->fresh(102,4,tick));}
    }
    assert(!bounds.queue(source.Get(),a)); // leased textures cannot be overwritten
    auto staging_desc=desc;staging_desc.Usage=D3D11_USAGE_STAGING;staging_desc.BindFlags=0;staging_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;assert(SUCCEEDED(device->CreateTexture2D(&staging_desc,nullptr,&staging)));
    const auto pixels_at=[&](ID3D11Texture2D* image,UINT x,UINT y){
        context->CopyResource(staging.Get(),image);D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
        std::array<unsigned char,4> pixel{};std::memcpy(pixel.data(),static_cast<unsigned char*>(map.pData)+y*map.RowPitch+x*4,4);context->Unmap(staging.Get(),0);return pixel;
    };
    assert(pixels_at(source.Get(),2,3)[3]==0); // original remains last uploaded empty image
    for(const auto& result:results)if(result->metadata.serial==11){assert(pixels_at(result->texture.Get(),2,3)[3]==255 && pixels_at(result->texture.Get(),64,45)[3]==0);}
    // Enqueue the core's compact copy, release only that lease, and reuse the
    // SAME full-size ring surface without waiting between these operations.
    unsigned first_result=3;for(unsigned i=0;i<3;++i)if(results[i]->metadata.serial==11)first_result=i;
    assert(first_result<3);const auto* reused_surface=results[first_result]->texture.Get();
    auto compact_desc=desc;compact_desc.Width=7;compact_desc.Height=9;
    ComPtr<ID3D11Texture2D> compact;assert(SUCCEEDED(device->CreateTexture2D(&compact_desc,nullptr,&compact)));
    context->CopySubresourceRegion(compact.Get(),0,0,0,0,results[first_result]->texture.Get(),0,&results[first_result]->crop);
    results[first_result].reset();upload(20,10,24,15);assert(bounds.queue(source.Get(),a));
    context->Flush();d2vr::NativeArtBounds::Lease moved;
    for(unsigned attempts=0;attempts<5000 && !moved;++attempts){moved=bounds.poll();if(!moved)Sleep(1);}
    assert(moved && moved->crop.left==20 && moved->crop.top==10 && moved->crop.right==24 && moved->crop.bottom==15);
    assert(moved->texture.Get()==reused_surface); // proves actual ring overwrite
    compact_desc.Usage=D3D11_USAGE_STAGING;compact_desc.BindFlags=0;compact_desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> compact_staging;assert(SUCCEEDED(device->CreateTexture2D(&compact_desc,nullptr,&compact_staging)));
    context->CopyResource(compact_staging.Get(),compact.Get());D3D11_MAPPED_SUBRESOURCE compact_map{};
    assert(SUCCEEDED(context->Map(compact_staging.Get(),0,D3D11_MAP_READ,0,&compact_map)));
    for(UINT y=0;y<9;++y)for(UINT x=0;x<7;++x){
        const auto* p=static_cast<unsigned char*>(compact_map.pData)+y*compact_map.RowPitch+x*4;
        assert(p[0]==77 && p[3]==255); // earlier crop survives subsequent ring copy
    }
    context->Unmap(compact_staging.Get(),0);
    assert(pixels_at(moved->texture.Get(),2,3)[3]==0 && pixels_at(moved->texture.Get(),20,10)[3]==255);
    auto invalid=a;invalid.owner=0;assert(!bounds.queue(source.Get(),invalid));
    invalid=a;invalid.serial=0;assert(!bounds.queue(source.Get(),invalid));
    // A near-transparent residual (alpha 1-3) is not artwork: the 2026-10-29 blank
    // tutorial capture must report empty, or it suppresses the readable hint.
    results={};moved.reset();
    upload(3,3,6,6,1);auto faint=a;faint.serial=14;assert(bounds.queue(source.Get(),faint));context->Flush();
    d2vr::NativeArtBounds::Lease faint_result;
    for(unsigned attempts=0;attempts<5000 && !faint_result;++attempts){faint_result=bounds.poll();if(!faint_result)Sleep(1);}
    assert(faint_result && faint_result->metadata.serial==14 && faint_result->empty);
    std::cout<<"PASS: GPU native alpha bounds, matched pixels, compact-copy/lease-release ordering, moving crops, backpressure, freshness and faint-alpha rejection\n";
}
