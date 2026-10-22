#include "native_temporal_census.hpp"
#include <cassert>
#include <iostream>
#include <memory>
#include <string>

using namespace d2vr::temporal_census;
namespace {
struct Tracked {unsigned held{},retains{},releases{};};
void retain(std::uintptr_t value) noexcept {auto& object=*reinterpret_cast<Tracked*>(value);++object.held;++object.retains;}
void release(std::uintptr_t value) noexcept {auto& object=*reinterpret_cast<Tracked*>(value);assert(object.held);--object.held;++object.releases;}
Object tracked(Tracked& value) {return {reinterpret_cast<std::uintptr_t>(&value),&retain,&release};}
temporal_eye::SetupObservation setup(unsigned eye,std::uint32_t counter=0) {
    temporal_eye::SetupObservation value;value.eye=eye;value.pass.view=100+eye;value.pass.counter=counter;
    value.allocation=200;value.camera_owner={300,400,5};value.setup_completed=true;return value;
}
void arm(Ledger& ledger,std::uint32_t counter=0) {
    ledger.configure(true);assert(!ledger.setup(setup(0,counter),true));assert(ledger.setup(setup(1,counter),true));assert(ledger.armed());
}
Event dispatch(std::uintptr_t context=0,std::uintptr_t shader=0) {Event e;e.kind=Kind::dispatch;e.context=context;e.shader=shader;e.groups={3,5,7};return e;}
}
int main() {
    // Large trace storage is deliberately off the 1MiB Windows stack. No GPU/COM calls.
    auto ledger=std::make_unique<Ledger>();Tracked context,shader;
    const Object objects[]{tracked(context),tracked(shader)};
    auto event=dispatch(objects[0].address,objects[1].address);
    assert(!ledger->append(event,objects,2) && !context.held && ledger->summary().attempted==0);
    ledger->configure(true);assert(!ledger->armed());
    assert(!ledger->setup(setup(0),false) && !ledger->setup(setup(1),true)); // no paired-world proof
    auto changed=setup(0);changed.setup_completed=false;assert(!ledger->setup(changed,true) && !ledger->armed());
    ledger->configure(true);assert(!ledger->setup(setup(0),true));assert(!ledger->setup(setup(0),true));assert(!ledger->setup(setup(1),true));
    assert(!ledger->setup(setup(0,1),true));assert(ledger->setup(setup(1,1),true));
    auto receipt=ledger->resolve(setup(0,1).pass);assert(receipt && receipt->pair && receipt->serial && receipt->setup.pass.counter==1);
    changed=setup(0,1);changed.pass.projection[0]=1;assert(!ledger->resolve(changed.pass));
    // Nested exact job receipt scope restores its prior observation, including unknown metadata.
    Scope scope;Job outer;outer.context=11;outer.view=100;outer.pass=setup(0,1).pass;outer.setup=receipt;
    const auto first=scope.push(outer);Job inner;inner.context=12;inner.view=101;
    const auto nested=scope.push(inner);assert(scope.current.context==12 && !scope.current.setup);
    scope.pop(nested);assert(scope.current.context==11 && scope.current.setup->serial==receipt->serial);
    scope.pop(first);assert(!scope.current.context && !scope.current.setup);
    event.job=outer;event.bindings[0].resource_desc[0]=99;event.bindings[0].resource_size=1;
    assert(ledger->append(event,objects,2));assert(ledger->append(event,objects,2));
    assert(context.held==1 && shader.held==1 && context.retains==1 && shader.retains==1);
    assert(ledger->event(0).context==ledger->event(1).context && ledger->event(0).shader==ledger->event(1).shader);
    assert(ledger->event(0).sequence<ledger->event(1).sequence && ledger->event(0).bindings[0].resource_desc[0]==99);
    ledger->stop();assert(context.held && shader.held); // stopped snapshot consumer still needs strong identities
    assert(ledger->summary().stop==Stop::requested && !ledger->summary().incomplete);
    assert(ledger->event(0).job.setup->pair==receipt->pair);ledger->release_objects();assert(!context.held && !shader.held && context.releases==1);
    // Unknown provenance stays unknown; a shape or shader object never becomes a bytecode/writer identity.
    arm(*ledger);assert(ledger->append(dispatch()));assert(ledger->summary().unknown==1 && !ledger->event(0).job.setup);
    // Never overwrite on event exhaustion, and every admitted loss makes capture incomplete.
    arm(*ledger);
    for(unsigned i=0;i<Ledger::event_capacity;++i)assert(ledger->append(dispatch()));
    const auto saved=ledger->event(0).sequence;assert(!ledger->append(dispatch()));
    assert(ledger->summary().incomplete && ledger->summary().dropped==1 && ledger->summary().stop==Stop::event_capacity && ledger->event(0).sequence==saved);
    // Fixed object references remain retained after overflow until controlled release.
    arm(*ledger);std::array<Tracked,Ledger::object_capacity+1> many{};
    for(unsigned i=0;i<many.size();++i) {const auto object=tracked(many[i]);const bool accepted=ledger->append(dispatch(0,object.address),&object,1);assert(accepted==(i<Ledger::object_capacity));}
    assert(ledger->summary().incomplete && ledger->summary().stop==Stop::object_capacity && ledger->summary().objects==Ledger::object_capacity);
    for(unsigned i=0;i<Ledger::object_capacity;++i)assert(many[i].held==1);
    ledger->release_objects();for(auto& value:many)assert(!value.held);
    arm(*ledger);std::array<Tracked,Ledger::context_capacity+1> contexts{};
    for(unsigned i=0;i<contexts.size();++i) {const auto object=tracked(contexts[i]);const bool accepted=ledger->append(dispatch(object.address),&object,1);assert(accepted==(i<Ledger::context_capacity));}
    assert(ledger->summary().incomplete && ledger->summary().stop==Stop::context_capacity);
    ledger->release_objects();
    // Exactly120 complete scopes is lossless normal completion, not an overflow.
    arm(*ledger);
    for(unsigned i=0;i<Ledger::pass_capacity;++i) {
        Event enter;enter.kind=Kind::job_enter;enter.job=outer;assert(ledger->append(enter));
        Event exit=enter;exit.kind=Kind::job_exit;exit.counter_after_present=true;exit.counter_after=1;exit.stamp_stable=true;assert(ledger->append(exit,nullptr,0,true));
    }
    assert(ledger->summary().stop==Stop::pass_limit && !ledger->summary().incomplete && !ledger->summary().dropped && ledger->summary().active_jobs==0 && ledger->summary().passes==120);
    // A reader admitted just before stop, but stored afterward, is an explicit loss.
    assert(!ledger->append(dispatch()) && ledger->summary().incomplete && ledger->summary().dropped==1);
    arm(*ledger);
    for(unsigned i=0;i<Ledger::pass_capacity;++i) {Event enter;enter.kind=Kind::job_enter;assert(ledger->append(enter));}
    Event extra;extra.kind=Kind::job_enter;assert(!ledger->append(extra));
    assert(!ledger->summary().incomplete && !ledger->summary().dropped && ledger->summary().pass_admission_refused==1);
    for(unsigned i=0;i<Ledger::pass_capacity;++i) {Event exit;exit.kind=Kind::job_exit;assert(ledger->append(exit,nullptr,0,true));}
    assert(ledger->summary().active_jobs==0 && !ledger->summary().incomplete && !ledger->summary().dropped && ledger->summary().stop==Stop::pass_limit);
    arm(*ledger);assert(!ledger->setup(setup(0,2),false));assert(ledger->summary().stop==Stop::world_lost && ledger->summary().incomplete);
    ledger->configure(true);ledger->stop();assert(ledger->summary().stop==Stop::waiting_pair && ledger->summary().passes==0);
#ifdef _WIN32
    { // CPU-only control-wrapper/file-publication fixture; NO D3D device or getters.
        auto& capture=d2vr::NativeTemporalCensus::process_owner();
        const auto root=std::filesystem::temp_directory_path()/(L"d2vr-census-control-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        const auto missing=root/L"missing";
        assert(capture.configure(true,nullptr,nullptr) && capture.stop());
        assert(!capture.flush(missing) && !capture.configure(false,nullptr,nullptr));
        assert(!std::filesystem::exists(missing/L"native-temporal-census.json"));
        std::filesystem::create_directories(root);
        {std::ofstream old(root/L"native-temporal-census.json");old<<"{\"complete\":true}";}
        std::filesystem::create_directory(root/L"native-temporal-census.csv.tmp"); // force data-file open failure
        assert(!capture.flush(root) && !std::filesystem::exists(root/L"native-temporal-census.json"));
        assert(!capture.configure(true,nullptr,nullptr)); // unconsumed data cannot be discarded
        std::filesystem::remove(root/L"native-temporal-census.csv.tmp");
        assert(capture.flush(root));
        std::ifstream summary(root/L"native-temporal-census.json");
        const std::string text((std::istreambuf_iterator<char>(summary)),std::istreambuf_iterator<char>());
        assert(text.find("\"complete\":false")!=std::string::npos && text.find("waiting_missing_qualified_pair")!=std::string::npos);
        summary.close();assert(capture.configure(false,nullptr,nullptr));std::filesystem::remove_all(root);
    }
#endif
    std::cout<<"native_temporal_census_check: passed (portable binding/lifetime evidence only)\n";
}
