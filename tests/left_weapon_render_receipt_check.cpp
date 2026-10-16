#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/left_weapon_render_receipt.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <thread>
#include <iterator>

namespace d2vr {
struct LeftWeaponRenderReceiptCheck {
    using Sink=LeftWeaponRenderReceipt;
    static bool read(std::uintptr_t,void*,std::size_t){return false;}
    static void reset_tls(){Sink::depth_=0;for(auto& pending:Sink::pending_)pending={};}
    static NativeAttachmentSource source() {
        NativeAttachmentSource source{};source.tick=source.source_tick=GetTickCount64();source.revision=400;
        source.diagnostic_socket_token=7;source.epoch=10;source.reference_epoch=11;source.mapping_revision=12;
        source.model=0x810000;source.animation_identity={5,10};return source;
    }
    static Sink::Row row() {
        Sink::Row row{};row.token=7;row.publication_revision=99;row.pose=10;row.reference=11;row.mapping=12;
        row.arm_model=0x810000;row.animation=0x820000;row.animation_identity={5,10};return row;
    }
    static void pending(Sink::Row row,unsigned depth=1) {
        Sink::depth_=depth;auto& pending=Sink::pending_[depth-1];pending={};pending.sampled=true;pending.row=row;
    }
    static void reader_cases(Sink& sink) {
        NativeLeftWeaponSocketOwner::Diagnostic first_publication{},second_publication{};
        first_publication.token=second_publication.token=7;first_publication.revision=second_publication.revision=99;
        first_publication.epochs=second_publication.epochs={10,11,12};
        assert(Sink::same_publication(first_publication,second_publication));++second_publication.revision;
        assert(!Sink::same_publication(first_publication,second_publication));
        const auto source_value=source();rig::Transform local{};local.f[0]=local.f[5]=local.f[10]=local.f[15]=1;
        reset_tls();pending(row());sink.reader(source_value,0x820000,59,6,false,false,nullptr);
        sink.reader(source_value,0x820000,59,6,true,true,&local);
        assert((Sink::pending_[0].row.reasons&64u)!=0 && Sink::pending_[0].row.reader_count==2);
        assert(Sink::pending_[0].row.reader_revision==400 && Sink::pending_[0].row.publication_revision==99);
        for(unsigned lane:{3u,7u,11u,15u}) {
            pending(row());auto invalid_local=local;invalid_local.f[lane]=std::numeric_limits<float>::quiet_NaN();
            sink.reader(source_value,0x820000,59,6,true,true,&invalid_local);
            assert(!Sink::pending_[0].row.local_valid && (Sink::pending_[0].row.reasons&64u));
            for(float value:Sink::pending_[0].row.selected_local.f)assert(std::isfinite(value));
        }
        for(unsigned mismatch=0;mismatch<6;++mismatch) {
            pending(row());auto mismatched=source_value;
            if(mismatch==0)++mismatched.diagnostic_socket_token;
            if(mismatch==1)++mismatched.epoch;
            if(mismatch==2)++mismatched.model;
            if(mismatch==3)++mismatched.animation_identity.generation;
            if(mismatch==4)++mismatched.reference_epoch;
            if(mismatch==5)++mismatched.mapping_revision;
            sink.reader(mismatched,0x820000,59,6,true,true,&local);
            assert((Sink::pending_[0].row.reasons&128u)!=0 && Sink::pending_[0].row.reader_count==0);
        }
        pending(row());sink.reader(source_value,0x820000,59,6,true,true,&local);
        auto later=source_value;++later.revision;sink.reader(later,0x820000,59,6,true,true,&local);
        assert(Sink::pending_[0].row.reasons&256u);
        reset_tls();
    }
    static void ring_cases(Sink& sink) {
        sink.head_=sink.count_=0;sink.dropped_=0;const auto sample=row();
        for(unsigned index=0;index<256;++index)sink.enqueue(sample);
        assert(sink.count_==256 && sink.dropped_==0);sink.enqueue(sample);
        assert(sink.count_==256 && sink.dropped_==1);
        sink.head_=sink.count_=0;std::atomic<bool> locked{},release{};
        std::thread holder([&]{std::lock_guard<std::mutex> lock(sink.queue_mutex_);locked=true;const auto deadline=GetTickCount64()+2000;
            while(!release.load()){assert(GetTickCount64()<deadline);std::this_thread::yield();}});
        const auto deadline=GetTickCount64()+2000;
        while(!locked.load()){assert(GetTickCount64()<deadline);std::this_thread::yield();}sink.enqueue(sample);release=true;holder.join();
        assert(sink.count_==0 && sink.dropped_==2);
    }
    static void nested_and_unwind_cases(Sink& sink) {
        sink.head_=sink.count_=0;reset_tls();pending(row());pending(row(),2);
        sink.bind(3,0,0,0,nullptr,nullptr,nullptr,nullptr,0);
        assert(Sink::depth_==1 && sink.count_==1 && (sink.rows_[0].reasons&8u));
        sink.bind(3,0,0,0,nullptr,nullptr,nullptr,nullptr,0);
        assert(Sink::depth_==0 && sink.count_==2 && (sink.rows_[1].reasons&8u));
        Sink::depth_=8;sink.active_=true;
        sink.bind(0,0,0,0,nullptr,nullptr,nullptr,nullptr,0);assert(Sink::depth_==9 && sink.overflow_>0);
        sink.bind(3,0,0,0,nullptr,nullptr,nullptr,nullptr,0);assert(Sink::depth_==8);
        reset_tls();sink.head_=sink.count_=0;
    }
    static void admission_cases(Sink& sink,const std::filesystem::path& folder) {
        sink.retire(true);assert(sink.configure(&read,0x140000000,folder));
        std::uint64_t sample_id{};assert(sink.try_sample(1000,sample_id));assert(!sink.try_sample(1099,sample_id));
        sink.attempts_=32767;assert(sink.try_sample(1100,sample_id) && sample_id==32768);
        assert(!sink.try_sample(1200,sample_id) && !sink.enabled() && sink.cap_==1);
        assert(!sink.try_sample(100000,sample_id));
        const auto attempts=sink.attempts_.load();assert(!sink.configure(&read,0x140000000,folder));assert(sink.attempts_==attempts);
        sink.retire(false);assert(sink.failed() && sink.log_.is_open());
        assert(!sink.configure(&read,0x140000000,folder) && sink.attempts_==attempts);
        sink.retire(true);assert(sink.failed() && !sink.log_.is_open());
        assert(sink.configure(&read,0x140000000,folder) && !sink.failed() && sink.attempts_==0);
    }
    static void serialization_and_io_cases(Sink& sink,const std::filesystem::path& folder) {
        Sink::Row invalid=row();invalid.dt=std::numeric_limits<float>::infinity();
        invalid.selected_local.f[3]=std::numeric_limits<float>::quiet_NaN();invalid.native.position[0]=-std::numeric_limits<float>::infinity();
        sink.enqueue(invalid);sink.retire(true);assert(!sink.failed());
        std::ifstream log(folder/L"left-weapon-bind-receipts.jsonl");
        const std::string text{std::istreambuf_iterator<char>(log),std::istreambuf_iterator<char>()};
        assert(text.find("nan")==std::string::npos && text.find("inf")==std::string::npos);
        assert(text.find("\"dt\":null")!=std::string::npos && text.find("null")!=std::string::npos);
        assert(sink.configure(&read,0x140000000,folder));sink.bytes_=64ull*1024*1024-32768;sink.enqueue(row());sink.flush();
        assert(sink.limit_ && !sink.enabled() && sink.dropped_==1);assert(!sink.configure(&read,0x140000000,folder));sink.retire(true);
        const auto blocked_log=folder/L"log-open-failure";std::filesystem::create_directories(blocked_log/L"left-weapon-bind-receipts.jsonl");
        assert(!sink.configure(&read,0x140000000,blocked_log) && sink.failed());sink.retire(true);
        assert(sink.configure(&read,0x140000000,folder));sink.summary_=folder;sink.flush(true);
        assert(sink.failed());sink.retire(true);
        assert(sink.configure(&read,0x140000000,folder));sink.log_.setstate(std::ios::badbit);sink.retire(true);
        assert(sink.failed());
    }
    static void run(const std::filesystem::path& folder) {
        Sink sink;assert(sink.configure(&read,0x140000000,folder));
        reader_cases(sink);ring_cases(sink);nested_and_unwind_cases(sink);
        admission_cases(sink,folder);serialization_and_io_cases(sink,folder);
    }
};

struct NativeHeldObjectCheck {
    inline static std::array<unsigned,8> events{};inline static unsigned event_count{},original_count{};inline static bool raise{};
    static std::uintptr_t __fastcall original(void*,int,float*,float*,float*,float*,float) {
        ++original_count;events[event_count++]=9;if(raise)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return 1234;
    }
    static void observe(unsigned phase,std::uintptr_t,int,std::uintptr_t,const float*,const float*,const float*,const float*,float)noexcept {
        events[event_count++]=phase;
    }
    static bool invoke_seh(float* position,float* axis) {
        __try{NativeHeldObject::bind_hook(reinterpret_cast<void*>(0x500000),3,position,axis,nullptr,nullptr,.01f);return false;}
        __except(EXCEPTION_EXECUTE_HANDLER){return true;}
    }
    static void run() {
        const auto saved_original=NativeHeldObject::bind_;NativeHeldObject::bind_=&original;NativeHeldObject::rf_=0;
        NativeHeldObject::bind_observer=&observe;float position[3]{},axis[9]{1,0,0,0,1,0,0,0,1};
        original_count=event_count=0;raise=false;
        assert(NativeHeldObject::bind_hook(reinterpret_cast<void*>(0x500000),3,position,axis,nullptr,nullptr,.01f)==1234);
        assert(original_count==1 && event_count==5 && events[0]==0 && events[1]==9 && events[2]==1 && events[3]==2 && events[4]==3);
        NativeHeldObject::rf_=0x500000;event_count=0;
        assert(NativeHeldObject::bind_hook(reinterpret_cast<void*>(0x500000),3,position,axis,nullptr,nullptr,.01f)==1234);
        assert(original_count==2 && event_count==6 && events[0]==0 && events[1]==9 && events[2]==1 && events[3]==4 && events[4]==2 && events[5]==3);
        NativeHeldObject::rf_=0;
        event_count=0;raise=true;assert(invoke_seh(position,axis));
        assert(original_count==3 && event_count==3 && events[0]==0 && events[1]==9 && events[2]==3 && NativeHeldObject::depth_==0);
        NativeHeldObject::bind_observer=nullptr;event_count=0;raise=false;
        assert(NativeHeldObject::bind_hook(reinterpret_cast<void*>(0x500000),3,position,axis,nullptr,nullptr,.01f)==1234);
        assert(original_count==4 && event_count==1 && events[0]==9);
        NativeHeldObject::bind_=saved_original;
    }
};
}

int main() {
    _set_error_mode(_OUT_TO_STDERR);
    try {
    const auto sandbox_base=std::filesystem::current_path()/L"build";
    const auto folder=sandbox_base/(L"d2vr-left-receipt-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directories(folder);d2vr::LeftWeaponRenderReceiptCheck::run(folder);d2vr::NativeHeldObjectCheck::run();
    const auto resolved_folder=std::filesystem::canonical(folder);
    const auto resolved_base=std::filesystem::canonical(sandbox_base);
    assert(resolved_folder.is_absolute() && resolved_folder.parent_path()==resolved_base);
    std::filesystem::remove_all(resolved_folder);
    std::puts("PASS bounded passive receipt state, identity, finite JSON, failed-drain admission, I/O errors, and original hook ordering/unwind");
    }catch(const std::exception& error){std::fprintf(stderr,"RECEIPT CHECK FAILED: %s\n",error.what());return 1;}
}
