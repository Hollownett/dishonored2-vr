// Standalone: FSR3 per-eye upscaler on immediate vs deferred D3D11 contexts.
#include <d3d11_1.h>
#include <d3d11sdklayers.h>
#include <wrl/client.h>
#include <DirectXPackedVector.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <cstdint>
#include "../src/fsr3_eye_upscaler.hpp"

using Microsoft::WRL::ComPtr;
using namespace DirectX::PackedVector;
using d2vr::Fsr3EyeUpscaler;

static ComPtr<ID3D11Device> dev;
static ComPtr<ID3D11DeviceContext> imm;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static ComPtr<ID3D11Texture2D> tex(UINT w, UINT h, DXGI_FORMAT f, UINT bind, const void* data, UINT bpp) {
  D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = d.ArraySize = 1; d.Format = f;
  d.SampleDesc.Count = 1; d.BindFlags = bind;
  D3D11_SUBRESOURCE_DATA sd{data, w * bpp, 0};
  ComPtr<ID3D11Texture2D> t;
  if (FAILED(dev->CreateTexture2D(&d, data ? &sd : nullptr, &t))) std::printf("CreateTexture2D failed\n");
  return t;
}

struct Stats { double mean[3] = {}; int bad = 0; };
static Stats readback(ID3D11Texture2D* t, UINT w, UINT h) {
  D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d); d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> s; dev->CreateTexture2D(&d, nullptr, &s);
  imm->CopyResource(s.Get(), t);
  D3D11_MAPPED_SUBRESOURCE m{}; Stats st;
  if (FAILED(imm->Map(s.Get(), 0, D3D11_MAP_READ, 0, &m))) { st.bad = -1; return st; }
  for (UINT y = 0; y < h; ++y) {
    const auto* row=(const char*)m.pData+y*m.RowPitch;
    auto* p = (const HALF*)row;
    for (UINT x = 0; x < w; ++x) for (int c = 0; c < 3; ++c) {
      float v = d.Format==DXGI_FORMAT_R8G8B8A8_UNORM ? float(reinterpret_cast<const unsigned char*>(row)[x*4+c])/255.f : XMConvertHalfToFloat(p[x * 4 + c]);
      if (!std::isfinite(v) || v < 0) ++st.bad; else st.mean[c] += v;
    }
  }
  imm->Unmap(s.Get(), 0);
  for (double& m2 : st.mean) m2 /= double(w) * h;
  return st;
}

