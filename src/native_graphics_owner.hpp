#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <atomic>
#include <stdexcept>

namespace d2vr {
// Bind/verify/release under the caller's capture lock on the Present thread.
// A new graphics owner requires a complete XR shutdown before another bind.
class GraphicsOwner {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    Ptr<IDXGISwapChain> chain_;
    Ptr<IUnknown> device_identity_;
    Ptr<ID3D11DeviceContext> context_;
    std::atomic<HWND> window_{};
    std::atomic<DWORD> thread_{};
    static Ptr<IUnknown> identity(IUnknown* object) {
        Ptr<IUnknown> result;
        if(!object || FAILED(object->QueryInterface(IID_PPV_ARGS(&result))))
            throw std::runtime_error("Cannot verify canonical game graphics identity");
        return result;
    }
public:
    bool matches_window(HWND window) const noexcept {
        return window && window==window_.load(std::memory_order_acquire);
    }
    bool bound() const noexcept {return window_.load(std::memory_order_acquire)!=nullptr;}
    bool on_owner_thread() const noexcept {
        const auto thread=thread_.load(std::memory_order_acquire);return thread && thread==GetCurrentThreadId();
    }
    bool bind_or_verify(IDXGISwapChain* chain,ID3D11Device* device,ID3D11DeviceContext* context,void* expected_resize_entry) {
        if(bound() && !on_owner_thread()) throw std::runtime_error("Game Present thread changed");
        if(!chain || !device || !context || !expected_resize_entry || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
            throw std::runtime_error("Missing or non-immediate game graphics owner");
        const auto resize_entry=(*reinterpret_cast<void***>(chain))[13];
        if(resize_entry!=expected_resize_entry) throw std::runtime_error("Actual game ResizeBuffers dispatch differs from the installed hook");
        DXGI_SWAP_CHAIN_DESC desc{};
        if(FAILED(chain->GetDesc(&desc)) || !desc.OutputWindow)
            throw std::runtime_error("Cannot verify game swapchain window");
        const auto device_identity=identity(device);
        Ptr<ID3D11Device> chain_device,context_device;
        if(FAILED(chain->GetDevice(IID_PPV_ARGS(&chain_device)))) throw std::runtime_error("Cannot verify swapchain device");
        context->GetDevice(&context_device);
        Ptr<ID3D11DeviceContext> immediate;device->GetImmediateContext(&immediate);
        if(identity(chain_device.Get()).Get()!=device_identity.Get() || identity(context_device.Get()).Get()!=device_identity.Get() ||
           immediate.Get()!=context) throw std::runtime_error("Game swapchain/device/context ownership differs");
        if(bound()) {
            if(chain_.Get()!=chain || device_identity_.Get()!=device_identity.Get() || context_.Get()!=context ||
               window_.load(std::memory_order_acquire)!=desc.OutputWindow)
                throw std::runtime_error("Game graphics owner changed; complete XR shutdown before rebinding");
            return false;
        }
        chain_=chain;device_identity_=device_identity;context_=context;
        thread_.store(GetCurrentThreadId(),std::memory_order_release);
        window_.store(desc.OutputWindow,std::memory_order_release);
        return true;
    }
    void release() {
        if(bound() && !on_owner_thread()) throw std::runtime_error("Game graphics owner release requires the Present thread");
        window_.store(nullptr,std::memory_order_release);
        context_.Reset();device_identity_.Reset();chain_.Reset();
        thread_.store(0,std::memory_order_release);
    }
};
} // namespace d2vr
