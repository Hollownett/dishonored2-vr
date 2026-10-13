// Private, never activated Win32 desktop for owned fixtures or guarded fresh startup.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <iostream>
#include <fstream>

static std::wstring quote(const wchar_t* value) {
    std::wstring out=L"\"";unsigned slashes=0;
    for(const wchar_t* p=value;*p;++p) {
        if(*p==L'\\') {++slashes;continue;}
        out.append(*p==L'"'?2*slashes+1:slashes,L'\\');slashes=0;out+=*p;
    }
    out.append(2*slashes,L'\\');return out+L'"';
}
int wmain(int argc,wchar_t** argv) {
    const bool native=argc==4 && std::wstring(argv[1])==L"--native-fresh";
    if(!native && (argc<2 || std::wstring(argv[1]).find(L"playground-owned-")!=0 ||
       std::wstring(argv[1]).find_first_of(L"/\\:")!=std::wstring::npos)) {
        std::cerr<<"Only a local playground-owned-* fixture executable is permitted\n";return 2;
    }
    const auto name=L"D2VRPlayground-"+std::to_wstring(GetCurrentProcessId());
    HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,
        DESKTOP_CREATEWINDOW|DESKTOP_CREATEMENU|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS,nullptr);
    if(!desktop) {std::cerr<<"CreateDesktop failed: "<<GetLastError()<<"\n";return 3;}
    // No DESKTOP_SWITCHDESKTOP right, SwitchDesktop, SetForegroundWindow or input injection.
    HANDLE job=CreateJobObjectW(nullptr,nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if(native) {
        limits.BasicLimitInformation.LimitFlags|=JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit=1;
    }
    if(!job || !SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) {
        if(job)CloseHandle(job);CloseDesktop(desktop);return 4;
    }
    std::wstring command;
    const wchar_t* application=native?L"Dishonored2.exe":argv[1];
    if(native) {
        wchar_t cwd[32768]{};
        if(!GetCurrentDirectoryW(32768,cwd)) {CloseHandle(job);CloseDesktop(desktop);return 8;}
        std::ifstream marker("native-playground-owned.marker");std::string pin;std::getline(marker,pin);
        if(pin!="b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180") {
            CloseHandle(job);CloseDesktop(desktop);return 8;
        }
        const auto save=std::wstring(cwd)+L"\\fs-save";
        if(!CreateDirectoryW(save.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) {
            CloseHandle(job);CloseDesktop(desktop);return 8;
        }
        command=L"Dishonored2.exe +set net_usePlatformBackend 0 +set r_fullscreen 0 +set r_mode 0"
            L" +set r_windowWidth 640 +set r_windowHeight 480 +set fs_basepath "+quote(argv[2])+
            L" +set fs_savepath "+quote(save.c_str());
    } else for(int i=1;i<argc;++i) {if(i>1)command+=L' ';command+=quote(argv[i]);}
    STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.lpDesktop=const_cast<wchar_t*>(name.c_str());
    startup.dwFlags=STARTF_USESTDHANDLES;startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(application,command.data(),nullptr,nullptr,TRUE,
        CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) {
        std::cerr<<"CreateProcess failed: "<<GetLastError()<<"\n";
        CloseHandle(job);CloseDesktop(desktop);return 5;
    }
    if(!AssignProcessToJobObject(job,process.hProcess)) {
        TerminateProcess(process.hProcess,6);CloseHandle(process.hThread);CloseHandle(process.hProcess);
        CloseHandle(job);CloseDesktop(desktop);return 6;
    }
    if(native) {
        std::ofstream pid(argv[3]);pid<<process.dwProcessId<<"\n";pid.close();
        if(!pid) {CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);CloseDesktop(desktop);return 9;}
    }
    if(ResumeThread(process.hThread)==static_cast<DWORD>(-1)) {
        CloseHandle(process.hThread);CloseHandle(process.hProcess);CloseHandle(job);CloseDesktop(desktop);return 7;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess,INFINITE);
    DWORD result=1;GetExitCodeProcess(process.hProcess,&result);CloseHandle(process.hProcess);
    CloseHandle(job);CloseDesktop(desktop);return static_cast<int>(result);
}
