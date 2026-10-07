#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <bcrypt.h>
#include <wrl/client.h>
#include <MinHook.h>
#include "game_camera.hpp"
#include "game_stereo.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace fs=std::filesystem;
using PresentFn=HRESULT (WINAPI*)(IDXGISwapChain*,UINT,UINT);
static HMODULE module{};
static PresentFn original_present{};
static void* hook_target{};
static std::array<std::byte,16> disabled_target_bytes{};
static std::atomic<bool> started{false},capturing{false};
static std::atomic<bool> callback_failed{false};
static std::atomic<UINT64> frames{0},valid_poses{0};
static std::atomic<UINT> width{0},height{0},format{0},feature{0};
static std::uintptr_t camera_address{},image_base{};
static std::mutex log_mutex;
static std::ofstream csv;
static fs::path output_directory;
static bool stereo_requested=false;
static bool single_eye_requested=false,left_captured=false,right_phase=false;
alignas(8) static std::array<std::byte,80> left_metadata{},right_metadata{};
static constexpr char metadata_end[]="D2VR diagnostic end";
static void* diagnostic_mode_pointer{};
static std::atomic<bool> stereo_active{false};
static bool mono_captured=false,stereo_captured=false;
static bool mode_restored=true,values_restored=true;
static std::atomic<UINT64> stereo_frames{0},distinct_view_frames{0};
static std::atomic<float> measured_separation{0};
static void* saved_mode{};
static LONG saved_separation{},saved_screen{};
static constexpr float diagnostic_half_separation=0.032f; // Game units; metre scale not calibrated.
static LONG diagnostic_separation{};
static UINT64 stereo_start_frame{};
static ULONGLONG stereo_start_tick{};
static std::optional<d2vr::GameCameraPose> baseline_pose;
static bool camera_moved=false;
static bool stereo_ever_enabled=false;

bool read_self(std::uintptr_t address,void* data,std::size_t size) {
    SIZE_T count{};
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),data,size,&count) && count==size;
}

struct NativeView {
    std::array<int,4> viewport{};
    std::optional<d2vr::GameCameraPose> pose;
};

std::vector<NativeView> native_views() {
    std::array<std::byte,16> header{};
    if(!read_self(image_base+d2vr::prepared_views_rva,header.data(),header.size())) return {};
    std::uintptr_t address{};std::uint32_t packed{};
    std::memcpy(&address,header.data(),8);std::memcpy(&packed,header.data()+8,4);
    auto count=packed&0xffffff;
    if(!address || address%8 || count<1 || count>2) return {};
    std::vector<NativeView> result;
    for(std::uint32_t eye=0;eye<count;++eye) {
        std::array<std::byte,d2vr::camera_read_size> bytes{};
        if(!read_self(address+eye*d2vr::prepared_view_stride,bytes.data(),bytes.size())) return {};
        NativeView view;
        std::memcpy(view.viewport.data(),bytes.data(),16);
        view.pose=d2vr::decode_view_fields(bytes);
        if(!view.pose) return {};
        result.push_back(view);
    }
    return result;
}

void prepare_stereo() {
    std::array<std::byte,80> metadata{};
    if(!read_self(image_base+d2vr::side_by_side_mode_rva,metadata.data(),metadata.size()) ||
       !d2vr::valid_stereo_metadata(metadata,image_base)) throw std::runtime_error("Stereo metadata differs from supported profile");
    diagnostic_mode_pointer=reinterpret_cast<void*>(image_base+d2vr::side_by_side_mode_rva);
    if(single_eye_requested) {
        // Clone one validated eye descriptor with a full viewport. The next
        // record's nonempty label terminates the engine's descriptor loop.
        left_metadata={};std::memcpy(left_metadata.data(),metadata.data(),40);
        const std::array<float,4> viewport{0,0,1,1};
        std::memcpy(left_metadata.data()+12,viewport.data(),16);
        auto end=reinterpret_cast<std::uintptr_t>(metadata_end);
        std::memcpy(left_metadata.data()+40,&end,8);
        right_metadata=left_metadata;
        const float side=1;std::memcpy(right_metadata.data()+32,&side,4);
        diagnostic_mode_pointer=left_metadata.data();
    }
    if(!read_self(image_base+d2vr::selected_mode_rva,&saved_mode,8) ||
       saved_mode!=reinterpret_cast<void*>(image_base+d2vr::single_mode_rva))
        throw std::runtime_error("Native renderer is not in single-camera mode");
    if(native_views().size()!=1) throw std::runtime_error("Enter a gameplay scene with one active camera first");
    if(!read_self(image_base+d2vr::separation_value_rva,&saved_separation,4) ||
       !read_self(image_base+d2vr::screen_separation_value_rva,&saved_screen,4))
        throw std::runtime_error("Cannot read stereo configuration values");
    std::memcpy(&diagnostic_separation,&diagnostic_half_separation,4);
}

