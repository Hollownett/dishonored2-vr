#pragma once
#include "fsr3_eye_upscaler.hpp"
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wintrust.h>
#include <softpub.h>
#include <wrl/client.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <array>
#include <chrono>
#include <stdexcept>
#pragma warning(push,0)
#include "../third_party/fsr4/include/ffx_api.h"
#include "../third_party/fsr4/include/ffx_upscale.h"
#include "../third_party/fsr4/include/dx12/ffx_api_dx12.h"
#pragma warning(pop)
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"wintrust.lib")
#pragma comment(lib,"crypt32.lib")

namespace d2vr {
// Loader process-detach query, resolved once at DLL load: RtlDllShutdownInProgress() reports that the DLL
// shutdown notifications are running because the process is exiting. AMD's FFX teardown faults there
// (2026-10-29 18:16 record: Fsr4EyeUpscaler::State::~State -> ffxDestroyContext -> amdxcffx64 ->
// amdxc64 access violation on 0xfeeefeeefeeefeee), so that call is skipped while the process is dying.
inline BOOLEAN (NTAPI*const rtl_dll_shutdown_in_progress)()=[]() noexcept -> BOOLEAN (NTAPI*)() {
    const auto module=GetModuleHandleW(L"ntdll.dll");
    return module?reinterpret_cast<BOOLEAN (NTAPI*)()>(GetProcAddress(module,"RtlDllShutdownInProgress")):nullptr;
}();
inline bool dll_process_detaching() noexcept {return rtl_dll_shutdown_in_progress && rtl_dll_shutdown_in_progress()!=FALSE;}
// Optional FSR4 provider bridge. Immediate-context owner only; one wrapper per eye.
// No CPU completion waits. The bounded registry retains retiring contexts until
// their final D3D11 consumption signal completes, including across wrapper destruction.
class Fsr4EyeUpscaler {
public:
    struct Id {std::uint64_t epoch{},frame{},dispatch{};int eye{};};
    enum class Phase {inputs_begin,inputs_ready,output_ready,output_copied,cpu_flush,ffx_gpu,ffx_dropped,cpu_record,cpu_handoff,cpu_dispatch};
    struct Diagnostics {
        Id id{};
        void (*callback)(void*,const Id&,Phase,ID3D11DeviceContext*,double) noexcept{};
        void* user{};
    };
private:
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    struct Shared {Ptr<ID3D12Resource> dx12;Ptr<ID3D11Texture2D> dx11;Ptr<ID3D11RenderTargetView> clear_view;};
    struct Slot {
        std::array<Shared,7> textures; // colour, depth, motion, reactive, output, unity exposure, zero composition
        Ptr<ID3D12CommandAllocator> allocator;Ptr<ID3D12GraphicsCommandList> list;
        Ptr<ID3D12QueryHeap> timestamps;Ptr<ID3D12Resource> timestamp_readback;
        std::array<Ptr<ID3D11Resource>,5> caller;UINT64 done{};
        bool constants_initialized{},reactive_zero{},timestamp_pending{};
        Diagnostics diagnostics{};
    };
    struct State {
        Ptr<ID3D11Device> device11;Ptr<ID3D11Device5> device5;Ptr<ID3D11DeviceContext4> context11;
        Ptr<ID3D12Device> device12;Ptr<ID3D12CommandQueue> queue;Ptr<ID3D12Fence> fence12;Ptr<ID3D11Fence> fence11;
        std::array<Slot,4> slots;
        HMODULE module{};ffxContext context{};
        PfnFfxCreateContext create{};PfnFfxDestroyContext destroy{};PfnFfxQuery query{};PfnFfxDispatch dispatch{};
        Fsr3EyeUpscaler::Desc desc;DXGI_FORMAT colour{},output{};bool retired{},poisoned{},reset=true;
        UINT64 submitted{};std::uint64_t version{};std::string version_name;std::wstring provider;
        bool timing_attempted{},timing_available{};UINT64 timestamp_frequency{},gpu_samples{},gpu_latest_done{};
        double gpu_last_ms{},gpu_total_ms{};
        // The provider DLL goes last: members would otherwise release the D3D12 device, queue and lists after FreeLibrary,
        // while the driver may still hold work or callbacks that reference provider code.
        ~State() {
            // The driver's own teardown is unsafe once the process is exiting; the OS reclaims the context.
            if(context && destroy && !dll_process_detaching()) destroy(&context,nullptr);
            slots={};fence12.Reset();queue.Reset();device12.Reset();fence11.Reset();
            if(module && !dll_process_detaching()) FreeLibrary(module);
        }
    };
    // Fixed global ownership includes active and retiring states. No unbounded
    // retirement queue; at capacity create fails and the caller keeps FSR3.
    static auto& registry() {static auto* value=new std::array<std::shared_ptr<State>,8>;return *value;}
    static auto& registry_mutex() {static auto* value=new std::mutex;return *value;}
    static void collect() {
        for(auto& value:registry()) if(value && value->retired && !value->poisoned &&
            (!value->submitted || (value->fence12 && value->fence12->GetCompletedValue()>=value->submitted))) value.reset();
    }
    std::shared_ptr<State> state_;
    std::string error_;
    unsigned failure_code_{};float record_ms_{},handoff_ms_{};
    static float ms_since(std::chrono::steady_clock::time_point t) {return std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now()-t).count();}
    bool fail(const char* reason,unsigned code=1) {error_=reason;failure_code_=code;return false;}
    static void hr(HRESULT value,const char* reason) {if(FAILED(value)) throw std::runtime_error(reason);}
    static void api(ffxReturnCode_t value,const char* reason) {if(value!=FFX_API_RETURN_OK) throw std::runtime_error(reason);}
    static bool fsr4_name(const char* name) {return name && name[0]=='4' && name[1]=='.';}
    template<class T> static T function(HMODULE module,const char* name) {
        const auto symbol=GetProcAddress(module,name);T result{};
        static_assert(sizeof result==sizeof symbol);std::memcpy(&result,&symbol,sizeof result);
        if(!result) throw std::runtime_error("Missing official FSR API export");return result;
    }
    static bool signed_amd(const std::wstring& path) {
        WINTRUST_FILE_INFO file{};file.cbStruct=sizeof file;file.pcwszFilePath=path.c_str();
        WINTRUST_DATA trust{};trust.cbStruct=sizeof trust;trust.dwUIChoice=WTD_UI_NONE;
        trust.fdwRevocationChecks=WTD_REVOKE_NONE;trust.dwUnionChoice=WTD_CHOICE_FILE;trust.pFile=&file;
        trust.dwStateAction=WTD_STATEACTION_VERIFY;trust.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
        GUID policy=WINTRUST_ACTION_GENERIC_VERIFY_V2;
        const LONG result=WinVerifyTrust(nullptr,&policy,&trust);bool amd=false;
        if(result==ERROR_SUCCESS) {
            const auto data=WTHelperProvDataFromStateData(trust.hWVTStateData);
            const auto signer=data?WTHelperGetProvSignerFromChain(data,0,FALSE,0):nullptr;
            if(signer && signer->csCertChain) {
                wchar_t subject[512]{};
                CertGetNameStringW(signer->pasCertChain[0].pCert,CERT_NAME_SIMPLE_DISPLAY_TYPE,0,nullptr,subject,512);
                amd=std::wstring(subject).find(L"Advanced Micro Devices")!=std::wstring::npos;
            }
        }
        trust.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&policy,&trust);return result==ERROR_SUCCESS && amd;
    }
    static Shared shared(State& s,UINT w,UINT h,DXGI_FORMAT format,bool output) {
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width=w;desc.Height=h;desc.DepthOrArraySize=desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;
        desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS|D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if(output) desc.Flags|=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        Shared result;hr(s.device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,
            nullptr,IID_PPV_ARGS(&result.dx12)),"Create FSR4 shared texture");
        HANDLE handle{};hr(s.device12->CreateSharedHandle(result.dx12.Get(),nullptr,GENERIC_ALL,nullptr,&handle),"Share FSR4 texture");
        const auto opened=s.device5->OpenSharedResource1(handle,IID_PPV_ARGS(&result.dx11));CloseHandle(handle);
        hr(opened,"Open FSR4 shared texture in D3D11");
        if(format==DXGI_FORMAT_R8_UNORM) hr(s.device11->CreateRenderTargetView(result.dx11.Get(),nullptr,&result.clear_view),"Create FSR4 mask clear view");
        return result;
    }
    static bool matches(State& s,ID3D11Resource* resource,UINT w,UINT h,DXGI_FORMAT format,bool exact,bool rgba8_copy=false) {
        if(!resource) return false;
        Ptr<ID3D11Device> owner;resource->GetDevice(&owner);if(owner.Get()!=s.device11.Get()) return false;
        Ptr<ID3D11Texture2D> texture;if(FAILED(resource->QueryInterface(IID_PPV_ARGS(&texture)))) return false;
        D3D11_TEXTURE2D_DESC d{};texture->GetDesc(&d);
        // Shared input/output copies preserve bytes across the RGBA8 type group;
        // an sRGB view does not introduce a gamma conversion during CopyResource.
        const bool rgba8=rgba8_copy && format==DXGI_FORMAT_R8G8B8A8_UNORM &&
            (d.Format==DXGI_FORMAT_R8G8B8A8_TYPELESS || d.Format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        return (d.Format==format || rgba8) && d.Width>=w && d.Height>=h && (!exact || (d.Width==w && d.Height==h)) &&
            d.ArraySize==1 && ((!exact && rgba8_copy)?d.MipLevels>=1:d.MipLevels==1) && d.SampleDesc.Count==1;
    }
    static D3D12_RESOURCE_BARRIER barrier(ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource=resource;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore=before;b.Transition.StateAfter=after;return b;
    }
    static void prepare_timing(State& s) {
        if(s.timing_attempted) return;
        s.timing_attempted=true;
        if(FAILED(s.queue->GetTimestampFrequency(&s.timestamp_frequency)) || !s.timestamp_frequency) return;
        D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=2;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=2*sizeof(UINT64);
        buffer.Height=buffer.DepthOrArraySize=buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        for(auto& slot:s.slots) {
            if(FAILED(s.device12->CreateQueryHeap(&query,IID_PPV_ARGS(&slot.timestamps))) ||
               FAILED(s.device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,
                   nullptr,IID_PPV_ARGS(&slot.timestamp_readback)))) return; // Diagnostics never disable rendering.
        }
        s.timing_available=true;
    }
    static void finish_diagnostics(Slot& slot,Phase phase,ID3D11DeviceContext* context,double duration=0) noexcept {
        const auto diagnostic=slot.diagnostics;
        slot.timestamp_pending=false;slot.diagnostics.callback=nullptr;
        if(diagnostic.callback) diagnostic.callback(diagnostic.user,diagnostic.id,phase,context,duration);
    }
    static void drop_pending_diagnostics(State& state) noexcept {
        for(auto& slot:state.slots) if(slot.timestamp_pending) finish_diagnostics(slot,Phase::ffx_dropped,state.context11.Get());
    }
    static void poll_timing(State& s) {
        if(!s.timing_attempted) return;
        const auto completed=s.fence12->GetCompletedValue();
        for(auto& slot:s.slots) if(slot.timestamp_pending && slot.done<=completed) {
            slot.timestamp_pending=false;
            D3D12_RANGE range{0,2*sizeof(UINT64)};void* mapped{};
            if(FAILED(slot.timestamp_readback->Map(0,&range,&mapped))) {
                s.timing_available=false;finish_diagnostics(slot,Phase::ffx_dropped,s.context11.Get());continue;
            }
            UINT64 ticks[2]{};std::memcpy(ticks,mapped,sizeof ticks);
            const D3D12_RANGE written{0,0};slot.timestamp_readback->Unmap(0,&written);
            if(ticks[1]>=ticks[0]) {
                const double elapsed=double(ticks[1]-ticks[0])*1000.0/double(s.timestamp_frequency);
                if(slot.done>s.gpu_latest_done) {s.gpu_last_ms=elapsed;s.gpu_latest_done=slot.done;}
                s.gpu_total_ms+=elapsed;++s.gpu_samples;
                finish_diagnostics(slot,Phase::ffx_gpu,s.context11.Get(),elapsed);
            } else finish_diagnostics(slot,Phase::ffx_dropped,s.context11.Get());
        }
    }