static bool runCase(const char* name, UINT rw, UINT rh, UINT dw, UINT dh, bool deferred,bool padded_ldr=false) {
  const float colours[2][3] = {{0.5f, 0.25f, 0.1f}, {0.1f, 0.25f, 0.5f}};
  const UINT tw=padded_ldr?dw:rw,th=padded_ldr?dh:rh;
  std::vector<HALF> c(size_t(tw) * th * 4), mv(size_t(tw) * th * 2, XMConvertFloatToHalf(0.f));
  std::vector<float> z(size_t(tw) * th);
  for (UINT y = 0; y < rh; ++y) for (UINT x = 0; x < rw; ++x) {
    float g = 0.8f + 0.4f * (float(x) / rw);  // gradient, mean 1.0 * col
    for (int k = 0; k < 3; ++k) c[(y * tw + x) * 4 + k] = XMConvertFloatToHalf(colours[0][k] * g);
    c[(y * tw + x) * 4 + 3] = XMConvertFloatToHalf(1.f);
    z[y * tw + x] = 0.05f + 0.4f * float(y) / rh;  // reversed-Z values
  }
  const int kEyes = 2, kFrames = 8;
  std::vector<Fsr3EyeUpscaler> eyev(kEyes); Fsr3EyeUpscaler* eye = eyev.data();  // context is large: keep off the stack
  ComPtr<ID3D11Texture2D> tc[kEyes], tz[kEyes], tm[kEyes], to[kEyes];
  for (int e = 0; e < kEyes; ++e) {
    Fsr3EyeUpscaler::Desc d; d.render_w = rw; d.render_h = rh; d.display_w = dw; d.display_h = dh; d.debug_checks = true;
    if(padded_ldr) {d.hdr=false;d.auto_exposure=false;}
    if (!eye[e].create(dev.Get(), d)) { std::printf("[%s] create failed eye %d\n", name, e); ++failures; return false; }
    if(e==1) for(UINT y=0;y<rh;++y) for(UINT x=0;x<rw;++x) {
      const float g=0.8f+0.4f*(float(x)/rw);
      for(int k=0;k<3;++k) c[(y*tw+x)*4+k]=XMConvertFloatToHalf(colours[e][k]*g);
    }
    if(padded_ldr) {
      std::vector<unsigned char> ldr(c.size());
      for(size_t i=0;i<c.size();++i) ldr[i]=static_cast<unsigned char>(std::lround(XMConvertHalfToFloat(c[i])*255.f));
      tc[e]=tex(tw,th,DXGI_FORMAT_R8G8B8A8_UNORM,D3D11_BIND_SHADER_RESOURCE,ldr.data(),4);
    } else tc[e] = tex(tw, th, DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_SHADER_RESOURCE, c.data(), 8);
    tz[e] = tex(tw, th, DXGI_FORMAT_R32_FLOAT, D3D11_BIND_SHADER_RESOURCE, z.data(), 4);
    tm[e] = tex(tw, th, DXGI_FORMAT_R16G16_FLOAT, D3D11_BIND_SHADER_RESOURCE, mv.data(), 4);
    to[e] = tex(dw, dh,padded_ldr?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R16G16B16A16_FLOAT, D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE, nullptr, padded_ldr?4:8);
  }
  ComPtr<ID3D11DeviceContext> dc;
  if (deferred) dev->CreateDeferredContext(0, &dc); else dc = imm;
  std::printf("[%s] contexts+resources created\n", name);
  CHECK(eye[0].phase_count()==eye[1].phase_count(), "%s eye jitter phases disagree",name);
  bool allok = true;
  for (int f = 0; f < kFrames; ++f) {
    for (int e = 0; e < kEyes; ++e) {
      Fsr3EyeUpscaler::Inputs in; in.colour = tc[e].Get(); in.depth = tz[e].Get(); in.motion = tm[e].Get(); in.output = to[e].Get();
      in.render_w = rw; in.render_h = rh; in.reset = f == 0; in.cam_near = 0.1f; in.cam_far = 1000.f; in.vfov_rad = 1.6f;
      eye[e].jitter(f, in.jitter_px_x, in.jitter_px_y);
      if (!eye[e].dispatch(dc.Get(), in)) { std::printf("[%s] dispatch failed f%d e%d\n", name, f, e); ++failures; allok = false; }
    }
    if (deferred) {
      ComPtr<ID3D11CommandList> cl;
      HRESULT hr = dc->FinishCommandList(FALSE, &cl);
      CHECK(SUCCEEDED(hr), "FinishCommandList 0x%08x", (unsigned)hr);
      if (cl) imm->ExecuteCommandList(cl.Get(), FALSE);
    }
  }
  for (int e = 0; e < kEyes; ++e) {
    const float* col=colours[e];
    Stats s = readback(to[e].Get(), dw, dh);
    float jx, jy; eye[e].jitter(3, jx, jy);
    std::printf("[%s %ux%u->%ux%u eye%d phases=%d jit3=(%.3f,%.3f)] mean rgb=(%.4f %.4f %.4f) expect=(%.2f %.2f %.2f) bad=%d\n",
                name, rw, rh, dw, dh, e, eye[e].phase_count(), jx, jy, s.mean[0], s.mean[1], s.mean[2], col[0], col[1], col[2], s.bad);
    bool ok = s.bad == 0;
    for (int k = 0; k < 3; ++k) ok = ok && std::fabs(s.mean[k] - col[k]) < 0.25 * col[k];
    CHECK(ok, "%s eye%d output implausible", name, e);
    allok = allok && ok;
  }
  // Rebuild an existing context at a new render size, then restore native mode; reject oversized jobs.
  for(int e=0;e<kEyes;++e) {
    Fsr3EyeUpscaler::Desc d;d.render_w=rw/2;d.render_h=rh/2;d.display_w=dw;d.display_h=dh;d.debug_checks=true;
    if(padded_ldr) {d.hdr=false;d.auto_exposure=false;}
    CHECK(eye[e].create(dev.Get(),d), "%s resize failed eye%d",name,e);
    CHECK(eye[e].create(dev.Get(),d), "%s unchanged descriptor failed eye%d",name,e);
    Fsr3EyeUpscaler::Inputs invalid;invalid.colour=tc[e].Get();invalid.depth=tz[e].Get();invalid.motion=tm[e].Get();invalid.output=to[e].Get();
    invalid.render_w=rw;invalid.render_h=rh;
    CHECK(!eye[e].dispatch(imm.Get(),invalid), "%s accepted oversized render dispatch eye%d",name,e);
    d.render_w=rw;d.render_h=rh;
    CHECK(eye[e].create(dev.Get(),d), "%s restore size failed eye%d",name,e);
    auto unsafe_output=tex(dw,dh,padded_ldr?DXGI_FORMAT_R8G8B8A8_UNORM:DXGI_FORMAT_R16G16B16A16_FLOAT,D3D11_BIND_UNORDERED_ACCESS,nullptr,padded_ldr?4:8);
    invalid.output=unsafe_output.Get();
    CHECK(!eye[e].dispatch(imm.Get(),invalid), "%s accepted output lacking SRV bind eye%d",name,e);
    invalid.output=to[e].Get();
    invalid.reset=true;
    CHECK(eye[e].dispatch(imm.Get(),invalid), "%s reset after resize failed eye%d",name,e);
  }
  return allok;
}

