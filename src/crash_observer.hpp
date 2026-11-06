#pragma once
#include <cstring>
#include <windows.h>
#include <dbghelp.h>
#include <atomic>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace d2vr {
// Tags do not catch an exception or change its disposition. The first-chance
// record identifies the active hook boundary; a handled fault is not a crash.
inline thread_local const char* crash_operation=nullptr;
class CrashOperation {
    const char* previous_;
public:
    explicit CrashOperation(const char* name):previous_(crash_operation) {crash_operation=name;}
    ~CrashOperation() {crash_operation=previous_;}
};
class CrashObserver {
    inline static std::atomic<CrashObserver*> current_{nullptr};
    inline static std::atomic<unsigned> callback_depth_{0};
    PVOID handler_{};
    HANDLE file_=INVALID_HANDLE_VALUE;
    std::atomic<unsigned> records_{0};
    std::atomic<bool> native_faults_{false};
    std::uintptr_t module_begin_{},module_end_{};
    std::wstring dump_path_;inline static std::atomic<bool> dumped_{false};
    // One minidump per process when code runs from an address no loaded module owns (always fatal: sessions stopping crashed
    // in amdxx64 calling unmapped code, v369/v397). Unloaded-module and referenced-memory streams show what that address was.
    static void write_dump(const CrashObserver& self,EXCEPTION_POINTERS* exception) noexcept {write_dump_to(self.dump_path_,exception);}
    static void write_dump_to(const std::wstring& path,EXCEPTION_POINTERS* exception) noexcept {
        using Write=BOOL (WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
        const auto library=LoadLibraryW(L"dbghelp.dll");if(!library) return;
        const auto write=reinterpret_cast<Write>(reinterpret_cast<void*>(GetProcAddress(library,"MiniDumpWriteDump")));
        const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(write && file!=INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),exception,FALSE};
            write(GetCurrentProcess(),GetCurrentProcessId(),file,static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory|
                MiniDumpWithUnloadedModules|MiniDumpWithThreadInfo|MiniDumpScanMemory),&info,nullptr,nullptr);
        }
        if(file!=INVALID_HANDLE_VALUE) CloseHandle(file);
    }
    // v1041: the LAST distinct faults, kept in a mapped file (the OS writes the dirty pages of a mapped file back when the process dies,
    // so a crash costs no I/O here): 16 fixed-width lines, one per distinct fault address, with count/tick/seq rewritten in place when
    // the same address faults again (an expected guarded read that faults every frame fills one line, not the whole record budget),
    // then a FATAL line (the unhandled-exception filter) and an EXIT line (DLL_PROCESS_DETACH while the process terminates).
    static constexpr unsigned ring_slots=16,ring_line=1024,ring_fatal=ring_slots,ring_exit=ring_slots+1;
    struct RingEntry {std::uint64_t rip{};unsigned count{},seq{};char body[ring_line]{};};
    std::array<RingEntry,ring_slots> ring_entries_{};
    char* ring_view_{};HANDLE ring_map_{};HANDLE ring_file_=INVALID_HANDLE_VALUE;
    std::atomic<unsigned> ring_seq_{0},ring_total_{0};std::atomic_flag ring_busy_=ATOMIC_FLAG_INIT;
    std::wstring fatal_dump_path_;
    inline static std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> previous_filter_{nullptr};
    void ring_put(unsigned slot,const char* text) noexcept {
        char* at=ring_view_+static_cast<std::size_t>(slot)*ring_line;
        const auto n=std::min<std::size_t>(std::strlen(text),ring_line-2);
        std::memset(at+n,' ',ring_line-2-n);std::memcpy(at,text,n);at[ring_line-2]='\r';at[ring_line-1]='\n';
    }
    void ring_render(unsigned slot) noexcept {
        char line[ring_line]{};const auto& e=ring_entries_[slot];
        std::snprintf(line,sizeof(line),"seq=%u count=%u tick=%llu %s",e.seq,e.count,static_cast<unsigned long long>(GetTickCount64()),e.body);
        ring_put(slot,line);
    }
    // True when this address is already in the ring (its line was updated, `count` is its new count) or the ring is busy/off.
    bool ring_bump(std::uint64_t rip,unsigned& count) noexcept {
        if(!ring_view_ || ring_busy_.test_and_set(std::memory_order_acquire)) return true; // busy: skip, never block a faulting thread
        ring_total_.fetch_add(1,std::memory_order_relaxed);
        bool found=false;
        for(unsigned i=0;i<ring_slots;++i) if(ring_entries_[i].count && ring_entries_[i].rip==rip) {
            count=++ring_entries_[i].count;ring_entries_[i].seq=++ring_seq_;ring_render(i);found=true;break;
        }
        ring_busy_.clear(std::memory_order_release);return found;
    }
    void ring_add(std::uint64_t rip,const char* body) noexcept {
        if(!ring_view_ || ring_busy_.test_and_set(std::memory_order_acquire)) return;
        unsigned slot=0; // an empty slot, else the least recently faulted one
        for(unsigned i=0;i<ring_slots;++i) {
            if(!ring_entries_[i].count) {slot=i;break;}
            if(ring_entries_[i].seq<ring_entries_[slot].seq) slot=i;
        }
        auto& e=ring_entries_[slot];e.rip=rip;e.count=1;e.seq=++ring_seq_;
        std::snprintf(e.body,sizeof(e.body),"%s",body);ring_render(slot);
        ring_busy_.clear(std::memory_order_release);
    }
    static const char* module_name(const void* address,HMODULE& owner,char* path,DWORD size) noexcept {
        owner=nullptr;path[0]=0;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,static_cast<LPCSTR>(address),&owner);
        if(owner) GetModuleFileNameA(owner,path,size);
        const char* name=path;for(const char* p=path;*p;++p) if(*p=='\\' || *p=='/') name=p+1;
        return *name?name:"unmapped";
    }
    unsigned repeat_of(std::uintptr_t rip) const noexcept {
        for(const auto& e:ring_entries_) if(e.count && e.rip==rip) return e.count;
        return 0;
    }
    // One ring line for a fault: code, where, the registers that matter and the top call frames (module+rva).
    void ring_record(EXCEPTION_POINTERS* exception,bool fatal=false) noexcept {
        const auto& record=*exception->ExceptionRecord;const auto& c=*exception->ContextRecord;
        const auto at=reinterpret_cast<std::uintptr_t>(record.ExceptionAddress);unsigned count=0;
        if(!fatal && ring_bump(at,count)) return;
        char path[MAX_PATH]{};HMODULE owner{};const char* name=module_name(record.ExceptionAddress,owner,path,MAX_PATH);
        char frames[480]{};std::size_t used=0;void* stack[10]{};
        const auto depth=RtlCaptureStackBackTrace(0,10,stack,nullptr);
        for(USHORT i=0;i<depth && used+48<sizeof(frames);++i) {
            HMODULE m{};char fp[MAX_PATH]{};const char* fn=module_name(stack[i],m,fp,MAX_PATH);
            const auto n=std::snprintf(frames+used,sizeof(frames)-used,"%s+0x%llx ",fn,static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(stack[i])-reinterpret_cast<std::uintptr_t>(m)));
            if(n<=0) break;
            used+=static_cast<std::size_t>(n);
        }
        char body[ring_line]{};
        std::snprintf(body,sizeof(body),"%s code=0x%08lx thread=%lu op=%s module=%s rva=0x%llx rip=0x%llx rsp=0x%llx access=%llu@0x%llx frames=%s",
            fatal?"UNHANDLED":"first-chance",record.ExceptionCode,GetCurrentThreadId(),crash_operation?crash_operation:"-",name,
            static_cast<unsigned long long>(owner?at-reinterpret_cast<std::uintptr_t>(owner):at),static_cast<unsigned long long>(at),static_cast<unsigned long long>(c.Rsp),
            static_cast<unsigned long long>(record.NumberParameters>0?record.ExceptionInformation[0]:0),static_cast<unsigned long long>(record.NumberParameters>1?record.ExceptionInformation[1]:0),frames);
        if(fatal) {
            if(!ring_view_) return;
            char line[ring_line]{};std::snprintf(line,sizeof(line),"tick=%llu %s",static_cast<unsigned long long>(GetTickCount64()),body);ring_put(ring_fatal,line);
        } else ring_add(at,body);
    }
    // The process's last chance: nothing else will run. Own line, own minidump, then the filter that was there before us (the game's).
    static LONG WINAPI unhandled(EXCEPTION_POINTERS* exception) noexcept {
        const auto self=current_.load(std::memory_order_acquire);
        if(self && exception && exception->ExceptionRecord && exception->ContextRecord) {
            self->ring_record(exception,true);
            if(!self->fatal_dump_path_.empty()) write_dump_to(self->fatal_dump_path_,exception);
        }
        const auto previous=previous_filter_.load();
        return previous?previous(exception):EXCEPTION_CONTINUE_SEARCH;
    }
    void ring_close() noexcept {
        if(ring_view_) {UnmapViewOfFile(ring_view_);ring_view_=nullptr;}
        if(ring_map_) {CloseHandle(ring_map_);ring_map_=nullptr;}
        if(ring_file_!=INVALID_HANDLE_VALUE) {CloseHandle(ring_file_);ring_file_=INVALID_HANDLE_VALUE;}
    }
    void ring_open(const std::wstring& path) noexcept {
        constexpr DWORD size=(ring_exit+1)*ring_line;
        ring_file_=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(ring_file_==INVALID_HANDLE_VALUE) return;
        ring_map_=CreateFileMappingW(ring_file_,nullptr,PAGE_READWRITE,0,size,nullptr);
        ring_view_=ring_map_?static_cast<char*>(MapViewOfFile(ring_map_,FILE_MAP_WRITE,0,0,size)):nullptr;
        if(!ring_view_) {ring_close();return;}
        for(auto& e:ring_entries_) e=RingEntry{};
        ring_seq_=0;ring_total_=0;
        for(unsigned i=0;i<ring_slots;++i) ring_put(i,"(empty)");
        ring_put(ring_fatal,"FATAL none (no unhandled exception reached the filter)");ring_put(ring_exit,"EXIT none (the DLL has not seen process detach)");
    }
    static LONG CALLBACK observe(EXCEPTION_POINTERS* exception) noexcept {
        struct Depth {
            Depth() {callback_depth_.fetch_add(1,std::memory_order_acq_rel);}
            ~Depth() {callback_depth_.fetch_sub(1,std::memory_order_acq_rel);}
        } depth;
        auto self=current_.load(std::memory_order_acquire);
        if(!self || !exception || !exception->ExceptionRecord || !exception->ContextRecord) return EXCEPTION_CONTINUE_SEARCH;
        const auto& record=*exception->ExceptionRecord;
        const auto at=reinterpret_cast<std::uintptr_t>(record.ExceptionAddress);
        const bool classic=record.ExceptionCode==EXCEPTION_ACCESS_VIOLATION || record.ExceptionCode==EXCEPTION_ILLEGAL_INSTRUCTION ||
            record.ExceptionCode==EXCEPTION_IN_PAGE_ERROR;
        // Also the faults that end a process without a C++ trace: stack overflow, heap corruption, fast fail, divide, privileged.
        if(!classic && record.ExceptionCode!=0xC00000FDu && record.ExceptionCode!=0xC0000374u && record.ExceptionCode!=0xC0000409u &&
           record.ExceptionCode!=EXCEPTION_INT_DIVIDE_BY_ZERO && record.ExceptionCode!=EXCEPTION_PRIV_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
        // Every such fault, anywhere in the process (a driver, the game; tagged or not), lands in the ring; a hot repeat stays one line.
        self->ring_record(exception);
        if(!classic) return EXCEPTION_CONTINUE_SEARCH;
        if(!self->native_faults_ && !crash_operation && (at<self->module_begin_ || at>=self->module_end_)) return EXCEPTION_CONTINUE_SEARCH;
        if(self->repeat_of(at)>4) return EXCEPTION_CONTINUE_SEARCH; // the same address again and again: counted in the ring, not in the 64-record file budget
        const auto number=self->records_.fetch_add(1,std::memory_order_acq_rel);
        if(number<(self->native_faults_?64u:8u) && self->file_!=INVALID_HANDLE_VALUE) {
            HMODULE owner{};char path[512]{};
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(record.ExceptionAddress),&owner);
            if(owner) GetModuleFileNameA(owner,path,static_cast<DWORD>(sizeof(path)));
            const char* name=path;
            for(const char* p=path;*p;++p) if(*p=='\\' || *p=='/') name=p+1;
            const auto& c=*exception->ContextRecord;
            char line[2048]{};
            const auto length=std::snprintf(line,sizeof(line),
                "first_chance=1 record=%u pid=%lu thread=%lu operation=%s code=0x%08lx "
                "module=%s module_base=0x%llx rva=0x%llx rip=0x%llx rsp=0x%llx "
                "rcx=0x%llx rdx=0x%llx r8=0x%llx r9=0x%llx access_kind=%llu access_address=0x%llx\r\n",
                number,GetCurrentProcessId(),GetCurrentThreadId(),crash_operation?crash_operation:(at>=self->module_begin_ && at<self->module_end_?"diagnostic module":"native/external thread"),
                record.ExceptionCode,*name?name:"unmapped",static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(owner)),
                static_cast<unsigned long long>(owner?at-reinterpret_cast<std::uintptr_t>(owner):at),
                c.Rip,c.Rsp,c.Rcx,c.Rdx,c.R8,c.R9,
                static_cast<unsigned long long>(record.NumberParameters>0?record.ExceptionInformation[0]:0),
                static_cast<unsigned long long>(record.NumberParameters>1?record.ExceptionInformation[1]:0));
            if(length>0 && static_cast<std::size_t>(length)<sizeof(line)) {
                DWORD written{};WriteFile(self->file_,line,static_cast<DWORD>(length),&written,nullptr);
                // ReadProcessMemory fails cleanly on an unreadable stack.
                std::array<ULONG64,48> stack{};SIZE_T got{};
                if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(c.Rsp),stack.data(),sizeof(stack),&got) && got==sizeof(stack)) {
                    for(unsigned i=0;i<stack.size();++i) {
                        // Name code addresses so an external-runtime fault shows which caller led there.
                        HMODULE slot_module{};char slot_name[MAX_PATH]{};
                        if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCSTR>(stack[i]),&slot_module) && slot_module) {
                            GetModuleFileNameA(slot_module,slot_name,MAX_PATH);
                            const char* base_name=std::strrchr(slot_name,'\\');base_name=base_name?base_name+1:slot_name;
                            const auto m=std::snprintf(line,sizeof(line),"record=%u stack_slot=%u value=0x%llx module=%s rva=0x%llx\r\n",number,i,stack[i],base_name,
                                static_cast<unsigned long long>(stack[i]-reinterpret_cast<std::uintptr_t>(slot_module)));
                            if(m>0 && static_cast<std::size_t>(m)<sizeof(line)) WriteFile(self->file_,line,static_cast<DWORD>(m),&written,nullptr);
                            continue;
                        }
                        const auto n=std::snprintf(line,sizeof(line),"record=%u stack_slot=%u value=0x%llx\r\n",number,i,stack[i]);
                        if(n>0 && static_cast<std::size_t>(n)<sizeof(line)) WriteFile(self->file_,line,static_cast<DWORD>(n),&written,nullptr);
                    }
                }
                {   // Real call chain through the exception dispatch (callers of the faulting code).
                    void* frames[62]{};const auto n=RtlCaptureStackBackTrace(0,62,frames,nullptr);
                    for(USHORT i=0;i<n;++i) {
                        HMODULE m{};char frame_path[MAX_PATH]{};const char* base_name="?";
                        if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(frames[i]),&m) && m) {GetModuleFileNameA(m,frame_path,MAX_PATH);const char* b=std::strrchr(frame_path,'\\');base_name=b?b+1:frame_path;}
                        const auto k=std::snprintf(line,sizeof(line),"record=%u frame=%u module=%s rva=0x%llx\r\n",number,static_cast<unsigned>(i),base_name,
                            static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(frames[i])-reinterpret_cast<std::uintptr_t>(m)));
                        if(k>0 && static_cast<std::size_t>(k)<sizeof(line)) WriteFile(self->file_,line,static_cast<DWORD>(k),&written,nullptr);
                    }
                }
                FlushFileBuffers(self->file_);
            }
            // Also the first access violation inside the game's own code on a thread the mod is not running in: both headset
            // crashes of 2026-10-13 (audio update 0xc3e00, heap free 0x156880) died on a pointer holding float data, and no dump
            // was written because the game module owned the address.
            const bool game_av=record.ExceptionCode==0xc0000005 && owner==GetModuleHandleW(nullptr) && !crash_operation;
            if((!owner || game_av) && !self->dump_path_.empty() && !dumped_.exchange(true)) write_dump(*self,exception);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
