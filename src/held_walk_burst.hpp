#pragma once
// v1249 diagnostic (diagnostics on): the user sees held loose objects (plates, ashtrays, a severed head) look wrong while WALKING, yet every
// number says they are drawn exactly in the hand. This saves a BURST of 16 consecutive presented frames of the final eye image(s) while a
// physically held prop / severed part / game-carried (use) thing is held >= 1 s and the player moves > 1 m/s; a second burst per kind above
// 3 m/s. At most 6 bursts a session. Per eye the CENTRE-BOTTOM CROP (half the eye each way, x centred, y from a third down) is copied on the GPU
// into a ring of 32 staging textures (no scaling path exists to reuse; a crop needs none), read back AFTER the burst with
// D3D11_MAP_FLAG_DO_NOT_WAIT (2 a call), and written by a worker thread:
//   held-burst-<kind>-<n>-f<00..15>-<left|right>.rgba   raw RGBA8 of the crop
//   held-burst-<kind>-<n>.json                          sizes, crop origin, mode, per-frame tick/dt/speeds, counters, status
// Not recorded (the publisher's B and the eye VPs are not at hand at the final-eye hook): the projected pose origin and hold_sync lag.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace d2vr::held_walk_burst {
enum Kind:unsigned {prop,severed,use,kinds};
inline const char* kind_name(unsigned k) noexcept {static const char* n[]={"prop","severed","use"};return k<kinds?n[k]:"?";}

// Pure trigger / state machine (times in ms). A hold is "alive" while a placement was noted within gap_ms.
struct Machine {
    static constexpr std::uint64_t hold_ms=1000,gap_ms=300,cooldown_ms=2000;
    static constexpr double walk_mps=1.0,run_mps=3.0;
    static constexpr unsigned frames=16,max_per_kind=2;
    struct Hold {int kind{-1};std::uint64_t since{},last{};};
    std::array<Hold,2> hold{};
    std::array<unsigned,kinds> bursts{};
    bool active{};unsigned kind{},hand{},index{},captured{};std::uint64_t cooldown_until{};
    void note(unsigned h,unsigned k,std::uint64_t now) {
        if(h>1 || k>=kinds) return;
        auto& x=hold[h];
        if(x.kind!=static_cast<int>(k) || now>x.last+gap_ms) {x.kind=static_cast<int>(k);x.since=now;}
        x.last=now;
    }
    // true = a burst starts now (hand/kind/index set). index = 1 (walking) or 2 (running tier).
    bool try_start(std::uint64_t now,double speed) {
        if(active || now<cooldown_until) return false;
        for(unsigned h=0;h<2;++h) {
            const auto& x=hold[h];
            if(x.kind<0 || now>x.last+gap_ms || now-x.since<hold_ms) continue;
            const unsigned k=static_cast<unsigned>(x.kind);
            const bool go=(bursts[k]==0 && speed>walk_mps) || (bursts[k]==1 && speed>run_mps);
            if(!go) continue;
            active=true;hand=h;kind=k;index=++bursts[k];captured=0;return true;
        }
        return false;
    }
    // One presented frame captured; true when the burst is complete.
    bool frame_done(std::uint64_t now) {
        if(!active || ++captured<frames) return false;
        active=false;cooldown_until=now+cooldown_ms;return true;
    }
    bool exhausted() const {for(auto b:bursts) if(b<max_per_kind) return false;return true;}
};

#ifndef D2VR_HELD_WALK_BURST_PURE
struct Frame {std::uint64_t tick{};double dt_ms{},player{},hand{};int slot[2]{-1,-1};};
struct Counters {std::uint64_t a[4]{};};   // held_exact_placed[0..1], held_use_exact[0..1]
struct State {
    Machine m;
    std::atomic<std::uint64_t> placed_ms[2]{},bursts_done[kinds]{};   // note_place mirror: written from the publisher thread
    std::atomic<int> placed_kind[2]{-1,-1};
    std::atomic<std::uint64_t> frames_captured{},dropped{},bytes_written{},readback_us_max{};
    std::string last_error;
    // capture ring
    Microsoft::WRL::ComPtr<ID3D11Device> device;Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>,32> ring{};
    std::array<int,32> slot_eye{};std::array<int,32> slot_frame{};
    UINT cw{},ch{},cx{},cy{},sw{},sh{};DXGI_FORMAT format{};
    unsigned used{},read_next{};bool reading{};
    std::vector<Frame> fr;int last_eye{-1};std::uint64_t last_tick{};
    Counters c_start,c_end;bool alternate{};
    std::wstring stem;std::filesystem::path dir;
    // writer
    std::mutex wm;std::condition_variable wcv;std::deque<std::pair<std::filesystem::path,std::vector<char>>> q;bool worker{};
};
inline State& st() {static State s;return s;}

