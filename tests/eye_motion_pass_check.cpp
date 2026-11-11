// v1151 offline check of the motion texture pass (src/eye_motion_pass.hpp) on WARP: the real constants builder and the real HLSL on
// one render's inputs. Driven by tools/alt_motion_probe.py --check-pass, which writes the inputs from an alt-motion probe dump (or a
// synthetic scene) and compares the output with its own numpy reference.
//   eye_motion_pass_check <cameras.txt> <depth.r32> <mv.rg16f> <out.rgba16f>
//   eye_motion_pass_check ... <out.rgba16f> legacy   (v1178: MVs of a dump made before v1178, without the jitter step)
// cameras.txt: current then previous snapshot, each 16 projection, 16 view-projection, 3 position, 2 jitter floats (whitespace separated).
// Raw files: u32 width, height, DXGI format, bytes per row, then rows (the probe's format). Output: the same header, RGBA16F rows.
#include <windows.h>
#include <d3d11.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include "../src/eye_motion_pass.hpp"
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using Microsoft::WRL::ComPtr;

static bool read_raw(const char* path,UINT header[4],std::vector<char>& data) {
    std::ifstream f(path,std::ios::binary);
    if(!f.read(reinterpret_cast<char*>(header),16)) return false;
    data.assign(static_cast<std::size_t>(header[1])*header[3],0);
    return static_cast<bool>(f.read(data.data(),static_cast<std::streamsize>(data.size())));
}
static ComPtr<ID3D11Texture2D> texture(ID3D11Device* d,UINT w,UINT h,DXGI_FORMAT f,const std::vector<char>* data,UINT pitch,UINT bind) {
    D3D11_TEXTURE2D_DESC t{};t.Width=w;t.Height=h;t.MipLevels=t.ArraySize=1;t.Format=f;t.SampleDesc.Count=1;t.Usage=D3D11_USAGE_DEFAULT;t.BindFlags=bind;
    D3D11_SUBRESOURCE_DATA s{data?data->data():nullptr,pitch,0};ComPtr<ID3D11Texture2D> out;
    if(FAILED(d->CreateTexture2D(&t,data?&s:nullptr,&out))) return nullptr;
    return out;
}
static float half_to_float(unsigned short h) {
    const unsigned e=(h>>10)&31,m=h&1023;const float sign=(h>>15)?-1.f:1.f;
    if(e==31) return m?NAN:sign*INFINITY;
    return sign*(e?std::ldexp(1.f+m/1024.f,int(e)-15):std::ldexp(m/1024.f,-14));
}
static std::vector<float> run(ID3D11Device* device,ID3D11DeviceContext* context,UINT w,UINT h,const std::vector<char>& depth,const std::vector<char>& mv,
                              const d2vr::EyeMotionConstants& k,std::vector<char>* raw=nullptr);
