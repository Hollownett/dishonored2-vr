// Standalone prerequisite probe only: no AMD provider, FSR4 dispatch or game hooks.
// Sources: Microsoft ID3D12Device::CreateSharedHandle and ID3D11Device5::OpenSharedFence.
// cl /std:c++17 /EHsc /W4 /WX /O2 /MD tools\fsr4_interop_probe.cpp /link d3d11.lib d3d12.lib dxgi.lib
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
struct Handle {
    HANDLE value{};
    ~Handle() {if(value) CloseHandle(value);}
    Handle()=default;Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
static void checked(HRESULT result,const char* step) {
    if(FAILED(result)) {
        char text[256]{};std::snprintf(text,sizeof text,"%s HRESULT 0x%08lx",step,static_cast<unsigned long>(result));
        throw std::runtime_error(text);
    }
}
static std::string quoted(const std::string& text) {
    std::string out="\"";
    for(unsigned char c:text) {
        if(c=='"' || c=='\\') {out+='\\';out+=static_cast<char>(c);}
        else if(c<32) out+=' ';
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
static std::string utf8(const wchar_t* text) {
    const int bytes=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    if(bytes<=0) return "unknown";
    std::string result(static_cast<std::size_t>(bytes),'\0');
    WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),bytes,nullptr,nullptr);result.pop_back();return result;
}
struct SharedTexture {Ptr<ID3D12Resource> twelve;Ptr<ID3D11Texture2D> eleven;};
static SharedTexture texture(ID3D12Device* device12,ID3D11Device5* device11,DXGI_FORMAT format) {
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=desc.Height=4;desc.DepthOrArraySize=desc.MipLevels=1;desc.Format=format;desc.SampleDesc.Count=1;
    desc.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    SharedTexture result;
    checked(device12->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_SHARED,&desc,D3D12_RESOURCE_STATE_COMMON,
        nullptr,IID_PPV_ARGS(&result.twelve)),"Create shared texture");
    Handle shared;checked(device12->CreateSharedHandle(result.twelve.Get(),nullptr,GENERIC_ALL,nullptr,&shared.value),"Share texture handle");
    checked(device11->OpenSharedResource1(shared.value,IID_PPV_ARGS(&result.eleven)),"D3D11 open texture");
    return result;
}
static D3D12_RESOURCE_BARRIER transition(ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=resource;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=before;barrier.Transition.StateAfter=after;return barrier;
}
static void roundtrip(ID3D11Device* device11,ID3D11Device5* device5,ID3D11DeviceContext4* context,
                      ID3D12Device* device12,ID3D12CommandQueue* queue,ID3D12Fence* fence12,ID3D11Fence* fence11,
                      DXGI_FORMAT format,unsigned bytes_per_pixel,unsigned index) {
    auto source=texture(device12,device5,format),destination=texture(device12,device5,format);
    const unsigned pitch=4*bytes_per_pixel;
    std::vector<unsigned char> expected(static_cast<std::size_t>(pitch)*4);
    for(std::size_t i=0;i<expected.size();++i) expected[i]=static_cast<unsigned char>((i*37+index*11+1)%251);
    context->UpdateSubresource(source.eleven.Get(),0,nullptr,expected.data(),pitch,0);

    // All dependencies cross APIs on the GPU. The CPU waits only for the final
    // validation copy, not to order D3D11 production and D3D12 consumption.
    const UINT64 produced=static_cast<UINT64>(index)*3+1,copied=produced+1,readback=copied+1;
    checked(context->Signal(fence11,produced),"D3D11 producer Signal");context->Flush();
    checked(queue->Wait(fence12,produced),"D3D12 producer Wait");
    Ptr<ID3D12CommandAllocator> allocator;Ptr<ID3D12GraphicsCommandList> commands;
    checked(device12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Create allocator");
    checked(device12->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&commands)),"Create copy list");
    std::array<D3D12_RESOURCE_BARRIER,2> barriers{
        transition(source.twelve.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE),
        transition(destination.twelve.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST)};
    commands->ResourceBarrier(static_cast<UINT>(barriers.size()),barriers.data());
    commands->CopyResource(destination.twelve.Get(),source.twelve.Get());
    barriers={transition(source.twelve.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_COMMON),
              transition(destination.twelve.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_COMMON)};
    commands->ResourceBarrier(static_cast<UINT>(barriers.size()),barriers.data());checked(commands->Close(),"Close copy list");
    ID3D12CommandList* lists[]{commands.Get()};queue->ExecuteCommandLists(1,lists);
    checked(queue->Signal(fence12,copied),"D3D12 copied Signal");
    checked(context->Wait(fence11,copied),"D3D11 copied Wait");
    D3D11_TEXTURE2D_DESC desc{};destination.eleven->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    Ptr<ID3D11Texture2D> staging;checked(device11->CreateTexture2D(&desc,nullptr,&staging),"Create validation staging");
    context->CopyResource(staging.Get(),destination.eleven.Get());
    checked(context->Signal(fence11,readback),"D3D11 readback Signal");context->Flush();
    Handle event;event.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!event.value) throw std::runtime_error("Create completion event failed");
    checked(fence12->SetEventOnCompletion(readback,event.value),"Set final completion event");
    if(WaitForSingleObject(event.value,3000)!=WAIT_OBJECT_0) throw std::runtime_error("Final GPU completion timeout (3000 ms)");
    if(fence12->GetCompletedValue()<readback) throw std::runtime_error("Shared fence completion not visible in D3D12");
    D3D11_MAPPED_SUBRESOURCE mapped{};
    checked(context->Map(staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped),"Map completed validation copy");
    bool equal=true;
    for(unsigned y=0;y<4;++y) if(std::memcmp(static_cast<unsigned char*>(mapped.pData)+static_cast<std::size_t>(y)*mapped.RowPitch,
        expected.data()+static_cast<std::size_t>(y)*pitch,pitch)!=0) equal=false;
    context->Unmap(staging.Get(),0);
    if(!equal) throw std::runtime_error("D3D11 to D3D12 to D3D11 bytes differ");
}
int main() {
    std::cout << "{\"probe\":\"standalone_d3d11_d3d12_prerequisites\",\"fsr4_dispatch_proven\":false,"
                 "\"amd_provider_loaded\":false,\"game_integration_proven\":false,";
    try {
        Ptr<ID3D11Device> device11;Ptr<ID3D11DeviceContext> context11;
        const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1};
        checked(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,levels,1,D3D11_SDK_VERSION,
            &device11,nullptr,&context11),"Create separate D3D11 hardware device");
        Ptr<IDXGIDevice> dxgi;Ptr<IDXGIAdapter> adapter;checked(device11.As(&dxgi),"D3D11 DXGI device");
        checked(dxgi->GetAdapter(&adapter),"Get matching adapter");DXGI_ADAPTER_DESC adapter_desc{};
        checked(adapter->GetDesc(&adapter_desc),"Read adapter identity");
        std::cout << "\"adapter\":" << quoted(utf8(adapter_desc.Description)) << ',';
        Ptr<ID3D12Device> device12;checked(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&device12)),"Create same-adapter D3D12 device");
        D3D12_FEATURE_DATA_SHADER_MODEL shader{D3D_SHADER_MODEL_6_6};
        const HRESULT shader_result=device12->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&shader,sizeof shader);
        const bool sm66=SUCCEEDED(shader_result) && shader.HighestShaderModel>=D3D_SHADER_MODEL_6_6;
        std::cout << "\"same_adapter\":true,\"shader_model_6_6\":" << (sm66?"true":"false") << ',';
        Ptr<ID3D11Device5> device5;Ptr<ID3D11DeviceContext4> context4;
        checked(device11.As(&device5),"Query ID3D11Device5");checked(context11.As(&context4),"Query ID3D11DeviceContext4");
        std::cout << "\"d3d11_device5\":true,\"d3d11_context4\":true,";
        D3D12_COMMAND_QUEUE_DESC queue_desc{};queue_desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        Ptr<ID3D12CommandQueue> queue;checked(device12->CreateCommandQueue(&queue_desc,IID_PPV_ARGS(&queue)),"Create own D3D12 queue");
        Ptr<ID3D12Fence> fence12;checked(device12->CreateFence(0,D3D12_FENCE_FLAG_SHARED,IID_PPV_ARGS(&fence12)),"Create shared fence");
        Handle shared;checked(device12->CreateSharedHandle(fence12.Get(),nullptr,GENERIC_ALL,nullptr,&shared.value),"Share fence handle");
        Ptr<ID3D11Fence> fence11;checked(device5->OpenSharedFence(shared.value,IID_PPV_ARGS(&fence11)),"D3D11 OpenSharedFence");
        struct Format {DXGI_FORMAT value;unsigned bytes;const char* name;};
        const std::array<Format,4> formats{{{DXGI_FORMAT_R16G16B16A16_FLOAT,8,"HDR_R16G16B16A16_FLOAT"},
            {DXGI_FORMAT_R16G16_FLOAT,4,"MV_R16G16_FLOAT"},{DXGI_FORMAT_R32_FLOAT,4,"DEPTH_R32_FLOAT"},
            {DXGI_FORMAT_R8G8B8A8_UNORM,4,"OUTPUT_RGBA8_UNORM"}}};
        std::cout << "\"formats\":[";
        for(unsigned i=0;i<formats.size();++i) {
            if(i) std::cout << ',';
            std::cout << "{\"format\":" << quoted(formats[i].name) << ',';
            try {
                roundtrip(device11.Get(),device5.Get(),context4.Get(),device12.Get(),queue.Get(),fence12.Get(),fence11.Get(),
                    formats[i].value,formats[i].bytes,i);
                std::cout << "\"byte_exact_roundtrip\":true,\"gpu_shared_fence_sequence\":true}";
            } catch(const std::exception& error) {
                std::cout << "\"byte_exact_roundtrip\":false,\"error\":" << quoted(error.what())
                          << "}],\"prerequisites_pass\":false}\n";return 1;
            }
        }
        std::cout << "],\"prerequisites_pass\":" << (sm66?"true":"false")
                  << ",\"limitation\":\"Copy and fence prerequisites only; no FSR4 provider, shaders, native game resources or headset performance tested\"}\n";
        return sm66?0:1;
    } catch(const std::exception& error) {
        std::cout << "\"prerequisites_pass\":false,\"error\":" << quoted(error.what()) << "}\n";return 1;
    }
}
