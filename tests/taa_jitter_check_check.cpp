// v910: the per-view jitter read from a TAA dispatch's b1 (taa_jitter_check.hpp) and the GPU dejitter step that reads the same
// b1 (taa_mv_dejitter.hpp shader_source, run on WARP) agree with the CPU step for the engine pattern, symmetric and off-centre.
#include "../src/taa_jitter_check.hpp"
#include "../src/taa_mv_dejitter.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
using Microsoft::WRL::ComPtr;
static void check(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);std::exit(1);}}
static std::vector<unsigned char> block(float jx,float jy,float px,float py,float off) {
    std::vector<unsigned char> b(0x320);
    const float p[16]{.7265f,0,0,0, 0,.7002f,0,0, 2*jx+off,2*jy,1.4e-6f,-1, 0,0,.01f,0};
    const float pp[16]{.7265f,0,0,0, 0,.7002f,0,0, 2*px+off,2*py,1.4e-6f,-1, 0,0,.01f,0};
    const float jv[4]{jx,jy,jx,jy}; // paired views: the "previous" half repeats the current jitter (v900 capture)
    std::memcpy(b.data()+0xb0,p,64);std::memcpy(b.data()+0x170,pp,64);std::memcpy(b.data()+0x1d0,jv,16);
    return b;
}
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"warp device");
    ComPtr<ID3DBlob> code;
    check(SUCCEEDED(D3DCompile(d2vr::TaaMvDejitter::shader_source,sizeof d2vr::TaaMvDejitter::shader_source-1,nullptr,nullptr,nullptr,"main","cs_5_0",0,0,&code,nullptr)),"compile");
    ComPtr<ID3D11ComputeShader> cs;check(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs)),"shader");
    D3D11_TEXTURE2D_DESC td{};td.Width=8;td.Height=8;td.MipLevels=1;td.ArraySize=1;td.Format=DXGI_FORMAT_R32G32_FLOAT;td.SampleDesc.Count=1;
    td.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    std::vector<float> field(8*8*2);for(std::size_t k=0;k<field.size();k+=2) {field[k]=.01f;field[k+1]=-.02f;}
    field[(1*8+1)*2]=0;field[(1*8+1)*2+1]=0; // one texel the motion-vector pass never wrote (the sky keeps (0,0))
    D3D11_SUBRESOURCE_DATA init{field.data(),8*8,0};
    ComPtr<ID3D11Texture2D> in,out,staging;check(SUCCEEDED(device->CreateTexture2D(&td,&init,&in)) && SUCCEEDED(device->CreateTexture2D(&td,nullptr,&out)),"textures");
    td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&staging)),"staging");
    ComPtr<ID3D11ShaderResourceView> srv;ComPtr<ID3D11UnorderedAccessView> uav;
    check(SUCCEEDED(device->CreateShaderResourceView(in.Get(),nullptr,&srv)) && SUCCEEDED(device->CreateUnorderedAccessView(out.Get(),nullptr,&uav)),"views");
    const auto run=[&](const std::vector<unsigned char>& per_view,const float (&c)[4]) {
        D3D11_BUFFER_DESC bd{};bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;bd.ByteWidth=16;
        D3D11_SUBRESOURCE_DATA d0{c,0,0},d1{per_view.data(),0,0};ComPtr<ID3D11Buffer> b0,b1;
        check(SUCCEEDED(device->CreateBuffer(&bd,&d0,&b0)),"b0");bd.ByteWidth=0x320;check(SUCCEEDED(device->CreateBuffer(&bd,&d1,&b1)),"b1");
        ID3D11Buffer* cbs[2]{b0.Get(),b1.Get()};
        context->CSSetShader(cs.Get(),nullptr,0);context->CSSetConstantBuffers(0,2,cbs);
        context->CSSetShaderResources(0,1,srv.GetAddressOf());context->CSSetUnorderedAccessViews(0,1,uav.GetAddressOf(),nullptr);
        context->Dispatch(1,1,1);context->CopyResource(staging.Get(),out.Get());
        D3D11_MAPPED_SUBRESOURCE m{};check(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&m)),"map");
        std::array<float,4> v{};const auto* bytes=static_cast<const unsigned char*>(m.pData);
        std::memcpy(v.data(),bytes+m.RowPitch*3+8*5,8);std::memcpy(v.data()+2,bytes+m.RowPitch*1+8*1,8);context->Unmap(staging.Get(),0);
        return std::array<float,4>{.01f-v[0],-.02f-v[1],v[2],v[3]}; // the step taken from a written texel, and the unwritten texel
    };
    const float w=2496,h=2688; // pattern (3,7)/16 px after (-7,1)/16 px
    const float cx=3/16.f/w,cy=7/16.f/h,px=-7/16.f/w,py=1/16.f/h;
    for(float off:{0.f,.137f}) {
        const auto b=block(cx,cy,px,py,off);const auto j=d2vr::cb_pass_jitter(b.data());
        check(std::abs(j.cur[0]-cx)<1e-9f && std::abs(j.cur[1]-cy)<1e-9f,"cb current jitter");
        check(std::abs(j.prev[0]-px)<5e-8f && std::abs(j.prev[1]-py)<1e-9f,"cb previous jitter from the previous projection");
        const auto want=d2vr::TaaMvDejitter::step(cx,cy,px,py);
        const auto gpu=run(b,{9,9,1,0});
        check(std::abs(gpu[0]-want[0])<5e-8f && std::abs(gpu[1]-want[1])<5e-8f,"gpu step from b1 equals the cpu step");
        check(gpu[2]==0 && gpu[3]==0,"an unwritten (0,0) texel stays (0,0)");
    }
    const auto legacy=run(block(cx,cy,px,py,0),{1e-3f,-2e-3f,0,0});
    check(std::abs(legacy[0]-1e-3f)<1e-7f && std::abs(legacy[1]+2e-3f)<1e-7f && legacy[2]==-1e-3f && legacy[3]==2e-3f,"legacy constant step, everywhere");
    std::printf("taa_jitter_check_check OK\n");
    return 0;
}
