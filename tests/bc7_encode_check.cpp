#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>
#include "../src/bc7_encode.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using Microsoft::WRL::ComPtr;
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}

// Independent mode 6 decoder used as the reference for both the CPU round trip
// and the WARP decode: mode (bits 0..6), R0 R1 G0 G1 B0 B1 A0 A1 at 7 bits,
// P0 P1, the anchor index in 3 bits, then fifteen 4-bit indices.
constexpr unsigned kWeights[16]={0,4,9,13,17,21,26,30,34,38,43,47,51,55,60,64};

unsigned read_bits(const std::uint8_t* block,unsigned position,unsigned count) {
    unsigned value=0;
    for(unsigned bit=0;bit<count;++bit) {
        const unsigned at=position+bit;
        value|=((block[at>>3]>>(at&7))&1u)<<bit;
    }
    return value;
}
struct Block {unsigned endpoint[2][4];unsigned index[16];};
Block decode_block(const std::uint8_t* block) {
    require((block[0]&0x7fu)==0x40u,"Encoded block is not BC7 mode 6");
    Block result{};
    unsigned position=7;
    unsigned pbit[2];
    for(unsigned c=0;c<4;++c) for(unsigned e=0;e<2;++e) {
        result.endpoint[e][c]=read_bits(block,position,7);position+=7;
    }
    pbit[0]=read_bits(block,position,1);++position;
    pbit[1]=read_bits(block,position,1);++position;
    for(unsigned e=0;e<2;++e) for(unsigned c=0;c<4;++c)
        result.endpoint[e][c]=(result.endpoint[e][c]<<1)|pbit[e];
    result.index[0]=read_bits(block,position,3);position+=3;
    for(unsigned i=1;i<16;++i) {result.index[i]=read_bits(block,position,4);position+=4;}
    require(position==128,"A block consumed the wrong number of bits");
    return result;
}
void palette(const Block& block,std::uint8_t colours[16][4]) {
    for(unsigned k=0;k<16;++k) for(unsigned c=0;c<4;++c)
        colours[k][c]=static_cast<std::uint8_t>(((64-kWeights[k])*block.endpoint[0][c]+
            kWeights[k]*block.endpoint[1][c]+32)>>6);
}
// Also enforces the anchor rule on every block of every image under test.
std::vector<std::uint8_t> decode_image(const std::vector<std::uint8_t>& blocks,unsigned width,unsigned height) {
    const unsigned across=(width+3)/4,down=(height+3)/4;
    require(blocks.size()==static_cast<std::size_t>(across)*down*16,"Encoded byte count differs");
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width)*height*4);
    for(unsigned by=0;by<down;++by) for(unsigned bx=0;bx<across;++bx) {
        const auto block=decode_block(blocks.data()+(static_cast<std::size_t>(by)*across+bx)*16);
        require(block.index[0]<8,"An anchor index has its high bit set");
        std::uint8_t colours[16][4];palette(block,colours);
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
            const unsigned sx=bx*4+x,sy=by*4+y;
            if(sx>=width || sy>=height) continue;
            std::memcpy(image.data()+(static_cast<std::size_t>(sy)*width+sx)*4,colours[block.index[y*4+x]],4);
        }
    }
    return image;
}
struct Difference {unsigned maximum{};double mean{};};
Difference compare(const std::vector<std::uint8_t>& a,const std::vector<std::uint8_t>& b) {
    require(a.size()==b.size(),"Compared images differ in size");
    Difference result{};std::uint64_t total=0;
    for(std::size_t i=0;i<a.size();++i) {
        const auto difference=static_cast<unsigned>(std::abs(static_cast<int>(a[i])-static_cast<int>(b[i])));
        result.maximum=std::max(result.maximum,difference);total+=difference;
    }
    result.mean=a.empty()?0.0:static_cast<double>(total)/a.size();
    return result;
}
// Endpoints are quantised to +-1 and the palette only interpolates between them,
// so no decoded pixel may leave its block's per-channel source range.
void require_inside_block_range(const std::vector<std::uint8_t>& source,const std::vector<std::uint8_t>& decoded,
                                unsigned width,unsigned height) {
    const unsigned across=(width+3)/4,down=(height+3)/4;
    for(unsigned by=0;by<down;++by) for(unsigned bx=0;bx<across;++bx) {
        unsigned low[4]={255,255,255,255},high[4]{};
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
            const unsigned sx=std::min(bx*4+x,width-1),sy=std::min(by*4+y,height-1);
            const auto* pixel=source.data()+(static_cast<std::size_t>(sy)*width+sx)*4;
            for(unsigned c=0;c<4;++c) {
                low[c]=std::min(low[c],static_cast<unsigned>(pixel[c]));
                high[c]=std::max(high[c],static_cast<unsigned>(pixel[c]));
            }
        }
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
            const unsigned sx=std::min(bx*4+x,width-1),sy=std::min(by*4+y,height-1);
            const auto* pixel=decoded.data()+(static_cast<std::size_t>(sy)*width+sx)*4;
            for(unsigned c=0;c<4;++c)
                require(static_cast<unsigned>(pixel[c])+1u>=low[c] && static_cast<unsigned>(pixel[c])<=high[c]+1u,
                    "A decoded pixel left its block's source range");
        }
    }
}
// Worst error over blocks whose pixels are all opaque: those carry a single
// card-to-label direction, unlike blocks that also reach transparent black.
unsigned opaque_block_error(const std::vector<std::uint8_t>& source,const std::vector<std::uint8_t>& decoded,
                            unsigned width,unsigned height) {
    const unsigned across=(width+3)/4,down=(height+3)/4;
    unsigned maximum=0;
    for(unsigned by=0;by<down;++by) for(unsigned bx=0;bx<across;++bx) {
        unsigned minimum=255;
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
            const unsigned sx=std::min(bx*4+x,width-1),sy=std::min(by*4+y,height-1);
            minimum=std::min(minimum,static_cast<unsigned>(source[(static_cast<std::size_t>(sy)*width+sx)*4+3]));
        }
        if(minimum!=255) continue;
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
            const unsigned sx=std::min(bx*4+x,width-1),sy=std::min(by*4+y,height-1);
            const auto* a=source.data()+(static_cast<std::size_t>(sy)*width+sx)*4;
            const auto* b=decoded.data()+(static_cast<std::size_t>(sy)*width+sx)*4;
            for(unsigned c=0;c<4;++c)
                maximum=std::max(maximum,static_cast<unsigned>(std::abs(static_cast<int>(a[c])-static_cast<int>(b[c]))));
        }
    }
    return maximum;
}
// The source padded to whole blocks by replicating its last row and column, which
// is what the encoder is required to do for the partial blocks at either edge.
std::vector<std::uint8_t> pad_image(const std::vector<std::uint8_t>& image,unsigned width,unsigned height) {
    const unsigned padded_width=(width+3)/4*4,padded_height=(height+3)/4*4;
    std::vector<std::uint8_t> padded(static_cast<std::size_t>(padded_width)*padded_height*4);
    for(unsigned y=0;y<padded_height;++y) for(unsigned x=0;x<padded_width;++x) {
        const unsigned sx=std::min(x,width-1),sy=std::min(y,height-1);
        std::memcpy(padded.data()+(static_cast<std::size_t>(y)*padded_width+x)*4,
            image.data()+(static_cast<std::size_t>(sy)*width+sx)*4,4);
    }
    return padded;
}
// Black-transparent and white-opaque: every channel of both colours has the same
// parity, so each endpoint is representable exactly and the block must survive.
std::vector<std::uint8_t> two_colour(unsigned width,unsigned height,bool first_bright) {
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width)*height*4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        bool bright=(((x^y)&1u)!=0);
        if(!x && !y) bright=first_bright;
        const auto value=static_cast<std::uint8_t>(bright?255:0);
        auto* pixel=image.data()+(static_cast<std::size_t>(y)*width+x)*4;
        pixel[0]=pixel[1]=pixel[2]=pixel[3]=value;
    }
    return image;
}
std::vector<std::uint8_t> gradient(unsigned width,unsigned height) {
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width)*height*4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        auto* pixel=image.data()+(static_cast<std::size_t>(y)*width+x)*4;
        pixel[0]=static_cast<std::uint8_t>(x*255u/std::max(1u,width-1));
        pixel[1]=static_cast<std::uint8_t>(y*255u/std::max(1u,height-1));
        pixel[2]=static_cast<std::uint8_t>((x+y)*255u/std::max(1u,width+height-2));
        pixel[3]=static_cast<std::uint8_t>(255u-x*128u/std::max(1u,width-1));
    }
    return image;
}
// White text on a dark background with antialiased coverage that blends the
// colour and the alpha together, so every pixel of a block sits near the
// background-to-label line.
std::vector<std::uint8_t> text_image(unsigned width,unsigned height) {
    constexpr unsigned background[4]={8,8,12,0},label[4]={248,248,255,255};
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width)*height*4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        const unsigned bar=x%12,row=y%13;
        const unsigned coverage=(bar<5 || row<2)?255u:(bar<8?(7-bar)*85u:0u);
        auto* pixel=image.data()+(static_cast<std::size_t>(y)*width+x)*4;
        for(unsigned c=0;c<4;++c)
            pixel[c]=static_cast<std::uint8_t>((coverage*label[c]+(255-coverage)*background[c]+127)/255);
    }
    return image;
}
// The glyph painter marks every painted pixel opaque and leaves the untouched
// black fully transparent, so a block that reaches both ends of that range
// carries more than one colour direction and no min/max endpoint pair can fit
// it; such an image is only required to stay inside its block's source range.
std::vector<std::uint8_t> glyph_image(unsigned width,unsigned height) {
    constexpr unsigned card[4]={17,24,32,255},label[4]={236,241,245,255};
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width)*height*4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        if(x<2 || y<2 || x>=width-2 || y>=height-2) continue;
        const unsigned bar=(x/3)%4;
        const unsigned level=bar==0?255u:(bar==1?128u:0u);
        auto* pixel=image.data()+(static_cast<std::size_t>(y)*width+x)*4;
        for(unsigned c=0;c<4;++c)
            pixel[c]=static_cast<std::uint8_t>((level*label[c]+(255-level)*card[c]+127)/255);
    }
    return image;
}
// Pixels drawn exactly from a random mode 6 palette line, so the endpoint
// quantisation is the only loss. Every channel of the line rises from the dark
// end, so the block's per-channel min/max corners lie on that line, and the two
// extreme weights are always drawn so the block keeps the whole line.
std::vector<std::uint8_t> line_image(unsigned width,unsigned height,unsigned seed) {
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width)*height*4);
    const unsigned across=(width+3)/4,down=(height+3)/4;
    for(unsigned by=0;by<down;++by) for(unsigned bx=0;bx<across;++bx) {
        unsigned base[4],span[4];
        for(unsigned c=0;c<4;++c) {seed=seed*1664525u+1013904223u;base[c]=(seed>>24)&0x7fu;}
        for(unsigned c=0;c<4;++c) {seed=seed*1664525u+1013904223u;span[c]=(seed>>24)&0x7fu;}
        unsigned endpoint[2][4];
        for(unsigned c=0;c<4;++c) {endpoint[0][c]=base[c];endpoint[1][c]=base[c]+span[c];}
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
            seed=seed*1664525u+1013904223u;
            const unsigned weight=(!x && !y)?0u:((x==3 && y==3)?64u:kWeights[(seed>>16)&15u]);
            const unsigned sx=std::min(bx*4+x,width-1),sy=std::min(by*4+y,height-1);
            auto* pixel=image.data()+(static_cast<std::size_t>(sy)*width+sx)*4;
            for(unsigned c=0;c<4;++c)
                pixel[c]=static_cast<std::uint8_t>(((64-weight)*endpoint[0][c]+weight*endpoint[1][c]+32)>>6);
        }
    }
    return image;
}
std::vector<std::uint8_t> noise_image(unsigned width,unsigned height,unsigned seed) {
    std::vector<std::uint8_t> image(static_cast<std::size_t>(width)*height*4);
    for(auto& value:image) {seed=seed*1664525u+1013904223u;value=static_cast<std::uint8_t>(seed>>24);}
    return image;
}