public:
    void observe_native_faults(bool enabled) noexcept {native_faults_=enabled;}
    // v1046: an EMPTY path = diagnostics off. The observer has no duty beyond records (its handler only logs and always continues the search;
    // the unhandled filter only logs, dumps and chains to the game's), so off installs nothing: no handler, no mapped ring, no dump.
    void install(const std::filesystem::path& file_path,std::uintptr_t module_begin,std::size_t module_size) {
        if(file_path.empty()) return;
        const wchar_t* path=file_path.c_str();
        if(handler_ || !module_begin || !module_size || module_begin+module_size<module_begin)
            throw std::runtime_error("Invalid crash observer installation");
        file_=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file_==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open diagnostic exception record");
        module_begin_=module_begin;module_end_=module_begin+module_size;records_=0;native_faults_=false;
        dump_path_=path;if(const auto dot=dump_path_.rfind(L'.');dot!=std::wstring::npos) dump_path_.resize(dot);dump_path_+=L".dmp";
        CrashObserver* empty=nullptr;
        if(!current_.compare_exchange_strong(empty,this)) {CloseHandle(file_);file_=INVALID_HANDLE_VALUE;throw std::runtime_error("Crash observer already active");}
        handler_=AddVectoredExceptionHandler(1,&observe);
        if(!handler_) {current_=nullptr;CloseHandle(file_);file_=INVALID_HANDLE_VALUE;throw std::runtime_error("Cannot register diagnostic exception observer");}
        std::wstring base=dump_path_;if(base.size()>4) base.resize(base.size()-4); // "<name>.dmp" -> "<name>"
        fatal_dump_path_=base+L"-fatal.dmp";ring_open(base+L"-last.txt");
        previous_filter_=SetUnhandledExceptionFilter(&unhandled);
    }
    // The game may install its own unhandled-exception filter after ours: call this about once a second to take the top slot again
    // (the filter found there is kept and called after ours).
    void reassert() noexcept {
        if(!handler_) return;
        const auto previous=SetUnhandledExceptionFilter(&unhandled);
        if(previous && previous!=&unhandled) previous_filter_=previous;
    }
    // DLL_PROCESS_DETACH: `terminating` is the loader's lpReserved!=null (ExitProcess), i.e. the process is ending, not a FreeLibrary.
    void note_exit(bool terminating) noexcept {
        if(!ring_view_) return;
        char line[ring_line]{};
        std::snprintf(line,sizeof(line),"EXIT tick=%llu process_terminating=%d file_records=%u ring_records=%u",static_cast<unsigned long long>(GetTickCount64()),terminating?1:0,records(),ring_total_.load());
        ring_put(ring_exit,line);
    }
    unsigned ring_records() const noexcept {return ring_total_.load(std::memory_order_relaxed);}
    bool remove() noexcept {
        if(handler_ && !RemoveVectoredExceptionHandler(handler_)) return false;
        const bool was_installed=handler_!=nullptr;
        handler_=nullptr;
        if(was_installed) {const auto current=SetUnhandledExceptionFilter(previous_filter_.load());if(current!=&unhandled) SetUnhandledExceptionFilter(current);} // another owner's newer filter stays
        auto expected=this;current_.compare_exchange_strong(expected,nullptr,std::memory_order_acq_rel);
        const auto deadline=GetTickCount64()+1000;
        while(callback_depth_.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(1);
        if(callback_depth_.load(std::memory_order_acquire)) return false;
        ring_close();
        if(file_!=INVALID_HANDLE_VALUE) {CloseHandle(file_);file_=INVALID_HANDLE_VALUE;}
        return true;
    }
    unsigned records() const noexcept {return records_.load(std::memory_order_acquire);}
    ~CrashObserver() {remove();}
};
} // namespace d2vr