void enable_stereo() {
    auto separation=reinterpret_cast<volatile LONG*>(image_base+d2vr::separation_value_rva);
    auto screen=reinterpret_cast<volatile LONG*>(image_base+d2vr::screen_separation_value_rva);
    auto mode=reinterpret_cast<void* volatile*>(image_base+d2vr::selected_mode_rva);
    if(InterlockedCompareExchange(separation,diagnostic_separation,saved_separation)!=saved_separation)
        throw std::runtime_error("Stereo separation changed before test");
    if(InterlockedCompareExchange(screen,0,saved_screen)!=saved_screen) {
        InterlockedCompareExchange(separation,saved_separation,diagnostic_separation);
        throw std::runtime_error("Stereo projection setting changed before test");
    }
    if(InterlockedCompareExchangePointer(mode,diagnostic_mode_pointer,saved_mode)!=saved_mode) {
        InterlockedCompareExchange(separation,saved_separation,diagnostic_separation);
        InterlockedCompareExchange(screen,saved_screen,0);
        throw std::runtime_error("Native mode changed before test");
    }
    stereo_active=true;
    stereo_ever_enabled=true;
}

void restore_stereo() noexcept {
    if(!stereo_active) return;
    auto mode=reinterpret_cast<void* volatile*>(image_base+d2vr::selected_mode_rva);
    mode_restored=InterlockedCompareExchangePointer(mode,saved_mode,
        diagnostic_mode_pointer)==diagnostic_mode_pointer;
    auto separation=reinterpret_cast<volatile LONG*>(image_base+d2vr::separation_value_rva);
    auto screen=reinterpret_cast<volatile LONG*>(image_base+d2vr::screen_separation_value_rva);
    values_restored=InterlockedCompareExchange(separation,saved_separation,diagnostic_separation)==diagnostic_separation;
    values_restored=(InterlockedCompareExchange(screen,saved_screen,0)==0) && values_restored;
    stereo_active=false;
}

