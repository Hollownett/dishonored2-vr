#pragma once
#include <cstring>
#include <windows.h>
#include <dbghelp.h>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstdint>
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
    static void write_dump(const CrashObserver& self,EXCEPTION_POINTERS* exception) noexcept {
        using Write=BOOL (WINAPI*)(HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION);
        const auto library=LoadLibraryW(L"dbghelp.dll");if(!library) return;
        const auto write=reinterpret_cast<Write>(reinterpret_cast<void*>(GetProcAddress(library,"MiniDumpWriteDump")));
        const auto file=CreateFileW(self.dump_path_.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(write && file!=INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(),exception,FALSE};
            write(GetCurrentProcess(),GetCurrentProcessId(),file,static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory|
                MiniDumpWithUnloadedModules|MiniDumpWithThreadInfo|MiniDumpScanMemory),&info,nullptr,nullptr);
        }
        if(file!=INVALID_HANDLE_VALUE) CloseHandle(file);
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
        if(record.ExceptionCode!=EXCEPTION_ACCESS_VIOLATION && record.ExceptionCode!=EXCEPTION_ILLEGAL_INSTRUCTION &&
           record.ExceptionCode!=EXCEPTION_IN_PAGE_ERROR) return EXCEPTION_CONTINUE_SEARCH;
        if(!self->native_faults_ && !crash_operation && (at<self->module_begin_ || at>=self->module_end_)) return EXCEPTION_CONTINUE_SEARCH;
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
            if(!owner && !self->dump_path_.empty() && !dumped_.exchange(true)) write_dump(*self,exception);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
public:
    void observe_native_faults(bool enabled) noexcept {native_faults_=enabled;}
    void install(const wchar_t* path,std::uintptr_t module_begin,std::size_t module_size) {
        if(handler_ || !path || !module_begin || !module_size || module_begin+module_size<module_begin)
            throw std::runtime_error("Invalid crash observer installation");
        file_=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file_==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open diagnostic exception record");
        module_begin_=module_begin;module_end_=module_begin+module_size;records_=0;native_faults_=false;
        dump_path_=path;if(const auto dot=dump_path_.rfind(L'.');dot!=std::wstring::npos) dump_path_.resize(dot);dump_path_+=L".dmp";
        CrashObserver* empty=nullptr;
        if(!current_.compare_exchange_strong(empty,this)) {CloseHandle(file_);file_=INVALID_HANDLE_VALUE;throw std::runtime_error("Crash observer already active");}
        handler_=AddVectoredExceptionHandler(1,&observe);
        if(!handler_) {current_=nullptr;CloseHandle(file_);file_=INVALID_HANDLE_VALUE;throw std::runtime_error("Cannot register diagnostic exception observer");}
    }
    bool remove() noexcept {
        if(handler_ && !RemoveVectoredExceptionHandler(handler_)) return false;
        handler_=nullptr;
        auto expected=this;current_.compare_exchange_strong(expected,nullptr,std::memory_order_acq_rel);
        const auto deadline=GetTickCount64()+1000;
        while(callback_depth_.load(std::memory_order_acquire) && GetTickCount64()<deadline) Sleep(1);
        if(callback_depth_.load(std::memory_order_acquire)) return false;
        if(file_!=INVALID_HANDLE_VALUE) {CloseHandle(file_);file_=INVALID_HANDLE_VALUE;}
        return true;
    }
    unsigned records() const noexcept {return records_.load(std::memory_order_acquire);}
    ~CrashObserver() {remove();}
};
} // namespace d2vr
