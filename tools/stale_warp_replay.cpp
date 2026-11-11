// v1152 offline replay of stale-eye warp dumps (capture_stale_warp in src/dxgi_probe.cpp) through the shipped shader
// (src/stale_eye_warp.hpp, StaleEyeWarp::apply) on WARP, with the mover rules off and on, scored against the eye's own neighbours.
//
//   stale_warp_replay <dump dir> <out dir> <camera probe csv> [N ...]
//
// Inputs are rebuilt in FULL-SIZE textures (the dump's image size); each crop is placed at its box, so every layer matrix from the
// meta is used unchanged and the shader runs exactly as in the game. Outside the crops the images are black and the depths 0 (sky):
// scores use a margin (48 px) inside the kept box. Not in the dump and derived here:
//  - each hand layer's back matrix (the hand fit): the inverse of its world homography, back_shift = -back * shift (exact for a rigid layer);
//  - kept_axes (world -> kept camera): yaw from the game camera probe (dxgi-camera-probe.csv, nearest present; the probe camera is level),
//    pitch fitted so the kept movers' world motion (xyz) projects to their pixel motion (w); no roll (the eye views have none);
//  - the target rotation homography h: unused when depth is on.
// Per dump: replay rules on (vs the dumped -warped.rgba), rules off; byte identity off/on; flicker F = (|R-K|+|R-X|-|X-K|)/2 per pixel
// (mean absolute RGB levels), K kept, X next, R rebuilt; PNG sheets (kept | off | on | next | F off-on: green on better, red worse) and a
// path map from a debug build of the same shader source (REPLAY_PATHS, text hooks applied here only): green M1 own shading, cyan M1 edge carried, yellow M1
// fresh colour, blue M3, red M2-cleared (a kept mover's old place, shown by the fallbacks), grey the rest.
#define _CRT_SECURE_NO_WARNINGS  // std::getenv
#include "../src/stale_eye_warp.hpp"
#include <cctype>
#include <chrono>
#include <iterator>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <map>
#include <vector>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using namespace d2vr;
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(0)

// The pixel shader of a StaleEyeWarp (private) through the explicit-instantiation access rule, for the path-map build only.
ComPtr<ID3D11PixelShader>& pixel_of(StaleEyeWarp&);
template<ComPtr<ID3D11PixelShader> StaleEyeWarp::*M> struct PixelAccess {friend ComPtr<ID3D11PixelShader>& pixel_of(StaleEyeWarp& w) {return w.*M;}};
template struct PixelAccess<&StaleEyeWarp::pixel_>;

// ---- a small JSON reader (the meta files) ----
struct Json {
    enum Kind {null, number, boolean, string, array, object} kind{null};
    double n{}; bool b{}; std::string s; std::vector<Json> a; std::vector<std::pair<std::string, Json>> o;
    const Json& operator[](const char* key) const {for (const auto& p : o) if (p.first == key) return p.second; static const Json none; return none;}
    double at(std::size_t i) const {return a.at(i).n;}
    bool has(const char* key) const {return (*this)[key].kind != null;}
};
static Json parse(const char*& p) {
    while (std::isspace(static_cast<unsigned char>(*p))) ++p;
    Json j;
    if (*p == '{') {
        j.kind = Json::object; ++p;
        for (;;) {
            while (std::isspace(static_cast<unsigned char>(*p)) || *p == ',') ++p;
            if (*p == '}') {++p; return j;}
            const Json key = parse(p); while (std::isspace(static_cast<unsigned char>(*p)) || *p == ':') ++p;
            j.o.emplace_back(key.s, parse(p));
        }
    }
    if (*p == '[') {
        j.kind = Json::array; ++p;
        for (;;) {while (std::isspace(static_cast<unsigned char>(*p)) || *p == ',') ++p; if (*p == ']') {++p; return j;} j.a.push_back(parse(p));}
    }
    if (*p == '"') {j.kind = Json::string; ++p; while (*p && *p != '"') j.s += *p++; ++p; return j;}
    if (!std::strncmp(p, "true", 4)) {j.kind = Json::boolean; j.b = true; p += 4; return j;}
    if (!std::strncmp(p, "false", 5)) {j.kind = Json::boolean; p += 5; return j;}
    if (!std::strncmp(p, "null", 4)) {p += 4; return j;}
    char* end{}; j.kind = Json::number; j.n = std::strtod(p, &end); CHECK(end != p); p = end; return j;
}
static std::vector<unsigned char> load(const std::string& path) {
    std::ifstream f(path, std::ios::binary); CHECK(f);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), {});
}
static float half_to_float(std::uint16_t h) {
    const std::uint32_t sign = (h & 0x8000u) << 16, e = (h >> 10) & 31u, m = h & 0x3ffu;
    std::uint32_t x;
    if (!e) {if (!m) x = sign; else {int s = -1; std::uint32_t mm = m; do {++s; mm <<= 1;} while (!(mm & 0x400u)); x = sign | ((127 - 15 - s) << 23) | ((mm & 0x3ffu) << 13);}}
    else if (e == 31) x = sign | 0x7f800000u | (m << 13);
    else x = sign | ((e + 112) << 23) | (m << 13);
    float f; std::memcpy(&f, &x, 4); return f;
}