// Publisher thread (held_placed in publish_weapon_draw): a thing is placed in a hand this render. kind: prop/severed/use.
inline void note_place(unsigned hand,unsigned kind) noexcept {
    if(hand>1 || kind>=kinds) return;
    auto& s=st();s.placed_kind[hand]=static_cast<int>(kind);s.placed_ms[hand]=GetTickCount64();
}
inline void enqueue(const std::filesystem::path& p,std::vector<char>&& b) {
    auto& s=st();
    {std::lock_guard<std::mutex> l(s.wm);s.q.emplace_back(p,std::move(b));
     if(!s.worker) {
        s.worker=true;
        std::thread([]{auto& s=st();for(;;) {
            std::pair<std::filesystem::path,std::vector<char>> j;
            {std::unique_lock<std::mutex> l(s.wm);s.wcv.wait(l,[&]{return !s.q.empty();});j=std::move(s.q.front());s.q.pop_front();}
            try {std::ofstream(j.first,std::ios::binary|std::ios::trunc).write(j.second.data(),static_cast<std::streamsize>(j.second.size()));s.bytes_written+=j.second.size();} catch(...) {}
        }}).detach();
     }}
    s.wcv.notify_one();
}
inline void release_ring() {auto& s=st();for(auto& t:s.ring) t.Reset();s.ctx.Reset();s.device.Reset();s.reading=false;s.used=0;s.read_next=0;}
inline void write_json(bool complete) {
    auto& s=st();
    std::string j="{\"width\":"+std::to_string(s.cw)+",\"height\":"+std::to_string(s.ch)+",\"crop_x\":"+std::to_string(s.cx)+",\"crop_y\":"+std::to_string(s.cy)+
        ",\"eye_width\":"+std::to_string(s.sw)+",\"eye_height\":"+std::to_string(s.sh)+",\"dxgi_format\":"+std::to_string(static_cast<unsigned>(s.format))+
        ",\"kind\":\""+kind_name(s.m.kind)+"\",\"hand\":"+std::to_string(s.m.hand)+",\"n\":"+std::to_string(s.m.index)+",\"mode\":\""+(s.alternate?"alternate":"paired")+
        "\",\"complete\":"+(complete?"true":"false")+",\"counters_start\":["+std::to_string(s.c_start.a[0])+","+std::to_string(s.c_start.a[1])+","+std::to_string(s.c_start.a[2])+","+std::to_string(s.c_start.a[3])+
        "],\"counters_end\":["+std::to_string(s.c_end.a[0])+","+std::to_string(s.c_end.a[1])+","+std::to_string(s.c_end.a[2])+","+std::to_string(s.c_end.a[3])+"],\"frames\":[";
    for(std::size_t i=0;i<s.fr.size();++i) {
        const auto& f=s.fr[i];
        j+=std::string(i?",":"")+"{\"tick\":"+std::to_string(f.tick)+",\"dt_ms\":"+std::to_string(f.dt_ms)+",\"player_mps\":"+std::to_string(f.player)+",\"hand_mps\":"+std::to_string(f.hand)+
            ",\"slots\":["+std::to_string(f.slot[0])+","+std::to_string(f.slot[1])+"]}";
    }
    j+="]}\n";
    enqueue(s.dir/(s.stem+L".json"),std::vector<char>(j.begin(),j.end()));
}
// Read up to two captured slots back (never waits); true when the whole burst is read.
inline bool read_some() {
    auto& s=st();
    for(unsigned n=0;n<2 && s.read_next<s.used;) {
        LARGE_INTEGER a,b,fq;QueryPerformanceFrequency(&fq);QueryPerformanceCounter(&a);
        D3D11_MAPPED_SUBRESOURCE mp{};
        const HRESULT hr=s.ctx->Map(s.ring[s.read_next].Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mp);
        if(hr==DXGI_ERROR_WAS_STILL_DRAWING) return false;   // retry next call
        const unsigned i=s.read_next++;++n;
        if(FAILED(hr) || !mp.pData) {++s.dropped;s.last_error="map failed";continue;}
        const std::size_t row=static_cast<std::size_t>(s.cw)*4;std::vector<char> out(row*s.ch);
        for(UINT y=0;y<s.ch;++y) std::memcpy(out.data()+y*row,static_cast<const char*>(mp.pData)+static_cast<std::size_t>(y)*mp.RowPitch,row);
        s.ctx->Unmap(s.ring[i].Get(),0);
        QueryPerformanceCounter(&b);
        const auto us=static_cast<std::uint64_t>((b.QuadPart-a.QuadPart)*1000000/fq.QuadPart);
        if(us>s.readback_us_max) s.readback_us_max=us;
        char nm[64];std::snprintf(nm,sizeof nm,"-f%02d-%s.rgba",s.slot_frame[i],s.slot_eye[i]?"right":"left");
        enqueue(s.dir/(s.stem+std::wstring(nm,nm+std::strlen(nm))),std::move(out));
        s.ring[i].Reset();
    }
    return s.read_next>=s.used;
}
// Final-eye hook (the render thread, once per submitted eye image). texture = the eye image with every widget drawn.
inline void observe(unsigned eye,ID3D11Texture2D* texture,double player_mps,const std::array<float,2>& hand_mps,bool alternate,
                    const Counters& counters,const std::filesystem::path& dir) noexcept {
    if(eye>1 || !texture) return;
    try {
        auto& s=st();const auto now=GetTickCount64();
        for(unsigned h=0;h<2;++h) {
            const int k=s.placed_kind[h].load();
            if(k>=0 && now<=s.placed_ms[h].load()+Machine::gap_ms) s.m.note(h,static_cast<unsigned>(k),s.placed_ms[h].load());
        }
        if(s.reading) {
            if(read_some()) {write_json(true);s.c_end=counters;release_ring();s.fr.clear();}
            return;
        }
        if(!s.m.active) {
            if(s.m.exhausted() || !s.m.try_start(now,player_mps)) return;
            D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
            if(d.SampleDesc.Count!=1 || d.ArraySize!=1 || (d.Format!=DXGI_FORMAT_R8G8B8A8_TYPELESS && d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && d.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB &&
                d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM && d.Format!=DXGI_FORMAT_B8G8R8A8_UNORM_SRGB && d.Format!=DXGI_FORMAT_B8G8R8A8_TYPELESS)) {
                s.last_error="eye format";s.m.active=false;s.m.cooldown_until=now+60000;return;
            }
            s.sw=d.Width;s.sh=d.Height;s.format=d.Format;s.cw=d.Width/2;s.ch=d.Height/2;s.cx=(d.Width-s.cw)/2;s.cy=d.Height/3;
            texture->GetDevice(&s.device);s.device->GetImmediateContext(&s.ctx);
            d.Width=s.cw;d.Height=s.ch;d.MipLevels=1;d.Usage=D3D11_USAGE_STAGING;d.BindFlags=0;d.CPUAccessFlags=D3D11_CPU_ACCESS_READ;d.MiscFlags=0;
            for(auto& t:s.ring) if(FAILED(s.device->CreateTexture2D(&d,nullptr,&t))) {s.last_error="ring alloc";release_ring();s.m.active=false;s.m.cooldown_until=now+60000;return;}
            s.alternate=alternate;s.c_start=counters;s.fr.clear();s.last_eye=-1;s.used=0;s.read_next=0;s.dir=dir;
            s.stem=L"held-burst-"+std::wstring(kind_name(s.m.kind),kind_name(s.m.kind)+std::strlen(kind_name(s.m.kind)))+L"-"+std::to_wstring(s.m.index);
            s.last_tick=now;
        }
        // capture this eye image into the next slot; a new frame starts when the eye index does not rise (or on every call in alternate mode)
        const bool new_frame=s.fr.empty() || alternate || static_cast<int>(eye)<=s.last_eye;
        if(new_frame && !s.fr.empty() && s.m.frame_done(now)) {  // burst complete: the previous call finished it; do not capture a 17th frame
            s.c_end=counters;s.reading=true;return;
        }
        if(new_frame) {Frame f;f.tick=now;f.dt_ms=static_cast<double>(now-s.last_tick);f.player=player_mps;f.hand=hand_mps[s.m.hand];s.last_tick=now;s.fr.push_back(f);}
        s.last_eye=static_cast<int>(eye);
        if(s.used>=s.ring.size()) {++s.dropped;return;}
        const D3D11_BOX box{s.cx,s.cy,0,s.cx+s.cw,s.cy+s.ch,1};
        s.ctx->CopySubresourceRegion(s.ring[s.used].Get(),0,0,0,0,texture,0,&box);
        s.slot_eye[s.used]=static_cast<int>(eye);s.slot_frame[s.used]=static_cast<int>(s.fr.size())-1;
        s.fr.back().slot[eye]=static_cast<int>(s.used);++s.used;++s.frames_captured;
    } catch(...) {st().last_error="exception";}
}
inline std::string status_json() {
    auto& s=st();
    std::string e=s.last_error;for(auto& c:e) if(c=='"'||c=='\\') c='_';
    std::string o="{\"bursts\":{";
    for(unsigned k=0;k<kinds;++k) o+=std::string(k?",":"")+"\""+kind_name(k)+"\":"+std::to_string(s.m.bursts[k]);
    o+="},\"active\":"+std::string(s.m.active?"true":"false")+",\"reading\":"+(s.reading?"true":"false")+",\"frames_captured\":"+std::to_string(s.frames_captured.load())+
        ",\"dropped\":"+std::to_string(s.dropped.load())+",\"readback_ms_max\":"+std::to_string(s.readback_us_max.load()/1000.0)+
        ",\"bytes_written\":"+std::to_string(s.bytes_written.load())+",\"last_error\":\""+e+"\"}";
    return o;
}
#endif
}