// d3dcompiler.lib is not in this check's link line; the runtime compiler ships
// as a system DLL, so resolve it dynamically.
using CompileFn=HRESULT(WINAPI*)(LPCVOID,SIZE_T,LPCSTR,const D3D_SHADER_MACRO*,ID3DInclude*,LPCSTR,LPCSTR,UINT,UINT,
                                 ID3DBlob**,ID3DBlob**);
ComPtr<ID3DBlob> compile(const char* source,const char* entry,const char* profile) {
    static const HMODULE library=LoadLibraryW(L"d3dcompiler_47.dll");
    require(library!=nullptr,"d3dcompiler_47.dll is unavailable");
    static const auto compile_function=reinterpret_cast<CompileFn>(GetProcAddress(library,"D3DCompile"));
    require(compile_function!=nullptr,"D3DCompile is unavailable");
    ComPtr<ID3DBlob> binary,errors;
    const auto result=compile_function(source,std::strlen(source),"D2VR BC7 check",nullptr,nullptr,entry,profile,
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&binary,&errors);
    if(FAILED(result)) throw std::runtime_error(std::string("Shader ")+entry+" failed: "+
        (errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"compile failed"));
    return binary;
}
ComPtr<ID3D11Texture2D> texture(ID3D11Device* device,UINT width,UINT height,DXGI_FORMAT format,UINT bindings,
                                const void* initial,UINT pitch) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=format;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=bindings;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=initial;data.SysMemPitch=pitch;
    ComPtr<ID3D11Texture2D> result;
    require(SUCCEEDED(device->CreateTexture2D(&desc,initial?&data:nullptr,&result)),"Test texture allocation failed");
    return result;
}
std::vector<std::uint8_t> readback(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,
                                   UINT width,UINT height) {
    D3D11_TEXTURE2D_DESC desc{};desc.Width=width;desc.Height=height;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_STAGING;
    desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    require(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)),"Staging allocation failed");
    context->CopyResource(staging.Get(),source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    require(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped)),"Staging map failed");
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);
    for(UINT row=0;row<height;++row)
        std::memcpy(pixels.data()+static_cast<std::size_t>(row)*width*4,
            static_cast<const std::uint8_t*>(mapped.pData)+static_cast<std::size_t>(row)*mapped.RowPitch,
            static_cast<std::size_t>(width)*4);
    context->Unmap(staging.Get(),0);
    return pixels;
}
struct Renderer {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11SamplerState> sampler;
    explicit Renderer(ID3D11Device* owner):device(owner) {
        device->GetImmediateContext(&context);
        const std::string source=R"HLSL(
Texture2D sourceTex : register(t0);
SamplerState pointSampler : register(s0);
struct Vertex { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
Vertex CopyVS(uint id : SV_VertexID) {
    const float2 uv=float2((id<<1)&2,id&2);
    Vertex v;v.uv=uv;v.position=float4(uv*float2(2,-2)+float2(-1,1),0,1);return v;
}
float4 CopyPS(Vertex v) : SV_TARGET { return sourceTex.SampleLevel(pointSampler,v.uv,0); }
)HLSL";
        const auto vertex_binary=compile(source.c_str(),"CopyVS","vs_5_0"),
            pixel_binary=compile(source.c_str(),"CopyPS","ps_5_0");
        require(SUCCEEDED(device->CreateVertexShader(vertex_binary->GetBufferPointer(),vertex_binary->GetBufferSize(),
            nullptr,&vertex)),"Vertex shader creation failed");
        require(SUCCEEDED(device->CreatePixelShader(pixel_binary->GetBufferPointer(),pixel_binary->GetBufferSize(),
            nullptr,&pixel)),"Pixel shader creation failed");
        D3D11_SAMPLER_DESC description{};description.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        description.AddressU=description.AddressV=description.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        description.MaxLOD=D3D11_FLOAT32_MAX;
        require(SUCCEEDED(device->CreateSamplerState(&description,&sampler)),"Sampler creation failed");
    }
    // One-to-one point sample of the source into an RGBA8 render target and back.
    std::vector<std::uint8_t> draw(ID3D11Texture2D* source,DXGI_FORMAT view_format,UINT width,UINT height) {
        D3D11_SHADER_RESOURCE_VIEW_DESC view_description{};view_description.Format=view_format;
        view_description.ViewDimension=D3D11_SRV_DIMENSION_TEXTURE2D;view_description.Texture2D.MipLevels=1;
        ComPtr<ID3D11ShaderResourceView> view;
        require(SUCCEEDED(device->CreateShaderResourceView(source,&view_description,&view)),"Source view creation failed");
        auto target=texture(device.Get(),width,height,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_RENDER_TARGET,nullptr,0);
        D3D11_RENDER_TARGET_VIEW_DESC target_description{};target_description.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
        target_description.ViewDimension=D3D11_RTV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11RenderTargetView> render_target;
        require(SUCCEEDED(device->CreateRenderTargetView(target.Get(),&target_description,&render_target)),
            "Target view creation failed");
        const D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
        context->RSSetViewports(1,&viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11SamplerState* samplers[]{sampler.Get()};context->PSSetSamplers(0,1,samplers);
        ID3D11ShaderResourceView* views[]{view.Get()};context->PSSetShaderResources(0,1,views);
        auto* target_view=render_target.Get();context->OMSetRenderTargets(1,&target_view,nullptr);
        context->VSSetShader(vertex.Get(),nullptr,0);context->PSSetShader(pixel.Get(),nullptr,0);
        context->Draw(3,0);
        ID3D11ShaderResourceView* empty[1]{};context->PSSetShaderResources(0,1,empty);
        context->OMSetRenderTargets(0,nullptr,nullptr);
        return readback(device.Get(),context.Get(),target.Get(),width,height);
    }
};
int main() {
    try {
        // Two-colour blocks must survive bit exactly, including the anchor swap
        // that a bright first pixel forces.
        const auto dark_first=two_colour(24,20,false);
        require(decode_image(d2vr::encode_bc7_mode6(dark_first.data(),24,20),24,20)==dark_first,
            "A two-colour block was not reproduced exactly");
        const auto bright_first=two_colour(24,20,true);
        require(decode_image(d2vr::encode_bc7_mode6(bright_first.data(),24,20),24,20)==bright_first,
            "The anchor swap changed the two-colour round trip");
        // Smooth gradient: bounded per-channel error.
        constexpr unsigned gradient_width=37,gradient_height=19;
        const auto smooth=gradient(gradient_width,gradient_height);
        const auto smooth_error=compare(decode_image(d2vr::encode_bc7_mode6(smooth.data(),gradient_width,gradient_height),
            gradient_width,gradient_height),smooth);
        require(smooth_error.maximum<=20,"A smooth gradient exceeded 20 levels of error");
        // Text-like image: antialiased label coverage over dark transparency.
        constexpr unsigned text_size=64;
        const auto text=text_image(text_size,text_size);
        const auto text_error=compare(decode_image(d2vr::encode_bc7_mode6(text.data(),text_size,text_size),
            text_size,text_size),text);
        require(text_error.maximum<=20,"A text-like image exceeded 20 levels of error");
        // Glyph-style data as the mod paints it: opaque card and label over
        // untouched transparent black.
        const auto glyph=glyph_image(text_size,text_size);
        const auto glyph_decoded=decode_image(d2vr::encode_bc7_mode6(glyph.data(),text_size,text_size),
            text_size,text_size);
        require_inside_block_range(glyph,glyph_decoded,text_size,text_size);
        const auto glyph_error=compare(glyph_decoded,glyph);
        const auto glyph_opaque=opaque_block_error(glyph,glyph_decoded,text_size,text_size);
        require(glyph_opaque<=24,"An all-opaque glyph block exceeded 24 levels of error");
        // Data drawn from random mode 6 palette lines: quantisation only.
        constexpr unsigned line_width=45,line_height=23;
        const auto line=line_image(line_width,line_height,7);
        const auto line_error=compare(decode_image(d2vr::encode_bc7_mode6(line.data(),line_width,line_height),
            line_width,line_height),line);
        require(line_error.maximum<=4,"Random palette-line data lost more than the endpoint quantisation");
        // Uniform noise: the palette may only interpolate the block's endpoints.
        constexpr unsigned noise_width=33,noise_height=17;
        const auto noise=noise_image(noise_width,noise_height,11);
        const auto noise_decoded=decode_image(d2vr::encode_bc7_mode6(noise.data(),noise_width,noise_height),
            noise_width,noise_height);
        require_inside_block_range(noise,noise_decoded,noise_width,noise_height);
        const auto noise_error=compare(noise_decoded,noise);
        // The partial blocks of a 33x17 image must match an explicitly padded copy.
        const auto padded=pad_image(noise,noise_width,noise_height);
        require(d2vr::encode_bc7_mode6(noise.data(),noise_width,noise_height)==
            d2vr::encode_bc7_mode6(padded.data(),(noise_width+3)/4*4,(noise_height+3)/4*4),
            "Edge blocks did not replicate the last pixel");
        // A degenerate size and an all-flat image.
        require(d2vr::encode_bc7_mode6(nullptr,4,4).empty(),"A missing input was accepted");
        require(d2vr::encode_bc7_mode6(noise.data(),0,4).empty(),"A zero width was accepted");
        require(d2vr::encode_bc7_mode6(noise.data(),4,0).empty(),"A zero height was accepted");
        const std::vector<std::uint8_t> flat(4*16,std::uint8_t{100});
        const auto flat_decoded=decode_image(d2vr::encode_bc7_mode6(flat.data(),4,4),4,4);
        for(unsigned i=0;i<4;++i) require(flat_decoded[i]==100,"A flat block was not reproduced exactly");
        // WARP decode against the reference decoder, linear and sRGB.
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
            &device,nullptr,&context)),"Cannot create WARP device");
        Renderer renderer(device.Get());
        unsigned srgb_difference=0;
        // D3D11 rejects a BC texture whose dimensions are not multiples of four,
        // so the GPU sizes stay block aligned; the edge-replication behaviour is
        // checked on the CPU against an explicitly padded source instead.
        for(const auto& subject:{std::make_pair(32u,16u),std::make_pair(64u,64u),std::make_pair(36u,20u)}) {
            const unsigned width=subject.first,height=subject.second;
            const auto source=noise_image(width,height,width*31+height);
            const auto blocks=d2vr::encode_bc7_mode6(source.data(),width,height);
            const auto reference=decode_image(blocks,width,height);
            const UINT pitch=((width+3)/4)*16;
            auto linear=texture(device.Get(),width,height,DXGI_FORMAT_BC7_UNORM,D3D11_BIND_SHADER_RESOURCE,
                blocks.data(),pitch);
            const auto decoded=renderer.draw(linear.Get(),DXGI_FORMAT_BC7_UNORM,width,height);
            const auto error=compare(decoded,reference);
            require(error.maximum<=1,"The WARP BC7_UNORM decode differs from the reference decoder");
            // The sRGB view must decode exactly like an sRGB RGBA8 texture holding
            // the same bytes; both are sampled into a linear RGBA8 target.
            auto srgb=texture(device.Get(),width,height,DXGI_FORMAT_BC7_UNORM_SRGB,D3D11_BIND_SHADER_RESOURCE,
                blocks.data(),pitch);
            auto plain=texture(device.Get(),width,height,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
                D3D11_BIND_SHADER_RESOURCE,reference.data(),width*4);
            const auto from_block=renderer.draw(srgb.Get(),DXGI_FORMAT_BC7_UNORM_SRGB,width,height);
            const auto from_plain=renderer.draw(plain.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,width,height);
            const auto srgb_error=compare(from_block,from_plain);
            require(srgb_error.maximum<=2,"The BC7_UNORM_SRGB view does not decode like the equivalent sRGB texture");
            srgb_difference=std::max(srgb_difference,srgb_error.maximum);
        }
        std::cout << "PASS: BC7 mode 6 round trip (two-colour exact including the anchor swap, smooth max="
            << smooth_error.maximum << " mean=" << smooth_error.mean << ", text max=" << text_error.maximum
            << " mean=" << text_error.mean << ", random palette lines max=" << line_error.maximum
            << ", uniform noise max=" << noise_error.maximum << " mean=" << noise_error.mean
            << ", glyph opaque-block max=" << glyph_opaque << " mixed transparent/label blocks max="
            << glyph_error.maximum << " inside their block range"
            << "); WARP BC7_UNORM and BC7_UNORM_SRGB decode matches the reference decoder (srgb max="
            << srgb_difference << ")\n";
        return 0;
    } catch(const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