// ---- PNG (stored deflate: no compression library needed) ----
static std::uint32_t crc(const unsigned char* d, std::size_t n, std::uint32_t c = 0xffffffffu) {
    static std::uint32_t table[256]; static bool ready = false;
    if (!ready) {for (std::uint32_t i = 0; i < 256; ++i) {std::uint32_t r = i; for (int k = 0; k < 8; ++k) r = r & 1 ? 0xedb88320u ^ (r >> 1) : r >> 1; table[i] = r;} ready = true;}
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xff] ^ (c >> 8);
    return c;
}
static void write_png(const std::string& path, int w, int h, const std::vector<unsigned char>& rgb) {
    std::vector<unsigned char> raw; raw.reserve(static_cast<std::size_t>(h) * (w * 3 + 1));
    for (int y = 0; y < h; ++y) {raw.push_back(0); raw.insert(raw.end(), rgb.begin() + static_cast<std::ptrdiff_t>(y) * w * 3, rgb.begin() + static_cast<std::ptrdiff_t>(y + 1) * w * 3);}
    std::vector<unsigned char> z{0x78, 0x01};
    for (std::size_t at = 0; at < raw.size(); at += 65535) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        z.push_back(at + n == raw.size() ? 1 : 0); z.push_back(n & 0xff); z.push_back((n >> 8) & 0xff); z.push_back(~n & 0xff); z.push_back((~n >> 8) & 0xff);
        z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(at), raw.begin() + static_cast<std::ptrdiff_t>(at + n));
    }
    std::uint32_t s1 = 1, s2 = 0; for (unsigned char c : raw) {s1 = (s1 + c) % 65521; s2 = (s2 + s1) % 65521;}
    const std::uint32_t adler = (s2 << 16) | s1; for (int i = 3; i >= 0; --i) z.push_back((adler >> (i * 8)) & 0xff);
    std::ofstream f(path, std::ios::binary); CHECK(f);
    const auto be = [&](std::uint32_t v) {unsigned char b[4]{static_cast<unsigned char>(v >> 24), static_cast<unsigned char>(v >> 16), static_cast<unsigned char>(v >> 8), static_cast<unsigned char>(v)}; f.write(reinterpret_cast<char*>(b), 4);};
    const auto chunk = [&](const char* type, const std::vector<unsigned char>& data) {
        be(static_cast<std::uint32_t>(data.size())); std::vector<unsigned char> t(type, type + 4); t.insert(t.end(), data.begin(), data.end());
        f.write(reinterpret_cast<const char*>(t.data()), static_cast<std::streamsize>(t.size())); be(crc(t.data(), t.size()) ^ 0xffffffffu);
    };
    const unsigned char sig[8]{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'}; f.write(reinterpret_cast<const char*>(sig), 8);
    std::vector<unsigned char> ihdr(13, 0);
    for (int i = 0; i < 4; ++i) {ihdr[i] = (w >> (24 - 8 * i)) & 0xff; ihdr[4 + i] = (h >> (24 - 8 * i)) & 0xff;}
    ihdr[8] = 8; ihdr[9] = 2; chunk("IHDR", ihdr); chunk("IDAT", z); chunk("IEND", {});
}

// ---- the shader source as shipped, for the path-map variant ----
static std::string shader_source(const std::string& header) {
    const auto text = load(header); const std::string s(text.begin(), text.end());
    std::size_t at = s.find("const std::string source=R\"HLSL(");
    CHECK(at != std::string::npos); at = s.find("R\"HLSL(", at);
    std::string out;
    while (at != std::string::npos && !s.compare(at, 7, "R\"HLSL(")) {
        const std::size_t begin = at + 7, end = s.find(")HLSL\"", begin); CHECK(end != std::string::npos);
        out += s.substr(begin, end - begin);
        at = end + 6; while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) ++at;
    }
    return out;
}
static void hook(std::string& s, const std::string& from, const std::string& to) {
    const std::size_t at = s.find(from);
    if (at == std::string::npos || s.find(from, at + 1) != std::string::npos) {std::fprintf(stderr, "path hook not unique: %s\n", from.c_str()); std::exit(1);}
    s.replace(at, from.size(), to);
}
static std::string path_variant(std::string s) {
    hook(s, "float4 WarpPS(Vertex v) : SV_TARGET {", "static bool replayM2;\nfloat4 WarpCore(Vertex v) {");
    hook(s, "if(kept_mover && ca[0]!=-2) ca[0]=-1;", "if(kept_mover && ca[0]!=-2) ca[0]=-1;\n#ifdef REPLAY_PATHS\n    replayM2=kept_mover;\n#endif");
    hook(s, "?Smooth(sourceTex,q):Smooth(freshTex,p);", "?float4(Smooth(sourceTex,q).rgb,2):float4(Smooth(freshTex,p).rgb,3);");
    hook(s, "colour=Smooth(sourceTex,c);", "colour=float4(Smooth(sourceTex,c).rgb,4);");
    s = "#define REPLAY_PATHS 1\n" + s + R"(
float4 WarpPS(Vertex v) : SV_TARGET {
    replayM2=false;
    const float4 c=WarpCore(v);
    const float g=0.3*dot(c.rgb,float3(0.2126,0.7152,0.0722));
    if(c.a>4.5) return float4(0,1,1,1);      // M1, an anti-aliased edge carried with its mover
    if(c.a>3.5) return float4(0,0.35,1,1);   // M3
    if(c.a>2.5) return float4(1,0.9,0,1);    // M1, fresh colour
    if(c.a>1.5) return float4(0,1,0.2,1);    // M1, own shading
    if(replayM2) return float4(1,0,0,1);     // M2: a kept mover's old place, the fallbacks
    return float4(g,g,g,1);
}
)";
    return s;
}


