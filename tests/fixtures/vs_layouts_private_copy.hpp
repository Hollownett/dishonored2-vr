#pragma once
// Lab-only fallback for a private copy of the mod that does not ship the linux/proxy tree (the real reflect() lives there and is used
// whenever ../linux/proxy/vs_layouts.hpp exists; object_motion_check prefers it). It reflects the same information the proxy does: the
// byte k of the returned word is the b2 float4 index of cb_mvpmatrix row k (k 0-3) and cb_previousmvpmatrix row k-4 (k 4-7), 0xff absent.
// v1103: a current-only layout (no cb_previousmvpmatrix) is returned with its four previous bytes 0xff; 0 means no usable b2 layout.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace d2vr::vs_layouts {
inline constexpr std::uint64_t previous_absent=0xffffffff00000000ull;
inline bool has_previous(std::uint64_t layout) noexcept {return layout && (layout&previous_absent)!=previous_absent;}

inline std::uint64_t reflect(const void* code,std::size_t size) noexcept {
    if(!code || !size) return 0;
    ID3D11ShaderReflection* reflection{};
    if(FAILED(D3DReflect(code,size,IID_PPV_ARGS(&reflection))) || !reflection) return 0;
    std::uint64_t layout=~0ull;bool found=false,have_x=false,have_y=false,have_w=false;
    D3D11_SHADER_DESC shader{};
    if(FAILED(reflection->GetDesc(&shader))) {reflection->Release();return 0;}
    for(UINT i=0;i<shader.ConstantBuffers;++i) {
        ID3D11ShaderReflectionConstantBuffer* buffer=reflection->GetConstantBufferByIndex(i);
        D3D11_SHADER_BUFFER_DESC desc{};
        if(!buffer || FAILED(buffer->GetDesc(&desc)) || !desc.Name) continue;
        D3D11_SHADER_INPUT_BIND_DESC bind{};
        if(FAILED(reflection->GetResourceBindingDescByName(desc.Name,&bind)) || bind.BindPoint!=2) continue;
        found=true;
        for(UINT v=0;v<desc.Variables;++v) {
            ID3D11ShaderReflectionVariable* variable=buffer->GetVariableByIndex(v);
            D3D11_SHADER_VARIABLE_DESC info{};
            if(!variable || FAILED(variable->GetDesc(&info)) || !info.Name || info.Size<16) continue;
            const std::string name(info.Name);
            unsigned base{};
            if(name.rfind("cb_previousmvpmatrix",0)==0) base=4;
            else if(name.rfind("cb_mvpmatrix",0)==0) base=0;
            else continue;
            if(name.size()!=(base?21u:13u)) continue;
            const char axis=name.back();
            const unsigned row=axis=='x'?0u:axis=='y'?1u:axis=='z'?2u:axis=='w'?3u:4u;
            if(row>3) continue;
            const unsigned shift=8*(base+row);
            layout=(layout&~(0xffull<<shift))|(std::uint64_t(info.StartOffset/16)<<shift);
            if(base==0) {if(row==0) have_x=true; else if(row==1) have_y=true; else if(row==3) have_w=true;}
        }
    }
    reflection->Release();
    return found && have_x && have_y && have_w?layout:0;
}
} // namespace d2vr::vs_layouts
