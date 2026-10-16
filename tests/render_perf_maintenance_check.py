import argparse
import hashlib
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True, type=Path)
parser.add_argument('--gpu-header', required=True, type=Path)
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8')
names = ('render_perf_enter_operation', 'render_perf_abort_operation', 'render_perf_release_operation',
         'render_perf_leave_operation', 'render_perf_maintenance_operation',
         'render_perf_retire_locked', 'render_perf_abort', 'render_perf_retire')


def function(name, content=source):
    match = re.search(r'(?m)^[ \t]*(?:static\s+)?(?:bool|void)\s+' + re.escape(name) + r'\s*\(', content)
    if not match:
        raise ValueError('Missing actual function definition: ' + name)
    start = marker = match.start()
    opening = content.index('{', marker)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (content[cursor] == '{') - (content[cursor] == '}')
        cursor += 1
    return content[start:cursor] + '\n'


scopes = '\n'.join(function(name) for name in names)
header = args.gpu_header.read_text(encoding='utf-8')
header_methods = '\n'.join(function(name, header) for name in ('abort_frame', 'poll', 'clear', 'maintenance_fault_locked'))
prefix = r'''
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <array>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <cstdio>
static int fault_kind{},fault_phase{};
static constexpr DWORD fixture_exception=0xe0424242;
static void check(bool accepted,const char* description) {if(!accepted)throw std::runtime_error(description);}
static void fault(unsigned phase) {
    if(static_cast<unsigned>(fault_phase)!=phase)return;
    if(fault_kind==1)throw std::runtime_error("fixture C++ fault");
    if(fault_kind==2)RaiseException(fixture_exception,0,0,nullptr);
}
struct ID3D11DeviceContext {
    unsigned references=1;
    void AddRef() {++references;}
    void Release() {check(references>0,"reference balance");--references;}
};
struct ContextHolder {
    ID3D11DeviceContext* pointer{};
    ID3D11DeviceContext* Get() const {return pointer;}
    explicit operator bool() const {return pointer!=nullptr;}
    void Reset() {if(pointer)pointer->Release();pointer=nullptr;}
};
struct Profiler {
    std::mutex mutex;
    std::mutex mutex_;
    struct Frame {bool invalid{};};
    std::array<Frame,8> frames_{};
    unsigned rows_used_{};
    std::uint64_t failures_{},queued_rows_lost_{},dropped_{};
    unsigned depth{},aborts{},polls{},clears{};
    DWORD thread{};
    ID3D11DeviceContext* owner{};
    bool reject{};
    bool begin_operation(ID3D11DeviceContext* context) {
        std::lock_guard<std::mutex> guard(mutex);
        if(reject || !context || (depth && (thread!=GetCurrentThreadId() || owner!=context)))return false;
        thread=GetCurrentThreadId();owner=context;++depth;return true;
    }
    void end_operation(ID3D11DeviceContext* context) {
        std::lock_guard<std::mutex> guard(mutex);
        check(depth && thread==GetCurrentThreadId() && owner==context,"release only admitted lease");
        if(!--depth)owner=nullptr;
    }
    void abort_frame_locked(ID3D11DeviceContext*) {++aborts;fault(1);}
    void poll_locked(ID3D11DeviceContext*) {++polls;fault(2);}
    void clear_owned_locked() {++clears;fault(3);}
    HEADER_METHODS
};
static std::unique_ptr<Profiler> render_perf_gpu;
static std::atomic<bool> render_perf_ready{};
static std::mutex render_perf_setup_mutex;
static thread_local unsigned render_perf_operation_depth{};
static ContextHolder render_perf_context;
static bool render_perf_prepare(ID3D11DeviceContext*) noexcept {return false;}
'''
tail = r'''
static void seed(ID3D11DeviceContext& context) {
    fault_kind=fault_phase=0;context.references=1;render_perf_context.pointer=&context;
    render_perf_ready=true;render_perf_gpu=std::make_unique<Profiler>();render_perf_operation_depth=0;
}
static bool invoke_seh(bool retiring) {
    __try {
        if(retiring)render_perf_retire();else render_perf_abort();
        return false;
    } __except(GetExceptionCode()==fixture_exception?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
}
static void unlocked() {
    bool acquired{};
    std::thread worker([&] {acquired=render_perf_setup_mutex.try_lock();if(acquired)render_perf_setup_mutex.unlock();});
    worker.join();check(acquired,"setup mutex released on normal/C++/SEH paths");
}
static void balanced(ID3D11DeviceContext& context) {
    check(render_perf_operation_depth==0 && render_perf_gpu->depth==0,"both lease depths released");
    unlocked();fault_kind=fault_phase=0;
    bool admitted{};
    std::thread worker([&] {admitted=render_perf_enter_operation(&context,false);if(admitted)render_perf_leave_operation(&context);});
    worker.join();check(admitted,"new callback role enters after maintenance failure");
    bool unlocked_header{};
    std::thread header_worker([&] {unlocked_header=render_perf_gpu->mutex_.try_lock();if(unlocked_header)render_perf_gpu->mutex_.unlock();});
    header_worker.join();check(unlocked_header,"actual extracted header wrappers release their mutex");
}
int main() {
    try {
        ID3D11DeviceContext context;
        for(bool retiring:{false,true}) {
            const int last_phase=retiring?3:1;
            for(int phase=1;phase<=last_phase;++phase)for(int kind=1;kind<=2;++kind) {
                seed(context);fault_phase=phase;fault_kind=kind;
                render_perf_gpu->rows_used_=3;
                if(kind==2)check(invoke_seh(retiring),"native SEH propagates unchanged");
                else {if(retiring)render_perf_retire();else render_perf_abort();}
                check(render_perf_gpu->aborts==1,"fault cleanup does not retry abort");
                check(render_perf_context.Get()==&context && render_perf_ready,"failed retirement does not claim completed reset");
                check(context.references==1,"temporary context reference released after failure");
                check(render_perf_gpu->rows_used_==0 && render_perf_gpu->queued_rows_lost_==3 && render_perf_gpu->failures_==1,
                    "abnormal maintenance discards partial queued query rows and records loss");
                for(const auto& frame:render_perf_gpu->frames_)check(frame.invalid,"abnormal maintenance invalidates partial query frames");
                balanced(context);
                render_perf_retire();check(!render_perf_ready && !render_perf_context,"retry can complete retirement");
                check(context.references==0,"successful retirement releases owned context once");
                check(render_perf_gpu->depth==0 && render_perf_operation_depth==0,"successful clear balances lease");unlocked();
            }
        }
        for(bool retiring:{false,true})for(int kind=0;kind<=2;++kind) {
            seed(context);check(render_perf_enter_operation(&context,false),"outer operation enters");
            fault_kind=kind;fault_phase=1;
            if(kind==2)check(invoke_seh(retiring),"nested SEH propagates");
            else {if(retiring)render_perf_retire();else render_perf_abort();}
            check(render_perf_gpu->depth==1 && render_perf_operation_depth==1,"nested maintenance releases only its lease");
            if(retiring && kind==0)check(context.references==0,"nested successful retirement releases context");
            else check(context.references==1,"nested failed or abort operation retains publication");
            fault_kind=fault_phase=0;render_perf_leave_operation(&context);unlocked();
        }
        for(bool retiring:{false,true}) {
            seed(context);render_perf_gpu->reject=true;
            if(retiring)render_perf_retire();else render_perf_abort();
            check(!render_perf_gpu->aborts && !render_perf_gpu->polls && !render_perf_gpu->clears,"refusal invokes no maintenance");
            check(render_perf_ready && render_perf_context && context.references==1,"refusal never resets publication");
            check(render_perf_operation_depth==0 && render_perf_gpu->depth==0,"refusal never decrements an unacquired lease");unlocked();
            render_perf_gpu->reject=false;
            check(render_perf_enter_operation(&context,false),"foreign refusal existing owner enters");
            std::thread worker([&] {if(retiring)render_perf_retire();else render_perf_abort();});worker.join();
            check(render_perf_gpu->depth==1 && render_perf_operation_depth==1 && render_perf_ready && render_perf_context,
                "foreign refusal does not clear or release another callback");
            check(!render_perf_gpu->aborts && !render_perf_gpu->polls && !render_perf_gpu->clears,"foreign refusal keeps native/query maintenance untouched");
            render_perf_leave_operation(&context);unlocked();
        }
        seed(context);check(render_perf_enter_operation(&context,false),"abnormal cleanup owner enters");
        fault_kind=2;fault_phase=1;
        const auto abnormal=[](ID3D11DeviceContext* owned) {
            __try {render_perf_leave_operation(owned,true);return false;}
            __except(GetExceptionCode()==fixture_exception?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
        };
        check(abnormal(&context),"cleanup abort SEH propagates");balanced(context);
        std::puts("PASS: actual extracted caller + header mutex wrappers; CPU fault seams; normal/C++/SEH/nested/refused/foreign; partial query rows rejected");
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
'''
setup = Path(r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat')
environment_output = subprocess.run(f'call "{setup}" >nul && set', shell=True, creationflags=subprocess.CREATE_NO_WINDOW,
                                    timeout=30, check=True, capture_output=True, text=True).stdout
environment = os.environ.copy()
environment.update((name.upper(), value) for name, value in
                   (line.split('=', 1) for line in environment_output.splitlines() if '=' in line and not line.startswith('=')))
compiler = shutil.which('cl.exe', path=environment['PATH'])
if not compiler:
    raise RuntimeError('MSVC unavailable')
with tempfile.TemporaryDirectory(prefix='d2vr-profiler-maintenance-') as directory:
    folder = Path(directory)
    generated = folder / 'maintenance.cpp'
    generated.write_text(prefix.replace('HEADER_METHODS', header_methods) + scopes + tail, encoding='utf-8')
    executable = folder / 'maintenance.exe'
    compiled = subprocess.run([compiler, '/nologo', '/std:c++17', '/EHsc', '/W4', '/WX', '/O2', '/MT', str(generated),
                              '/Fo:' + str(folder / 'maintenance.obj'), '/Fe:' + str(executable)], env=environment,
                              creationflags=subprocess.CREATE_NO_WINDOW, capture_output=True, text=True, timeout=90)
    print(compiled.stdout + compiled.stderr, end='')
    compiled.check_returncode()
    executed = subprocess.run([str(executable)], env=environment, creationflags=subprocess.CREATE_NO_WINDOW,
                              capture_output=True, text=True, timeout=20)
    print(executed.stdout + executed.stderr, end='')
    executed.check_returncode()
print('Extracted maintenance source SHA256:', hashlib.sha256(scopes.encode()).hexdigest())
print('Extracted header wrapper SHA256:', hashlib.sha256(header_methods.encode()).hexdigest())