// STALE_REPLAY_REASONS=1: which early return of MoverSeen (red, level 20*(i+1)) and MoverHidden (green, level 20*(i+1)) each pixel took
// (255: it drew; 0: not called), with the shader's selection unchanged, for debugging the rules.
static std::string reasons_variant(std::string s) {
    for (const char* name : {"float MoverSeen(float3 t,out float4 colour) {", "float MoverHidden(float3 t,float2 q,out float4 colour) {"}) {
        const std::size_t a = s.find(name), b = s.find("\n}", a);
        CHECK(a != std::string::npos && b != std::string::npos);
        std::string f = s.substr(a, b - a); int i = 0; std::size_t at = 0;
        while ((at = f.find("return 1e38;", at)) != std::string::npos) {const std::string r = "{colour=float4(0,0,0," + std::to_string(1 + i++) + ");return 1e38;}"; f.replace(at, 12, r); at += r.size();}
        s.replace(a, b - a, f);
    }
    hook(s, "float4 WarpPS(Vertex v) : SV_TARGET {", "static float replaySeen,replayHidden;\nfloat4 WarpCore(Vertex v) {");
    hook(s, "mover_k=MoverSeen(t,mover_colour);", "mover_k=MoverSeen(t,mover_colour);replaySeen=mover_k<1e37?12.75:mover_colour.a;");
    hook(s, "mover_k=MoverHidden(t,q,mover_colour);", "mover_k=MoverHidden(t,q,mover_colour);replayHidden=mover_k<1e37?12.75:mover_colour.a;");
    return s + R"(
float4 WarpPS(Vertex v) : SV_TARGET {
    replaySeen=0;replayHidden=0;
    WarpCore(v);
    return float4(pow(saturate(replaySeen*20/255),2.2),pow(saturate(replayHidden*20/255),2.2),0,1);
}
)";
}
// STALE_REPLAY_EXITS=1: which return of WarpPS painted each pixel (written as -exits.rgba: red = 10 * (index + 1), in source order).
static std::string exits_variant(std::string s) {
    const std::string head = "float4 WarpPS(Vertex v) : SV_TARGET {";
    const std::size_t a = s.find(head), b = s.find("\n}", a);
    CHECK(a != std::string::npos && b != std::string::npos);
    std::string f = s.substr(a, b - a); int i = 0; std::size_t at = head.size();
    while ((at = f.find("return ", at)) != std::string::npos) {
        const std::size_t end = f.find(';', at);
        const std::string expr = f.substr(at + 7, end - at - 7);
        const std::string r = "{replayExit=" + std::to_string(++i) + ";return " + expr + ";}";
        f.replace(at, end - at + 1, r); at += r.size();
    }
    f.replace(0, head.size(), "static int replayExit;\nfloat4 WarpCore(Vertex v) {");
    s.replace(a, b - a, f);
    return s + R"(
float4 WarpPS(Vertex v) : SV_TARGET {
    replayExit=0;WarpCore(v);
    const float e=pow(saturate(replayExit*10/255.0),2.2);
    return float4(e,e,e,1);
}
)";
}
struct Box {int x{}, y{}, w{}, h{};};
static Box box_of(const Json& j) {return {static_cast<int>(j.at(0)), static_cast<int>(j.at(1)), static_cast<int>(j.at(2)), static_cast<int>(j.at(3))};}

