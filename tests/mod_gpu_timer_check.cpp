#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/mod_gpu_timer.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
// Plan E task 3b: the mod's own GPU spans. Labels sum per frame, an inactive timer is a no-op, DONOTFLUSH polling publishes.
template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
static void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
int main() {
    try {
        Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"create device");
        d2vr::ModGpuTimer timer;
        check(!timer.active() && timer.begin(context.Get(),d2vr::ModSpan::eye_copy)==0,"inactive timer is a no-op");
        check(timer.prepare(device.Get()) && timer.active(),"prepare");
        D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=1024;desc.MipLevels=desc.ArraySize=1;desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        Ptr<ID3D11Texture2D> a,b;check(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&a)) && SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&b)),"textures");
        const auto t1=timer.begin(context.Get(),d2vr::ModSpan::eye_copy);check(t1!=0,"begin span");
        context->CopyResource(b.Get(),a.Get());timer.end(context.Get(),t1);
        const auto t2=timer.begin(context.Get(),d2vr::ModSpan::eye_copy);context->CopyResource(b.Get(),a.Get());timer.end(context.Get(),t2);
        const auto t3=timer.begin(context.Get(),d2vr::ModSpan::fsr_eye1);context->CopyResource(b.Get(),a.Get());timer.end(context.Get(),t3);
        timer.end(context.Get(),t3); // double end ignored
        check(timer.begin(nullptr,d2vr::ModSpan::fsr_eye0)==0,"foreign context rejected");
        timer.finish_frame(context.Get());
        context->Flush(); // test-only progress; production never flushes
        const auto deadline=GetTickCount64()+3000;
        while(timer.stats().frames<1 && GetTickCount64()<deadline) {timer.poll(context.Get());Sleep(1);}
        const auto s=timer.stats();
        check(s.frames==1 && s.last.valid,"frame published");
        check(s.last.ms[static_cast<unsigned>(d2vr::ModSpan::eye_copy)]>=0 && s.last.ms[static_cast<unsigned>(d2vr::ModSpan::fsr_eye1)]>=0 && s.last.ms[static_cast<unsigned>(d2vr::ModSpan::layer_overlays)]==0,"labels separate");
        for(unsigned i=0;i<d2vr::ModGpuTimer::max_spans+3;++i) {const auto t=timer.begin(context.Get(),d2vr::ModSpan::aa_sharpen);if(t) timer.end(context.Get(),t);}
        check(timer.stats().dropped_spans==3,"bounded spans");
        { d2vr::ModGpuScope scope(context.Get(),d2vr::ModSpan::eye_capture); }
        timer.clear();check(!timer.active(),"clear");
        std::printf("PASS: mod gpu timer\n");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
