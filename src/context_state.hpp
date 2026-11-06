#pragma once
#include <d3d11_1.h>
#include <wrl/client.h>
#include <stdexcept>

namespace d2vr {
// v992: a scope swaps the whole context state and restores it on exit, so it never describes the game's state. The PS state
// census reads this depth on the render thread and neither records nor elides calls made while it is non-zero; the restore
// puts back exactly the game state the shadow already holds.
inline unsigned& overlay_scope_depth() noexcept {static thread_local unsigned depth=0;return depth;}
// The runtime shares the game's device. Keep its pipeline changes in a
// separate native context-state object and restore the game on every exit.
class ContextState {
    Microsoft::WRL::ComPtr<ID3D11DeviceContext1> context_;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> isolated_;
public:
    explicit ContextState(ID3D11Device* device) {
        Microsoft::WRL::ComPtr<ID3D11Device1> extended;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> immediate;
        device->GetImmediateContext(&immediate);
        if(FAILED(device->QueryInterface(IID_PPV_ARGS(&extended))) ||
           FAILED(immediate.As(&context_)))
            throw std::runtime_error("D3D11.1 context isolation is unavailable");
        const auto level=device->GetFeatureLevel();
        const UINT flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?
            D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
        if(FAILED(extended->CreateDeviceContextState(flags,&level,1,D3D11_SDK_VERSION,
            __uuidof(ID3D11Device),nullptr,&isolated_)))
            throw std::runtime_error("Cannot create isolated runtime graphics state");
    }
    class Scope {
        ID3D11DeviceContext1* context_;
        Microsoft::WRL::ComPtr<ID3DDeviceContextState> previous_;
    public:
        explicit Scope(ContextState& owner):context_(owner.context_.Get()) {
            ++overlay_scope_depth();
            context_->SwapDeviceContextState(owner.isolated_.Get(),&previous_);
        }
        ~Scope() {context_->SwapDeviceContextState(previous_.Get(),nullptr);--overlay_scope_depth();}
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    };
};
} // namespace d2vr
