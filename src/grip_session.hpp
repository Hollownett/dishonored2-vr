#pragma once
#include "grip_anchor.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

namespace d2vr {
struct GripEpochs {
    std::uint64_t owner{},pose{},reference{},mapping{};
    bool valid() const {return owner && pose && reference && mapping;}
};
inline bool operator==(const GripEpochs& a,const GripEpochs& b) {
    return a.owner==b.owner && a.pose==b.pose && a.reference==b.reference && a.mapping==b.mapping;
}
// The adapter supplies a real shape/contact lease owner, retaining native body
// and selected shape ownership until its final shared reference is released.
// current() certifies the receipt's exact captured hand/body frame, surface point,
// normal and body/shape ID, as well as current retained native ownership.
// A timestamp, body handle, or this interface alone is not native contact proof.
// current() must be bounded,
// nonthrowing, and must not call back into GripSession or issue native queries.
struct GripContactLease {
    virtual ~GripContactLease()=default;
    virtual bool current(std::uint64_t receipt,const GripAnchor& anchor) const noexcept=0;
};
struct GripContactReceipt {
    std::shared_ptr<const GripContactLease> lease;
    std::uint64_t receipt{};
    bool current(const GripAnchor& anchor) const noexcept {return lease && receipt && lease->current(receipt,anchor);}
};

// One instance per hand. Only callback-scoped, versioned admission exposes a pose.
class GripSession {
public:
    struct Snapshot {
        std::uint64_t token{};
        GripEpochs epochs{};
        GripAnchor anchor;
        GripBodyMotion body{};
        std::uint64_t sample_tick{},source_tick{};
    };
    enum class Drain {drained,pending};
private:
    struct State {
        std::recursive_timed_mutex mutex;
        std::condition_variable_any changed;
        std::atomic<bool> open{false};
        std::optional<Snapshot> snapshot;
        GripContactReceipt contact;
        std::uint64_t last_token{},revision{};
        unsigned inflight{};
    };
    struct CallbackFrame {
        std::shared_ptr<State> state;
        std::optional<Snapshot> snapshot;
        GripContactReceipt contact;
        std::uint64_t revision{};
        CallbackFrame* previous{};
        bool admitted{};
        explicit CallbackFrame(std::shared_ptr<State> value):state(std::move(value)) {}
    };
    inline static thread_local CallbackFrame* callback_{};
    std::shared_ptr<State> state_=std::make_shared<State>();
    static bool fresh(std::uint64_t tick,std::uint64_t now) {return tick && now>=tick && now-tick<=32;}
    static bool own_callback(const State* state) {
        for(auto* frame=callback_;frame;frame=frame->previous)if(frame->state.get()==state)return true;
        return false;
    }
    static bool valid(const Snapshot& s,std::uint64_t now) {
        return s.token && s.epochs.valid() && s.body.valid() && s.body.id==s.anchor.body() &&
            fresh(s.sample_tick,now) && fresh(s.source_tick,now);
    }
    static bool current_locked(const CallbackFrame& frame,std::uint64_t now) {
        const auto& s=*frame.state;
        return s.open.load(std::memory_order_acquire) && s.snapshot && s.revision==frame.revision &&
            valid(*frame.snapshot,now) && s.contact.current(frame.snapshot->anchor);
    }
    // Keep this trampoline free of objects needing C++ unwinding: /EHsc does not
    // destroy them for a native SEH exception. Both admission and commit use it.
    static unsigned invoke_cleanup(unsigned(*invoke)(void*),void* context,void(*cleanup)(void*) noexcept) {
        unsigned result{};
#if defined(_MSC_VER)
        __try {result=invoke(context);}
        __finally {cleanup(context);}
#else
        try {result=invoke(context);}catch(...) {cleanup(context);throw;}
        cleanup(context);
#endif
        return result;
    }
public:
    class Admission {
        friend class GripSession;
        CallbackFrame* frame_;
        explicit Admission(CallbackFrame* frame):frame_(frame) {}
    public:
        Admission(const Admission&)=delete;
        Admission& operator=(const Admission&)=delete;
        bool current(std::uint64_t now) const {
            std::lock_guard<std::recursive_timed_mutex> lock(frame_->state->mutex);
            return current_locked(*frame_,now);
        }
        // Only a bounded owned publication belongs here, not native queries.
        // Revoke closes new commits immediately; an already admitted commit
        // finishes before an external drain can report drained.
        template<class Commit> bool commit(std::uint64_t now,Commit&& commit) {
            struct Frame {
                CallbackFrame* admission;
                std::decay_t<Commit> action;
                std::uint64_t now;
                bool locked{};
                Frame(CallbackFrame* a,Commit&& c,std::uint64_t tick):admission(a),action(std::forward<Commit>(c)),now(tick) {}
            };
            auto* frame=new Frame(frame_,std::forward<Commit>(commit),now);
            const auto invoke=[](void* raw)->unsigned {
                auto& f=*static_cast<Frame*>(raw);
                f.admission->state->mutex.lock();f.locked=true;
                if(!current_locked(*f.admission,f.now))return 0;
                f.action(*f.admission->snapshot);return 1;
            };
            const auto cleanup=[](void* raw) noexcept {
                auto* f=static_cast<Frame*>(raw);
                if(f->locked)f->admission->state->mutex.unlock();
                delete f;
            };
            return invoke_cleanup(invoke,frame,cleanup)!=0;
        }
    };
    GripSession()=default;
    GripSession(const GripSession&)=delete;
    GripSession& operator=(const GripSession&)=delete;
    ~GripSession() {revoke(std::chrono::milliseconds{0});}

