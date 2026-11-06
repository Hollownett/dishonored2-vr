#pragma once
// Vertex shader motion rows for the mod's per-eye object motion (src/object_motion.hpp), recorded from process start:
// the game creates its shaders a few seconds after it starts, long before the proxy starts the mod (present + settle),
// and Direct3D cannot return a shader's bytecode later. install() hooks D3D11CreateDevice(AndSwapChain) when the proxy
// loads, then the first created device's CreateVertexShader (vtable 12, shared by every device of that class), and
// reflects each shader that names the current MVP rows (cb_mvpmatrixx/y/z/w); the previous rows are recorded when the
// shader also names cb_previousmvpmatrixx/y/z/w. v1103: a shader with current rows only (the held weapon's colour/forward
// vertex shader; docs/WEAPON-DRAW-PLACE-TEMPORAL.md "Round 3") is recorded with all four previous bytes 0xff "absent" and
// is placed on its current rows. Before v1103 reflect() returned 0 for it, so ObjectMotion never saw the colour draw.
// D2vrVertexShaderLayout(shader): byte k = float4 index of cb_mvpmatrix{x,y,z,w}[k] (k<4), then of
// cb_previousmvpmatrix{x,y,z,w}, in the cbuffer at b2; 0xff = not declared. 0 = not a layout (no b2 cbuffer or no current
// x/y/w rows). A current-only layout is non-zero: its top four bytes are 0xffffffff.
// D2vrVertexShaderStats(out[5]): shaders created, with previous rows, not stored (table full), devices left unhooked,
// current-only layouts recorded.
#include <windows.h>
#include <d3d11.h>
#include <d3d11shader.h>
#include <MinHook.h>
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace d2vr::vs_layouts {
inline constexpr std::uint64_t absent=0xff;
// The four previous-row bytes of a packed layout. All four 0xff: the shader declares no previous rows.
inline constexpr std::uint64_t previous_absent=0xffffffff00000000ull;
inline bool has_previous(std::uint64_t layout) noexcept {return layout && (layout&previous_absent)!=previous_absent;}

inline std::uint64_t reflect(const void* code,SIZE_T size) noexcept {
    // The cheap gate before D3DReflect: only a shader whose bytecode names the current MVP rows can be a layout, and a
    // shader with both row sets names them too (cb_previousmvpmatrix does not contain this substring). This keeps the
    // per-CreateVertexShader cost to one scan of the bytecode for the ~2/3 of vertex shaders that are not model shaders.
    static constexpr char marker[]="cb_mvpmatrix";
    const auto* bytes=static_cast<const char*>(code);
    if(std::search(bytes,bytes+size,marker,marker+sizeof marker-1)==bytes+size) return 0;
    using Reflect=HRESULT(WINAPI*)(LPCVOID,SIZE_T,REFIID,void**);
    static const Reflect d3d_reflect=[] {
        const HMODULE compiler=LoadLibraryW(L"d3dcompiler_47.dll");
        return compiler?reinterpret_cast<Reflect>(reinterpret_cast<void*>(GetProcAddress(compiler,"D3DReflect"))):nullptr;
    }();
    ID3D11ShaderReflection* r{};
    if(!d3d_reflect || FAILED(d3d_reflect(code,size,__uuidof(ID3D11ShaderReflection),reinterpret_cast<void**>(&r))) || !r) return 0;
    static constexpr const char* names[8]={"cb_mvpmatrixx","cb_mvpmatrixy","cb_mvpmatrixz","cb_mvpmatrixw",
        "cb_previousmvpmatrixx","cb_previousmvpmatrixy","cb_previousmvpmatrixz","cb_previousmvpmatrixw"};
    std::uint64_t packed=0;D3D11_SHADER_DESC desc{};
    if(SUCCEEDED(r->GetDesc(&desc))) for(UINT i=0;i<desc.BoundResources;++i) {
        D3D11_SHADER_INPUT_BIND_DESC bind{};
        if(FAILED(r->GetResourceBindingDesc(i,&bind)) || bind.Type!=D3D_SIT_CBUFFER || bind.BindPoint!=2) continue;
        ID3D11ShaderReflectionConstantBuffer* cb=r->GetConstantBufferByName(bind.Name);
        bool have_x=false,have_y=false,have_w=false;
        for(unsigned k=0;k<8 && cb;++k) {
            D3D11_SHADER_VARIABLE_DESC v{};std::uint64_t row=absent;
            ID3D11ShaderReflectionVariable* var=cb->GetVariableByName(names[k]);
            if(var && SUCCEEDED(var->GetDesc(&v)) && v.StartOffset%16==0 && v.StartOffset/16<absent) {
                row=v.StartOffset/16;
                if(k==0) have_x=true; else if(k==1) have_y=true; else if(k==3) have_w=true;
            }
            packed|=row<<(8*k);
        }
        // x, y and w are the rows object motion needs (z may be filled from the eye's VP); without them this is not a
        // layout. The previous bytes stay 0xff when absent, which is exactly the "current-only" encoding.
        if(!have_x || !have_y || !have_w) packed=0;
        break;
    }
    r->Release();
    return packed;
}

struct Slot {std::atomic<void*> shader{};std::atomic<std::uint64_t> layout{};};
// Capacity: the shipped shader cache has 6,184 vertex shaders; 2,023 declare the current MVP rows (1,931 current-only,
// 92 with previous rows), and the v1007 session created 6,251 vertex shaders. 1<<16 slots leave >10x headroom for the
// whole cache plus runtime variants; stats[2] below is the "not stored (table full)" canary if that ever changes.
inline constexpr unsigned table_size=1u<<16;
inline Slot table[table_size];
inline std::atomic<std::uint64_t> stats[5]{};

// Slots are claimed once per address and never freed: a shader created at a reused address overwrites its layout.
inline Slot* find(void* shader,bool claim) noexcept {
    const unsigned h=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(shader)>>4)*0x9e3779b1u>>16;
    for(unsigned i=0;i<512;++i) {
        Slot& s=table[(h+i)&(table_size-1)];void* key=s.shader.load(std::memory_order_acquire);
        if(key==shader) return &s;
        if(key) continue;
        if(!claim) return nullptr;
        if(s.shader.compare_exchange_strong(key,shader,std::memory_order_acq_rel) || key==shader) return &s;
    }
    return nullptr;
}

