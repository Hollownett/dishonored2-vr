#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace d2vr {
// Actual immediate-context primitives, independent of NGX and the renderer.
// Context state restores pipeline BINDINGS, not video state, asynchronous query
// activity, UAV data/counters or SO data/write cursors. Callers must account for
// those effects separately. No native producer/callback ownership is certified.
class D3D11TemporalOwner {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
public:
    using Lease=std::shared_ptr<const void>;
    static constexpr std::size_t capacity=8;
    struct Ticket {
        std::uint64_t owner{},serial{};
        explicit operator bool() const noexcept {return owner!=0 && serial!=0;}
    };
    enum class Status {ok, unsupported, wrong_device, wrong_context, wrong_thread, busy,
        closed, full, stale_ticket, callback_failed, callback_exception, state_unavailable};
    struct Result {Status status=Status::unsupported;HRESULT native_result=E_FAIL;};
    struct Completion {
        Ticket ticket;
        Lease payload,generation;
        bool device_abandoned{};
        HRESULT query_result=S_OK,device_result=S_OK;
    };
    struct Statistics {unsigned reserved{},pending{},quarantined{},completed{};};
private:
    enum class Phase {free,reserved,encoding,pending,quarantined,completed,abandoned};
    struct Slot {
        Ptr<ID3D11Query> query;
        Ticket ticket;
        Lease payload,generation;
        Phase phase=Phase::free;
        HRESULT query_result=S_OK,device_result=S_OK;
    };
    struct State {
        Ptr<ID3D11Device> device;
        Ptr<ID3D11DeviceContext1> context;
        Ptr<IUnknown> device_id,context_id;
        Ptr<ID3DDeviceContextState> isolated;
        std::array<Slot,capacity> slots;
        std::mutex mutex;
        DWORD thread{};
        bool thread_affine=true; // false: the caller serializes every call on whichever thread owns the immediate context now
        D3D_FEATURE_LEVEL level{};
        UINT flags{};
        std::uint64_t identity{},serial{};
        bool accepting=true,busy=false,isolating=false;
    };
    // Weak, fixed-size registry refuses a second primitive owner of one native
    // stream. It does not retain GPU leases or implement renderer stop/unload.
    static auto& registry() {static std::array<std::weak_ptr<State>,capacity> value;return value;}
    static auto& owner_serial() {static std::uint64_t value{};return value;} // registry mutex
    static auto& registry_mutex() {static std::mutex value;return value;}
    std::shared_ptr<State> state_;
    explicit D3D11TemporalOwner(std::shared_ptr<State> state):state_(std::move(state)) {}
    static Status access(const State& s) noexcept {return !s.thread_affine || GetCurrentThreadId()==s.thread?Status::ok:Status::wrong_thread;}
    static Slot* find(State& s,Ticket ticket) noexcept {
        if(!ticket || ticket.owner!=s.identity)return nullptr;
        for(auto& slot:s.slots)if(slot.phase!=Phase::free && slot.ticket.serial==ticket.serial)return &slot;
        return nullptr;
    }
    static Statistics statistics(State& s) noexcept {
        Statistics out;
        for(const auto& slot:s.slots)switch(slot.phase) {
        case Phase::reserved:case Phase::encoding:++out.reserved;break;
        case Phase::pending:++out.pending;break;
        case Phase::quarantined:++out.quarantined;break;
        case Phase::completed:case Phase::abandoned:++out.completed;break;
        default:break;
        }
        return out;
    }
    static Status close(State& s) noexcept {
        if(access(s)!=Status::ok)return Status::wrong_thread;
        std::lock_guard<std::mutex> lock(s.mutex);
        s.accepting=false;return Status::ok;
    }
    static Status poll(State& s) noexcept {
        if(access(s)!=Status::ok)return Status::wrong_thread;
        std::lock_guard<std::mutex> lock(s.mutex);
        if(s.busy)return Status::busy;
        for(auto& slot:s.slots) {
            if(slot.phase==Phase::pending) {
                BOOL done=FALSE;
                const HRESULT hr=s.context->GetData(slot.query.Get(),&done,sizeof done,D3D11_ASYNC_GETDATA_DONOTFLUSH);
                slot.query_result=hr;
                if(hr==S_OK && done)slot.phase=Phase::completed;
                else if(FAILED(hr))slot.phase=Phase::quarantined;
            }
            if(slot.phase==Phase::quarantined) {
                slot.device_result=s.device->GetDeviceRemovedReason();
                if(FAILED(slot.device_result))slot.phase=Phase::abandoned;
            }
        }
        return Status::ok;
    }
    static std::optional<Completion> take(State& s) noexcept {
        if(access(s)!=Status::ok)return {};
        std::optional<Completion> out;
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            if(s.busy)return {};
            for(auto& slot:s.slots)if(slot.phase==Phase::completed || slot.phase==Phase::abandoned) {
                out.emplace(Completion{slot.ticket,std::move(slot.payload),std::move(slot.generation),
                    slot.phase==Phase::abandoned,slot.query_result,slot.device_result});
                slot.phase=Phase::free;slot.ticket={};break;
            }
        }
        // Ownership moves out, never runs caller deleters under the mutex.
        return out;
    }
    static Status cancel(State& state,Ticket ticket) noexcept {
        if(access(state)!=Status::ok)return Status::wrong_thread;
        Lease payload,generation;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if(state.busy)return Status::busy;
            auto* slot=find(state,ticket);if(!slot || slot->phase!=Phase::reserved)return Status::stale_ticket;
            payload=std::move(slot->payload);generation=std::move(slot->generation);slot->ticket={};slot->phase=Phase::free;
        }
        return Status::ok;
    }
