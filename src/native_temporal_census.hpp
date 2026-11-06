#pragma once
#include "temporal_eye_job.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace d2vr::temporal_census {
// Recording/binding evidence only. These identities never certify a shader hash,
// depth writer, GPU execution order, projection consumption or DLSS admission.
enum class Kind {job_enter,dispatch,job_exit};
enum class Stop {disabled,waiting_pair,capturing,pass_limit,requested,event_capacity,object_capacity,context_capacity,world_lost,callback_error};
struct Job {
    std::uintptr_t context{},view{};std::uint64_t thread{};
    std::optional<temporal_eye::NativePass> pass;
    std::optional<temporal_eye::ViewReceipt> setup;
};
struct Token {Job previous,entered;bool active{},scoped{};};
struct Scope {
    Job current;
    Token push(const Job& job) noexcept {Token token{current,job,true,true};current=job;return token;}
    void pop(const Token& token) noexcept {if(token.scoped)current=token.previous;}
};
struct Binding {
    std::uint64_t view{},resource{};
    unsigned resource_kind{},resource_size{},view_size{}; // 1 Texture2D, 2 buffer, 0 unknown
    std::array<std::uint8_t,64> resource_desc{};
    std::array<std::uint8_t,32> view_desc{};
};
struct Event {
    std::uint64_t sequence{},thread{};Kind kind{};Job job;
    std::uint64_t context{},device{},shader{};unsigned context_type{};
    std::array<unsigned,3> groups{};
    bool counter_after_present{},stamp_stable{},view_stable{};
    std::uint32_t counter_after{};
    std::array<Binding,8> bindings{}; // t0..t3, u0..u3; descriptors are complete native bytes
};
struct Object {
    std::uintptr_t address{};
    void (*retain)(std::uintptr_t) noexcept=nullptr;
    void (*release)(std::uintptr_t) noexcept=nullptr;
};
class Ledger {
public:
    static constexpr unsigned context_capacity=16,object_capacity=128,event_capacity=4096,pass_capacity=120;
    struct Summary {
        Stop stop=Stop::disabled;bool incomplete{};std::uint64_t attempted{},accepted{},dropped{},unknown{},passes{},active_jobs{},pass_admission_refused{};
        unsigned objects{},contexts{};
        std::uint64_t inside_dispatches{},outside_dispatches{};
    };
private:
    Summary summary_;
    temporal_eye::NativePairBook pairs_;
    std::array<std::optional<temporal_eye::NativePass>,2> setup_passes_{};
    std::array<Object,object_capacity> objects_{};
    std::array<std::uintptr_t,context_capacity> contexts_{};
    std::array<Event,event_capacity> events_{};
    bool armed_once_{};
    void overflow(Stop why) noexcept {summary_.stop=why;summary_.incomplete=true;}
    std::uint64_t object(std::uintptr_t address,const Object* incoming,unsigned count) noexcept {
        if(!address)return 0;
        for(unsigned i=0;i<summary_.objects;++i)if(objects_[i].address==address)return i+1;
        if(summary_.objects==object_capacity) {overflow(Stop::object_capacity);return 0;}
        for(unsigned i=0;i<count;++i)if(incoming[i].address==address && incoming[i].retain && incoming[i].release) {
            incoming[i].retain(address);objects_[summary_.objects]=incoming[i];return ++summary_.objects;
        }
        return 0;
    }
public:
    Ledger()=default;
    Ledger(const Ledger&)=delete;Ledger& operator=(const Ledger&)=delete;
    ~Ledger() {release_objects();}
    // Only after callback readers and file consumers drain; never from a command callback.
    void release_objects() noexcept {for(unsigned i=0;i<summary_.objects;++i) {const auto value=objects_[i];objects_[i]={};if(value.release)value.release(value.address);}summary_.objects=0;}
    void configure(bool enabled) noexcept {
        release_objects();summary_={};summary_.stop=enabled?Stop::waiting_pair:Stop::disabled;
        pairs_.invalidate();setup_passes_={};armed_once_=false;
    }
    bool wants_setup() const noexcept {return summary_.stop==Stop::waiting_pair || summary_.stop==Stop::capturing;}
    bool armed() const noexcept {return summary_.stop==Stop::capturing;}
    bool accepts_job() const noexcept {return armed() && summary_.passes<pass_capacity;}
    bool job_limit_closed() const noexcept {return summary_.passes==pass_capacity;}
    void refuse_job() noexcept {++summary_.pass_admission_refused;}
    bool setup(const temporal_eye::SetupObservation& observation,bool paired_world) noexcept {
        if(!wants_setup())return false;
        if(!paired_world) {pairs_.invalidate();setup_passes_={};if(armed()) {summary_.stop=Stop::world_lost;summary_.incomplete=true;}return false;}
        if(!pairs_.publish(observation)) {setup_passes_={};return false;}
        setup_passes_[observation.eye]=observation.pass;
        if(setup_passes_[0] && setup_passes_[1] && pairs_.resolve(0,*setup_passes_[0]) && pairs_.resolve(1,*setup_passes_[1])) {
            if(!armed_once_) {summary_.stop=Stop::capturing;armed_once_=true;}return true;
        }
        return false;
    }
    std::optional<temporal_eye::ViewReceipt> resolve(const temporal_eye::NativePass& pass) const noexcept {
        for(unsigned eye=0;eye<2;++eye)if(auto value=pairs_.resolve(eye,pass))return value;
        return {};
    }
    void stop() noexcept {
        if(summary_.stop==Stop::capturing || summary_.stop==Stop::waiting_pair) {
            if(summary_.stop==Stop::capturing && summary_.active_jobs)summary_.incomplete=true;
            // Waiting remains explicit missing evidence, not a completed capture.
            if(summary_.stop==Stop::capturing)summary_.stop=Stop::requested;
        }
    }
    bool append(Event event,const Object* incoming=nullptr,unsigned count=0,bool exiting=false) noexcept {
        if(event.kind==Kind::job_enter && armed() && job_limit_closed()) {refuse_job();return false;}
        if((summary_.stop==Stop::disabled || summary_.stop==Stop::waiting_pair) && !exiting)return false;
        ++summary_.attempted;
        if((!armed() && !exiting) || summary_.accepted==event_capacity) {
            if(summary_.accepted==event_capacity)overflow(Stop::event_capacity);
            ++summary_.dropped;summary_.incomplete=true;return false;
        }
        if(event.kind==Kind::job_enter) {
            ++summary_.passes;++summary_.active_jobs;
        }
        if(event.context) {
            bool known=false;for(unsigned i=0;i<summary_.contexts;++i)known=known || contexts_[i]==event.context;
            if(!known) {
                if(summary_.contexts==context_capacity)overflow(Stop::context_capacity);
                else contexts_[summary_.contexts++]=static_cast<std::uintptr_t>(event.context);
            }
        }
        if(!summary_.incomplete) {
            event.context=object(static_cast<std::uintptr_t>(event.context),incoming,count);
            event.device=object(static_cast<std::uintptr_t>(event.device),incoming,count);
            event.shader=object(static_cast<std::uintptr_t>(event.shader),incoming,count);
            for(auto& binding:event.bindings) {
                binding.view=object(static_cast<std::uintptr_t>(binding.view),incoming,count);
                binding.resource=object(static_cast<std::uintptr_t>(binding.resource),incoming,count);
            }
        }
        if(event.kind==Kind::job_exit && summary_.active_jobs)--summary_.active_jobs;
        if(summary_.incomplete) {++summary_.dropped;return false;}
        if(!event.job.setup || (event.kind==Kind::dispatch && !event.shader))++summary_.unknown;
        if(event.kind==Kind::dispatch) {if(event.job.context)++summary_.inside_dispatches;else ++summary_.outside_dispatches;}
        event.sequence=summary_.attempted;events_[summary_.accepted++]=event;
        if(event.kind==Kind::job_exit && summary_.passes==pass_capacity && !summary_.active_jobs)summary_.stop=Stop::pass_limit;
        return true;
    }
    void invalidate_setup() noexcept {pairs_.invalidate();setup_passes_={};}
    void reject(Stop reason) noexcept {++summary_.attempted;++summary_.dropped;overflow(reason);}
    bool armed_once() const noexcept {return armed_once_;}
    const Summary& summary() const noexcept {return summary_;}
    const Event& event(unsigned i) const noexcept {return events_[i];}
    const Object& retained_object(unsigned i) const noexcept {return objects_[i];}
};
} // namespace d2vr::temporal_census

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
namespace d2vr {
static_assert(sizeof(D3D11_TEXTURE2D_DESC)<=64 && sizeof(D3D11_BUFFER_DESC)<=64,"census resource descriptor capacity");
static_assert(sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC)<=32 && sizeof(D3D11_UNORDERED_ACCESS_VIEW_DESC)<=32,"census view descriptor capacity");
class NativeTemporalCensus {
    using Job=temporal_census::Job;using Event=temporal_census::Event;using Kind=temporal_census::Kind;
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    using Metadata=std::optional<temporal_eye::NativePass> (*)(std::uintptr_t) noexcept;
private:
    temporal_census::Ledger ledger_;
    std::mutex mutex_;
    std::atomic<bool> accepting_{false},setup_enabled_{false},requested_{false},jobs_closed_{false};
    std::atomic<unsigned> readers_{};
    Read read_{};Metadata metadata_{};
    bool flushed_{};
    inline static thread_local temporal_census::Scope scope_{};
    bool enter_reader() noexcept {
        if(!accepting_.load(std::memory_order_acquire))return false;
        ++readers_;
        if(accepting_.load(std::memory_order_acquire))return true;
        --readers_;return false;
    }
    void sync_admission() noexcept {
        const bool armed=ledger_.armed();accepting_.store(armed,std::memory_order_release);
        setup_enabled_.store(ledger_.wants_setup(),std::memory_order_release);
        jobs_closed_.store(ledger_.job_limit_closed(),std::memory_order_release);
    }
    static std::uint64_t thread() noexcept {return GetCurrentThreadId();}
    static void retain(std::uintptr_t at) noexcept {reinterpret_cast<IUnknown*>(at)->AddRef();}
    static void release(std::uintptr_t at) noexcept {reinterpret_cast<IUnknown*>(at)->Release();}
    template<class T> static void descriptor(T* resource,temporal_census::Binding& out) noexcept {
        Ptr<ID3D11Texture2D> texture;
        if(SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) {
            D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);out.resource_kind=1;out.resource_size=sizeof desc;std::memcpy(out.resource_desc.data(),&desc,sizeof desc);return;
        }
        Ptr<ID3D11Buffer> buffer;
        if(SUCCEEDED(resource->QueryInterface(IID_PPV_ARGS(&buffer)))) {
            D3D11_BUFFER_DESC desc{};buffer->GetDesc(&desc);out.resource_kind=2;out.resource_size=sizeof desc;std::memcpy(out.resource_desc.data(),&desc,sizeof desc);
        }
    }
    static void hex(std::ostream& out,const std::uint8_t* bytes,unsigned n) {const char digits[]="0123456789abcdef";for(unsigned i=0;i<n;++i)out<<digits[bytes[i]>>4]<<digits[bytes[i]&15];}