    bool admit(std::uint64_t token,const GripEpochs& epochs,const GripAnchor& anchor,GripContactReceipt contact,
            const GripBodyMotion& body,std::uint64_t sample_tick,std::uint64_t source_tick,std::uint64_t now) {
        Snapshot next{token,epochs,anchor,body,sample_tick,source_tick};
        if(!valid(next,now) || !contact.current(anchor))return false;
        std::lock_guard<std::recursive_timed_mutex> lock(state_->mutex);
        if(state_->open.load(std::memory_order_acquire) || state_->inflight || token<=state_->last_token || !contact.current(anchor))return false;
        state_->last_token=token;state_->snapshot=std::move(next);state_->contact=std::move(contact);++state_->revision;
        state_->open.store(true,std::memory_order_release);return true;
    }
    bool publish(Snapshot next,std::uint64_t now) {
        if(!valid(next,now))return false;
        std::lock_guard<std::recursive_timed_mutex> lock(state_->mutex);
        if(!state_->open.load(std::memory_order_acquire) || !state_->snapshot)return false;
        const auto& old=*state_->snapshot;
        if(next.token!=old.token || !(next.epochs==old.epochs) || next.body.id!=old.body.id ||
           next.sample_tick<old.sample_tick || next.source_tick<old.source_tick || !state_->contact.current(old.anchor))return false;
        // The captured anchor is immutable, even if a caller supplies another one.
        next.anchor=old.anchor;state_->snapshot=std::move(next);++state_->revision;return true;
    }
    template<class Consume> bool consume(std::uint64_t token,std::uint64_t now,Consume&& consume) {
        struct Frame : CallbackFrame {
            std::decay_t<Consume> action;
            Frame(std::shared_ptr<State> state,Consume&& c):CallbackFrame(std::move(state)),action(std::forward<Consume>(c)) {}
        };
        auto* frame=new Frame(state_,std::forward<Consume>(consume));
        const auto invoke=[](void* raw)->unsigned {
            auto& f=*static_cast<Frame*>(raw);
            Admission admission(&f);f.action(admission);return 1;
        };
        const auto cleanup=[](void* raw) noexcept {
            auto* f=static_cast<Frame*>(raw);
            if(f->admitted) {
                callback_=f->previous;
                auto state=f->state;
                delete f;
                {std::lock_guard<std::recursive_timed_mutex> lock(state->mutex);--state->inflight;}
                state->changed.notify_all();
                return;
            }
            delete f;
        };
        {
            std::lock_guard<std::recursive_timed_mutex> lock(state_->mutex);
            if(!state_->open.load(std::memory_order_acquire) || !state_->snapshot || state_->snapshot->token!=token ||
               !valid(*state_->snapshot,now) || !state_->contact.current(state_->snapshot->anchor)) {delete frame;return false;}
            frame->snapshot=state_->snapshot;frame->contact=state_->contact;frame->revision=state_->revision;++state_->inflight;
            frame->previous=callback_;frame->admitted=true;callback_=frame;
        }
        return invoke_cleanup(invoke,frame,cleanup)!=0;
    }
    Drain revoke(std::chrono::milliseconds timeout=std::chrono::milliseconds{0}) {
        auto state=state_;
        const auto deadline=std::chrono::steady_clock::now()+std::max(timeout,std::chrono::milliseconds{0});
        state->open.store(false,std::memory_order_release);
        std::unique_lock<std::recursive_timed_mutex> lock(state->mutex,std::defer_lock);
        if(!lock.try_lock_until(deadline))return Drain::pending;
        state->open.store(false,std::memory_order_release);
        state->snapshot.reset();state->contact={};++state->revision;
        if(!state->inflight)return Drain::drained;
        if(own_callback(state.get()) || timeout.count()<=0)return Drain::pending;
        return state->changed.wait_until(lock,deadline,[&]{return state->inflight==0;})?Drain::drained:Drain::pending;
    }
};
}
