#define NOMINMAX
#include "../src/grip_session.hpp"
#include <cassert>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#if defined(_WIN32)
#include <windows.h>
#endif
using namespace d2vr;
using namespace std::chrono_literals;
namespace {
struct FixtureLease final : GripContactLease {
    GripAnchor anchor;
    std::atomic<bool> live{true};
    explicit FixtureLease(GripAnchor value):anchor(value) {}
    bool current(std::uint64_t receipt,const GripAnchor& candidate) const noexcept override {
        return live.load() && receipt==73 && anchor.same(candidate);
    }
};
struct Fixture {
    GripBodyId id{1,2,3,4,5,6,7};
    WorldTrackedPose pose{{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};
    GripAnchor anchor=*GripAnchor::capture(id,pose,pose,{.02,0,0},{1,0,0});
    GripBodyMotion body{id,pose,{},{},{},1,{1,1,1}};
    GripEpochs epochs{1,2,3,4};
    std::shared_ptr<FixtureLease> lease=std::make_shared<FixtureLease>(anchor);
    bool admit(GripSession& s,std::uint64_t token=1) {
        return s.admit(token,epochs,anchor,{lease,73},body,100,100,100);
    }
    GripSession::Snapshot sample(std::uint64_t tick=101) const {return {1,epochs,anchor,body,tick,tick};}
};
struct Destruction {std::atomic<unsigned>* count;~Destruction(){++*count;}};
#if defined(_MSC_VER)
constexpr DWORD test_exception=0xe0421234u;
bool catches_seh(void(*call)(void*),void* context) {
    __try {call(context);}
    __except(GetExceptionCode()==test_exception?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
    return false;
}
void seh_consume(void* raw) {
    auto& session=*static_cast<GripSession*>(raw);
    session.consume(1,100,[](GripSession::Admission&) {RaiseException(test_exception,0,0,nullptr);});
}
#endif
}
int main() {
    {
        Fixture f;GripSession s;
        assert(!s.admit(1,f.epochs,f.anchor,{},f.body,100,100,100));
        assert(!s.admit(1,f.epochs,f.anchor,{f.lease,72},f.body,100,100,100));
        const auto other_contact=GripAnchor::capture(f.id,f.pose,f.pose,{.03,0,0},{1,0,0});assert(other_contact);
        assert(!s.admit(1,f.epochs,*other_contact,{f.lease,73},f.body,100,100,100));
        const auto other_normal=GripAnchor::capture(f.id,f.pose,f.pose,{.02,0,0},{0,1,0});assert(other_normal);
        assert(!s.admit(1,f.epochs,*other_normal,{f.lease,73},f.body,100,100,100));
        auto shifted_hand=f.pose;shifted_hand.position.x=.01;
        const auto other_hand=GripAnchor::capture(f.id,f.pose,shifted_hand,{.02,0,0},{1,0,0});assert(other_hand);
        assert(!s.admit(1,f.epochs,*other_hand,{f.lease,73},f.body,100,100,100));
        assert(f.admit(s));assert(!f.admit(s));unsigned commits{};
        assert(s.consume(1,100,[&](GripSession::Admission& a) {
            assert(a.current(132));assert(!a.current(133));
            assert(a.commit(100,[&](const GripSession::Snapshot& value) {
                assert(value.token==1 && value.body.id==f.id);++commits;
            }));
        }));assert(commits==1);
        assert(!s.consume(2,100,[](auto&){}));assert(!s.consume(1,99,[](auto&){}));
        assert(!s.consume(1,133,[](auto&){}));
        assert(s.revoke()==GripSession::Drain::drained);assert(!f.admit(s));assert(f.admit(s,2));
    }
    {
        Fixture f;GripSession s;assert(f.admit(s));
        for(unsigned field=0;field<7;++field) {
            auto p=f.sample();
            switch(field) {
            case 0:++p.body.id.world;break;case 1:++p.body.id.body_owner;break;
            case 2:++p.body.id.shape_owner;break;case 3:p.body.id.body_handle+=0x1000000;break;
            case 4:++p.body.id.shape_key;break;case 5:++p.body.id.body_generation;break;
            case 6:++p.body.id.geometry_generation;break;
            }
            p.anchor=*GripAnchor::capture(p.body.id,f.pose,f.pose,{.02,0,0},{1,0,0});
            assert(!s.publish(p,101));
        }
        for(unsigned field=0;field<4;++field) {
            auto p=f.sample();
            switch(field){case 0:++p.epochs.owner;break;case 1:++p.epochs.pose;break;
                case 2:++p.epochs.reference;break;case 3:++p.epochs.mapping;break;}
            assert(!s.publish(p,101));
        }
        auto p=f.sample();p.token=2;assert(!s.publish(p,101));
        p=f.sample(99);assert(!s.publish(p,101));
        p=f.sample();p.source_tick=99;assert(!s.publish(p,101));
        assert(!s.publish(f.sample(102),101));assert(!s.publish(f.sample(100),133));
        assert(s.publish(f.sample(101),101));
        assert(!s.publish(f.sample(100),101));
        f.lease->live=false;assert(!s.publish(f.sample(102),102));
        assert(!s.consume(1,101,[](auto&){}));
    }
    {
        Fixture f;GripSession s;assert(f.admit(s));
        // A newer actual-body sample invalidates a frozen admission, even in the same hold.
        assert(s.consume(1,100,[&](GripSession::Admission& old) {
            auto p=f.sample();p.body.pose.position.x=.1;
            auto moved_hand=f.pose;moved_hand.position.x=.05;
            p.anchor=*GripAnchor::capture(f.id,f.pose,moved_hand,{.02,0,0},{1,0,0});
            assert(s.publish(p,101));
            assert(!old.current(101));assert(!old.commit(101,[](auto&){assert(false);}));
        }));
        assert(s.consume(1,101,[&](GripSession::Admission& a) {
            assert(a.commit(101,[&](const auto& p) {
                assert(p.body.pose.position.x==.1);
                auto palm=p.anchor.hand_target(p.body.id,p.body.pose);assert(palm && palm->position.x==.1);
            }));
            f.lease->live=false;assert(!a.current(101));
            assert(!a.commit(101,[](auto&){assert(false);}));
        }));
    }
    {
        Fixture f;GripSession s;assert(f.admit(s));unsigned nested{};
        assert(s.consume(1,100,[&](GripSession::Admission& outer) {
            assert(s.consume(1,100,[&](GripSession::Admission& inner) {
                assert(inner.commit(100,[&](const auto&){++nested;}));
            }));
            assert(outer.commit(100,[&](const auto&) {
                assert(s.revoke(1s)==GripSession::Drain::pending);
                assert(!outer.current(100));assert(!outer.commit(100,[](auto&){assert(false);}));
                assert(!s.consume(1,100,[](auto&){assert(false);}));
            }));
            assert(!outer.current(100));assert(!f.admit(s,2));
        }));assert(nested==1);assert(s.revoke(20ms)==GripSession::Drain::drained);assert(f.admit(s,2));
    }
    {
        Fixture f;GripSession s;assert(f.admit(s));
        std::promise<void> entered,release;auto released=release.get_future();std::atomic<bool> stale{};
        std::thread consumer([&] {
            assert(s.consume(1,100,[&](GripSession::Admission& a) {
                entered.set_value();released.wait();
                stale=!a.current(100) && !a.commit(100,[](auto&){assert(false);});
            }));
        });
        assert(entered.get_future().wait_for(2s)==std::future_status::ready);
        assert(s.revoke(5ms)==GripSession::Drain::pending);
        assert(!s.consume(1,100,[](auto&){assert(false);}));
        assert(!f.admit(s,2));release.set_value();consumer.join();assert(stale);
        assert(s.revoke(100ms)==GripSession::Drain::drained);assert(f.admit(s,2));
    }
    {
        Fixture f;GripSession s;assert(f.admit(s));
        std::weak_ptr<FixtureLease> retained=f.lease;
        std::promise<void> entered,release;auto released=release.get_future();
        std::thread consumer([&] {
            assert(s.consume(1,100,[&](auto&) {entered.set_value();released.wait();}));
        });
        assert(entered.get_future().wait_for(2s)==std::future_status::ready);
        f.lease.reset();assert(s.revoke()==GripSession::Drain::pending);
        assert(!retained.expired());release.set_value();consumer.join();
        assert(retained.expired());assert(s.revoke()==GripSession::Drain::drained);
    }
    {
        Fixture f;GripSession s;assert(f.admit(s));
        std::promise<void> entered,release,drained;auto released=release.get_future();auto done=drained.get_future();
        std::atomic<unsigned> destroyed{};
        std::thread consumer([&] {
            auto owner=std::make_shared<Destruction>();owner->count=&destroyed;
            assert(s.consume(1,100,[&,owner=std::move(owner)](auto&) {entered.set_value();released.wait();}));
        });
        assert(entered.get_future().wait_for(2s)==std::future_status::ready);
        std::thread revoker([&] {assert(s.revoke(1s)==GripSession::Drain::drained);assert(destroyed==1);drained.set_value();});
        assert(done.wait_for(5ms)==std::future_status::timeout);
        release.set_value();assert(done.wait_for(2s)==std::future_status::ready);
        consumer.join();revoker.join();
    }
    {
        Fixture f;GripSession s;assert(f.admit(s));std::atomic<unsigned> destroyed{};volatile bool fault=true;
        try {
            auto owner=std::make_shared<Destruction>();owner->count=&destroyed;
            s.consume(1,100,[owner=std::move(owner),&fault](auto&) {if(fault)throw std::runtime_error("consume");});
            assert(false);
        }catch(const std::runtime_error&){}
        assert(destroyed==1);
        try {
            s.consume(1,100,[&](GripSession::Admission& a) {
                auto owner=std::make_shared<Destruction>();owner->count=&destroyed;
                a.commit(100,[owner=std::move(owner),&fault](const auto&) {if(fault)throw std::runtime_error("commit");});
            });assert(false);
        }catch(const std::runtime_error&){}
        assert(destroyed==2);assert(s.revoke(100ms)==GripSession::Drain::drained);
    }
#if defined(_MSC_VER)
    {
        Fixture f;GripSession s;assert(f.admit(s));assert(catches_seh(seh_consume,&s));
        assert(s.revoke(100ms)==GripSession::Drain::drained);assert(f.admit(s,2));
    }
    {
        Fixture f;GripSession s;assert(f.admit(s));std::atomic<unsigned> destroyed{};
        std::promise<void> entered,release;auto released=release.get_future();
        struct Context {GripSession* session;std::promise<void>* entered;std::future<void>* released;std::atomic<unsigned>* destroyed;};
        Context context{&s,&entered,&released,&destroyed};
        const auto call=[](void* raw) {
            auto& c=*static_cast<Context*>(raw);
            auto owner=std::make_shared<Destruction>();owner->count=c.destroyed;
            c.session->consume(1,100,[owner=std::move(owner),&c](GripSession::Admission& a) {
                a.commit(100,[owner,&c](const auto&) {
                    c.entered->set_value();c.released->wait();RaiseException(test_exception,0,0,nullptr);
                });
            });
        };
        std::thread worker([&]{assert(catches_seh(call,&context));});
        assert(entered.get_future().wait_for(2s)==std::future_status::ready);
        // Revocation is bounded even while the final commit owns serialization.
        assert(s.revoke(5ms)==GripSession::Drain::pending);
        release.set_value();worker.join();assert(destroyed==1);
        assert(s.revoke(100ms)==GripSession::Drain::drained);assert(f.admit(s,2));
    }
#endif
    std::cout<<"PASS grip session: exact identity and clocks, frozen-frame revocation, real thread race, reentry, self-drain, C++ and SEH cleanup\n";
}