bool capture_frame(IDXGISwapChain* chain,const wchar_t* filename) {
    ComPtr<ID3D11Texture2D> buffer,staging;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    if(FAILED(chain->GetBuffer(0,IID_PPV_ARGS(&buffer))) || FAILED(chain->GetDevice(IID_PPV_ARGS(&device)))) return false;
    D3D11_TEXTURE2D_DESC desc{};buffer->GetDesc(&desc);
    if(desc.Width>4096 || desc.Height>4096 || desc.SampleDesc.Count!=1 ||
       (desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM && desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) return false;
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    if(FAILED(device->CreateTexture2D(&desc,nullptr,&staging))) return false;
    device->GetImmediateContext(&context);context->CopyResource(staging.Get(),buffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if(FAILED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) return false;
    bool success=false;
    try {
        std::ofstream output(output_directory/filename,std::ios::binary|std::ios::trunc);
        for(UINT row=0;row<desc.Height;++row)
            output.write(static_cast<const char*>(mapped.pData)+row*mapped.RowPitch,static_cast<std::streamsize>(desc.Width)*4);
        success=static_cast<bool>(output);
    } catch(...) {context->Unmap(staging.Get(),0);throw;}
    context->Unmap(staging.Get(),0);
    return success;
}

void log_pose(UINT64 number,UINT eye,const DXGI_SWAP_CHAIN_DESC& desc,const d2vr::GameCameraPose& pose) {
    csv << number << ',' << GetTickCount64() << ',' << eye << ',' << desc.BufferDesc.Width << ','
        << desc.BufferDesc.Height << ',' << desc.BufferDesc.Format << ',' << pose.horizontal_fov << ',' << pose.vertical_fov;
    for(float value:pose.position) csv << ',' << value;
    for(float value:pose.basis) csv << ',' << value;
    csv << '\n';
}

void log_view_snapshot(const wchar_t* filename,const std::vector<NativeView>& views) {
    std::ofstream output(output_directory/filename,std::ios::trunc);
    std::array<std::byte,16> header{};
    if(!read_self(image_base+d2vr::prepared_views_rva,header.data(),header.size())) throw std::runtime_error("Cannot snapshot view header");
    std::uintptr_t address{};std::memcpy(&address,header.data(),8);
    output << std::setprecision(9) << "{\"views\":[";
    for(std::size_t eye=0;eye<views.size();++eye) {
        std::array<std::byte,0x1a0> bytes{};
        if(!read_self(address+eye*d2vr::prepared_view_stride,bytes.data(),bytes.size())) throw std::runtime_error("Cannot snapshot prepared view");
        if(eye) output << ',';
        output << "{\"viewport\":[";
        for(int i=0;i<4;++i) {if(i) output << ',';output << views[eye].viewport[i];}
        output << "],\"position\":[";
        for(int i=0;i<3;++i) {if(i) output << ',';output << views[eye].pose->position[i];}
        output << "],\"projection\":[";
        for(int i=0;i<16;++i) {float value{};std::memcpy(&value,bytes.data()+0x130+i*4,4);if(i) output << ',';output << value;}
        output << "],\"projection_enabled\":" << (bytes[0x170]!=std::byte{}?"true":"false") << '}';
    }
    output << "]}\n";
}

void sample_stereo(IDXGISwapChain* chain,UINT64 number,const DXGI_SWAP_CHAIN_DESC& desc) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if(!capturing.load()) return;
    auto views=native_views();
    if(single_eye_requested) {
        if(views.size()!=1) return;
        ++valid_poses;
        if(number%6==0) log_pose(number,right_phase?1:0,desc,*views[0].pose);
        if(!stereo_active && number>=10) {
            mono_captured=capture_frame(chain,L"dxgi-mono.rgba");
            log_view_snapshot(L"dxgi-mono-views.json",views);
            enable_stereo();stereo_start_tick=GetTickCount64();
        } else if(stereo_active && GetTickCount64()-stereo_start_tick>=2000) {
            ++stereo_frames;
            if(!right_phase) {
                left_captured=capture_frame(chain,L"dxgi-left.rgba");
                log_view_snapshot(L"dxgi-left-views.json",views);
                auto mode=reinterpret_cast<void* volatile*>(image_base+d2vr::selected_mode_rva);
                if(InterlockedCompareExchangePointer(mode,right_metadata.data(),diagnostic_mode_pointer)!=diagnostic_mode_pointer)
                    throw std::runtime_error("One-eye mode changed before right projection test");
                diagnostic_mode_pointer=right_metadata.data();right_phase=true;stereo_start_tick=GetTickCount64();
            } else if(!stereo_captured) {
                stereo_captured=capture_frame(chain,L"dxgi-stereo.rgba");
                log_view_snapshot(L"dxgi-right-views.json",views);
            }
        }
        return;
    }
    if(!stereo_active && views.size()==1) {
        if(!baseline_pose) baseline_pose=views[0].pose;
        float movement{};
        for(int axis=0;axis<3;++axis) movement+=std::abs(views[0].pose->position[axis]-baseline_pose->position[axis]);
        for(int axis=0;axis<9;++axis) movement+=std::abs(views[0].pose->basis[axis]-baseline_pose->basis[axis]);
        camera_moved=camera_moved || movement>0.01f;
    }
    if(stereo_active && views.size()==2) {
        ++stereo_frames;
        float distance{};
        for(int axis=0;axis<3;++axis) {
            float difference=views[0].pose->position[axis]-views[1].pose->position[axis];
            distance+=difference*difference;
        }
        distance=std::sqrt(distance);measured_separation=distance;
        if(distance>0.001f && distance<0.2f) {
            ++distinct_view_frames;
            if(!stereo_captured && number>stereo_start_frame+10 && GetTickCount64()-stereo_start_tick>=2000) {
                stereo_captured=capture_frame(chain,L"dxgi-stereo.rgba");
                log_view_snapshot(L"dxgi-stereo-views.json",views);
            }
        }
    }
    for(UINT eye=0;eye<views.size();++eye) {
        ++valid_poses;
        if(number%6==0) log_pose(number,eye,desc,*views[eye].pose);
    }
    if(!stereo_active && number>=10 && views.size()==1 && camera_moved) {
        mono_captured=capture_frame(chain,L"dxgi-mono.rgba");
        log_view_snapshot(L"dxgi-mono-views.json",views);
        enable_stereo();stereo_start_frame=number;stereo_start_tick=GetTickCount64();
    }
}

bool focused_movement() {
    DWORD foreground_pid{};
    GetWindowThreadProcessId(GetForegroundWindow(),&foreground_pid);
    if(foreground_pid!=GetCurrentProcessId()) return false;
    for(int key:{0x57,0x41,0x53,0x44,0x25,0x26,0x27,0x28})
        if(GetAsyncKeyState(key)&0x8000) return true;
    return false;
}

std::wstring module_path(HMODULE handle) {
    std::vector<wchar_t> path(32768);
    DWORD size=GetModuleFileNameW(handle,path.data(),static_cast<DWORD>(path.size()));
    if(!size || size>=path.size()) throw std::runtime_error("Cannot read module path");
    return std::wstring(path.data(),size);
}

std::string sha256(const fs::path& path) {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    auto cleanup=[&]() { if(hash) BCryptDestroyHash(hash); if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0); };
    try {
        if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
            throw std::runtime_error("Cannot open SHA-256 provider");
        DWORD size{},received{};
        if(BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&size),sizeof(size),&received,0)<0)
            throw std::runtime_error("Cannot size SHA-256 state");
        std::vector<UCHAR> object(size),buffer(256*1024);
        if(BCryptCreateHash(algorithm,&hash,object.data(),size,nullptr,0,0)<0)
            throw std::runtime_error("Cannot create SHA-256 state");
        std::ifstream input(path,std::ios::binary);
        if(!input) throw std::runtime_error("Cannot read game executable");
        while(input) {
            input.read(reinterpret_cast<char*>(buffer.data()),static_cast<std::streamsize>(buffer.size()));
            auto count=input.gcount();
            if(count && BCryptHashData(hash,buffer.data(),static_cast<ULONG>(count),0)<0)
                throw std::runtime_error("Cannot hash game executable");
        }
        if(!input.eof()) throw std::runtime_error("Executable read failed");
        UCHAR digest[32]{};
        if(BCryptFinishHash(hash,digest,sizeof(digest),0)<0) throw std::runtime_error("Cannot finish SHA-256");
        static constexpr char hex[]="0123456789abcdef";
        std::string result;
        for(auto value:digest) {result+=hex[value>>4]; result+=hex[value&15];}
        cleanup(); return result;
    } catch(...) {cleanup(); throw;}
}

