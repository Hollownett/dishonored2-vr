#pragma once
#include "native_hud_child_draw.hpp"
#include "world_ui_capture.hpp"
#include "world_ui_completion.hpp"

namespace d2vr {
// Native child-render capture only. No scene/whole-HUD claiming rectangles,
// engine metadata writes, extra native draws or immediate-context fallback.
class NativeHudChildCapture {
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
public:
    using Fallback=void (*)(ID3D11DeviceContext*,ID3D11RenderTargetView*,ID3D11RenderTargetView*,const NativeHudChildScope&);
    static constexpr unsigned slot_count=13; // prompt, vitals/equipment, bounded individual markers, tutorial (11), HUD window panel (12)
    struct Snapshot {
        WorldUiCapture::Snapshot image;NativeHudChildScope child;
        explicit operator bool()const noexcept{return bool(image);}
    };
    std::atomic<UINT64> accepted{},rejected{},root_rebinds{},ended{};
private:
    std::array<WorldUiCapture,5> banks_;
    std::array<WorldUiCompletion,5> completion_;
    std::mutex mutex_;
    std::array<std::array<NativeHudChildScope,2>,slot_count> owners_{};
    std::array<std::array<UINT64,2>,slot_count> owner_serials_{};
    struct Transaction {
        NativeHudChildCapture* owner{};Ptr<ID3D11DeviceContext> context;
        Ptr<ID3D11RenderTargetView> original,capture;Ptr<ID3D11DepthStencilView> depth;
        unsigned slot{},eye{};UINT64 serial{};std::uintptr_t node{};
    };
    inline static thread_local Transaction transaction_;
public:
    // Parent obtains context ONLY in native1037930 scope from renderer+8,
    // root RTV from renderer+B90. The native getter can be null out of scope.
    // Parent validates COM dispatch; an immediate context must additionally be
    // on the verified Present owner thread supplied here, never a worker.
    bool begin(ID3D11DeviceContext* context,ID3D11RenderTargetView* root,const NativeHudChildScope& child,
        unsigned slot,unsigned eye,UINT64 serial,DWORD immediate_owner_thread=0) {
        auto reject=[&]{++rejected;return false;};
        if(transaction_.owner || !context || !root || child.kind==NativeHudChildKind::none || !child.node ||
           !child.owner || slot>=slot_count || eye>=2 || !serial || NativeHudChildDraw::current!=&child)return reject();
        if(context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE && (!immediate_owner_thread || GetCurrentThreadId()!=immediate_owner_thread))return reject();
        std::array<Ptr<ID3D11RenderTargetView>,8> held;std::array<ID3D11RenderTargetView*,8> raw{};Ptr<ID3D11DepthStencilView> depth;
        context->OMGetRenderTargets(8,raw.data(),&depth);for(unsigned i=0;i<8;++i)held[i].Attach(raw[i]);
        if(held[0].Get()!=root)return reject();for(unsigned i=1;i<8;++i)if(held[i])return reject();
        auto capture=banks_[slot/3].redirect(context,root,slot%3,eye,serial);if(!capture)return reject();
        Transaction tx;tx.owner=this;tx.context=context;tx.original=held[0];tx.capture=capture;tx.depth=depth;
        tx.slot=slot;tx.eye=eye;tx.serial=serial;tx.node=child.node;
        {std::lock_guard<std::mutex> lock(mutex_);owners_[slot][eye]=child;owner_serials_[slot][eye]=serial;}
        if(context->GetType()==D3D11_DEVICE_CONTEXT_DEFERRED)completion_[slot/3].recorded(context,slot%3,eye,serial);
        // Arm only after allocation-capable metadata/completion operations.
        // A declined/throwing Begin must never leave root rebind routing active.
        transaction_=std::move(tx);
        auto* target=capture.Get();context->OMSetRenderTargets(1,&target,depth.Get());++accepted;return true;
    }
    // Wire the existing OMSetRenderTargets hook here. Internal native filter
    // targets pass through; only an exact rebind of this child's root routes.
    Ptr<ID3D11RenderTargetView> replacement(ID3D11DeviceContext* context,UINT count,ID3D11RenderTargetView* const* targets) {
        const auto& tx=transaction_;
        if(tx.owner!=this || context!=tx.context.Get() || count!=1 || !targets || targets[0]!=tx.original.Get() ||
           !NativeHudChildDraw::current || NativeHudChildDraw::current->node!=tx.node)return {};
        ++root_rebinds;return tx.capture;
    }
    bool end(const NativeHudChildScope& child,Fallback fallback=nullptr) {
        if(transaction_.owner!=this || transaction_.node!=child.node){++rejected;return false;}
        // Disarm before restoring: the core OM hook must not recapture restore.
        auto tx=std::move(transaction_);transaction_={};auto* target=tx.original.Get();
        tx.context->OMSetRenderTargets(1,&target,tx.depth.Get());
        // A producer can preserve the native flat draw until its exact captured
        // replacement is committed. Record this on the ORIGINAL context, so
        // deferred native work never jumps to the Present/immediate queue.
        if(fallback) fallback(tx.context.Get(),tx.original.Get(),tx.capture.Get(),child);
        if(tx.context->GetType()==D3D11_DEVICE_CONTEXT_IMMEDIATE)completion_[tx.slot/3].immediate(tx.slot%3,tx.eye,tx.serial);
        ++ended;return true;
    }
    void finished(void* context,void* list){for(auto& tracker:completion_)tracker.finished(context,list);}
    // AFTER the original native list's complete execution, including any FSR
    // split remainder. CPU submission ordering only; copies must use that same
    // immediate queue. This is not a GPU-fence completion claim.
    void executed(void* list){for(auto& tracker:completion_)tracker.executed(list);}
    Snapshot snapshot(unsigned slot,unsigned eye=0) {
        if(slot>=slot_count || eye>=2)return {};
        const auto image=banks_[slot/3].snapshot(slot%3,eye);
        if(!image || !completion_[slot/3].ready(slot%3,eye,image.scope_id))return {};
        std::lock_guard<std::mutex> lock(mutex_);
        // Slot reuse must not pair a prior executed image with a newer owner.
        if(owner_serials_[slot][eye]!=image.scope_id)return {};
        return {image,owners_[slot][eye]};
    }
    // Exact measured alpha bounds for optional completed-image diagnostics.
    // No assumed stage rectangles or font/image reconstruction. Caller owns
    // readback lifetime and can publish this crop with the retained texture.
    static std::optional<D3D11_BOX> alpha_bounds(const unsigned char* bytes,UINT width,UINT height,UINT pitch,std::size_t length) {
        if(!bytes || !width || !height || width>16384 || height>16384 || pitch<width*4 || length<static_cast<std::size_t>(pitch)*height)return {};
        D3D11_BOX box{width,height,0,0,0,1};
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x)if(bytes[static_cast<std::size_t>(y)*pitch+x*4+3]){
            box.left=std::min(box.left,x);box.top=std::min(box.top,y);box.right=std::max(box.right,x+1);box.bottom=std::max(box.bottom,y+1);
        }
        return box.left<box.right && box.top<box.bottom?std::optional<D3D11_BOX>{box}:std::nullopt;
    }
    // Invoke only after NativeHudChildDraw::disable has drained native scopes.
    void clear(){for(auto& bank:banks_)bank.clear();for(auto& tracker:completion_)tracker.clear();std::lock_guard<std::mutex> lock(mutex_);owners_={};owner_serials_={};}
};
} // namespace d2vr
