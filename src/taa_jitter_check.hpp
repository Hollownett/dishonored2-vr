#pragma once
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <ostream>
#include <string>

namespace d2vr {
// Every-frame phase check (the rival's "phaseloop" idea, our own code). The native TAA dispatch's per-view constants (CS b1,
// CBPerViewGlobals, 0x320 bytes; offsets from the TAA shader source in base/shaderCache) hold the jitter its image was rendered
// with: cb_projectionmatrix (0xb0) and cb_previousprojectionmatrix (0x170) store the frustum shift 2*jitter (+ off-centre) at
// floats 8 and 9, cb_jittervectors (0x1d0) the current jitter. Its "previous" half equals the current one in paired views
// (v900 capture), so the previous jitter is taken from the previous projection, which is the one inside the previous
// view-projection the engine's motion vectors were built from (v900: prevVP = prevV*prevP, carrying the eye's last jitter).
struct PassJitter {float cur[2],prev[2];}; // UV of the render size, engine sign (view+0x7850)
inline PassJitter cb_pass_jitter(const unsigned char* b) noexcept {
    float p[2],pp[2],jv[2];
    std::memcpy(p,b+0xb0+32,8);std::memcpy(pp,b+0x170+32,8);std::memcpy(jv,b+0x1d0,8);
    // prev = the previous projection's shift less this frame's off-centre (0 for symmetric frusta).
    return {{jv[0],jv[1]},{pp[0]*.5f-(p[0]*.5f-jv[0]),pp[1]*.5f-(p[1]*.5f-jv[1])}};
}
inline bool same_jitter(const float* a,const float* b) noexcept {return std::abs(a[0]-b[0])<1e-6f && std::abs(a[1]-b[1])<1e-6f;}

// The dispatch copies b1 next to what the mod used for that pass; three frames later the copy is read back without waiting.
class TaaJitterCheck {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    static constexpr float nan=std::numeric_limits<float>::quiet_NaN();
public:
    static constexpr UINT block_bytes=0x320;
    // FSR's current jitter, the native TAA dejitter pair (cur, prev) and the pass view's own +0x7850/+0x7858 at the dispatch.
    struct Used {float fsr[2]{nan,nan},taa[4]{nan,nan,nan,nan},view[4]{nan,nan,nan,nan};};
    static constexpr const char* header="frame,eye,cb_cur_x,cb_cur_y,cb_prev_x,cb_prev_y,fsr_x,fsr_y,taa_cur_x,taa_cur_y,taa_prev_x,taa_prev_y,view_cur_x,view_cur_y,view_prev_x,view_prev_y";
    // Per eye: [checked, mismatched] for the FSR jitter, the TAA pair and the view fields.
    std::array<std::array<std::atomic<std::uint64_t>,6>,2> counts{};
    std::atomic<std::uint64_t> skips{};
private:
    struct Slot {Ptr<ID3D11Buffer> staging;std::atomic<int> state{0};std::uint64_t frame{};int eye{};Used used{};}; // 0 free, 1 copying, 2 recorded
    std::array<Slot,16> ring_{};
    std::mutex mutex_;
    Ptr<ID3D11Device> device_;
    unsigned rows_{};
    void tally(int eye,int k,bool checked,bool bad) noexcept {
        if(!checked) return;
        ++counts[static_cast<std::size_t>(eye)][static_cast<std::size_t>(k*2)];
        if(bad) ++counts[static_cast<std::size_t>(eye)][static_cast<std::size_t>(k*2+1)];
    }
public:
    // Job thread, on the dispatch's own deferred context, before the dispatch is replaced or recorded.
    void record(ID3D11DeviceContext* c,ID3D11Buffer* cb,int eye,std::uint64_t frame,const Used& used) noexcept {
        if(!c || !cb || eye<0 || eye>1) return;
        D3D11_BUFFER_DESC d{};cb->GetDesc(&d);
        if(d.ByteWidth!=block_bytes) {++skips;return;}
        Slot* slot{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(!device_) {
                Ptr<ID3D11Device> device;cb->GetDevice(device.GetAddressOf());
                D3D11_BUFFER_DESC s{};s.ByteWidth=block_bytes;s.Usage=D3D11_USAGE_STAGING;s.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
                for(auto& r:ring_) if(!device || FAILED(device->CreateBuffer(&s,nullptr,r.staging.GetAddressOf()))) {++skips;for(auto& x:ring_) x.staging.Reset();return;}
                device_=device;
            }
            for(auto& r:ring_) if(!r.state.load(std::memory_order_relaxed)) {slot=&r;r.state.store(1,std::memory_order_relaxed);break;}
            if(!slot) {++skips;return;}
            slot->frame=frame;slot->eye=eye;slot->used=used;
        }
        c->CopyResource(slot->staging.Get(),cb);
        slot->state.store(2,std::memory_order_release);
    }
    // Present thread with the immediate context; csv (optional) gets the first `max_rows` compared passes.
    void drain(ID3D11DeviceContext* immediate,std::uint64_t frame,std::ostream* csv,unsigned max_rows=600) {
        if(!immediate) return;
        for(auto& r:ring_) {
            if(r.state.load(std::memory_order_acquire)!=2 || r.frame>frame || frame-r.frame<3) continue;
            D3D11_MAPPED_SUBRESOURCE m{};
            const HRESULT hr=immediate->Map(r.staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&m);
            if(hr==DXGI_ERROR_WAS_STILL_DRAWING) continue;
            if(SUCCEEDED(hr) && m.pData) {
                const auto truth=cb_pass_jitter(static_cast<const unsigned char*>(m.pData));immediate->Unmap(r.staging.Get(),0);
                const auto& u=r.used;
                tally(r.eye,0,!std::isnan(u.fsr[0]),!same_jitter(u.fsr,truth.cur));
                tally(r.eye,1,!std::isnan(u.taa[0]),!same_jitter(u.taa,truth.cur) || !same_jitter(u.taa+2,truth.prev));
                tally(r.eye,2,!std::isnan(u.view[0]),!same_jitter(u.view,truth.cur) || !same_jitter(u.view+2,truth.prev));
                if(csv && rows_<max_rows && r.frame%50<4) { // four consecutive frames of every fifty, for the whole session
                    if(!rows_++) *csv<<header<<'\n';
                    *csv<<r.frame<<','<<r.eye<<','<<truth.cur[0]<<','<<truth.cur[1]<<','<<truth.prev[0]<<','<<truth.prev[1];
                    for(float f:u.fsr) *csv<<','<<f;
                    for(float f:u.taa) *csv<<','<<f;
                    for(float f:u.view) *csv<<','<<f;
                    *csv<<'\n';
                    if(rows_%16==0) csv->flush();
                }
            } else ++skips;
            r.state.store(0,std::memory_order_release);
        }
    }
    std::string json() const {
        std::string s="{";
        static constexpr const char* names[3]{"fsr","taa","view"};
        for(int k=0;k<3;++k) {
            s+=std::string(k?",\"":"\"")+names[k]+"\":[";
            for(int e=0;e<2;++e) s+=(e?",[":"[")+std::to_string(counts[static_cast<std::size_t>(e)][static_cast<std::size_t>(k*2)].load())+','+
                std::to_string(counts[static_cast<std::size_t>(e)][static_cast<std::size_t>(k*2+1)].load())+']';
            s+=']';
        }
        return s+",\"skips\":"+std::to_string(skips.load())+'}';
    }
};
} // namespace d2vr