public:
    static NativeTemporalCensus& process_owner() {static NativeTemporalCensus value;return value;}
    bool configure(bool requested,Read reader,Metadata metadata) noexcept {
        if(requested_.load() && !flushed_)return false; // never reset an unconsumed prior capture
        accepting_=false;setup_enabled_=false;
        if(readers_.load())return false;
        std::lock_guard<std::mutex> lock(mutex_);
        ledger_.configure(requested);read_=reader;metadata_=metadata;flushed_=false;requested_=requested;sync_admission();return true;
    }
    bool wants_setup() const noexcept {return setup_enabled_.load(std::memory_order_acquire);}
    bool wants_metadata() const noexcept {return wants_setup() || readers_.load()!=0;}
    void setup(const temporal_eye::SetupObservation& observation,bool paired_world) noexcept {
        if(!wants_setup())return;
        ++readers_;
        {std::lock_guard<std::mutex> lock(mutex_);if(wants_setup()) {ledger_.setup(observation,paired_world);sync_admission();}}
        --readers_;
    }
    void invalidate_setup() noexcept {if(!wants_setup())return;std::lock_guard<std::mutex> lock(mutex_);if(wants_setup())ledger_.invalidate_setup();}
    void observe_job(bool entering,void* context,temporal_census::Token& token) noexcept {
        bool held=!entering && token.active;
        try {
            if(entering) {
                if(jobs_closed_.load(std::memory_order_acquire)) {
                    if(accepting_.load()) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        // The control thread may have stopped/drained while this
                        // callback waited. Never mutate a flushed snapshot then.
                        if(accepting_.load() && jobs_closed_.load())ledger_.refuse_job();
                    }
                    if(scope_.current.context) {token=scope_.push({});token.active=false;}
                    return;
                }
                if(!enter_reader())return;
                held=true;
                Job job;job.context=reinterpret_cast<std::uintptr_t>(context);job.thread=thread();
                // Only the byte-pinned POSTPROC ctx+70 ABI; never used for another job.
                if(read_ && read_(job.context+0x70,&job.view,sizeof job.view) && job.view && metadata_)job.pass=metadata_(job.view);
                Event e;e.kind=Kind::job_enter;e.thread=thread();e.job=job;
                // Counter stability is provided by the safe metadata reader;
                // the engine job view pointer is independently rechecked.
                std::uintptr_t checked_view{};e.view_stable=read_ && read_(job.context+0x70,&checked_view,sizeof checked_view) && checked_view==job.view;
                if(!e.view_stable) {job.pass.reset();job.setup.reset();e.job=job;}
                if(job.pass) {e.counter_after_present=true;e.counter_after=job.pass->counter;e.stamp_stable=true;}
                {
                    std::lock_guard<std::mutex> lock(mutex_);if(job.pass)job.setup=ledger_.resolve(*job.pass);e.job=job;
                    if(!ledger_.append(e)) {sync_admission();--readers_;held=false;if(scope_.current.context) {token=scope_.push({});token.active=false;}return;}
                    sync_admission();
                }
                token=scope_.push(job);
            } else if(token.scoped && !token.active) {scope_.pop(token);token.scoped=false;}
            else if(token.active) {
                Event e;e.kind=Kind::job_exit;e.thread=thread();e.job=token.entered;
                std::uintptr_t current_view{};e.view_stable=read_ && read_(reinterpret_cast<std::uintptr_t>(context)+0x70,&current_view,sizeof current_view) && current_view==token.entered.view;
                const auto after=e.view_stable && current_view && metadata_?metadata_(current_view):std::nullopt;
                if(after) {e.counter_after_present=true;e.counter_after=after->counter;e.stamp_stable=token.entered.pass && *after==*token.entered.pass;}
                {std::lock_guard<std::mutex> lock(mutex_);ledger_.append(e,nullptr,0,true);sync_admission();}
                scope_.pop(token);token.active=false;token.scoped=false;--readers_;held=false;
            }
        } catch(...) {
            if(token.scoped) {scope_.pop(token);token.active=false;token.scoped=false;}
            {std::lock_guard<std::mutex> lock(mutex_);ledger_.reject(temporal_census::Stop::callback_error);sync_admission();}
            if(held)--readers_;
        }
    }
    void dispatch(ID3D11DeviceContext* context,UINT x,UINT y,UINT z) noexcept {
        if(!scope_.current.context)return; // Only admitted native POSTPROC scopes; no D3D access outside them.
        if(!enter_reader())return;
        try {
            Event e;e.kind=Kind::dispatch;e.thread=thread();e.job=scope_.current;e.groups={x,y,z};e.context_type=static_cast<unsigned>(context->GetType());
            std::array<Ptr<IUnknown>,19> holds{};std::array<temporal_census::Object,19> incoming{};unsigned count=0;
            auto canonical=[&](IUnknown* object)->std::uint64_t {
                if(!object || count==holds.size())return 0;
                if(FAILED(object->QueryInterface(IID_PPV_ARGS(&holds[count]))))return 0;
                const auto address=reinterpret_cast<std::uintptr_t>(holds[count].Get());incoming[count++]={address,&retain,&release};return address;
            };
            e.context=canonical(context);Ptr<ID3D11Device> device;context->GetDevice(&device);e.device=canonical(device.Get());
            Ptr<ID3D11ComputeShader> shader;context->CSGetShader(&shader,nullptr,nullptr);e.shader=canonical(shader.Get());
            std::array<ID3D11ShaderResourceView*,4> srv{};std::array<ID3D11UnorderedAccessView*,4> uav{};
            context->CSGetShaderResources(0,4,srv.data());context->CSGetUnorderedAccessViews(0,4,uav.data());
            // Adopt getter references immediately; no vectors/shared_ptr/control-block allocation.
            std::array<Ptr<ID3D11ShaderResourceView>,4> srvs{};std::array<Ptr<ID3D11UnorderedAccessView>,4> uavs{};
            for(unsigned i=0;i<4;++i) {srvs[i].Attach(srv[i]);uavs[i].Attach(uav[i]);}
            for(unsigned i=0;i<8;++i) {
                auto& binding=e.bindings[i];Ptr<ID3D11Resource> resource;
                if(i<4 && srvs[i]) {D3D11_SHADER_RESOURCE_VIEW_DESC desc{};srvs[i]->GetDesc(&desc);binding.view_size=sizeof desc;std::memcpy(binding.view_desc.data(),&desc,sizeof desc);binding.view=canonical(srvs[i].Get());srvs[i]->GetResource(&resource);}
                else if(i>=4 && uavs[i-4]) {D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};uavs[i-4]->GetDesc(&desc);binding.view_size=sizeof desc;std::memcpy(binding.view_desc.data(),&desc,sizeof desc);binding.view=canonical(uavs[i-4].Get());uavs[i-4]->GetResource(&resource);}
                if(resource) {binding.resource=canonical(resource.Get());descriptor(resource.Get(),binding);}
            }
            if(e.job.context && e.job.view && read_ && metadata_) {
                std::uintptr_t view{};e.view_stable=read_(e.job.context+0x70,&view,sizeof view) && view==e.job.view;
                if(e.view_stable) {const auto after=metadata_(view);if(after) {e.counter_after_present=true;e.counter_after=after->counter;e.stamp_stable=e.job.pass && *after==*e.job.pass;}}
            }
            {std::lock_guard<std::mutex> lock(mutex_);ledger_.append(e,incoming.data(),count);sync_admission();}
        } catch(...) {std::lock_guard<std::mutex> lock(mutex_);ledger_.reject(temporal_census::Stop::callback_error);sync_admission();}
        --readers_;
    }
    bool stop() noexcept {
        {std::lock_guard<std::mutex> lock(mutex_);ledger_.stop();accepting_=false;setup_enabled_=false;}
        return readers_.load()==0;
    }
    // Controlled non-render thread only. No callback CSV/JSON writes or waiting.
    bool flush(const std::filesystem::path& output,bool stopping=false) noexcept {
        if(!requested_.load() || flushed_)return true;
        if(stopping)stop();
        if(output.empty())return true;   // v1046: diagnostics off = nothing to publish (output/name would be relative, and remove() would hit the game's folder)
        if(accepting_.load() || setup_enabled_.load() || readers_.load())return false;
        try {
            const auto summary=ledger_.summary();
            // The JSON is the completion marker. Remove it before any rewrite;
            // publish a fresh marker only after both data files are committed.
            std::filesystem::remove(output/L"native-temporal-census.json");
            std::ofstream csv(output/L"native-temporal-census.csv.tmp",std::ios::trunc);
            csv<<"sequence,kind,thread,job,view,counter_present,counter_before,counter_after_present,counter_after,stamp_stable,view_stable,setup_serial,pair,owner_generation,allocation,camera_player,camera,camera_generation,setup_frame,setup_eye,context,context_type,device,shader,shader_hash_known,depth_producer_known,groups_x,groups_y,groups_z,slot_kind,slot,view_object,resource,resource_kind,width_or_bytes,height,mips,array_size,resource_format,sample_count,sample_quality,usage,bind_flags,cpu_access,misc_flags,buffer_stride,view_format,view_dimension,resource_descriptor_hex,view_descriptor_hex\n";
            for(unsigned i=0;i<summary.accepted;++i) {
                const auto& e=ledger_.event(i);const auto* receipt=e.job.setup?&*e.job.setup:nullptr;
                for(unsigned binding_index=0;binding_index<(e.kind==Kind::dispatch?8u:1u);++binding_index) {
                    const auto& b=e.bindings[binding_index];
                    csv<<e.sequence<<','<<static_cast<unsigned>(e.kind)<<','<<e.thread<<','<<e.job.context<<','<<e.job.view<<','<<bool(e.job.pass)<<','<<(e.job.pass?e.job.pass->counter:0)<<','<<e.counter_after_present<<','<<e.counter_after<<','<<e.stamp_stable<<','<<e.view_stable<<','<<(receipt?receipt->serial:0)<<','<<(receipt?receipt->pair:0)<<','<<(receipt?receipt->owner_generation:0)<<','<<(receipt?receipt->setup.allocation:0)<<','<<(receipt?receipt->setup.camera_owner[0]:0)<<','<<(receipt?receipt->setup.camera_owner[1]:0)<<','<<(receipt?receipt->setup.camera_owner[2]:0)<<','<<(receipt?receipt->frame:0)<<','<<(receipt?receipt->setup.eye:0)<<','<<e.context<<','<<e.context_type<<','<<e.device<<','<<e.shader<<",0,0,"<<e.groups[0]<<','<<e.groups[1]<<','<<e.groups[2]<<','<<(binding_index<4?'t':'u')<<','<<(binding_index%4)<<','<<b.view<<','<<b.resource<<','<<b.resource_kind<<',';
                    D3D11_TEXTURE2D_DESC texture{};D3D11_BUFFER_DESC buffer{};unsigned view_format=0,view_dimension=0;
                    if(b.resource_kind==1)std::memcpy(&texture,b.resource_desc.data(),sizeof texture);
                    if(b.resource_kind==2)std::memcpy(&buffer,b.resource_desc.data(),sizeof buffer);
                    if(b.view_size>=8) {std::memcpy(&view_format,b.view_desc.data(),4);std::memcpy(&view_dimension,b.view_desc.data()+4,4);}
                    csv<<(b.resource_kind==2?buffer.ByteWidth:texture.Width)<<','<<texture.Height<<','<<texture.MipLevels<<','<<texture.ArraySize<<','<<static_cast<unsigned>(texture.Format)<<','<<texture.SampleDesc.Count<<','<<texture.SampleDesc.Quality<<','<<(b.resource_kind==2?buffer.Usage:texture.Usage)<<','<<(b.resource_kind==2?buffer.BindFlags:texture.BindFlags)<<','<<(b.resource_kind==2?buffer.CPUAccessFlags:texture.CPUAccessFlags)<<','<<(b.resource_kind==2?buffer.MiscFlags:texture.MiscFlags)<<','<<buffer.StructureByteStride<<','<<view_format<<','<<view_dimension<<',';
                    hex(csv,b.resource_desc.data(),b.resource_size);csv<<',';hex(csv,b.view_desc.data(),b.view_size);csv<<'\n';
                }
            }
            std::ofstream objects(output/L"native-temporal-census-objects.csv.tmp",std::ios::trunc);objects<<"identity,canonical_address\n";
            for(unsigned i=0;i<summary.objects;++i)objects<<i+1<<','<<ledger_.retained_object(i).address<<'\n';
            csv.flush();objects.flush();csv.close();objects.close();if(!csv || !objects)return false;
            std::filesystem::remove(output/L"native-temporal-census.csv");
            std::filesystem::rename(output/L"native-temporal-census.csv.tmp",output/L"native-temporal-census.csv");
            std::filesystem::remove(output/L"native-temporal-census-objects.csv");
            std::filesystem::rename(output/L"native-temporal-census-objects.csv.tmp",output/L"native-temporal-census-objects.csv");
            const char* reason="unknown";
            switch(summary.stop) {
            case temporal_census::Stop::disabled:reason="disabled";break;
            case temporal_census::Stop::waiting_pair:reason="waiting_missing_qualified_pair";break;
            case temporal_census::Stop::capturing:reason="capturing";break;
            case temporal_census::Stop::pass_limit:reason="native_pass_limit";break;
            case temporal_census::Stop::requested:reason="requested_stop";break;
            case temporal_census::Stop::event_capacity:reason="event_capacity";break;
            case temporal_census::Stop::object_capacity:reason="object_capacity";break;
            case temporal_census::Stop::context_capacity:reason="context_capacity";break;
            case temporal_census::Stop::world_lost:reason="paired_world_lost";break;
            case temporal_census::Stop::callback_error:reason="callback_error";break;
            }
            std::ofstream json(output/L"native-temporal-census.json.tmp",std::ios::trunc);
            json<<"{\"diagnostic_only\":true,\"production_admission\":false,\"shader_bytecode_identity_known\":false,\"depth_producer_known\":false,\"gpu_order_proven\":false,\"all_process_dispatch_coverage\":false,\"coverage\":\"native POSTPROC-scoped existing hooked Dispatch implementation only\",\"stop\":"<<static_cast<unsigned>(summary.stop)<<",\"stop_reason\":\""<<reason<<"\",\"complete\":"<<((summary.stop==temporal_census::Stop::pass_limit && !summary.incomplete && !summary.dropped && !summary.active_jobs)?"true":"false")<<",\"incomplete\":"<<(summary.incomplete?"true":"false")<<",\"armed_once\":"<<(ledger_.armed_once()?"true":"false")<<",\"attempted\":"<<summary.attempted<<",\"accepted\":"<<summary.accepted<<",\"dropped\":"<<summary.dropped<<",\"unknown\":"<<summary.unknown<<",\"native_passes\":"<<summary.passes<<",\"pass_admission_refused\":"<<summary.pass_admission_refused<<",\"active_jobs\":"<<summary.active_jobs<<",\"inside_postproc_dispatches\":"<<summary.inside_dispatches<<",\"outside_postproc_dispatches_semantics\":\"recorded only; out-of-scope calls skipped before D3D access\",\"outside_postproc_dispatches\":"<<summary.outside_dispatches<<",\"objects\":"<<summary.objects<<",\"contexts\":"<<summary.contexts<<"}\n";
            json.flush();json.close();if(!json)return false;
            std::filesystem::rename(output/L"native-temporal-census.json.tmp",output/L"native-temporal-census.json");
            flushed_=true;requested_=false;ledger_.release_objects();return true;
        } catch(...) {return false;}
    }
};
} // namespace d2vr
#endif