int main(int argc, char** argv) {
    if (argc < 4) {std::fprintf(stderr, "usage: stale_warp_replay <dump dir> <out dir> <camera probe csv> [N ...]\n"); return 2;}
    const std::string dir = argv[1], out = argv[2], probe_path = argv[3];
    std::vector<int> dumps; for (int i = 4; i < argc; ++i) dumps.push_back(std::atoi(argv[i]));
    if (dumps.empty()) for (int n = 1; n <= 12; ++n) if (std::ifstream(dir + "/stale-warp-" + std::to_string(n) + "-meta.json")) dumps.push_back(n);
    // the game camera probe: present -> forward, left, up rows (engine world)
    std::map<long long, std::array<double, 9>> probe;
    {
        std::ifstream f(probe_path); CHECK(f); std::string line; std::getline(f, line);
        while (std::getline(f, line)) {
            std::stringstream ss(line); std::string cell; std::vector<double> v; while (std::getline(ss, cell, ',')) v.push_back(std::atof(cell.c_str()));
            if (v.size() >= 20) {std::array<double, 9> b{}; for (int i = 0; i < 9; ++i) b[i] = v[11 + i]; probe[static_cast<long long>(v[0])] = b;}
        }
        CHECK(!probe.empty());
    }
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    StaleEyeWarp warp(device.Get());
    ComPtr<ID3D11PixelShader> shipped = pixel_of(warp), paths;
    {
        const std::string src = path_variant(shader_source("src/stale_eye_warp.hpp"));
        ComPtr<ID3DBlob> blob, errors;
        if (FAILED(D3DCompile(src.data(), src.size(), "replay paths", nullptr, nullptr, "WarpPS", "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors))) {
            std::fprintf(stderr, "path variant: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "compile failed"); return 1;
        }
        CHECK(SUCCEEDED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &paths)));
    }
    std::printf("dump  match1  match4   off==on  F(game) F(off) F(on) | movers: F(off) F(on) | rest: F(off) F(on) | movers beyond reach  pitch  fit\n");
    for (const int n : dumps) {
        const std::string stem = dir + "/stale-warp-" + std::to_string(n) + "-";
        const auto meta_text = load(stem + "meta.json"); std::string meta_s(meta_text.begin(), meta_text.end()); const char* mp = meta_s.c_str();
        const Json meta = parse(mp);
        const int W = static_cast<int>(meta["image"].at(0)), H = static_cast<int>(meta["image"].at(1));
        const Box kb = box_of(meta["box"]), fb = box_of(meta["fresh_box"]), kdb = box_of(meta["kept_depth_box"]), fdb = box_of(meta["fresh_depth_box"]);
        const int kdw = static_cast<int>(meta["kept_depth_region"].at(0)), kdh = static_cast<int>(meta["kept_depth_region"].at(1));
        const int fdw = static_cast<int>(meta["fresh_depth_region"].at(0)), fdh = static_cast<int>(meta["fresh_depth_region"].at(1));
        // v1159: depth, motion and rig are render-sized (an upscaled session renders smaller than the eye image): each in its own region
        const auto kept = load(stem + "kept.rgba"), warped = load(stem + "warped.rgba"), next = load(stem + "next.rgba"), fresh = load(stem + "fresh.rgba");
        std::vector<unsigned char> kr, fr;  // v1159: the rig masks (R8, under the depth boxes)
        const bool rig = std::ifstream(stem + "kept-rig.r8").good() && std::ifstream(stem + "fresh-rig.r8").good();
        if (rig) {kr = load(stem + "kept-rig.r8"); fr = load(stem + "fresh-rig.r8");
                  CHECK(kr.size() == static_cast<std::size_t>(kdb.w) * kdb.h && fr.size() == static_cast<std::size_t>(fdb.w) * fdb.h);}
        // STALE_REPLAY_AB=rig: compare without the rig masks (off: v1158's palm spheres) and with them (on), the mover rules on in both
        const char* ab = std::getenv("STALE_REPLAY_AB");
        const bool ab_rig = rig && ab && std::string(ab) == "rig";
        const bool ab_rig_motion = rig && ab && std::string(ab) == "rigmotion";  // v1167: off = the rigid hand layers, on = rig motion (masks on)
        const bool rig_live = rig && meta.has("rig_mask") && meta["rig_mask"].a.size() == 2 && meta["rig_mask"].a[0].b && meta["rig_mask"].a[1].b;
        const auto kd = load(stem + "kept-depth.r32"), fd = load(stem + "fresh-depth.r32");
        std::vector<unsigned char> km, fm;
        const bool motion = std::ifstream(stem + "kept-motion.rgba16f").good() && std::ifstream(stem + "fresh-motion.rgba16f").good();
        if (motion) {km = load(stem + "kept-motion.rgba16f"); fm = load(stem + "fresh-motion.rgba16f");}
        CHECK(kept.size() == static_cast<std::size_t>(kb.w) * kb.h * 4 && fresh.size() == static_cast<std::size_t>(fb.w) * fb.h * 4);
        CHECK(kd.size() == static_cast<std::size_t>(kdb.w) * kdb.h * 4 && fd.size() == static_cast<std::size_t>(fdb.w) * fdb.h * 4);
        // full-size textures, each crop at its box
        const auto place = [&](const std::vector<unsigned char>& crop, const Box& b, int bpp, DXGI_FORMAT format, int RW, int RH) {
            std::vector<unsigned char> full(static_cast<std::size_t>(RW) * RH * bpp, 0);
            for (int y = 0; y < b.h; ++y) std::memcpy(&full[(static_cast<std::size_t>(b.y + y) * RW + b.x) * bpp], &crop[static_cast<std::size_t>(y) * b.w * bpp], static_cast<std::size_t>(b.w) * bpp);
            D3D11_TEXTURE2D_DESC d{}; d.Width = RW; d.Height = RH; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Format = format; d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            const D3D11_SUBRESOURCE_DATA data{full.data(), static_cast<UINT>(RW * bpp), 0};
            ComPtr<ID3D11Texture2D> t; CHECK(SUCCEEDED(device->CreateTexture2D(&d, &data, &t))); return t;
        };
        const auto kept_t = place(kept, kb, 4, DXGI_FORMAT_R8G8B8A8_TYPELESS, W, H), fresh_t = place(fresh, fb, 4, DXGI_FORMAT_R8G8B8A8_TYPELESS, W, H);
        const auto kd_t = place(kd, kdb, 4, DXGI_FORMAT_R32_FLOAT, kdw, kdh), fd_t = place(fd, fdb, 4, DXGI_FORMAT_R32_FLOAT, fdw, fdh);
        ComPtr<ID3D11Texture2D> kr_t, fr_t;
        if (rig) {kr_t = place(kr, kdb, 1, DXGI_FORMAT_R8_UNORM, kdw, kdh); fr_t = place(fr, fdb, 1, DXGI_FORMAT_R8_UNORM, fdw, fdh);}
        ComPtr<ID3D11Texture2D> km_t, fm_t;
        // STALE_REPLAY_MOVER_PX=v: replay as if mover_px were v (w is only compared with mover_px: scale w by mover_px / v)
        const char* threshold = std::getenv("STALE_REPLAY_MOVER_PX");
        const auto scaled = [&](std::vector<unsigned char> m) {
            if (!threshold) return m;
            const float k = static_cast<float>(mover_px / std::atof(threshold));
            for (std::size_t i = 0; i + 8 <= m.size(); i += 8) {
                std::uint16_t h; std::memcpy(&h, &m[i + 6], 2); float f = half_to_float(h) * k, ff = f;
                std::uint32_t x; std::memcpy(&x, &ff, 4); const int e = static_cast<int>((x >> 23) & 0xffu) - 127 + 15;
                h = e <= 0 ? 0 : e >= 31 ? 0x7c00 : static_cast<std::uint16_t>((e << 10) | ((x & 0x7fffffu) >> 13));
                std::memcpy(&m[i + 6], &h, 2);
            }
            return m;
        };
        if (motion) {km_t = place(scaled(km), kdb, 8, DXGI_FORMAT_R16G16B16A16_FLOAT, kdw, kdh); fm_t = place(scaled(fm), fdb, 8, DXGI_FORMAT_R16G16B16A16_FLOAT, fdw, fdh);}
        // the layers
        const auto parallax = [&](const Json& j) {
            StaleParallax p{};
            for (int i = 0; i < 9; ++i) p.world[i] = j["world"].at(i);
            for (int i = 0; i < 3; ++i) p.shift[i] = j["shift"].at(i);
            p.m10 = j["m10"].n; p.m11 = j["m11"].n;
            const auto& m = p.world;  // back = inverse(world), back_shift = -back * shift
            const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
            p.back = {(m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
                      (m[5] * m[6] - m[3] * m[8]) / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
                      (m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
            for (int i = 0; i < 3; ++i) p.back_shift[i] = -(p.back[i * 3] * p.shift[0] + p.back[i * 3 + 1] * p.shift[1] + p.back[i * 3 + 2] * p.shift[2]);
            return p;
        };
        StaleLayers layers{};
        layers.world = parallax(meta["world"]); layers.fresh = parallax(meta["fresh"]);
        if (meta.has("carried")) layers.carried = parallax(meta["carried"]);
        if (meta.has("hand0")) layers.hand[0] = parallax(meta["hand0"]);
        if (meta.has("hand1")) layers.hand[1] = parallax(meta["hand1"]);
        layers.kept_fov = {meta["kept_fov"].at(0), meta["kept_fov"].at(1), meta["kept_fov"].at(2), meta["kept_fov"].at(3)};
        layers.near_range = meta["near_range"].n; layers.near_from_kept = meta["near_from_kept"].b;
        for (int i = 0; i < 3; ++i) layers.start[i] = meta["start"].at(i);
        for (int s = 0; s < 2; ++s) for (int i = 0; i < 4; ++i) layers.palms[s][i] = meta["palms"].a.at(s).at(i);
        layers.fresh_part = meta["fresh_part"].n; layers.kept_part = meta["kept_part"].n;
        if (meta.has("rig_motion") && meta["rig_motion"].a.size() == 2) layers.rig_motion = {meta["rig_motion"].a[0].b, meta["rig_motion"].a[1].b};  // v1167
        layers.frame = static_cast<std::uint64_t>(meta["frame"].n); layers.eye = static_cast<unsigned>(meta["eye"].n);
        {   // v1171: the cut decision of stale_eye_layers from the dumped world layer (world = I m A, shift = I step; same FOV both sides)
            const auto I = warp_image_matrix(layers.kept_fov);
            const auto inv = [](const WarpMatrix& m) {
                const double det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
                return WarpMatrix{(m[4] * m[8] - m[5] * m[7]) / det, (m[2] * m[7] - m[1] * m[8]) / det, (m[1] * m[5] - m[2] * m[4]) / det,
                                  (m[5] * m[6] - m[3] * m[8]) / det, (m[0] * m[8] - m[2] * m[6]) / det, (m[2] * m[3] - m[0] * m[5]) / det,
                                  (m[3] * m[7] - m[4] * m[6]) / det, (m[1] * m[6] - m[0] * m[7]) / det, (m[0] * m[4] - m[1] * m[3]) / det};
            };
            const auto mul = [](const WarpMatrix& x, const WarpMatrix& y) {WarpMatrix r{}; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) r[i * 3 + j] += x[i * 3 + k] * y[k * 3 + j]; return r;};
            const auto A = inv(I); const auto m = mul(mul(A, layers.world.world), I);
            const auto& sh = layers.world.shift;
            const double sx = A[0] * sh[0] + A[1] * sh[1] + A[2] * sh[2], sy = A[3] * sh[0] + A[4] * sh[1] + A[5] * sh[2], sz = A[6] * sh[0] + A[7] * sh[1] + A[8] * sh[2];
            const double step = std::sqrt(sx * sx + sy * sy + sz * sz), turn = std::acos(std::clamp(m[8], -1.0, 1.0)) * 180 / 3.14159265358979;
            layers.cut = std::getenv("STALE_REPLAY_NO_CUT") ? false : (step > cut_step || (step > cut_turn_step && turn > cut_turn_deg));
            std::fprintf(stderr, "  dump %d: step %.3f m, turn %.1f deg%s\n", n, step, turn, layers.cut ? ": CUT" : "");
            // v1175: the motion flags the game sets in stale_eye_layers(). The replay builds its layers from the meta, so it set none and
            // never ran the v1173-v1175 rules. STALE_REPLAY_MOVING=0: none (pre-v1173), 1: hands only (v1173/v1174), 2 or unset: as the game.
            const char* mv = std::getenv("STALE_REPLAY_MOVING"); const int moving = mv ? std::atoi(mv) : 2;
            const bool hands = !layers.near_from_kept;  // the game's hands-still test at dump time (both hands: the meta has no per-hand flag)
            layers.hand_moving = {moving >= 1 && hands, moving >= 1 && hands};
            layers.camera_moving = moving >= 2 && !(step <= camera_still_m && turn <= camera_still_deg);
            std::fprintf(stderr, "  dump %d: hands moving %d, camera moving %d\n", n, int(layers.hand_moving[0]), int(layers.camera_moving));
        }
        // kept_axes: the probe's yaw, the fitted pitch
        const double L = std::tan(layers.kept_fov.left), R = std::tan(layers.kept_fov.right), U = std::tan(layers.kept_fov.up), D = std::tan(layers.kept_fov.down);
        auto nearest = probe.lower_bound(static_cast<long long>(meta["frame"].n));
        if (nearest == probe.end() || (nearest != probe.begin() && std::abs(std::prev(nearest)->first - meta["frame"].n) < std::abs(nearest->first - meta["frame"].n))) --nearest;
        const auto& b = nearest->second;
        const Vec3 fwd0{b[0], b[1], b[2]}, left{b[3], b[4], b[5]}, up0{b[6], b[7], b[8]};
        const auto axes_at = [&](double pitch) {
            const Vec3 f = fwd0 * std::cos(pitch) + up0 * std::sin(pitch), u = up0 * std::cos(pitch) - fwd0 * std::sin(pitch);
            return WarpMatrix{-left.x, -left.y, -left.z, u.x, u.y, u.z, f.x, f.y, f.z};
        };
        struct Sample {Vec3 p, xyz; double w;};
        std::vector<Sample> samples;
        if (motion) for (int y = 0; y < kdb.h; y += 2) for (int x = 0; x < kdb.w; x += 2) {
            const std::size_t i = static_cast<std::size_t>(y) * kdb.w + x;
            std::uint16_t m[4]; std::memcpy(m, &km[i * 8], 8);
            const double w = half_to_float(m[3]); float depth; std::memcpy(&depth, &kd[i * 4], 4);
            if (!(w >= mover_px) || !(depth > 1e-7f)) continue;
            const double a = layers.world.m11 / (depth + layers.world.m10), u = (kdb.x + x + 0.5) / kdw, v = (kdb.y + y + 0.5) / kdh;
            samples.push_back({{(L + (R - L) * u) * a, (U + (D - U) * v) * a, a}, {half_to_float(m[0]), half_to_float(m[1]), half_to_float(m[2])}, w});
        }
        double pitch = 0, best_cost = -1, fit_error = 0;
        for (double deg = -40; deg <= 40; deg += 0.25) {
            const auto ax = axes_at(deg * 3.14159265358979 / 180);
            double cost = 0;
            for (const auto& s : samples) {
                const Vec3 d{ax[0] * s.xyz.x + ax[1] * s.xyz.y + ax[2] * s.xyz.z, ax[3] * s.xyz.x + ax[4] * s.xyz.y + ax[5] * s.xyz.z, ax[6] * s.xyz.x + ax[7] * s.xyz.y + ax[8] * s.xyz.z};
                const Vec3 q = s.p - d;
                if (!(q.z > 0.01)) {cost += s.w * s.w; continue;}
                const double du = (s.p.x / s.p.z - q.x / q.z) / (R - L) * kdw, dv = (s.p.y / s.p.z - q.y / q.z) / (D - U) * kdh;
                const double e = std::sqrt(du * du + dv * dv) - s.w; cost += e * e;
            }
            if (best_cost < 0 || cost < best_cost) {best_cost = cost; pitch = deg;}
        }
        if (!samples.empty()) fit_error = std::sqrt(best_cost / samples.size()); else pitch = 0;
        layers.kept_axes = axes_at(pitch * 3.14159265358979 / 180);
        // replays
        const WarpMatrix id{1, 0, 0, 0, 1, 0, 0, 0, 1};
        const auto crop = [&](ID3D11Texture2D* t) {
            D3D11_TEXTURE2D_DESC d{}; t->GetDesc(&d); d.Usage = D3D11_USAGE_STAGING; d.BindFlags = 0; d.CPUAccessFlags = D3D11_CPU_ACCESS_READ; d.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> s; CHECK(SUCCEEDED(device->CreateTexture2D(&d, nullptr, &s))); context->CopyResource(s.Get(), t);
            D3D11_MAPPED_SUBRESOURCE m{}; CHECK(SUCCEEDED(context->Map(s.Get(), 0, D3D11_MAP_READ, 0, &m)));
            std::vector<unsigned char> c(static_cast<std::size_t>(kb.w) * kb.h * 4);
            for (int y = 0; y < kb.h; ++y) std::memcpy(&c[static_cast<std::size_t>(y) * kb.w * 4], static_cast<const unsigned char*>(m.pData) + static_cast<std::size_t>(kb.y + y) * m.RowPitch + kb.x * 4, static_cast<std::size_t>(kb.w) * 4);
            context->Unmap(s.Get(), 0); return c;
        };
        const StaleRig kept_rig = rig ? StaleRig{kr_t.Get(), static_cast<UINT>(kdw), static_cast<UINT>(kdh)} : StaleRig{};
        const StaleRig fresh_rig = rig ? StaleRig{fr_t.Get(), static_cast<UINT>(fdw), static_cast<UINT>(fdh)} : StaleRig{};
        const auto run = [&](bool variant) {
            const bool rules = ab_rig || ab_rig_motion ? true : variant, masks = ab_rig ? variant : rig_live;  // v1160 stencils were not the rig: masks where the meta says rig_mask
            layers.rig_motion_rules = ab_rig_motion ? variant : true;
            layers.motion_rules = rules;
            const auto t0 = std::chrono::steady_clock::now();
            auto c = crop(warp.apply(kept_t.Get(), id, &layers, {kd_t.Get(), static_cast<UINT>(kdw), static_cast<UINT>(kdh)}, fresh_t.Get(), {fd_t.Get(), static_cast<UINT>(fdw), static_cast<UINT>(fdh)},
                                     motion ? StaleMotion{km_t.Get(), static_cast<UINT>(kdw), static_cast<UINT>(kdh)} : StaleMotion{},
                                     motion ? StaleMotion{fm_t.Get(), static_cast<UINT>(fdw), static_cast<UINT>(fdh)} : StaleMotion{},
                                     masks ? kept_rig : StaleRig{}, masks ? fresh_rig : StaleRig{}));
            std::fprintf(stderr, "  dump %d rules %s, masks %s: %.1f s\n", n, rules ? "on" : "off", masks ? "on" : "off", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
            return c;
        };
        const auto on = run(true), off = run(false);
        if (std::getenv("STALE_REPLAY_FITS")) {  // the hand fit of each replay (target pixels; fraction of the hand's samples the fresh eye sees)
            context->Flush(); for (int i = 0; i < 50 && warp.hand_fits.size() < 2; ++i) warp.poll_hand_fits();
            for (const auto& f : warp.hand_fits) std::printf("      fit: L %+.1f %+.1f (%.2f/%.2f) R %+.1f %+.1f (%.2f/%.2f)\n", f.hands[0][0], f.hands[0][1], f.hands[0][2], f.hands[0][3], f.hands[1][0], f.hands[1][1], f.hands[1][2], f.hands[1][3]);
            warp.hand_fits.clear();
        }
        for (const auto& [suffix, bytes] : {std::pair<const char*, const std::vector<unsigned char>*>{"-replay-on.rgba", &on}, {"-replay-off.rgba", &off}})
            std::ofstream(out + "/stale-warp-" + std::to_string(n) + suffix, std::ios::binary).write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
        if (std::getenv("STALE_REPLAY_EXITS")) {
            const std::string src = exits_variant(shader_source("src/stale_eye_warp.hpp"));
            ComPtr<ID3DBlob> blob, errors; ComPtr<ID3D11PixelShader> exits;
            if (FAILED(D3DCompile(src.data(), src.size(), "replay exits", nullptr, nullptr, "WarpPS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors))) {
                std::fprintf(stderr, "exits variant: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "compile failed"); return 1;
            }
            CHECK(SUCCEEDED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &exits)));
            auto& exit_slot = pixel_of(warp); exit_slot = exits;
            const auto ex = run(true);
            exit_slot = shipped;
            std::ofstream(out + "/stale-warp-" + std::to_string(n) + "-exits.rgba", std::ios::binary).write(reinterpret_cast<const char*>(ex.data()), static_cast<std::streamsize>(ex.size()));
        }
        // scores inside the margin
        constexpr int margin = 48;
        const auto diff = [&](const std::vector<unsigned char>& a, const std::vector<unsigned char>& c, std::size_t i) {
            return (std::abs(a[i * 4] - c[i * 4]) + std::abs(a[i * 4 + 1] - c[i * 4 + 1]) + std::abs(a[i * 4 + 2] - c[i * 4 + 2])) / 3.0;
        };
        // movers (kept or fresh w >= mover_px at that screen pixel), dilated 16 px
        std::vector<unsigned char> mover(static_cast<std::size_t>(kb.w) * kb.h, 0);
        std::size_t kept_movers = 0, kept_beyond = 0, fresh_movers = 0, fresh_beyond = 0;
        if (ab_rig) for (int y = 0; y < kb.h; ++y) for (int x = 0; x < kb.w; ++x) {  // the region: the rig in either eye
            const int gx = (kb.x + x) * kdw / W, gy = (kb.y + y) * kdh / H;
            const auto at = [&](const std::vector<unsigned char>& m, const Box& bx) {
                const int lx = gx - bx.x, ly = gy - bx.y;
                return lx >= 0 && ly >= 0 && lx < bx.w && ly < bx.h && m[static_cast<std::size_t>(ly) * bx.w + lx] > 0;
            };
            mover[static_cast<std::size_t>(y) * kb.w + x] = at(kr, kdb) || at(fr, fdb);
        }
        if (motion && !ab_rig) for (int y = 0; y < kb.h; ++y) for (int x = 0; x < kb.w; ++x) {
            const int gx = kb.x + x, gy = kb.y + y;
            const auto w_at = [&](const std::vector<unsigned char>& m, const std::vector<unsigned char>& dd, const Box& bx, double& ahead) {
                const int lx = gx * kdw / W - bx.x, ly = gy * kdh / H - bx.y; ahead = 0;
                if (lx < 0 || ly < 0 || lx >= bx.w || ly >= bx.h) return 0.f;
                const std::size_t i = static_cast<std::size_t>(ly) * bx.w + lx; std::uint16_t v; std::memcpy(&v, &m[i * 8 + 6], 2);
                float depth; std::memcpy(&depth, &dd[i * 4], 4); ahead = depth > 1e-7f ? layers.world.m11 / (depth + layers.world.m10) : 0;
                return half_to_float(v);
            };
            double ka, fa; const bool k = w_at(km, kd, kdb, ka) >= mover_px, f = w_at(fm, fd, fdb, fa) >= mover_px;
            kept_movers += k; kept_beyond += k && ka >= 1.5; fresh_movers += f; fresh_beyond += f && fa >= 1.5;
            mover[static_cast<std::size_t>(y) * kb.w + x] = k || f;
        }
        {
            std::vector<unsigned char> tmp(mover.size(), 0);
            for (int y = 0; y < kb.h; ++y) for (int x = 0; x < kb.w; ++x) {unsigned char m = 0; for (int dx = -16; dx <= 16 && !m; ++dx) {const int xx = x + dx; if (xx >= 0 && xx < kb.w) m = mover[static_cast<std::size_t>(y) * kb.w + xx];} tmp[static_cast<std::size_t>(y) * kb.w + x] = m;}
            for (int y = 0; y < kb.h; ++y) for (int x = 0; x < kb.w; ++x) {unsigned char m = 0; for (int dy = -16; dy <= 16 && !m; ++dy) {const int yy = y + dy; if (yy >= 0 && yy < kb.h) m = tmp[static_cast<std::size_t>(yy) * kb.w + x];} mover[static_cast<std::size_t>(y) * kb.w + x] = m;}
        }
        std::size_t total = 0, within1 = 0, within4 = 0, identical = 0, movers_n = 0;
        double fg = 0, foff = 0, fon = 0, fm_off = 0, fm_on = 0, fr_off = 0, fr_on = 0;
        std::vector<double> f_off(mover.size(), 0), f_on(mover.size(), 0), b_off(mover.size(), 0), b_on(mover.size(), 0);
        double bm_off = 0, bm_on = 0, br_off = 0, br_on = 0;
        // B4: F scores a rebuilt frame equal to the kept one 0, so it cannot see a mover left a tick behind. B4 = max over the two neighbours
        // of the best match of R within +-4 px in that neighbour (mean absolute RGB): small only where R is near both (a mover between them).
        const auto match = [&](const std::vector<unsigned char>& r, const std::vector<unsigned char>& t, int x, int y) {
            const std::size_t i = static_cast<std::size_t>(y) * kb.w + x; double best = 1e9;
            for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) {
                const std::size_t j = static_cast<std::size_t>(y + dy) * kb.w + (x + dx);
                best = std::min(best, (std::abs(r[i * 4] - t[j * 4]) + std::abs(r[i * 4 + 1] - t[j * 4 + 1]) + std::abs(r[i * 4 + 2] - t[j * 4 + 2])) / 3.0);
            }
            return best;
        };
        const auto flicker = [&](const std::vector<unsigned char>& r, std::size_t i) {return (diff(r, kept, i) + diff(r, next, i) - diff(next, kept, i)) / 2;};
        for (int y = margin; y < kb.h - margin; ++y) for (int x = margin; x < kb.w - margin; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * kb.w + x;
            int worst = 0; for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(on[i * 4 + c] - warped[i * 4 + c]));
            ++total; within1 += worst <= 1; within4 += worst <= 4;
            f_off[i] = flicker(off, i); f_on[i] = flicker(on, i);
            fg += flicker(warped, i); foff += f_off[i]; fon += f_on[i];
            b_off[i] = std::max(match(off, kept, x, y), match(off, next, x, y)); b_on[i] = std::max(match(on, kept, x, y), match(on, next, x, y));
            if (mover[i]) {++movers_n; fm_off += f_off[i]; fm_on += f_on[i]; bm_off += b_off[i]; bm_on += b_on[i];}
            else {fr_off += f_off[i]; fr_on += f_on[i]; br_off += b_off[i]; br_on += b_on[i];}
        }
        for (std::size_t i = 0; i < on.size(); ++i) identical += on[i] == off[i];
        const std::size_t rest_n = total - movers_n;
        std::printf("%4d  %.4f  %.4f  %s  %6.3f  %6.3f %6.3f | %6.3f %6.3f (%zu px) | %6.3f %6.3f | kept %zu (%.0f%%) fresh %zu (%.0f%%)  %+.2f deg  %.2f px  parts %.3f/%.3f\n",
                    n, double(within1) / total, double(within4) / total, identical == on.size() ? "identical" : "DIFFER   ",
                    fg / total, foff / total, fon / total, movers_n ? fm_off / movers_n : 0., movers_n ? fm_on / movers_n : 0., movers_n,
                    rest_n ? fr_off / rest_n : 0., rest_n ? fr_on / rest_n : 0., kept_movers, kept_movers ? 100.0 * kept_beyond / kept_movers : 0.,
                    fresh_movers, fresh_movers ? 100.0 * fresh_beyond / fresh_movers : 0., pitch, fit_error, layers.fresh_part, layers.kept_part);
        if (movers_n) std::printf("      B4 movers off %.3f on %.3f | rest off %.3f on %.3f\n", bm_off / movers_n, bm_on / movers_n, rest_n ? br_off / rest_n : 0., rest_n ? br_on / rest_n : 0.);
        std::fflush(stdout);
        if (!kept_movers && !fresh_movers && !ab_rig) continue;
        // sheets: kept | off | on | next | F off - on | B4 off - on (green: on better; red: on worse; 1 level = 16; blue tint: mover region)
        const int pw = kb.w, ph = kb.h, sw = pw * 6;
        std::vector<unsigned char> sheet(static_cast<std::size_t>(sw) * ph * 3, 0);
        const std::vector<unsigned char>* panels[4]{&kept, &off, &on, &next};
        for (int y = 0; y < ph; ++y) for (int x = 0; x < pw; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * pw + x;
            for (int p = 0; p < 4; ++p) for (int c = 0; c < 3; ++c) sheet[(static_cast<std::size_t>(y) * sw + p * pw + x) * 3 + c] = (*panels[p])[i * 4 + c];
            for (int p = 4; p < 6; ++p) {
                const double d = p == 4 ? f_off[i] - f_on[i] : b_off[i] - b_on[i];
                unsigned char* s = &sheet[(static_cast<std::size_t>(y) * sw + p * pw + x) * 3];
                s[0] = static_cast<unsigned char>(std::min(255.0, std::max(0.0, -d * 16))); s[1] = static_cast<unsigned char>(std::min(255.0, std::max(0.0, d * 16)));
                s[2] = mover[i] ? 40 : 0;
            }
        }
        write_png(out + "/stale-warp-" + std::to_string(n) + "-sheet.png", sw, ph, sheet);
        // path map
        auto& slot = pixel_of(warp); slot = paths; layers.motion_rules = true;
        const auto path = crop(warp.apply(kept_t.Get(), id, &layers, {kd_t.Get(), static_cast<UINT>(kdw), static_cast<UINT>(kdh)}, fresh_t.Get(), {fd_t.Get(), static_cast<UINT>(fdw), static_cast<UINT>(fdh)},
                                          StaleMotion{km_t.Get(), static_cast<UINT>(kdw), static_cast<UINT>(kdh)}, StaleMotion{fm_t.Get(), static_cast<UINT>(fdw), static_cast<UINT>(fdh)}, kept_rig, fresh_rig));
        slot = shipped;
        if (std::getenv("STALE_REPLAY_REASONS")) {
            const std::string src = reasons_variant(shader_source("src/stale_eye_warp.hpp"));
            ComPtr<ID3DBlob> blob, errors; ComPtr<ID3D11PixelShader> reasons;
            if (FAILED(D3DCompile(src.data(), src.size(), "replay reasons", nullptr, nullptr, "WarpPS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &blob, &errors))) {
                std::fprintf(stderr, "reasons variant: %s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "compile failed"); return 1;
            }
            CHECK(SUCCEEDED(device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &reasons)));
            slot = reasons;
            const auto why = crop(warp.apply(kept_t.Get(), id, &layers, {kd_t.Get(), static_cast<UINT>(kdw), static_cast<UINT>(kdh)}, fresh_t.Get(), {fd_t.Get(), static_cast<UINT>(fdw), static_cast<UINT>(fdh)},
                                             StaleMotion{km_t.Get(), static_cast<UINT>(kdw), static_cast<UINT>(kdh)}, StaleMotion{fm_t.Get(), static_cast<UINT>(fdw), static_cast<UINT>(fdh)}, kept_rig, fresh_rig));
            slot = shipped;
            std::ofstream(out + "/stale-warp-" + std::to_string(n) + "-reasons.rgba", std::ios::binary).write(reinterpret_cast<const char*>(why.data()), static_cast<std::streamsize>(why.size()));
        }
        std::vector<unsigned char> rgb(static_cast<std::size_t>(pw) * ph * 3);
        std::size_t counts[6]{};
        for (std::size_t i = 0; i < static_cast<std::size_t>(pw) * ph; ++i) {
            for (int c = 0; c < 3; ++c) rgb[i * 3 + c] = path[i * 4 + c];
            const unsigned char r = path[i * 4], g = path[i * 4 + 1], bl = path[i * 4 + 2];
            counts[g > 200 && bl > 200 ? 5 : r > 200 && g > 200 ? 1 : g > 200 ? 0 : bl > 200 ? 2 : r > 200 ? 3 : 4]++;
        }
        write_png(out + "/stale-warp-" + std::to_string(n) + "-paths.png", pw, ph, rgb);
        std::printf("      paths over the crop: M1 own %zu, M1 fresh %zu, M3 %zu, M2-cleared %zu, rest %zu\n", counts[0], counts[1], counts[2], counts[3], counts[4]);
    }
    return 0;
}
