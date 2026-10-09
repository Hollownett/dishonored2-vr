#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/native_graphics_owner.hpp"
#include <cassert>
#include <thread>
#include <iostream>

template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
struct Fixture {
    Ptr<ID3D11Device> device;Ptr<ID3D11DeviceContext> context;Ptr<IDXGISwapChain> chain;
    explicit Fixture(HWND window) {
        DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=64;desc.BufferDesc.Height=64;
        desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
        desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=1;desc.OutputWindow=window;
        desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
        assert(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context)));
    }
    void* resize_entry() const {return (*reinterpret_cast<void***>(chain.Get()))[13];}
};
int main() {
    const auto instance=GetModuleHandleW(nullptr);const wchar_t* name=L"D2VRGraphicsOwnerCheck";
    WNDCLASSW klass{};klass.hInstance=instance;klass.lpfnWndProc=DefWindowProcW;klass.lpszClassName=name;
    assert(RegisterClassW(&klass));
    // Owned test windows remain hidden; no game input or native game calls.
    const auto window=CreateWindowExW(0,name,L"",WS_POPUP,0,0,64,64,nullptr,nullptr,instance,nullptr);
    const auto other_window=CreateWindowExW(0,name,L"",WS_POPUP,0,0,64,64,nullptr,nullptr,instance,nullptr);
    assert(window && other_window);
    {
        Fixture first(window),other(other_window);d2vr::GraphicsOwner owner;
        auto rejects=[](auto operation) {bool rejected=false;try {operation();} catch(const std::runtime_error&) {rejected=true;}assert(rejected);};
        assert(!owner.bound() && !owner.matches_window(window));
        rejects([&] {owner.bind_or_verify(first.chain.Get(),first.device.Get(),first.context.Get(),nullptr);});
        rejects([&] {owner.bind_or_verify(first.chain.Get(),first.device.Get(),first.context.Get(),reinterpret_cast<void*>(1));});
        assert(!owner.bound());
        assert(owner.bind_or_verify(first.chain.Get(),first.device.Get(),first.context.Get(),first.resize_entry()));
        assert(owner.matches_window(window) && !owner.matches_window(other_window) && owner.on_owner_thread());
        assert(!owner.bind_or_verify(first.chain.Get(),first.device.Get(),first.context.Get(),first.resize_entry()));
        rejects([&] {owner.bind_or_verify(first.chain.Get(),other.device.Get(),other.context.Get(),first.resize_entry());});
        rejects([&] {owner.bind_or_verify(first.chain.Get(),first.device.Get(),other.context.Get(),first.resize_entry());});
        rejects([&] {owner.bind_or_verify(other.chain.Get(),other.device.Get(),other.context.Get(),other.resize_entry());});
        Ptr<ID3D11DeviceContext> deferred;assert(SUCCEEDED(first.device->CreateDeferredContext(0,&deferred)));
        rejects([&] {owner.bind_or_verify(first.chain.Get(),first.device.Get(),deferred.Get(),first.resize_entry());});
        std::thread foreign([&] {
            assert(!owner.on_owner_thread());
            rejects([&] {owner.bind_or_verify(first.chain.Get(),first.device.Get(),first.context.Get(),first.resize_entry());});
            rejects([&] {owner.release();});
        });foreign.join();assert(owner.bound());
        // The owner retains the actual chain/device/context after caller refs
        // are dropped, and a normal ResizeBuffers keeps that identity intact.
        auto* chain=first.chain.Get();auto* device=first.device.Get();auto* context=first.context.Get();const auto resize=first.resize_entry();
        first.chain.Reset();first.context.Reset();first.device.Reset();
        assert(SUCCEEDED(chain->ResizeBuffers(1,96,80,DXGI_FORMAT_R8G8B8A8_UNORM,0)));
        assert(!owner.bind_or_verify(chain,device,context,resize));
        owner.release();assert(!owner.bound() && !owner.matches_window(window));
        assert(owner.bind_or_verify(other.chain.Get(),other.device.Get(),other.context.Get(),other.resize_entry()));
        owner.release();
    }
    DestroyWindow(other_window);DestroyWindow(window);assert(UnregisterClassW(name,instance));
    std::cout<<"PASS: WARP graphics owner identity, actual resize dispatch, immediate context, thread/window guards, retained COM lifetime and ordinary resize\n";
}
