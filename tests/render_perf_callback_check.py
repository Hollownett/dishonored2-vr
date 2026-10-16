import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--gpu-header', type=Path)
parser.add_argument('--source', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = (args.source or root / 'src/dxgi_probe.cpp').read_text()
scope = source.split('static std::mutex render_perf_setup_mutex;', 1)[1].split('static bool render_perf_ensure(', 1)[0]
scope = 'static std::mutex render_perf_setup_mutex;'+scope
publication = source.split('static void render_perf_publish_scope(', 1)[1].split('void acknowledge_native_hud_art()', 1)[0]
publication = 'static void render_perf_publish_scope('+publication
gpu_header = (args.gpu_header or root / 'src/render_perf_gpu.hpp').resolve()
assert gpu_header.is_file()
compiler_setup = Path(r'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat')
environment_output = subprocess.run(f'call "{compiler_setup}" >nul && set', shell=True,
    creationflags=subprocess.CREATE_NO_WINDOW, timeout=30, check=True, capture_output=True, text=True).stdout
environment = os.environ.copy()
environment.update((name.upper(), value) for name, value in (line.split('=', 1) for line in environment_output.splitlines() if '=' in line and not line.startswith('=')))
compiler = shutil.which('cl.exe', path=environment['PATH'])
assert compiler
prefix = r'''
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <thread>
#include <cstdio>
namespace d2vr {struct TaaDispatchHook {using ExecuteFn=void (STDMETHODCALLTYPE*)(ID3D11DeviceContext*,ID3D11CommandList*,BOOL);};}
template<class InterfaceType> using ComPtr=Microsoft::WRL::ComPtr<InterfaceType>;
static std::unique_ptr<d2vr::RenderPerfGpu> render_perf_gpu;
static std::atomic<bool> render_perf_requested{true},render_perf_ready{};
static bool render_perf_prepare_failed{};
static ComPtr<ID3D11DeviceContext> render_perf_context;
static unsigned abort_calls{};
'''
test = r'''
static unsigned body_calls{},original_calls{},publication_calls{},expected_depth=1;
static unsigned frame_identity=1;
static int failure_mode{};
static bool body_result=true,new_frame=true;
static constexpr DWORD fixture_exception=0xe0424242;
static void check(bool accepted,const char* description) {if(!accepted)throw std::runtime_error(description);}
static void fault() {
    if(failure_mode==1)throw std::runtime_error("fixture C++ exception");
    if(failure_mode==2)RaiseException(fixture_exception,0,0,nullptr);
}
static void STDMETHODCALLTYPE original(ID3D11DeviceContext*,ID3D11CommandList*,BOOL) {++original_calls;}
static bool body(ID3D11DeviceContext* context,ID3D11CommandList* list,BOOL restore,d2vr::TaaDispatchHook::ExecuteFn native) {
    ++body_calls;
    if(render_perf_observing()) {
        check(render_perf_operation_depth==expected_depth,"callback depth matches admitted nesting");
        if(new_frame)++frame_identity;
        check(render_perf_gpu->begin_frame(context,frame_identity,9),"admitted body starts or continues frame");
    }
    native(context,list,restore);fault();return body_result;
}
static bool catch_execute_seh(ID3D11DeviceContext* context) {
    __try {render_perf_execute_scope(context,nullptr,FALSE,&original,&body);return false;}
    __except(GetExceptionCode()==fixture_exception?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
}
static void publish_native_hud_art(UINT64) {++publication_calls;fault();}
'''
tail = r'''
static bool catch_publish_seh(ID3D11DeviceContext* context) {
    __try {render_perf_publish_scope(context,true,1);return false;}
    __except(GetExceptionCode()==fixture_exception?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) {return true;}
}
static void assert_balanced(ID3D11DeviceContext* context) {
    check(render_perf_operation_depth==0 && render_perf_rejected_scope_depth==0,"normal and abnormal exit restore thread-local depths");
    bool admitted{};
    std::thread next_role([&] {admitted=render_perf_enter_operation(context,false);if(admitted)render_perf_leave_operation(context);});
    next_role.join();check(admitted,"a different callback role can enter after unwind");
}
static void assert_active_frame(ID3D11DeviceContext* context,bool expected,bool finish=false) {
    check(render_perf_enter_operation(context,false),"frame inspection admitted");
    std::uint64_t present{},epoch{};
    check(render_perf_gpu->active_frame(context,present,epoch)==expected,"successful returns retain the frame; exceptional exits abort it");
    if(expected)check(present==frame_identity && epoch==9,"callback frame identity preserved");
    if(finish)render_perf_gpu->end_frame(context);
    render_perf_leave_operation(context);
}
int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context,deferred;
        check(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)),"create software fixture context");
        check(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"create rejected context");
        render_perf_gpu=std::make_unique<d2vr::RenderPerfGpu>();
        check(render_perf_execute_scope(context.Get(),nullptr,FALSE,&original,&body),"normal native callback returns normally");
        check(abort_calls==0,"normal true return must not abort the profiler frame");assert_active_frame(context.Get(),true);
        body_result=false;new_frame=false;
        check(!render_perf_execute_scope(context.Get(),nullptr,FALSE,&original,&body),"normal false result preserved");
        check(abort_calls==0,"normal false return must not abort the profiler frame");assert_active_frame(context.Get(),true,true);
        body_result=true;new_frame=true;
        assert_balanced(context.Get());
        failure_mode=1;bool caught{};
        try {render_perf_execute_scope(context.Get(),nullptr,FALSE,&original,&body);}catch(const std::runtime_error&){caught=true;}
        check(caught && abort_calls==1,"C++ exception propagates and aborts exactly once");assert_active_frame(context.Get(),false);assert_balanced(context.Get());
        failure_mode=2;check(catch_execute_seh(context.Get()) && abort_calls==2,"SEH propagates and aborts exactly once");assert_active_frame(context.Get(),false);assert_balanced(context.Get());
        check(body_calls==4 && original_calls==4,"normal true/false/C++/SEH each invoke body and original once");
        failure_mode=0;check(render_perf_enter_operation(context.Get(),false),"outer owner admitted");expected_depth=2;
        check(render_perf_execute_scope(context.Get(),nullptr,FALSE,&original,&body),"nested callback returns normally");
        check(abort_calls==2,"nested normal return does not abort");assert_active_frame(context.Get(),true);
        check(render_perf_operation_depth==1,"inner finally preserves outer ownership");
        failure_mode=1;caught=false;
        try {render_perf_execute_scope(context.Get(),nullptr,FALSE,&original,&body);}catch(const std::runtime_error&){caught=true;}
        check(caught && render_perf_operation_depth==1 && abort_calls==3,"nested C++ unwind releases exactly inner admission and aborts once");
        failure_mode=2;check(catch_execute_seh(context.Get()) && render_perf_operation_depth==1 && abort_calls==4,"nested SEH releases exactly inner admission and aborts once");
        failure_mode=0;
        check(render_perf_execute_scope(deferred.Get(),nullptr,FALSE,&original,&body),"rejected context still executes native body");
        check(render_perf_operation_depth==1 && render_perf_observing(),"rejected inner scope does not release or suppress its outer owner afterward");
        render_perf_leave_operation(context.Get());assert_balanced(context.Get());expected_depth=1;
        check(body_calls==8 && original_calls==8 && abort_calls==4,"nested and rejected paths preserve exact original-once and abort behavior");
        failure_mode=1;caught=false;
        try {render_perf_publish_scope(context.Get(),true,1);}catch(const std::runtime_error&){caught=true;}
        check(caught,"HUD publication C++ exception propagates");assert_balanced(context.Get());
        failure_mode=2;check(catch_publish_seh(context.Get()),"HUD publication SEH propagates");assert_balanced(context.Get());
        failure_mode=0;render_perf_publish_scope(context.Get(),false,1);assert_balanced(context.Get());
        check(publication_calls==2,"only gameplay publication executes once per call");
        check(abort_calls==6,"only actual publication faults add one abort each");
        check(render_perf_enter_operation(context.Get(),false),"retirement admitted");render_perf_gpu->clear();render_perf_leave_operation(context.Get());
        check(render_perf_operation_depth==0,"clear preserves balanced release");
        std::puts("PASS: actual extracted callback scopes with abort-site counter under /EHsc: normal true/false preserve frame; C++/SEH abort once; nesting, rejected contexts, original once, HUD faults, clear");
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
'''
with tempfile.TemporaryDirectory(prefix='d2vr-profiler-callback-') as directory:
    folder = Path(directory)
    generated = folder / 'callback.cpp'
    abort_site='try {render_perf_gpu->abort_frame(context);} catch(...) {}'
    assert scope.count(abort_site)==1
    counted_scope=scope.replace(abort_site,'try {++abort_calls;render_perf_gpu->abort_frame(context);} catch(...) {}')
    generated.write_text('#define NOMINMAX\n#include "'+gpu_header.as_posix()+'"\n'+prefix+counted_scope+test+publication+tail)
    executable = folder / 'callback.exe'
    compiled = subprocess.run([compiler, '/nologo', '/std:c++17', '/EHsc', '/W4', '/WX', '/O2', '/MT',
        str(generated), '/Fo:'+str(folder / 'callback.obj'), '/Fe:'+str(executable), '/link', 'd3d11.lib'], env=environment)
    compiled.check_returncode()
    subprocess.run([str(executable)], check=True, env=environment)
print('Extracted source SHA256:', hashlib.sha256((scope+publication).encode()).hexdigest())
