// v1287 record of a d3d11.dll behaviour the GPU hooks must live with (the v1286 sim failure), on WARP (the table code is d3d11.dll's,
// not the driver's: the game's measured slot addresses are these same entries). An immediate context changes dispatch tables in place:
//   B  the plain table (a fresh device; the game switched to it at present 2536 of the v1286 sim, dispatch transition),
//   C  after SetMultithreadProtected(TRUE),
//   A  after SetMultithreadProtected(FALSE) (the game's first table: slot 33/34/12 addresses equal the game's binding 0).
// A and C forward OMSetRenderTargets (33) and OMSetRenderTargetsAndUnorderedAccessViews (34) to B's entries. The GPU binding registry
// keeps every table it saw hooked, so a call through binding 0's trampoline (rig_mask_targets_uav<0>, the replay of a draw recorded
// through table A) re-enters the hook on B with the caller's arguments: D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL reached
// targets_uav_hook, and trace_targets took it for a target count (> 8: callback_failed). Checks: A's and C's slot-34 trampolines reach
// B's hook with the KEEP count, B's own trampoline reaches nothing.
//   Build/run: build\tmp-check-run.cmd d3d11_table_forward_check /Ithird_party\minhook\include build\md\*.obj
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <wrl/client.h>
#include <cstdio>
#include "MinHook.h"
#pragma comment(lib,"d3d11.lib")
using Microsoft::WRL::ComPtr;
static int failures=0;
#define CHECK(x) do {if(!(x)) {std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#x);++failures;}} while(0)
using T34=void(WINAPI*)(ID3D11DeviceContext*,UINT,ID3D11RenderTargetView* const*,ID3D11DepthStencilView*,UINT,UINT,ID3D11UnorderedAccessView* const*,const UINT*);
static T34 o34[3];static int n34[3];static UINT last34[3];
template<int K> void WINAPI h34(ID3D11DeviceContext* c,UINT n,ID3D11RenderTargetView* const* r,ID3D11DepthStencilView* d,UINT f,UINT u,ID3D11UnorderedAccessView* const* v,const UINT* i) {++n34[K];last34[K]=n;o34[K](c,n,r,d,f,u,v,i);}
int main() {
    ComPtr<ID3D11Device> dev;ComPtr<ID3D11DeviceContext> imm;
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&dev,nullptr,&imm))) {std::printf("FAIL WARP\n");return 1;}
    void** t=*reinterpret_cast<void***>(imm.Get());ComPtr<ID3D11Multithread> m;CHECK(SUCCEEDED(imm.As(&m)));if(!m) return 1;
    void* const b=t[34];m->SetMultithreadProtected(TRUE);void* const c=t[34];m->SetMultithreadProtected(FALSE);void* const a=t[34];
    std::printf("slot 34: A %p  B %p  C %p\n",a,b,c);
    if(a==b || c==b) {std::printf("this d3d11.dll keeps one immediate table: nothing to forward\nPASS: d3d11_table_forward_check (n/a)\n");return 0;}
    if(MH_Initialize()!=MH_OK) {std::printf("FAIL MinHook\n");return 1;}
    void* const targets[3]{a,b,c};
    CHECK(MH_CreateHook(a,reinterpret_cast<void*>(&h34<0>),reinterpret_cast<void**>(&o34[0]))==MH_OK);
    CHECK(MH_CreateHook(b,reinterpret_cast<void*>(&h34<1>),reinterpret_cast<void**>(&o34[1]))==MH_OK);
    CHECK(MH_CreateHook(c,reinterpret_cast<void*>(&h34<2>),reinterpret_cast<void**>(&o34[2]))==MH_OK);
    CHECK(MH_EnableHook(MH_ALL_HOOKS)==MH_OK);
    D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=16;td.MipLevels=td.ArraySize=1;td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R32_UINT;td.BindFlags=D3D11_BIND_UNORDERED_ACCESS;
    ComPtr<ID3D11Texture2D> x,y;dev->CreateTexture2D(&td,nullptr,&x);dev->CreateTexture2D(&td,nullptr,&y);
    ComPtr<ID3D11UnorderedAccessView> ux,uy;dev->CreateUnorderedAccessView(x.Get(),nullptr,&ux);dev->CreateUnorderedAccessView(y.Get(),nullptr,&uy);
    ID3D11UnorderedAccessView* ours[2]{ux.Get(),uy.Get()};ID3D11UnorderedAccessView* none[2]{};
    const char* names[3]{"A","B","C"};
    for(int k=0;k<3;++k) for(int add=1;add>=0;--add) {
        n34[0]=n34[1]=n34[2]=0;last34[1]=0;
        o34[k](imm.Get(),D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL,nullptr,nullptr,6,2,add?ours:none,nullptr);
        std::printf("%s trampoline, KEEP %s: hooks A %d B %d C %d, B saw count %#x\n",names[k],add?"add":"remove",n34[0],n34[1],n34[2],last34[1]);
        if(k==1) CHECK(n34[0]+n34[1]+n34[2]==0);
        else CHECK(n34[1]==1 && last34[1]==D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL && n34[0]+n34[2]==0);
    }
    for(void* p:targets) MH_DisableHook(p);
    MH_Uninitialize();
    if(failures) {std::printf("d3d11_table_forward_check: %d failure(s) (d3d11.dll behaviour changed: re-read the v1287 note in targets_uav_hook)\n",failures);return 1;}
    std::printf("PASS: d3d11_table_forward_check (tables A and C forward slot 34 to B's hooked entry with the KEEP count; B's trampoline is final)\n");
    return 0;
}
