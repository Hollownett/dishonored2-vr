#pragma once
// v1428 (sim profile 2026-11-20: the first gameplay frame of every session spent ~11-15 s in D3DCompile on the main thread, StaleEyeWarp's
// shaders for the canted-to-straight resample; the spacewarp pass compiles ~1 s more when it first engages): D3DCompile through a disk
// cache of compiled bytecode, keyed by the source text, entry, profile and flags (any source change recompiles). dir() is set once at
// startup (the mod's shader-cache folder); empty = no cache. Writes go to a temporary file renamed into place, so a reader never sees a
// partial file. A warm-up thread can fill the cache during the menus (StaleEyeWarp::warm_cache, SpacewarpPass::warm_cache).
#include <d3dcompiler.h>
#include <windows.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace d2vr::shader_cache {
inline std::wstring& dir() {static std::wstring d;return d;}
inline std::atomic<std::uint64_t>& hits() {static std::atomic<std::uint64_t> n{0};return n;}
inline std::atomic<std::uint64_t>& misses() {static std::atomic<std::uint64_t> n{0};return n;}
inline std::uint64_t key(const std::string& source,const char* entry,const char* profile,UINT flags) noexcept {
    std::uint64_t h=14695981039346656037ull;
    const auto mix=[&](const void* p,std::size_t n) {const auto* b=static_cast<const unsigned char*>(p);for(std::size_t i=0;i<n;++i) h=(h^b[i])*1099511628211ull;};
    mix(source.data(),source.size());mix(entry,std::char_traits<char>::length(entry));mix("|",1);mix(profile,std::char_traits<char>::length(profile));mix(&flags,sizeof flags);
    const unsigned compiler=D3D_COMPILER_VERSION;mix(&compiler,sizeof compiler);  // review 2026-11-20: a compiler change recompiles
    return h;
}
// review 2026-11-20: each file ends with an 8-byte FNV-1a of the bytecode, checked on read (a short or damaged file is compiled again)
inline std::uint64_t checksum(const void* data,std::size_t size) noexcept {
    std::uint64_t h=14695981039346656037ull;const auto* b=static_cast<const unsigned char*>(data);
    for(std::size_t i=0;i<size;++i) h=(h^b[i])*1099511628211ull;
    return h;
}
// Same contract as D3DCompile (no defines, no includes); `out` gets the bytecode from the cache or from a fresh compile.
inline HRESULT compile(const std::string& source,const char* name,const char* entry,const char* profile,UINT flags,ID3DBlob** out,ID3DBlob** errors) noexcept {
    if(!out) return E_POINTER;
    *out=nullptr;if(errors) *errors=nullptr;
    std::filesystem::path file;
    try {
        if(!dir().empty()) {
            char hex[17];std::snprintf(hex,sizeof hex,"%016llx",static_cast<unsigned long long>(key(source,entry,profile,flags)));
            file=std::filesystem::path(dir())/(std::string(entry)+"-"+hex+".cso");
            std::ifstream in(file,std::ios::binary|std::ios::ate);
            if(in) {
                const auto total=static_cast<std::streamoff>(in.tellg());
                const auto size=total-static_cast<std::streamoff>(sizeof(std::uint64_t));
                Microsoft::WRL::ComPtr<ID3DBlob> blob;std::uint64_t stored{};
                if(size>0 && SUCCEEDED(D3DCreateBlob(static_cast<SIZE_T>(size),&blob))) {
                    in.seekg(0);
                    if(in.read(static_cast<char*>(blob->GetBufferPointer()),size) && in.read(reinterpret_cast<char*>(&stored),sizeof stored) &&
                       stored==checksum(blob->GetBufferPointer(),static_cast<std::size_t>(size))) {++hits();*out=blob.Detach();return S_OK;}
                }
            }
        }
    } catch(...) {file.clear();}
    ++misses();
    const HRESULT result=D3DCompile(source.data(),source.size(),name,nullptr,nullptr,entry,profile,flags,0,out,errors);
    if(SUCCEEDED(result) && *out && !file.empty()) try {
        std::error_code ec;std::filesystem::create_directories(file.parent_path(),ec);
        auto temporary=file;temporary+=L".tmp"+std::to_wstring(GetCurrentThreadId());
        bool written=false;
        {std::ofstream o(temporary,std::ios::binary|std::ios::trunc);
         const auto* data=static_cast<const char*>((*out)->GetBufferPointer());const auto size=(*out)->GetBufferSize();
         const std::uint64_t sum=checksum(data,size);
         o.write(data,static_cast<std::streamsize>(size));o.write(reinterpret_cast<const char*>(&sum),sizeof sum);o.close();written=o.good();}
        if(written) std::filesystem::rename(temporary,file,ec);
        if(!written || ec) std::filesystem::remove(temporary,ec);
    } catch(...) {}
    return result;
}
} // namespace d2vr::shader_cache
