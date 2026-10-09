#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/eye_antialiasing.hpp"
#include "../src/render_size.hpp"
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdlib>

using Microsoft::WRL::ComPtr;
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
ComPtr<ID3D11Texture2D> input(ID3D11Device* device,UINT w,UINT h,const std::vector<std::byte>& pixels) {
    require(pixels.size()==static_cast<std::size_t>(w)*h*4,"Input byte count differs");
    D3D11_TEXTURE2D_DESC desc{};desc.Width=w;desc.Height=h;desc.MipLevels=1;desc.ArraySize=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;desc.SampleDesc.Count=1;desc.Usage=D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=pixels.data();data.SysMemPitch=w*4;
    ComPtr<ID3D11Texture2D> result;
    require(SUCCEEDED(device->CreateTexture2D(&desc,&data,&result)),"Test input allocation failed");return result;
}
int main(int argc,char** argv) {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&device,nullptr,&context)),"Cannot create WARP device");
        d2vr::EyeAntialiasing aa(device.Get());
        const D3D11_VIEWPORT native{7,11,123,99,.1f,.9f};context->RSSetViewports(1,&native);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        constexpr UINT width=256,height=192;
        std::vector<std::byte> pixels(width*height*4);
        for(UINT y=0;y<height;++y) for(UINT x=0;x<width;++x) {
            const auto shade=std::byte(x>y*3/4+40?230:20);const auto i=(y*width+x)*4;
            pixels[i]=pixels[i+1]=pixels[i+2]=shade;pixels[i+3]=std::byte{255};
        }
        auto source=input(device.Get(),width,height,pixels);
        const auto filtered=d2vr::readback_rgba(aa.apply(source.Get()));
        require(d2vr::readback_rgba(source.Get()).bytes==pixels,"Native source was modified");
        UINT changed{},intermediate{};
        for(std::size_t i=0;i<pixels.size();i+=4) {
            if(filtered.bytes[i]!=pixels[i]) ++changed;
            const auto value=std::to_integer<UINT>(filtered.bytes[i]);
            if(value>25 && value<225) ++intermediate;
            require(filtered.bytes[i+3]==std::byte{255},"Opaque input alpha changed");
        }
        require(changed>100 && changed<3000 && intermediate>100,"Diagonal edges did not get localized coverage blending");
        require(std::to_integer<int>(filtered.bytes[0])>=19 && std::to_integer<int>(filtered.bytes[0])<=21,"Flat dark colour changed");
        require(std::to_integer<int>(filtered.bytes[(width-1)*4])>=229 && std::to_integer<int>(filtered.bytes[(width-1)*4])<=231,"Flat bright colour changed");
        // A uniform opposite-eye image must clear all prior edge/weight data.
        std::vector<std::byte> uniform(117*83*4,std::byte{70});
        for(std::size_t i=3;i<uniform.size();i+=4) uniform[i]=std::byte{255};
        auto other=input(device.Get(),117,83,uniform);
        const auto flat=d2vr::readback_rgba(aa.apply(other.Get()));
        for(std::size_t i=0;i<flat.bytes.size();++i)
            require(std::abs(std::to_integer<int>(flat.bytes[i])-std::to_integer<int>(uniform[i]))<=1,"Opposite eye/frame contaminated a flat image");
        require(d2vr::readback_rgba(aa.apply(source.Get())).bytes==filtered.bytes,"History affected repeated eye output");
        // A render size above the old 4096 capture bound must resize and filter.
        D3D11_TEXTURE2D_DESC large_desc{};large_desc.Width=4200;large_desc.Height=4500;
        large_desc.MipLevels=1;large_desc.ArraySize=1;large_desc.SampleDesc.Count=1;
        large_desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;large_desc.Usage=D3D11_USAGE_DEFAULT;
        ComPtr<ID3D11Texture2D> large;
        require(SUCCEEDED(device->CreateTexture2D(&large_desc,nullptr,&large)),"Cannot create above-bound SMAA input");
        const auto large_output=aa.apply(large.Get());
        require(large_output!=nullptr,"SMAA refused a render size above the old capture bound");
        D3D11_TEXTURE2D_DESC large_result{};large_output->GetDesc(&large_result);
        require(large_result.Width==4200 && large_result.Height==4500,"SMAA output size differs from the above-bound input");
        D3D11_VIEWPORT restored{};UINT count=1;context->RSGetViewports(&count,&restored);
        D3D11_PRIMITIVE_TOPOLOGY topology{};context->IAGetPrimitiveTopology(&topology);
        require(restored.TopLeftX==7 && restored.TopLeftY==11 && restored.Width==123 && topology==D3D11_PRIMITIVE_TOPOLOGY_LINELIST,"Game graphics state was not restored");
        if(argc==5) {
            const auto w=static_cast<UINT>(std::stoul(argv[2])),h=static_cast<UINT>(std::stoul(argv[3]));
            require(w>0 && h>0 && w<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION && h<=D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION,"Capture dimensions exceed limits");
            std::vector<std::byte> captured(static_cast<std::size_t>(w)*h*4);
            std::ifstream in(argv[1],std::ios::binary);in.read(reinterpret_cast<char*>(captured.data()),static_cast<std::streamsize>(captured.size()));
            require(in.gcount()==static_cast<std::streamsize>(captured.size()) && in.peek()==std::char_traits<char>::eof(),"Capture size differs");
            auto native_image=input(device.Get(),w,h,captured);
            const auto output=d2vr::readback_rgba(aa.apply(native_image.Get()));
            std::ofstream out(argv[4],std::ios::binary);out.write(reinterpret_cast<const char*>(output.bytes.data()),static_cast<std::streamsize>(output.bytes.size()));
            require(static_cast<bool>(out),"Cannot save comparison image");
        }
        std::cout << "PASS: SMAA Ultra edge coverage, flat colour/alpha, independent eyes, resizing, source preservation and D3D11 state restoration; changed=" << changed << " blended=" << intermediate << '\n';
        return 0;
    } catch(const std::exception& error) {std::cerr << error.what() << '\n';return 1;}
}