public:
    using Desc=Fsr3EyeUpscaler::Desc;using Inputs=Fsr3EyeUpscaler::Inputs;
    Fsr4EyeUpscaler()=default;Fsr4EyeUpscaler(const Fsr4EyeUpscaler&)=delete;Fsr4EyeUpscaler& operator=(const Fsr4EyeUpscaler&)=delete;
    ~Fsr4EyeUpscaler() {destroy();}
    bool valid() const noexcept {return state_ && !state_->retired && !state_->poisoned;}
    std::uint64_t version_id() const {return state_?state_->version:0;}
    std::string version_name() const {return state_?state_->version_name:std::string{};}
    std::wstring provider_path() const {return state_?state_->provider:std::wstring{};}
    const std::string& last_error() const noexcept {return error_;}
    // CPU ms of the last dispatch: ffx command recording, then D3D11 copies+Flush+queue handoff.
    float record_ms() const noexcept {return record_ms_;}
    float handoff_ms() const noexcept {return handoff_ms_;}
    // Actual FFX GPU interval, excluding the surrounding interop waits/copies/barriers.
    void poll_gpu_timing() {if(state_) poll_timing(*state_);}
    double compute_gpu_ms() const noexcept {return state_?state_->gpu_last_ms:0;}
    double compute_gpu_total_ms() const noexcept {return state_?state_->gpu_total_ms:0;}
    UINT64 compute_gpu_samples() const noexcept {return state_?state_->gpu_samples:0;}
    unsigned failure_code() const noexcept {return failure_code_;} // 1 unavailable,2 owner,3 inputs,4 busy,5 handoff/SDK.
    UINT64 submitted_gpu_value() const {return state_?state_->submitted:0;}
    UINT64 completed_gpu_value() const {return state_ && state_->fence12?state_->fence12->GetCompletedValue():0;}
    void destroy() {
        std::lock_guard<std::mutex> lock(registry_mutex());
        if(state_) {drop_pending_diagnostics(*state_);state_->retired=true;state_.reset();}collect();
    }
    static void poll_retired() {std::lock_guard<std::mutex> lock(registry_mutex());collect();}
    // Explicit formats are required for the native R11G11B10/RGBA8 paths: no
    // implicit format conversion is performed by the shared input copies.
    bool create(ID3D11Device* device,const Desc& desc,const wchar_t* provider,
                DXGI_FORMAT colour_format=DXGI_FORMAT_R16G16B16A16_FLOAT,
                DXGI_FORMAT output_format=DXGI_FORMAT_R16G16B16A16_FLOAT) {
        if(valid() && provider && state_->device11.Get()==device && state_->provider==provider &&
            state_->colour==colour_format && state_->output==output_format &&
            state_->desc.render_w==desc.render_w && state_->desc.render_h==desc.render_h &&
            state_->desc.display_w==desc.display_w && state_->desc.display_h==desc.display_h &&
            state_->desc.hdr==desc.hdr && state_->desc.auto_exposure==desc.auto_exposure &&
            state_->desc.inverted_depth==desc.inverted_depth && state_->desc.mv_jittered==desc.mv_jittered &&
            state_->desc.debug_checks==desc.debug_checks) return true;
        destroy();error_.clear();
        if(!device || !provider || !desc.render_w || !desc.render_h || !desc.display_w || !desc.display_h ||
            desc.render_w>desc.display_w || desc.render_h>desc.display_h) return fail("Invalid FSR4 dimensions/device/provider");
        std::lock_guard<std::mutex> lock(registry_mutex());collect();
        auto free=registry().end();for(auto i=registry().begin();i!=registry().end();++i) if(!*i) {free=i;break;}
        if(free==registry().end()) return fail("FSR4 active/retiring context capacity reached");
        auto s=std::make_shared<State>();
        try {
            const std::filesystem::path path(provider);
            if(!path.is_absolute() || path.filename()!=L"amd_fidelityfx_upscaler_dx12.dll" || !signed_amd(path.wstring()))
                return fail("FSR4 provider path or AMD Authenticode verification failed");
            s->provider=path.wstring();s->module=LoadLibraryExW(provider,nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if(!s->module) return fail("Cannot load signed AMD FSR4 provider");
            s->create=function<PfnFfxCreateContext>(s->module,"ffxCreateContext");
            s->destroy=function<PfnFfxDestroyContext>(s->module,"ffxDestroyContext");
            s->query=function<PfnFfxQuery>(s->module,"ffxQuery");s->dispatch=function<PfnFfxDispatch>(s->module,"ffxDispatch");
            s->device11=device;hr(device->QueryInterface(IID_PPV_ARGS(&s->device5)),"FSR4 requires D3D11Device5");
            Ptr<ID3D11DeviceContext> immediate;device->GetImmediateContext(&immediate);
            hr(immediate.As(&s->context11),"FSR4 requires D3D11Context4");
            Ptr<IDXGIDevice> dxgi;Ptr<IDXGIAdapter> adapter;hr(device->QueryInterface(IID_PPV_ARGS(&dxgi)),"Get game adapter");
            hr(dxgi->GetAdapter(&adapter),"Get game adapter identity");
            hr(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&s->device12)),"Create same-adapter FSR4 D3D12 device");
            D3D12_FEATURE_DATA_SHADER_MODEL shader{D3D_SHADER_MODEL_6_6};
            hr(s->device12->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&shader,sizeof shader),"Query FSR4 SM6.6");
            if(shader.HighestShaderModel<D3D_SHADER_MODEL_6_6) return fail("FSR4 requires SM6.6");
            ffxQueryDescGetVersions versions{};versions.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
            versions.createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;versions.device=s->device12.Get();
            uint64_t count{};versions.outputCount=&count;api(s->query(nullptr,&versions.header),"Query FSR versions count");
            if(!count || count>32) return fail("Invalid FSR provider version count");
            std::array<uint64_t,32> ids{};std::array<const char*,32> names{};
            versions.versionIds=ids.data();versions.versionNames=names.data();api(s->query(nullptr,&versions.header),"Query FSR version identities");
            if(count>ids.size()) return fail("FSR provider changed version count beyond capacity");
            for(std::size_t i=0;i<count;++i) if(fsr4_name(names[i])) {s->version=ids[i];s->version_name=names[i];break;}
            if(!s->version) return fail("Provider offers no genuine FSR4 version");
            s->desc=desc;s->colour=colour_format;s->output=output_format;
            ffxOverrideVersion override_version{};override_version.header.type=FFX_API_DESC_TYPE_OVERRIDE_VERSION;override_version.versionId=s->version;
            ffxCreateContextDescUpscaleVersion api_version{};api_version.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION;
            api_version.version=FFX_UPSCALER_VERSION;api_version.header.pNext=&override_version.header;
            ffxCreateBackendDX12Desc backend{};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
            backend.device=s->device12.Get();backend.header.pNext=&api_version.header;
            ffxCreateContextDescUpscale create_desc{};create_desc.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
            create_desc.header.pNext=&backend.header;create_desc.maxRenderSize={desc.render_w,desc.render_h};
            create_desc.maxUpscaleSize={desc.display_w,desc.display_h};
            if(desc.hdr) create_desc.flags|=FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
            else create_desc.flags|=FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
            if(desc.auto_exposure) create_desc.flags|=FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
            if(desc.inverted_depth) create_desc.flags|=FFX_UPSCALE_ENABLE_DEPTH_INVERTED|FFX_UPSCALE_ENABLE_DEPTH_INFINITE;
            if(desc.mv_jittered) create_desc.flags|=FFX_UPSCALE_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION;
            api(s->create(&s->context,&create_desc.header,nullptr),"Create selected FSR4 context");
            ffxQueryGetProviderVersion actual{};actual.header.type=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
            api(s->query(&s->context,&actual.header),"Query created FSR4 identity");
            if(actual.versionId!=s->version || !fsr4_name(actual.versionName)) return fail("Created provider silently selected another version");
            s->version_name=actual.versionName;
            ffxQueryDescUpscaleGetResourceRequirements required{};
            required.header.type=FFX_API_QUERY_DESC_TYPE_UPSCALE_GET_RESOURCE_REQUIREMENTS;
            api(s->query(&s->context,&required.header),"Query FSR4 required inputs");
            constexpr std::uint64_t supported=FFX_API_QUERY_RESOURCE_INPUT_COLOR|FFX_API_QUERY_RESOURCE_INPUT_DEPTH|
                FFX_API_QUERY_RESOURCE_INPUT_MV|FFX_API_QUERY_RESOURCE_INPUT_REACTIVEMASK|
                FFX_API_QUERY_RESOURCE_INPUT_EXPOSURE|FFX_API_QUERY_RESOURCE_INPUT_TRANSPARENCYCOMPOSITION;
            if(required.required_resources&~supported) return fail("FSR4 provider requires unsupported exposure/composition input");
            D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
            hr(s->device12->CreateCommandQueue(&q,IID_PPV_ARGS(&s->queue)),"Create FSR4 queue");
            hr(s->device12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&s->fence12)),"Create FSR4 shared fence");
            HANDLE handle{};hr(s->device12->CreateSharedHandle(s->fence12.Get(),nullptr,GENERIC_ALL,nullptr,&handle),"Share FSR4 fence");
            const auto opened=s->device5->OpenSharedFence(handle,IID_PPV_ARGS(&s->fence11));CloseHandle(handle);hr(opened,"Open FSR4 fence");
            const std::array<DXGI_FORMAT,7> formats{colour_format,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R16G16_FLOAT,DXGI_FORMAT_R8_UNORM,output_format,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R8_UNORM};
            for(auto& slot:s->slots) {
                for(unsigned i=0;i<formats.size();++i) slot.textures[i]=shared(*s,i==5?1:i==4?desc.display_w:desc.render_w,i==5?1:i==4?desc.display_h:desc.render_h,formats[i],i==4);
                hr(s->device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocator)),"Create FSR4 allocator");
                hr(s->device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.list)),"Create FSR4 command list");
                hr(slot.list->Close(),"Close initial FSR4 list");
            }
            *free=s;state_=std::move(s);return true;
        } catch(const std::exception& error) {error_=error.what();return false;}
    }
    bool dispatch(ID3D11DeviceContext* context,const Inputs& in,bool timing_enabled=false,const Diagnostics* diagnostics=nullptr) {
        const auto rejected=[&](const char* reason,unsigned code=1) {
            if(diagnostics && diagnostics->callback) diagnostics->callback(diagnostics->user,diagnostics->id,Phase::ffx_dropped,context,0);
            return fail(reason,code);
        };
        if(!valid()) return rejected("FSR4 context is unavailable");
        auto& s=*state_;
        if(context!=static_cast<ID3D11DeviceContext*>(s.context11.Get()) || context->GetType()!=D3D11_DEVICE_CONTEXT_IMMEDIATE)
            return rejected("FSR4 dispatch requires its exact immediate owner",2);
        const UINT rw=in.render_w?in.render_w:s.desc.render_w,rh=in.render_h?in.render_h:s.desc.render_h;
        if(!rw || !rh || rw>s.desc.render_w || rh>s.desc.render_h || !std::isfinite(in.jitter_px_x) || !std::isfinite(in.jitter_px_y) ||
            !std::isfinite(in.mv_scale_x) || !std::isfinite(in.mv_scale_y) || !std::isfinite(in.frame_time_ms) || in.frame_time_ms<=0 ||
            !std::isfinite(in.vfov_rad) || in.vfov_rad<=0 || in.vfov_rad>=3.1415927f ||
            !std::isfinite(in.cam_near) || in.cam_near<=0 || !std::isfinite(in.cam_far) || in.cam_far<=in.cam_near ||
            !std::isfinite(in.pre_exposure) || !std::isfinite(in.sharpness) || in.sharpness>1 ||
            !matches(s,in.colour,in.src_x+rw,in.src_y+rh,s.colour,false,true) ||
            !matches(s,in.depth,in.src_x+rw,in.src_y+rh,DXGI_FORMAT_R32_FLOAT,false) || !matches(s,in.motion,in.src_x+rw,in.src_y+rh,DXGI_FORMAT_R16G16_FLOAT,false) ||
            !matches(s,in.output,in.dst_x+s.desc.display_w,in.dst_y+s.desc.display_h,s.output,!in.output_region,true) ||
            (!in.output_region && (in.dst_x || in.dst_y)) ||
            (in.reactive && !matches(s,in.reactive,in.src_x+rw,in.src_y+rh,DXGI_FORMAT_R8_UNORM,false))) {
                s.reset=true;return rejected("FSR4 input dimensions/format/device/parameters differ",3);
            }
        poll_timing(s);
        if(timing_enabled) prepare_timing(s);
        Slot* chosen=nullptr;const auto completed=s.fence12->GetCompletedValue();
        for(auto& slot:s.slots) if(slot.done<=completed) {chosen=&slot;break;}
        if(!chosen) {s.reset=true;return rejected("FSR4 bounded GPU slot ring is busy",4);}
        auto& slot=*chosen;slot.caller={};slot.diagnostics=diagnostics?*diagnostics:Diagnostics{};
        const auto observe=[&](Phase phase,double duration=0) noexcept {
            if(slot.diagnostics.callback) slot.diagnostics.callback(slot.diagnostics.user,slot.diagnostics.id,phase,context,duration);
        };
        bool queued_work=false;const auto t_record=std::chrono::steady_clock::now();
        try {
            hr(slot.allocator->Reset(),"Reset completed FSR4 allocator");hr(slot.list->Reset(slot.allocator.Get(),nullptr),"Reset completed FSR4 list");
            std::array<D3D12_RESOURCE_BARRIER,7> before{},after{};
            for(unsigned i=0;i<before.size();++i) {
                const auto next=i==4?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                before[i]=barrier(slot.textures[i].dx12.Get(),D3D12_RESOURCE_STATE_COMMON,next);
                after[i]=barrier(slot.textures[i].dx12.Get(),next,D3D12_RESOURCE_STATE_COMMON);
            }
            slot.list->ResourceBarrier(static_cast<UINT>(before.size()),before.data());
            ffxDispatchDescUpscale dispatch_desc{};dispatch_desc.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
            dispatch_desc.commandList=slot.list.Get();
            dispatch_desc.color=ffxApiGetResourceDX12(slot.textures[0].dx12.Get());
            dispatch_desc.depth=ffxApiGetResourceDX12(slot.textures[1].dx12.Get());
            dispatch_desc.motionVectors=ffxApiGetResourceDX12(slot.textures[2].dx12.Get());
            dispatch_desc.reactive=ffxApiGetResourceDX12(slot.textures[3].dx12.Get());
            if(!s.desc.auto_exposure) dispatch_desc.exposure=ffxApiGetResourceDX12(slot.textures[5].dx12.Get());
            dispatch_desc.transparencyAndComposition=ffxApiGetResourceDX12(slot.textures[6].dx12.Get());
            dispatch_desc.output=ffxApiGetResourceDX12(slot.textures[4].dx12.Get(),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
            dispatch_desc.renderSize={rw,rh};dispatch_desc.upscaleSize={s.desc.display_w,s.desc.display_h};
            dispatch_desc.jitterOffset={in.jitter_px_x,in.jitter_px_y};dispatch_desc.motionVectorScale={in.mv_scale_x,in.mv_scale_y};
            dispatch_desc.frameTimeDelta=in.frame_time_ms;dispatch_desc.preExposure=in.pre_exposure>0?in.pre_exposure:1.f;
            dispatch_desc.reset=in.reset || s.reset;dispatch_desc.enableSharpening=in.sharpness>=0;dispatch_desc.sharpness=in.sharpness>=0?in.sharpness:0;
            dispatch_desc.cameraNear=s.desc.inverted_depth?in.cam_far:in.cam_near;dispatch_desc.cameraFar=s.desc.inverted_depth?in.cam_near:in.cam_far;
            dispatch_desc.cameraFovAngleVertical=in.vfov_rad;dispatch_desc.viewSpaceToMetersFactor=1;
            if(!s.desc.hdr) dispatch_desc.flags=FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;
            const bool timed=timing_enabled && s.timing_available;
            if(timed) slot.list->EndQuery(slot.timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
            api(s.dispatch(&s.context,&dispatch_desc.header),"Record actual FSR4 dispatch");
            if(timed) {
                slot.list->EndQuery(slot.timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
                slot.list->ResolveQueryData(slot.timestamps.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,slot.timestamp_readback.Get(),0);
            }
            slot.list->ResourceBarrier(static_cast<UINT>(after.size()),after.data());hr(slot.list->Close(),"Close actual FSR4 dispatch");
            record_ms_=ms_since(t_record);const auto t_handoff=std::chrono::steady_clock::now();
            const std::array<ID3D11Resource*,5> inputs{in.colour,in.depth,in.motion,in.reactive,in.output};
            const D3D11_BOX box{in.src_x,in.src_y,0,in.src_x+rw,in.src_y+rh,1};
            observe(Phase::inputs_begin);
            queued_work=true; // the first D3D11 shared-input copy starts GPU ownership
            for(unsigned i=0;i<4;++i) if(inputs[i]) {slot.caller[i]=inputs[i];context->CopySubresourceRegion(slot.textures[i].dx11.Get(),0,0,0,0,inputs[i],0,&box);}
            const float zero[4]{};
            if(!in.reactive && !slot.reactive_zero) context->ClearRenderTargetView(slot.textures[3].clear_view.Get(),zero);
            slot.reactive_zero=!in.reactive;
            // These are read-only FSR inputs. Slot reuse is consumption-fenced;
            // initialize once, retaining identical pixels and COMMON transitions.
            if(!slot.constants_initialized) {
                context->ClearRenderTargetView(slot.textures[6].clear_view.Get(),zero);
                const float exposure=1;context->UpdateSubresource(slot.textures[5].dx11.Get(),0,nullptr,&exposure,sizeof exposure,0);
                slot.constants_initialized=true;
            }
            // v1042: the output is NOT retained. In the upscale modes it is the game's swapchain back buffer, and a slot holding it (4 slots
            // per eye, released only at reuse) made every engine ResizeBuffers fail with DXGI_ERROR_INVALID_CALL, which the game turns into
            // its fatal "ResizeBuffers" error (user, headset v1040: the game closed when the resolution slider moved; sim: FSR 4 Quality
            // died at every session stop). D3D11 keeps a resource alive for the copy recorded into it below; no reference is needed.
            const UINT64 produced=s.submitted+1,upscaled=produced+1,consumed=upscaled+1;
            observe(Phase::inputs_ready);
            hr(s.context11->Signal(s.fence11.Get(),produced),"Signal FSR4 D3D11 inputs");
            // Required submission, not a GPU wait: without this the DX12 queue
            // could wait on an unsubmitted D3D11 signal until the game's Present.
            LARGE_INTEGER flush_start{},flush_end{},flush_frequency{};
            if(slot.diagnostics.callback) {QueryPerformanceFrequency(&flush_frequency);QueryPerformanceCounter(&flush_start);}
            context->Flush();s.submitted=produced;
            if(slot.diagnostics.callback) {
                QueryPerformanceCounter(&flush_end);
                if(flush_frequency.QuadPart>0) observe(Phase::cpu_flush,
                    double(flush_end.QuadPart-flush_start.QuadPart)*1000.0/double(flush_frequency.QuadPart));
            }
            hr(s.queue->Wait(s.fence12.Get(),produced),"Wait for FSR4 inputs on GPU");
            ID3D12CommandList* lists[]{slot.list.Get()};s.queue->ExecuteCommandLists(1,lists);
            s.submitted=upscaled;hr(s.queue->Signal(s.fence12.Get(),upscaled),"Signal FSR4 output on GPU");
            hr(s.context11->Wait(s.fence11.Get(),upscaled),"Wait for FSR4 output on GPU");
            observe(Phase::output_ready);
            if(in.output_region) context->CopySubresourceRegion(in.output,0,in.dst_x,in.dst_y,0,slot.textures[4].dx11.Get(),0,nullptr);
            else context->CopyResource(in.output,slot.textures[4].dx11.Get());
            observe(Phase::output_copied);
            hr(s.context11->Signal(s.fence11.Get(),consumed),"Signal FSR4 consumption on GPU");
            handoff_ms_=ms_since(t_handoff);
            observe(Phase::cpu_record,record_ms_);
            observe(Phase::cpu_handoff,handoff_ms_);
            slot.timestamp_pending=timed;
            if(!timed) finish_diagnostics(slot,Phase::ffx_dropped,context);
            slot.done=consumed;s.submitted=consumed;s.reset=false;error_.clear();failure_code_=0;return true;
        } catch(const std::exception& error) {
            // A failed handoff may have queued GPU work without a reliable final
            // signal. Preserve this bounded registry entry until process exit.
            finish_diagnostics(slot,Phase::ffx_dropped,context);drop_pending_diagnostics(s);
            s.retired=true;s.poisoned=queued_work;error_=error.what();failure_code_=5;return false;
        }
    }
};
} // namespace d2vr