HRESULT WINAPI present_hook(IDXGISwapChain* chain,UINT interval,UINT flags) {
    try {
    if(capturing.load() && !(flags&DXGI_PRESENT_TEST)) {
        DXGI_SWAP_CHAIN_DESC desc{};
        if(SUCCEEDED(chain->GetDesc(&desc)) && desc.BufferDesc.Width>64 && desc.BufferDesc.Height>64) {
            auto number=++frames;
            width=desc.BufferDesc.Width; height=desc.BufferDesc.Height; format=desc.BufferDesc.Format;
            ComPtr<ID3D11Device> device;
            if(SUCCEEDED(chain->GetDevice(IID_PPV_ARGS(&device)))) feature=device->GetFeatureLevel();
            if(stereo_requested) {
                sample_stereo(chain,number,desc);
            } else {
            std::array<std::byte,d2vr::camera_read_size> bytes{};
            SIZE_T read{};
            if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(camera_address),bytes.data(),bytes.size(),&read) && read==bytes.size()) {
                auto pose=d2vr::decode_game_camera(bytes,image_base);
                if(pose) {
                    ++valid_poses;
                    if(number%6==0) {
                        std::lock_guard<std::mutex> lock(log_mutex);
                        if(capturing.load()) {
                            log_pose(number,0,desc,*pose);
                        }
                    }
                }
            }
            }
        }
    }
    } catch(...) {
        // Never allow diagnostic I/O exceptions to escape into the game.
        callback_failed=true;
        capturing=false;
    }
    return original_present(chain,interval,flags);
}

