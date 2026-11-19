#pragma once
// Shared by the offline replay tools (stale_warp_replay.cpp, spacewarp_replay.cpp): a small JSON reader for the dump meta files, raw file
// loading, half-float decoding and a PNG writer (stored deflate: no compression library needed).
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#ifndef CHECK
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(0)
#endif

// ---- a small JSON reader (the meta files) ----
struct Json {
    enum Kind {null, number, boolean, string, array, object} kind{null};
    double n{}; bool b{}; std::string s; std::vector<Json> a; std::vector<std::pair<std::string, Json>> o;
    const Json& operator[](const char* key) const {for (const auto& p : o) if (p.first == key) return p.second; static const Json none; return none;}
    double at(std::size_t i) const {return a.at(i).n;}
    bool has(const char* key) const {return (*this)[key].kind != null;}
};
inline Json parse(const char*& p) {
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
inline std::vector<unsigned char> load(const std::string& path) {
    std::ifstream f(path, std::ios::binary); CHECK(f);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), {});
}
inline float half_to_float(std::uint16_t h) {
    const std::uint32_t sign = (h & 0x8000u) << 16, e = (h >> 10) & 31u, m = h & 0x3ffu;
    std::uint32_t x;
    if (!e) {if (!m) x = sign; else {int s = -1; std::uint32_t mm = m; do {++s; mm <<= 1;} while (!(mm & 0x400u)); x = sign | ((127 - 15 - s) << 23) | ((mm & 0x3ffu) << 13);}}
    else if (e == 31) x = sign | 0x7f800000u | (m << 13);
    else x = sign | ((e + 112) << 23) | (m << 13);
    float f; std::memcpy(&f, &x, 4); return f;
}

// ---- PNG (stored deflate: no compression library needed) ----
inline std::uint32_t crc(const unsigned char* d, std::size_t n, std::uint32_t c = 0xffffffffu) {
    static std::uint32_t table[256]; static bool ready = false;
    if (!ready) {for (std::uint32_t i = 0; i < 256; ++i) {std::uint32_t r = i; for (int k = 0; k < 8; ++k) r = r & 1 ? 0xedb88320u ^ (r >> 1) : r >> 1; table[i] = r;} ready = true;}
    for (std::size_t i = 0; i < n; ++i) c = table[(c ^ d[i]) & 0xff] ^ (c >> 8);
    return c;
}
inline void write_png(const std::string& path, int w, int h, const std::vector<unsigned char>& rgb) {
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