public:
    // A drain is a STRONG lifetime obligation. Retain it AND this code's module
    // after facade release/stop/reset until can_destroy(), then destroy all handles
    // before unloading. can_unload() is not a global module-unload certificate. No last-handle loss,
    // blocking destructor, leak-on-stop or implicit device-abandonment shortcut.
    // Actual renderer drain scheduling/module-unload wiring is not supplied here.
    class DrainOwner {
        friend class D3D11TemporalOwner;
        std::shared_ptr<State> state_;
        explicit DrainOwner(std::shared_ptr<State> state):state_(std::move(state)) {}
    public:
        DrainOwner()=default;
        Status cancel(Ticket ticket) noexcept {return state_?D3D11TemporalOwner::cancel(*state_,ticket):Status::closed;}
        Status close() noexcept {return state_?D3D11TemporalOwner::close(*state_):Status::closed;}
        Status poll() noexcept {return state_?D3D11TemporalOwner::poll(*state_):Status::closed;}
        std::optional<Completion> take_completed() noexcept {return state_?take(*state_):std::nullopt;}
        Statistics pending() const noexcept {
            if(!state_)return {};
            std::lock_guard<std::mutex> lock(state_->mutex);return statistics(*state_);
        }
        bool can_destroy() const noexcept {
            if(!state_)return true;
            std::lock_guard<std::mutex> lock(state_->mutex);
            const auto count=statistics(*state_);
            return !state_->accepting && !state_->busy && !count.reserved && !count.pending && !count.quarantined && !count.completed;
        }
        bool can_unload() const noexcept {return can_destroy();} // this owner's obligation only
    };
    // Valid only during record(). The entire ordered native submission belongs
    // in record's callback; isolate ONLY the mod evaluation inside it. The EVENT
    // is ended after the callback (and its remainder), even on partial failure.
    class Recording {
        friend class D3D11TemporalOwner;
        State& state_;
        explicit Recording(State& s):state_(s) {}
    public:
        ID3D11DeviceContext1* context() const noexcept {return state_.context.Get();}
        template<class Work> Status isolate(Work&& work) {
            if(access(state_)!=Status::ok)return Status::wrong_thread;
            if(state_.isolating)return Status::busy;
            state_.isolating=true;
            Ptr<ID3DDeviceContextState> previous;
            state_.context->SwapDeviceContextState(state_.isolated.Get(),&previous);
            if(!previous) {state_.isolating=false;return Status::state_unavailable;}
            struct Restore {
                State& state;ID3DDeviceContextState* previous;
                ~Restore() {state.context->SwapDeviceContextState(previous,nullptr);state.isolating=false;}
            } restore{state_,previous.Get()};
            try {return std::forward<Work>(work)(state_.context.Get())?Status::ok:Status::callback_failed;}
            catch(...) {return Status::callback_exception;}
        }
        Recording(const Recording&)=delete;
        Recording& operator=(const Recording&)=delete;
    };
    D3D11TemporalOwner(const D3D11TemporalOwner&)=delete;
    D3D11TemporalOwner& operator=(const D3D11TemporalOwner&)=delete;
    D3D11TemporalOwner(D3D11TemporalOwner&&)=default;
    D3D11TemporalOwner& operator=(D3D11TemporalOwner&&)=default;
    // thread_affine=true (the standalone checks): every later call must come from this thread. false (the renderer):
    // the game executes its command lists from more than one thread, one at a time, and the caller makes every call
    // from the thread that owns the immediate context at that moment, under its own lock.
    static std::optional<D3D11TemporalOwner> prepare(ID3D11Device* device,ID3D11DeviceContext* immediate,Result& result,bool thread_affine=true) noexcept {
        result={Status::unsupported,E_INVALIDARG};
        if(!device || !immediate)return {};
        try {
            auto state=std::make_shared<State>();state->thread=GetCurrentThreadId();state->thread_affine=thread_affine;
            Ptr<ID3D11Device> actual_device;immediate->GetDevice(&actual_device);
            Ptr<ID3D11DeviceContext> canonical;device->GetImmediateContext(&canonical);
            Ptr<IUnknown> actual_device_id,canonical_id;
            HRESULT hr=device->QueryInterface(IID_PPV_ARGS(&state->device_id));
            if(SUCCEEDED(hr))hr=actual_device.As(&actual_device_id);
            if(FAILED(hr) || state->device_id.Get()!=actual_device_id.Get()) {result={Status::wrong_device,hr};return {};}
            if(immediate->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE) {result={Status::wrong_context,E_INVALIDARG};return {};}
            hr=immediate->QueryInterface(IID_PPV_ARGS(&state->context_id));
            if(SUCCEEDED(hr))hr=canonical.As(&canonical_id);
            if(FAILED(hr) || state->context_id.Get()!=canonical_id.Get()) {result={Status::wrong_context,hr};return {};}
            std::lock_guard<std::mutex> registry_lock(registry_mutex());
            std::weak_ptr<State>* entry=nullptr;
            for(auto& weak:registry()) {
                if(auto live=weak.lock()) {if(live->context_id.Get()==canonical_id.Get()) {result={Status::busy,E_FAIL};return {};}}
                else if(!entry)entry=&weak;
            }
            if(!entry) {result={Status::full,E_OUTOFMEMORY};return {};}
            Ptr<ID3D11Device1> extended;
            hr=device->QueryInterface(IID_PPV_ARGS(&extended));
            if(SUCCEEDED(hr))hr=immediate->QueryInterface(IID_PPV_ARGS(&state->context));
            if(FAILED(hr)) {result={Status::unsupported,hr};return {};}
            state->device=device;state->level=device->GetFeatureLevel();
            state->flags=(device->GetCreationFlags()&D3D11_CREATE_DEVICE_SINGLETHREADED)?D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED:0;
            D3D_FEATURE_LEVEL chosen{};
            hr=extended->CreateDeviceContextState(state->flags,&state->level,1,D3D11_SDK_VERSION,
                __uuidof(ID3D11Device),&chosen,&state->isolated);
            if(FAILED(hr) || chosen!=state->level || !state->isolated) {result={Status::unsupported,FAILED(hr)?hr:E_FAIL};return {};}
            const D3D11_QUERY_DESC query{D3D11_QUERY_EVENT,0};
            for(auto& slot:state->slots) {hr=device->CreateQuery(&query,&slot.query);if(FAILED(hr) || !slot.query) {result={Status::unsupported,FAILED(hr)?hr:E_FAIL};return {};}}
            if(owner_serial()==std::numeric_limits<std::uint64_t>::max()) {result={Status::full,E_FAIL};return {};}
            state->identity=++owner_serial();
            *entry=state;result={Status::ok,S_OK};return D3D11TemporalOwner(std::move(state));
        } catch(...) {result={Status::unsupported,E_OUTOFMEMORY};return {};}
    }
    DrainOwner drain_owner() const {return DrainOwner(state_);}
    // Pipeline-state isolation for lifecycle work that may record on the immediate context (feature create/release)
    // outside an EVENT ticket; the caller accounts for GPU completion itself where it matters.
    template<class Work> Status isolate(Work&& work) noexcept {
        const auto state=state_;
        if(access(*state)!=Status::ok)return Status::wrong_thread;
        Recording recording(*state);
        return recording.isolate(std::forward<Work>(work));
    }
    D3D_FEATURE_LEVEL feature_level() const noexcept {return state_->level;}
    UINT context_state_flags() const noexcept {return state_->flags;}
    Status close() noexcept {return D3D11TemporalOwner::close(*state_);}
    Status reset() noexcept {return close();} // closes admission; never clears outstanding work
    Status poll() noexcept {return D3D11TemporalOwner::poll(*state_);}
    std::optional<Completion> take_completed() noexcept {return take(*state_);}
    std::optional<Ticket> reserve(Lease payload,Lease generation,Status& status) noexcept {
        status=access(*state_);if(status!=Status::ok)return {};
        std::lock_guard<std::mutex> lock(state_->mutex);
        if(state_->busy) {status=Status::busy;return {};}
        if(!state_->accepting) {status=Status::closed;return {};}
        if(!payload || !generation || state_->serial==std::numeric_limits<std::uint64_t>::max()) {status=Status::closed;return {};}
        for(auto& slot:state_->slots)if(slot.phase==Phase::free) {
            slot.ticket={state_->identity,++state_->serial};slot.payload=std::move(payload);slot.generation=std::move(generation);
            slot.query_result=S_OK;slot.device_result=S_OK;slot.phase=Phase::reserved;return slot.ticket;
        }
        status=Status::full;return {};
    }
    Status cancel(Ticket ticket) noexcept {return cancel(*state_,ticket);}
    template<class Work> Status record(Ticket ticket,Work&& work) noexcept {
        const auto state=state_; // callback may destroy/move the facade
        if(access(*state)!=Status::ok)return Status::wrong_thread;
        Slot* slot;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if(state->busy)return Status::busy;
            slot=find(*state,ticket);if(!slot || slot->phase!=Phase::reserved)return Status::stale_ticket;
            state->busy=true;slot->phase=Phase::encoding;
        }
        Recording recording(*state);Status result;
        try {result=std::forward<Work>(work)(recording)?Status::ok:Status::callback_failed;}
        catch(...) {result=Status::callback_exception;}
        // End is void. Only a later successful GetData proves completion; CPU
        // failure, End returning and reset/stop never release GPU-owned payloads.
        state->context->End(slot->query.Get());
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            slot->phase=Phase::pending;state->busy=false;
        }
        return result;
    }
};
} // namespace d2vr