// v1153 edge case: the previous camera turned 90 degrees, so the surface (a wall 2 m ahead) straddles its camera plane. Every channel
// must be finite, w <= 4096, and pixels within 1 cm of the previous plane (the image's middle column) must be 0.
static int edge_case(ID3D11Device* device,ID3D11DeviceContext* context) {
    const UINT w=64,h=64;
    d2vr::alternate_taa::Snapshot cur{},prev{};
    const std::array<float,16> proj{.97f,0,0,0, 0,.97f,0,0, 0,0,1.4e-6f,.01f, 0,0,-1,0};
    cur.matrices[0]=prev.matrices[0]=proj;cur.matrices[2]=proj;
    // previous view: yaw 90 degrees (world -x ahead... the wall's x=0 line lies in its camera plane)
    const std::array<float,16> yaw{0,0,-1,0, 0,1,0,0, 1,0,0,0, 0,0,0,1};
    for(int i=0;i<4;++i) for(int j=0;j<4;++j) {float t=0;for(int q=0;q<4;++q) t+=proj[i*4+q]*yaw[q*4+j];prev.matrices[2][i*4+j]=t;}
    std::vector<char> depth(w*h*4),mv(w*h*4,0);
    for(UINT n=0;n<w*h;++n) {const float d=.01f/2;std::memcpy(depth.data()+n*4,&d,4);}
    d2vr::EyeMotionConstants k{};
    if(!d2vr::eye_motion_constants(cur,prev,w,h,k)) {std::fprintf(stderr,"edge: constants refused\n");return 1;}
    const auto out=run(device,context,w,h,depth,mv,k);
    if(out.size()!=w*h*4) return 1;
    float top=0;unsigned bad=0,middle_nonzero=0;
    for(UINT y=0;y<h;++y) for(UINT x=0;x<w;++x) for(int c=0;c<4;++c) {
        const float v=out[(y*w+x)*4+c];
        if(!std::isfinite(v)) ++bad;
        if(c==3) top=(std::max)(top,v);
        if((x==w/2-1 || x==w/2) && v!=0) ++middle_nonzero;
    }
    std::printf("edge: non-finite %u, max w %.1f, nonzero at the previous camera plane %u\n",bad,top,middle_nonzero);
    return bad || top>4096 || middle_nonzero?1:0;
}
int main(int argc,char** argv) {
    if(argc==2 && !std::strcmp(argv[1],"--edge")) {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context))) return 1;
        const int r=edge_case(device.Get(),context.Get());std::printf(r?"edge: FAIL\n":"edge: ok\n");return r;
    }
    // v1178: optional 5th argument "legacy": the dump's MVs predate v1178 (jitter step not in the vectors)
    if(argc!=5 && !(argc==6 && !std::strcmp(argv[5],"legacy"))) {std::fprintf(stderr,"usage: eye_motion_pass_check cameras.txt depth.r32 mv.rg16f out.rgba16f [legacy]\n");return 2;}
    d2vr::eye_motion_mv_jittered=argc!=6;
    d2vr::alternate_taa::Snapshot snap[3]{};bool other=false; // v1154: an optional third line, the other eye's render one Present earlier
    {
        std::ifstream f(argv[1]);
        for(int n=0;n<3;++n) {
            auto& s=snap[n];
            if(n==2) {f>>std::ws;if(f.peek()==EOF) break;other=true;}
            for(float& v:s.matrices[0]) f>>v;
            for(float& v:s.matrices[2]) f>>v;
            for(float& v:s.position) f>>v;
            for(float& v:s.jitter) f>>v;
        }
        if(f.fail()) {std::fprintf(stderr,"bad cameras file\n");return 2;}
    }
    UINT dh[4],mh[4];std::vector<char> depth,mv;
    if(!read_raw(argv[2],dh,depth) || !read_raw(argv[3],mh,mv) || dh[2]!=DXGI_FORMAT_R32_FLOAT || mh[2]!=DXGI_FORMAT_R16G16_FLOAT || dh[0]!=mh[0] || dh[1]!=mh[1]) {
        std::fprintf(stderr,"bad depth/mv input\n");return 2;
    }
    const UINT w=dh[0],h=dh[1];
    d2vr::EyeMotionConstants k{};
    if(!d2vr::eye_motion_constants(snap[0],snap[1],w,h,k,other?&snap[2]:nullptr)) {std::fprintf(stderr,"constants refused\n");return 1;}
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context))) {std::fprintf(stderr,"no WARP\n");return 1;}
    if(dh[3]!=w*4 || mh[3]!=w*4) {std::fprintf(stderr,"rows must be tightly packed\n");return 2;}
    std::vector<char> raw;
    if(run(device.Get(),context.Get(),w,h,depth,mv,k,&raw).empty()) {std::fprintf(stderr,"pass failed\n");return 1;}
    std::ofstream out(argv[4],std::ios::binary);
    const UINT header[4]{w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,w*8};out.write(reinterpret_cast<const char*>(header),16);
    out.write(raw.data(),static_cast<std::streamsize>(raw.size()));
    std::printf("eye_motion_pass_check: wrote %ux%u\n",w,h);
    return out?0:1;
}

static std::vector<float> run(ID3D11Device* device,ID3D11DeviceContext* context,UINT w,UINT h,const std::vector<char>& depth,const std::vector<char>& mv,
                              const d2vr::EyeMotionConstants& k,std::vector<char>* raw) {
    const auto dt=texture(device,w,h,DXGI_FORMAT_R32_FLOAT,&depth,w*4,D3D11_BIND_SHADER_RESOURCE);
    const auto mt=texture(device,w,h,DXGI_FORMAT_R16G16_FLOAT,&mv,w*4,D3D11_BIND_SHADER_RESOURCE);
    const auto ot=texture(device,w,h,DXGI_FORMAT_R16G16B16A16_FLOAT,nullptr,0,D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS);
    ComPtr<ID3D11ShaderResourceView> ds,ms;ComPtr<ID3D11UnorderedAccessView> ou;
    if(!dt || !mt || !ot || FAILED(device->CreateShaderResourceView(dt.Get(),nullptr,&ds)) || FAILED(device->CreateShaderResourceView(mt.Get(),nullptr,&ms)) ||
       FAILED(device->CreateUnorderedAccessView(ot.Get(),nullptr,&ou))) return {};
    d2vr::EyeMotionPass pass;
    if(!pass.prepare(device) || !pass.dispatch(context,ms.Get(),ds.Get(),ou.Get(),k)) return {};
    D3D11_TEXTURE2D_DESC sd{};ot->GetDesc(&sd);sd.Usage=D3D11_USAGE_STAGING;sd.BindFlags=0;sd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;D3D11_MAPPED_SUBRESOURCE m{};
    if(FAILED(device->CreateTexture2D(&sd,nullptr,&staging))) return {};
    context->CopyResource(staging.Get(),ot.Get());
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m))) return {};
    std::vector<float> out(static_cast<std::size_t>(w)*h*4);
    if(raw) raw->resize(static_cast<std::size_t>(w)*h*8);
    for(UINT y=0;y<h;++y) {
        const auto* row=static_cast<const unsigned short*>(static_cast<const void*>(static_cast<const char*>(m.pData)+static_cast<std::size_t>(y)*m.RowPitch));
        if(raw) std::memcpy(raw->data()+static_cast<std::size_t>(y)*w*8,row,w*8);
        for(UINT x=0;x<w*4;++x) out[static_cast<std::size_t>(y)*w*4+x]=half_to_float(row[x]);
    }
    context->Unmap(staging.Get(),0);
    return out;
}