void* find_present() {
    const wchar_t* name=L"Dishonored2VRHiddenProbeWindow";
    WNDCLASSW type{}; type.lpfnWndProc=DefWindowProcW; type.hInstance=module; type.lpszClassName=name;
    if(!RegisterClassW(&type)) throw std::runtime_error("Cannot register hidden graphics probe window");
    HWND window=CreateWindowExW(0,name,L"",WS_POPUP,0,0,16,16,nullptr,nullptr,module,nullptr);
    if(!window) {UnregisterClassW(name,module); throw std::runtime_error("Cannot create hidden graphics probe window");}
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width=16; desc.BufferDesc.Height=16; desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount=1;
    desc.OutputWindow=window; desc.Windowed=TRUE; desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; ComPtr<IDXGISwapChain> chain;
    HRESULT result=D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,
                    D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context);
    void* target=nullptr;
    if(SUCCEEDED(result)) target=(*reinterpret_cast<void***>(chain.Get()))[8];
    chain.Reset(); context.Reset(); device.Reset(); DestroyWindow(window); UnregisterClassW(name,module);
    if(!target) throw std::runtime_error("Cannot create diagnostic D3D11 swapchain");
    return target;
}

extern "C" __declspec(dllexport) DWORD WINAPI StartProbe(void*) {
    if(started.exchange(true)) return 2;
    fs::path output;
    void* target=nullptr;
    bool enabled=false;
    try {
        auto root=fs::path(module_path(module)).parent_path().parent_path();
        if(!fs::exists(root/L".git")) return 3;
        output=root/L"artifacts";
        output_directory=output;
        fs::create_directories(output);
        auto game=fs::path(module_path(nullptr));
        if(_wcsicmp(game.filename().c_str(),L"Dishonored2.exe") || sha256(game)!=d2vr::game_sha256)
            throw std::runtime_error("Unsupported executable: diagnostic was not enabled");
        std::ifstream config(root/L"build"/L"dxgi-probe.cfg");
        DWORD expected_pid{};
        if(!(config>>expected_pid>>camera_address) || expected_pid!=GetCurrentProcessId())
            throw std::runtime_error("Missing or mismatched process/camera configuration");
        int stereo_flag=0;config>>stereo_flag;
        if(stereo_flag<0 || stereo_flag>2) throw std::runtime_error("Unknown diagnostic mode");
        stereo_requested=stereo_flag!=0;single_eye_requested=stereo_flag==2;
        frames=0;valid_poses=0;stereo_frames=0;distinct_view_frames=0;measured_separation=0;
        callback_failed=false;stereo_active=false;mono_captured=false;stereo_captured=false;
        mode_restored=true;values_restored=true;baseline_pose.reset();camera_moved=false;
        stereo_ever_enabled=false;left_captured=false;right_phase=false;
        image_base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        if(stereo_requested) {
            prepare_stereo();
        } else {
        std::array<std::byte,d2vr::camera_read_size> bytes{}; SIZE_T count{};
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(camera_address),bytes.data(),bytes.size(),&count) ||
           count!=bytes.size() || !d2vr::decode_game_camera(bytes,image_base))
            throw std::runtime_error("Camera identity/pose check failed before hook installation");
        }
        csv.open(output/L"dxgi-camera-probe.csv",std::ios::trunc);
        if(!csv) throw std::runtime_error("Cannot open workspace diagnostic log");
        csv << std::setprecision(9) << "present,tick_ms,eye,width,height,format,hfov,vfov,x,y,z,b00,b01,b02,b10,b11,b12,b20,b21,b22\n";
        if(stereo_requested && !single_eye_requested) {
            auto input_deadline=GetTickCount64()+60'000;
            while(!focused_movement() && GetTickCount64()<input_deadline) Sleep(50);
            if(!focused_movement()) throw std::runtime_error("No focused gameplay movement within 60 seconds; diagnostic not enabled");
        }
        target=find_present();
        MH_STATUS status{};
        if(!original_present) {
            auto initialized=MH_Initialize();
            if(initialized!=MH_OK && initialized!=MH_ERROR_ALREADY_INITIALIZED) throw std::runtime_error("MinHook initialization failed");
            if(!read_self(reinterpret_cast<std::uintptr_t>(target),disabled_target_bytes.data(),disabled_target_bytes.size()))
                throw std::runtime_error("Cannot verify Present code before hook installation");
            status=MH_CreateHook(target,reinterpret_cast<void*>(&present_hook),reinterpret_cast<void**>(&original_present));
            if(status!=MH_OK) throw std::runtime_error(std::string("Present hook creation failed: ")+MH_StatusToString(status));
            hook_target=target;
        } else {
            std::array<std::byte,16> current{};
            if(target!=hook_target || !read_self(reinterpret_cast<std::uintptr_t>(target),current.data(),current.size()) || current!=disabled_target_bytes)
                throw std::runtime_error("Present code changed since the prior diagnostic; restart the game");
        }
        capturing=true;
        status=MH_EnableHook(target);
        if(status!=MH_OK) {capturing=false; throw std::runtime_error(std::string("Present hook enable failed: ")+MH_StatusToString(status));}
        enabled=true;
        ULONGLONG end=GetTickCount64()+10'000;
        while(GetTickCount64()<end) Sleep(20);
        capturing=false;
        auto disabled=MH_DisableHook(target);
        enabled=disabled!=MH_OK;
        // Keep the inert DLL and trampoline allocated until normal game exit:
        // a Present call can still have a return address into this DLL.
        std::lock_guard<std::mutex> lock(log_mutex);
        restore_stereo();
        csv.close();
        std::ofstream result(output/L"dxgi-probe-result.json",std::ios::trunc);
        result << "{\"presents\":" << frames.load() << ",\"valid_camera_poses\":" << valid_poses.load()
               << ",\"width\":" << width.load() << ",\"height\":" << height.load() << ",\"format\":" << format.load()
               << ",\"feature_level\":" << feature.load() << ",\"hook_disabled\":" << (disabled==MH_OK?"true":"false")
               << ",\"callback_failed\":" << (callback_failed.load()?"true":"false")
               << ",\"stereo_requested\":" << (stereo_requested?"true":"false")
               << ",\"stereo_frames\":" << stereo_frames.load() << ",\"distinct_view_frames\":" << distinct_view_frames.load()
               << ",\"measured_separation_game_units\":" << measured_separation.load()
               << ",\"mode_restored\":" << (mode_restored?"true":"false") << ",\"values_restored\":" << (values_restored?"true":"false")
               << ",\"mono_captured\":" << (mono_captured?"true":"false") << ",\"stereo_captured\":" << (stereo_captured?"true":"false")
               << ",\"camera_moved_before_stereo\":" << (camera_moved?"true":"false")
               << ",\"single_eye_requested\":" << (single_eye_requested?"true":"false") << ",\"left_captured\":" << (left_captured?"true":"false")
               << ",\"backend_hook_disabled\":true"
               << ",\"camera_object_writes\":false,\"native_stereo_settings_written\":" << (stereo_ever_enabled?"true":"false")
               << ",\"vr_rendering\":false,\"dll_retained_until_game_exit\":true}\n";
        bool stereo_success=!stereo_requested || (stereo_captured && (single_eye_requested?left_captured:distinct_view_frames.load()>0) && mode_restored && values_restored);
        if(disabled==MH_OK && mode_restored && values_restored) started=false;
        return disabled==MH_OK && !callback_failed.load() && frames.load()>0 && valid_poses.load()>0 && stereo_success ? 0 : 4;
    } catch(const std::exception& error) {
        capturing=false;
        if(enabled) enabled=MH_DisableHook(target)!=MH_OK;
        try {
            std::lock_guard<std::mutex> lock(log_mutex);
            restore_stereo();
            csv.close();
            if(!output.empty()) {std::ofstream log(output/L"dxgi-probe-failure.txt",std::ios::trunc); log << error.what() << '\n';}
        } catch(...) {}
        if(!enabled && mode_restored && values_restored) started=false;
        return 1;
    } catch(...) {
        capturing=false;
        if(enabled) MH_DisableHook(target);
        try {std::lock_guard<std::mutex> lock(log_mutex);restore_stereo();} catch(...) {}
        return 1;
    }
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH) module=instance;
    return TRUE;
}