static LONG WINAPI filt(EXCEPTION_POINTERS* e) {
  HMODULE m = nullptr; GetModuleHandleExA(6, (LPCSTR)e->ExceptionRecord->ExceptionAddress, &m); char n[260] = {}; GetModuleFileNameA(m, n, 260);
  std::printf("CRASH 0x%08lx at %p (%s) rva 0x%llx\n", e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, n, (unsigned long long)((char*)e->ExceptionRecord->ExceptionAddress - (char*)m));
  return EXCEPTION_EXECUTE_HANDLER;
}
int main() {
  SetUnhandledExceptionFilter(filt);
  setvbuf(stdout, nullptr, _IONBF, 0);
  D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0}, got{};
  UINT flags = D3D11_CREATE_DEVICE_DEBUG;
  HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, want, 2, D3D11_SDK_VERSION, &dev, &got, &imm);
  bool dbg = SUCCEEDED(hr);
  if (!dbg) hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, want, 2, D3D11_SDK_VERSION, &dev, &got, &imm);
  if (FAILED(hr)) { std::printf("no D3D11 device\n"); return 2; }
  std::printf("device FL=0x%x debug_layer=%d\n", (unsigned)got, (int)dbg);
  runCase("imm", 1440, 1584, 1920, 2112, false);
  runCase("deferred", 1440, 1584, 1920, 2112, true);
  runCase("imm-native", 1920, 2112, 1920, 2112, false);
  runCase("deferred-native", 1920, 2112, 1920, 2112, true);
  runCase("quality-77pct", 1478, 1626, 1920, 2112, false);
  runCase("balanced-67pct", 1286, 1415, 1920, 2112, false);
  runCase("quality-padded-ldr", 1478, 1626, 1920, 2112, false,true);
  runCase("balanced-padded-ldr", 1286, 1415, 1920, 2112, false,true);
  imm->Flush();
  ComPtr<ID3D11InfoQueue> iq;
  if (dbg && SUCCEEDED(dev.As(&iq))) {
    UINT64 n = iq->GetNumStoredMessages(); std::printf("debug layer messages: %llu\n", n);
    for (UINT64 i = 0; i < n; ++i) {
      SIZE_T len = 0; iq->GetMessage(i, nullptr, &len);
      std::vector<char> b(len); auto* m = (D3D11_MESSAGE*)b.data();
      if (SUCCEEDED(iq->GetMessage(i, m, &len))) {
        if(i<15) std::printf("  [sev %d id %d] %s\n", (int)m->Severity, (int)m->ID, m->pDescription);
        if(m->Severity==D3D11_MESSAGE_SEVERITY_ERROR || m->Severity==D3D11_MESSAGE_SEVERITY_CORRUPTION) ++failures;
      }
    }
  }
  std::printf(failures ? "FAILED (%d)\n" : "PASS\n", failures);
  return failures ? 1 : 0;
}