using CreateVs=HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*,const void*,SIZE_T,ID3D11ClassLinkage*,ID3D11VertexShader**);
inline CreateVs create_vs{};
inline void* create_vs_target{};
inline std::atomic<bool> hooking{};

inline HRESULT STDMETHODCALLTYPE create_vs_hook(ID3D11Device* d,const void* code,SIZE_T size,ID3D11ClassLinkage* link,ID3D11VertexShader** out) {
    const HRESULT hr=create_vs(d,code,size,link,out);
    if(SUCCEEDED(hr) && out && *out && code) {
        stats[0].fetch_add(1,std::memory_order_relaxed);
        const std::uint64_t layout=reflect(code,size);
        if(layout) stats[has_previous(layout)?1u:4u].fetch_add(1,std::memory_order_relaxed);
        if(Slot* s=find(*out,layout!=0)) s->layout.store(layout,std::memory_order_release);
        else if(layout) stats[2].fetch_add(1,std::memory_order_relaxed);
    }
    return hr;
}
inline void hook_device(ID3D11Device* device) noexcept {
    if(!device) return;
    void* target=(*reinterpret_cast<void***>(device))[12];
    if(target==create_vs_target) return;
    if(hooking.exchange(true)) {stats[3].fetch_add(1,std::memory_order_relaxed);return;} // another implementation: not recorded
    if(MH_CreateHook(target,reinterpret_cast<void*>(&create_vs_hook),reinterpret_cast<void**>(&create_vs))==MH_OK && MH_EnableHook(target)==MH_OK)
        create_vs_target=target;
}

using CreateDevice=HRESULT(WINAPI*)(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,ID3D11Device**,
    D3D_FEATURE_LEVEL*,ID3D11DeviceContext**);
using CreateDeviceSwap=HRESULT(WINAPI*)(IDXGIAdapter*,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL*,UINT,UINT,
    const DXGI_SWAP_CHAIN_DESC*,IDXGISwapChain**,ID3D11Device**,D3D_FEATURE_LEVEL*,ID3D11DeviceContext**);
inline CreateDevice create_device{};
inline CreateDeviceSwap create_device_swap{};
inline HRESULT WINAPI create_device_hook(IDXGIAdapter* a,D3D_DRIVER_TYPE t,HMODULE m,UINT f,const D3D_FEATURE_LEVEL* l,UINT n,UINT v,
    ID3D11Device** d,D3D_FEATURE_LEVEL* o,ID3D11DeviceContext** c) {
    const HRESULT hr=create_device(a,t,m,f,l,n,v,d,o,c);
    if(SUCCEEDED(hr) && d) hook_device(*d);
    return hr;
}
inline HRESULT WINAPI create_device_swap_hook(IDXGIAdapter* a,D3D_DRIVER_TYPE t,HMODULE m,UINT f,const D3D_FEATURE_LEVEL* l,UINT n,UINT v,
    const DXGI_SWAP_CHAIN_DESC* sd,IDXGISwapChain** sc,ID3D11Device** d,D3D_FEATURE_LEVEL* o,ID3D11DeviceContext** c) {
    const HRESULT hr=create_device_swap(a,t,m,f,l,n,v,sd,sc,d,o,c);
    if(SUCCEEDED(hr) && d) hook_device(*d);
    return hr;
}
// From the proxy's worker, before anything else. True when both device creation paths are hooked.
inline bool install() noexcept {
    if(!LoadLibraryW(L"d3d11.dll")) return false;
    const MH_STATUS init=MH_Initialize();
    if(init!=MH_OK && init!=MH_ERROR_ALREADY_INITIALIZED) return false;
    const bool device=MH_CreateHookApi(L"d3d11","D3D11CreateDevice",reinterpret_cast<void*>(&create_device_hook),reinterpret_cast<void**>(&create_device))==MH_OK;
    const bool swap=MH_CreateHookApi(L"d3d11","D3D11CreateDeviceAndSwapChain",reinterpret_cast<void*>(&create_device_swap_hook),reinterpret_cast<void**>(&create_device_swap))==MH_OK;
    return MH_EnableHook(MH_ALL_HOOKS)==MH_OK && device && swap;
}
} // namespace d2vr::vs_layouts

extern "C" __declspec(dllexport) std::uint64_t WINAPI D2vrVertexShaderLayout(void* shader) {
    const d2vr::vs_layouts::Slot* s=d2vr::vs_layouts::find(shader,false);
    return s?s->layout.load(std::memory_order_acquire):0;
}
extern "C" __declspec(dllexport) void WINAPI D2vrVertexShaderStats(std::uint64_t* out) {
    if(!out) return;
    for(int i=0;i<5;++i) out[i]=d2vr::vs_layouts::stats[i].load(std::memory_order_relaxed);
}
