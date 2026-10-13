#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/gpu_pass_timer.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>

template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
static void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
int main() {
    try {
        Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context,deferred;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"WARP device");
        d2vr::GpuPassTimer timer;check(!timer.prepare(nullptr) && !timer.valid(),"reject missing device");
        check(timer.prepare(device.Get()) && timer.valid(),"create timestamp ring");
        check(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"create deferred");
        check(timer.begin(deferred.Get())==-1,"timestamps sampled only on executed immediate path");
        const int active=timer.begin(context.Get());check(active>=0,"reserve first ring ticket");
        timer.end(deferred.Get(),active);
        Ptr<ID3D11Device> other;Ptr<ID3D11DeviceContext> foreign;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&other,nullptr,&foreign)),"second WARP device");
        timer.end(foreign.Get(),active);timer.poll(foreign.Get());
        check(timer.begin(foreign.Get())==-1,"foreign immediate context rejected");
        check(timer.begin(context.Get())==-1 && timer.stats().dropped==1,"overlapping measurement is refused without waiting");
        timer.end(context.Get(),active);
        for(unsigned i=1;i<8;++i) {const int ticket=timer.begin(context.Get());check(ticket>=0,"reserve ring ticket");timer.end(context.Get(),ticket);}
        context->Flush(); // test-only progress; timer never flushes.
        const auto deadline=GetTickCount64()+2000;
        while(timer.stats().samples+timer.stats().disjoint<8 && GetTickCount64()<deadline) {timer.poll(context.Get());Sleep(1);}
        const auto sample=timer.stats();
        check(sample.samples+sample.disjoint==8,"all timestamp samples retired");
        check(sample.samples>0 && std::isfinite(sample.last_ms) && sample.last_ms>=0,"valid elapsed GPU milliseconds");
        check(timer.prepare(device.Get()) && timer.stats().samples==sample.samples,"same device preserves measurement history");
        timer.end(context.Get(),-1);timer.end(context.Get(),99);
        timer.clear();check(!timer.valid() && timer.stats().samples==0 && timer.begin(context.Get())==-1,"owner cleanup discards query ring");
        check(timer.prepare(device.Get()),"reprepare timer");
        const int fresh=timer.begin(context.Get());check(fresh>=0 && fresh!=active,"ticket survives clear without aliasing");
        timer.end(context.Get(),active);
        check(timer.begin(context.Get())==-1,"stale ticket cannot finish new measurement");
        timer.end(context.Get(),fresh);timer.clear();
        std::printf("PASS: nonblocking timestamp ring, deferred rejection, overlap rejection, cleanup; %llu valid samples, %.6f ms (WARP)\n",static_cast<unsigned long long>(sample.samples),sample.last_ms);return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
