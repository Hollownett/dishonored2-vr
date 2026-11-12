#define _CRT_SECURE_NO_WARNINGS  // std::getenv (AFW2_VERBOSE)
// v792 lab: the stale-eye warp (src/stale_eye_warp.hpp). The homography against hand-derived values, then the GPU pass on WARP: a
// warped vertical line must land on the column the CPU math predicts, through both read paths (a readable source, and a kept copy
// without a read view), and an identity warp must reproduce the image.
#include "../src/stale_eye_warp.hpp"
#include "../src/canted_fov.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"d3dcompiler.lib")
using namespace d2vr;
// v1174: the lab's synthetic images have no bright edge band (their last rows and columns are scene): every case sees band-less layers;
// a case that models the band sets its own (r9)
template<class... A> static std::optional<StaleLayers> lab_layers(A&&... a) {
    auto l = d2vr::stale_eye_layers(std::forward<A>(a)...);
    if (l) l->border = {0, 0};
    return l;
}
#define stale_eye_layers lab_layers
using Microsoft::WRL::ComPtr;
#define CHECK(x) do { if(!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while(0)

static bool close_to(double a, double b, double tol) {return std::abs(a - b) <= tol;}
static Quaternion yaw(double radians) {return {0, std::sin(radians / 2), 0, std::cos(radians / 2)};}   // about +Y: turns the view left
static Quaternion pitch(double radians) {return {std::sin(radians / 2), 0, 0, std::cos(radians / 2)};} // about +X: turns the view up
static const WarpFov square{-std::atan(1.0), std::atan(1.0), std::atan(1.0), -std::atan(1.0)};           // +-45 deg: tan = 1

static void math() {
    // identity: every pixel maps to itself
    const auto id = stale_eye_homography({}, square, {}, square);
    CHECK(id);
    for (double u : {0.0, 0.25, 0.5, 0.9}) for (double v : {0.1, 0.5, 1.0}) {
        const auto p = warp_point(*id, u, v);
        CHECK(p && close_to((*p)[0], u, 1e-9) && close_to((*p)[1], v, 1e-9));
    }
    // the target view turned left by theta: its centre shows what was tan(theta) left of the source centre
    const double theta = 0.1;
    const auto left = stale_eye_homography({}, square, yaw(theta), square);
    CHECK(left);
    const auto c = warp_point(*left, 0.5, 0.5);
    CHECK(c && close_to((*c)[0], (1 - std::tan(theta)) / 2, 1e-9) && close_to((*c)[1], 0.5, 1e-9));
    // turned up: the centre shows what was above (smaller v)
    const auto up = stale_eye_homography({}, square, pitch(theta), square);
    const auto cu = warp_point(*up, 0.5, 0.5);
    CHECK(cu && close_to((*cu)[0], 0.5, 1e-9) && close_to((*cu)[1], (1 - std::tan(theta)) / 2, 1e-9));
    // both views turned alike: no warp (only the relative rotation counts)
    const auto same = stale_eye_homography(yaw(0.7), square, yaw(0.7), square);
    const auto s = warp_point(*same, 0.3, 0.6);
    CHECK(s && close_to((*s)[0], 0.3, 1e-9) && close_to((*s)[1], 0.6, 1e-9));
    // the opposite homography maps the point back (the reticle path)
    const WarpFov asym{-0.9, 0.7, 0.8, -0.95};
    const Quaternion from = normalized(multiplied(yaw(0.3), pitch(-0.05))), to = normalized(multiplied(yaw(0.32), pitch(-0.04)));
    const auto forward = stale_eye_homography(from, asym, to, asym), back = stale_eye_homography(to, asym, from, asym);
    CHECK(forward && back);
    for (double u : {0.2, 0.5, 0.8}) for (double v : {0.2, 0.7}) {
        const auto q = warp_point(*forward, u, v);
        CHECK(q);
        const auto r = warp_point(*back, (*q)[0], (*q)[1]);
        CHECK(r && close_to((*r)[0], u, 1e-9) && close_to((*r)[1], v, 1e-9));
    }
    // a direction behind the source camera has no source pixel
    const auto behind = stale_eye_homography({}, square, yaw(3.14159), square);
    CHECK(behind && !warp_point(*behind, 0.5, 0.5));
    // invalid FOVs and orientations are refused
    CHECK(!stale_eye_homography({}, WarpFov{0.5, -0.5, 0.5, -0.5}, {}, square));
    CHECK(!stale_eye_homography({0, 0, 0, 0}, square, {}, square));
}

static std::vector<unsigned char> read_back(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* texture) {
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging; CHECK(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)));
    context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{}; CHECK(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)));
    std::vector<unsigned char> out(desc.Width * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y)
        std::memcpy(out.data() + y * desc.Width * 4, static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch, desc.Width * 4);
    context->Unmap(staging.Get(), 0);
    return out;
}

static void gpu() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    constexpr UINT W = 256, H = 128, line = 128;
    std::vector<unsigned char> pixels(W * H * 4, 0);
    for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
        auto* p = &pixels[(y * W + x) * 4];
        const unsigned char value = x == line ? 255 : static_cast<unsigned char>(20 + (x * 7 + y * 3) % 40); // a bright line on a dim texture
        p[0] = p[1] = p[2] = value; p[3] = 255;
    }
    StaleEyeWarp warp(device.Get());
    for (int path = 0; path < 2; ++path) {  // 0: readable typeless source, 1: typed kept copy without a read view (copied first)
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = W; desc.Height = H; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
        desc.Format = path ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_R8G8B8A8_TYPELESS;
        desc.BindFlags = path ? 0 : D3D11_BIND_SHADER_RESOURCE; desc.Usage = D3D11_USAGE_DEFAULT;
        const D3D11_SUBRESOURCE_DATA data{pixels.data(), W * 4, 0};
        ComPtr<ID3D11Texture2D> source; CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &source)));
        // identity: the same image (sRGB decode/encode round trip within one step)
        const auto id = *stale_eye_homography({}, square, {}, square);
        const auto same = read_back(device.Get(), context.Get(), warp.apply(source.Get(), id));
        int worst = 0;
        for (std::size_t i = 0; i < same.size(); ++i) worst = std::max(worst, std::abs(int(same[i]) - int(pixels[i])));
        CHECK(worst <= 1);
        // a turn: the line moves to the column the CPU homography predicts
        const double theta = std::atan(20.0 / 128.0);   // about 20 px at the centre of a 256 px, 90 deg view
        const auto h = *stale_eye_homography({}, square, yaw(theta), square);
        double expected = -1;
        for (UINT x = 0; x < W; ++x) {  // the target column whose source sample is the line's centre
            const auto p = warp_point(h, (x + 0.5) / W, 0.5);
            if (p && std::abs((*p)[0] * W - 0.5 - line) < 0.5) expected = x;
        }
        CHECK(expected > line + 15 && expected < line + 25);
        const auto moved = read_back(device.Get(), context.Get(), warp.apply(source.Get(), h));
        UINT brightest = 0; int best = -1;
        for (UINT x = 0; x < W; ++x) {const int v = moved[((H / 2) * W + x) * 4]; if (v > best) {best = v; brightest = x;}}
        std::printf("path %d: line at column %u, expected %.0f (value %d)\n", path, brightest, expected, best);
        CHECK(std::abs(static_cast<double>(brightest) - expected) <= 1 && best > 200);
    }
}

// v796: a game camera from an OpenXR-axes orientation (the test world uses OpenXR axes) and a position.
static WarpCamera camera(Quaternion q, Vec3 p) {
    WarpCamera c; c.pose.position = p;
    c.pose.basis = {rotated(q, {0, 0, -1}), rotated(q, {-1, 0, 0}), rotated(q, {0, 1, 0})};  // forward, left, up
    c.m10 = 0; c.m11 = 0.01;
    return c;
}

static void parallax_math() {
    // the same camera: nothing moves, at any distance
    const auto same = stale_eye_parallax(camera({}, {1, 2, 3}), square, camera({}, {1, 2, 3}), square);
    CHECK(same);
    for (double d : {0.5, 2.0, 50.0}) {
        const auto p = parallax_source(*same, 0.3, 0.7, d);
        CHECK(p && close_to((*p)[0], 0.3, 1e-12) && close_to((*p)[1], 0.7, 1e-12));
    }
    // a pure rotation of the game camera is the rotation homography (an anchor turn is warped like a head turn)
    const WarpFov asym{-0.9, 0.7, 0.8, -0.95};
    const Quaternion from = normalized(multiplied(yaw(0.3), pitch(-0.05))), to = normalized(multiplied(yaw(0.32), pitch(-0.04)));
    const auto turned = stale_eye_parallax(camera(from, {0, 0, 0}), asym, camera(to, {0, 0, 0}), asym);
    const auto rotation = stale_eye_homography(from, asym, to, asym);
    CHECK(turned && rotation);
    for (int i = 0; i < 9; ++i) CHECK(close_to(turned->world[i], (*rotation)[i], 1e-12));
    CHECK(close_to(turned->shift[0], 0, 1e-12) && close_to(turned->shift[1], 0, 1e-12) && close_to(turned->shift[2], 0, 1e-12));
    // a step 10 cm right: the centre of the target view shows what was 0.1/2 to the right of the kept centre at 2 m (tan 0.05)
    const auto right = stale_eye_parallax(camera({}, {0, 0, 0}), square, camera({}, {0.1, 0, 0}), square);
    CHECK(right);
    const auto c = parallax_source(*right, 0.5, 0.5, 2.0);
    CHECK(c && close_to((*c)[0], (1 + 0.05) / 2, 1e-12) && close_to((*c)[1], 0.5, 1e-12));
    const auto distant = parallax_source(*right, 0.5, 0.5, 1e6);  // the sky does not move
    CHECK(distant && close_to((*distant)[0], 0.5, 1e-6));
    // a step 20 cm forward: a target pixel at tan 0.5 shows the kept pixel at tan 0.5*(2-0.2)/2 for a surface 2 m ahead of the kept camera
    const auto ahead = stale_eye_parallax(camera({}, {0, 0, 0}), square, camera({}, {0, 0, -0.2}), square);
    CHECK(ahead);
    const auto a = parallax_source(*ahead, 0.75, 0.5, 2.0);
    CHECK(a && close_to((*a)[0], (1 + 0.45) / 2, 1e-12) && close_to((*a)[1], 0.5, 1e-12));
    // turned and stepped: the reticle path (kept -> target) undoes the pass's mapping (target -> kept) at the kept distance
    const auto both = stale_eye_parallax(camera(from, {1, 2, 3}), asym, camera(to, {1.05, 2.01, 2.93}), asym);
    CHECK(both);
    for (double u : {0.2, 0.5, 0.8}) for (double v : {0.2, 0.7}) for (double d : {0.7, 3.0, 40.0}) {
        const auto s = parallax_source(*both, u, v, d);
        CHECK(s);
        const auto t = parallax_target(*both, (*s)[0], (*s)[1], d);
        CHECK(t && close_to((*t)[0], u, 1e-9) && close_to((*t)[1], v, 1e-9));
    }
    // refused: a blink-sized step, no depth terms, a broken basis, a bad FOV
    CHECK(!stale_eye_parallax(camera({}, {0, 0, 0}), square, camera({}, {1.5, 0, 0}), square));
    auto flat = camera({}, {0, 0, 0}); flat.m11 = 0;
    CHECK(!stale_eye_parallax(flat, square, camera({}, {0, 0, 0}), square));
    auto skew = camera({}, {0, 0, 0}); skew.pose.basis[1] = {0, 0, -1};
    CHECK(!stale_eye_parallax(skew, square, camera({}, {0, 0, 0}), square));
    CHECK(!stale_eye_parallax(camera({}, {0, 0, 0}), WarpFov{0.5, -0.5, 0.5, -0.5}, camera({}, {0, 0, 0}), square));
}

// The kept depth as FSR keeps it: R32 device depth, the render rectangle (rw x rh) at the top left of a larger texture whose margin
// holds hand depth (a wrong rectangle mapping would then read hands and freeze the lines).
static ComPtr<ID3D11Texture2D> depth_texture(ID3D11Device* device, UINT rw, UINT rh, float left, float right) {
    const UINT w = rw + 16, h = rh + 8;
    std::vector<float> d(w * h, 0.15f);
    for (UINT y = 0; y < rh; ++y) for (UINT x = 0; x < rw; ++x) d[y * w + x] = x < rw / 2 ? left : right;
    D3D11_TEXTURE2D_DESC desc{}; desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS; desc.Usage = D3D11_USAGE_DEFAULT;
    const D3D11_SUBRESOURCE_DATA data{d.data(), w * 4, 0};
    ComPtr<ID3D11Texture2D> t; CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &t)));
    return t;
}

static void gpu_parallax() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    constexpr UINT W = 256, H = 128, near_line = 64, far_line = 192, RW = W / 2, RH = H / 2;  // depth at half resolution (upscaling)
    std::vector<unsigned char> pixels(W * H * 4, 0);
    for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
        auto* p = &pixels[(y * W + x) * 4];
        p[0] = p[1] = p[2] = x == near_line || x == far_line ? 255 : static_cast<unsigned char>(20 + (x * 7 + y * 3) % 40); p[3] = 255;
    }
    D3D11_TEXTURE2D_DESC desc{}; desc.Width = W; desc.Height = H; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE; desc.Usage = D3D11_USAGE_DEFAULT;
    const D3D11_SUBRESOURCE_DATA data{pixels.data(), W * 4, 0};
    ComPtr<ID3D11Texture2D> source; CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &source)));
    StaleEyeWarp warp(device.Get());
    const auto id = *stale_eye_homography({}, square, {}, square);  // the tracking-space rotation: none
    const auto brightest = [&](ID3D11Texture2D* out, UINT x0, UINT x1) {
        const auto image = read_back(device.Get(), context.Get(), out);
        UINT best_x = 0; int best = -1;
        for (UINT x = x0; x < x1; ++x) {const int v = image[((H / 2) * W + x) * 4]; if (v > best) {best = v; best_x = x;}}
        CHECK(best > 150);
        return static_cast<double>(best_x);
    };
    const auto expected = [&](const StaleParallax& p, UINT line, double distance) {  // the target column that shows the line's centre
        double best = -1, err = 1e9;
        for (UINT x = 0; x < W; ++x) {
            const auto s = parallax_source(p, (x + 0.5) / W, 0.5, distance);
            if (s && std::abs((*s)[0] * W - 0.5 - line) < err) {err = std::abs((*s)[0] * W - 0.5 - line); best = x;}
        }
        return best;
    };
    // walking 4 cm right: the line on the 1 m surface moves about 5 px, the one on the 10 m surface about half a pixel
    const auto step = *stale_eye_parallax(camera({}, {0, 0, 0}), square, camera({}, {0.04, 0, 0}), square);
    const auto step_layers = *stale_eye_layers(camera({}, {0, 0, 0}), square, camera({}, {0.04, 0, 0}), square);
    const auto planes = depth_texture(device.Get(), RW, RH, 0.01f / 1.0f, 0.01f / 10.0f);
    auto* out = warp.apply(source.Get(), id, &step_layers, {planes.Get(), RW, RH});
    const double near_at = brightest(out, 0, W / 2), far_at = brightest(out, W / 2, W);
    const double near_want = expected(step, near_line, 1.0), far_want = expected(step, far_line, 10.0);
    std::printf("parallax: 1 m line at %.0f (want %.0f), 10 m line at %.0f (want %.0f)\n", near_at, near_want, far_at, far_want);
    CHECK(near_want < near_line - 4 && std::abs(near_at - near_want) <= 1 && std::abs(far_at - far_want) <= 1);
    CHECK(warp.parallax_passes == 1 && warp.depth_refusals == 0);
    // layers without a depth: rotation only (here none), nothing moves
    out = warp.apply(source.Get(), id, &step_layers);
    CHECK(brightest(out, 0, W / 2) == near_line && brightest(out, W / 2, W) == far_line);
    // a smooth turn of the game camera (no head turn): the sky (depth 0) turns with it (v798: near content would come from the fresh eye)
    const double theta = std::atan(20.0 / 128.0);
    const auto turn = *stale_eye_parallax(camera({}, {0, 0, 0}), square, camera(yaw(theta), {0, 0, 0}), square);
    const auto turn_layers = *stale_eye_layers(camera({}, {0, 0, 0}), square, camera(yaw(theta), {0, 0, 0}), square);
    const auto mixed = depth_texture(device.Get(), RW, RH, 0.0f, 0.0f);
    out = warp.apply(source.Get(), id, &turn_layers, {mixed.Get(), RW, RH});
    const double turned_at = brightest(out, W / 2, W), turned_want = expected(turn, far_line, 1e6);
    std::printf("turn: sky line at %.0f (want %.0f)\n", turned_at, turned_want);
    CHECK(turned_want > far_line + 15 && std::abs(turned_at - turned_want) <= 1);
    // a depth texture that is not R32 is refused and the pass falls back to rotation only
    D3D11_TEXTURE2D_DESC wrong{}; wrong.Width = RW; wrong.Height = RH; wrong.MipLevels = 1; wrong.ArraySize = 1; wrong.SampleDesc.Count = 1;
    wrong.Format = DXGI_FORMAT_R16_FLOAT; wrong.BindFlags = D3D11_BIND_SHADER_RESOURCE; wrong.Usage = D3D11_USAGE_DEFAULT;
    ComPtr<ID3D11Texture2D> half; CHECK(SUCCEEDED(device->CreateTexture2D(&wrong, nullptr, &half)));
    out = warp.apply(source.Get(), id, &step_layers, {half.Get(), RW, RH});
    CHECK(warp.depth_refusals == 1 && brightest(out, 0, W / 2) == near_line);
    // a sprint over a floor 1.6 m below the eye: 30 cm forward in one frame (7 m/s at 44 ms). The pass solves the varying depth by
    // iteration; a horizontal line on the floor must land on the row the floor's geometry gives.
    constexpr UINT row = 110;
    std::vector<unsigned char> floor_pixels(W * H * 4, 0);
    for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
        auto* p = &floor_pixels[(y * W + x) * 4];
        p[0] = p[1] = p[2] = y == row ? 255 : static_cast<unsigned char>(20 + (x * 3 + y * 7) % 40); p[3] = 255;
    }
    const D3D11_SUBRESOURCE_DATA floor_data{floor_pixels.data(), W * 4, 0};
    ComPtr<ID3D11Texture2D> floor_source; CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &floor_data, &floor_source)));
    std::vector<float> floor_depth(W * H, 0.0f);  // full resolution; above the horizon the sky (0)
    for (UINT y = 0; y < H; ++y) {const double t = 1 - 2 * (y + 0.5) / H; if (t < 0) for (UINT x = 0; x < W; ++x) floor_depth[y * W + x] = static_cast<float>(0.01 * -t / 1.6);}
    D3D11_TEXTURE2D_DESC fd{}; fd.Width = W; fd.Height = H; fd.MipLevels = 1; fd.ArraySize = 1; fd.SampleDesc.Count = 1;
    fd.Format = DXGI_FORMAT_R32_FLOAT; fd.BindFlags = D3D11_BIND_SHADER_RESOURCE; fd.Usage = D3D11_USAGE_DEFAULT;
    const D3D11_SUBRESOURCE_DATA depth_data{floor_depth.data(), W * 4, 0};
    ComPtr<ID3D11Texture2D> floor_texture; CHECK(SUCCEEDED(device->CreateTexture2D(&fd, &depth_data, &floor_texture)));
    const auto sprint = *stale_eye_layers(camera({}, {0, 0, 0}), square, camera({}, {0, 0, -0.3}), square);
    const auto image = read_back(device.Get(), context.Get(), warp.apply(floor_source.Get(), id, &sprint, {floor_texture.Get(), W, H}));
    UINT best_y = 0; int best = -1;
    for (UINT y = 0; y < H; ++y) {const int v = image[(y * W + W / 2) * 4]; if (v > best) {best = v; best_y = y;}}
    const double kept = 1.6 / -(1 - 2 * (row + 0.5) / H), ahead = 1.6 / (kept - 0.3), want = (1 + ahead) / 2 * H - 0.5;  // kept distance, target tan
    std::printf("floor: line at row %u (want %.1f, was %u)\n", best_y, want, row);
    CHECK(best > 150 && want > row + 6 && std::abs(best_y - want) <= 1);
}

// v798: the fresh eye's layer: a point seen now by the fresh eye comes to its place in the target view, at its fresh distance.
static void fresh_math() {
    const WarpCamera kept = camera({}, {0.032, 0, 0}), target = camera({}, {0.032, 0, 0}), fresh = camera({}, {-0.032, 0, 0});
    const auto layers = stale_eye_layers(kept, square, target, square, &fresh, square, 2.0);
    CHECK(layers && layers->fresh && close_to(layers->near_range, 2.0, 1e-12));
    const Vec3 x{0.05, -0.02, -0.5};
    const auto pixel = [](const WarpCamera& c, const Vec3& point, double& ahead) {
        const Vec3 v = point - c.pose.position; ahead = dot(v, c.pose.basis[0]);
        return std::array<double, 2>{(-dot(v, c.pose.basis[1]) / ahead + 1) / 2, (1 - dot(v, c.pose.basis[2]) / ahead) / 2};
    };
    double fresh_ahead{}, target_ahead{};
    const auto fp = pixel(fresh, x, fresh_ahead), tp = pixel(target, x, target_ahead);
    const auto got = parallax_source(*layers->fresh, tp[0], tp[1], fresh_ahead);
    CHECK(got && close_to((*got)[0], fp[0], 1e-9) && close_to((*got)[1], fp[1], 1e-9));
    // v806: canted eyes (Quest 3: each eye turned 6.65 degrees outward and 5.37 down): the same, through two turned views
    const Quaternion down = pitch(-0.0937);
    const WarpCamera canted_target = camera(multiplied(yaw(-0.1161), down), {0.032, 0, 0}), canted_fresh = camera(multiplied(yaw(0.1161), down), {-0.032, 0, 0});
    const auto canted = stale_eye_layers(canted_target, square, canted_target, square, &canted_fresh, square, 2.0);
    const auto cfp = pixel(canted_fresh, x, fresh_ahead), ctp = pixel(canted_target, x, target_ahead);
    const auto canted_got = canted && canted->fresh ? parallax_source(*canted->fresh, ctp[0], ctp[1], fresh_ahead) : std::nullopt;
    CHECK(canted_got && close_to((*canted_got)[0], cfp[0], 1e-9) && close_to((*canted_got)[1], cfp[1], 1e-9));
    CHECK(!stale_eye_layers(kept, square, target, square, &fresh, square, 0.0)->fresh);  // near range 0: the scene alone (v796)
    CHECK(!stale_eye_layers(kept, square, target, square)->fresh);
}

// Two eyes 6.4 cm apart look at a wall 5 m away and a hand (12 cm square) 0.5 m away. The kept right-eye image is a frame old: the hand
// was 5 cm further left. The fresh left-eye image is now. The warped right eye must show the hand where it is now (from the fresh eye),
// the wall where the hand was (from the fresh eye), and the wall beside the hand that only the right eye sees (from the kept image).
static void gpu_fresh() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    constexpr UINT W = 256, H = 128;
    // the hand: a 12 cm square hand_z ahead at its centre, turned about its vertical axis (ahead = hand_z + slope * (x - hand_x)); the
    // wall `wall` metres ahead; lift: the other eye's shading (levels added to every pixel)
    const auto render = [&](double eye_x, double hand_x, double hand_z, double slope, ComPtr<ID3D11Texture2D>& image, ComPtr<ID3D11Texture2D>& depth,
                            int lift = 0, double wall = 5.0) {
        std::vector<unsigned char> pixels(W * H * 4); std::vector<float> d(W * H);
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            const double tx = (x + 0.5) / W * 2 - 1, ty = 1 - (y + 0.5) / H * 2, wx = eye_x + tx * wall;
            const double ahead = (hand_z + slope * (eye_x - hand_x)) / (1 - slope * tx), hx = eye_x + ahead * tx, hy = ty * ahead;
            int c; float z;
            if (ahead > 0 && std::abs(hx - hand_x) < 0.06 && std::abs(hy) < 0.06) {c = std::abs(hx - hand_x) < ahead * 2 / W ? 255 : 120; z = static_cast<float>(0.01 / ahead);}
            else {c = std::abs(wx - 1.0) < wall / W ? 255 : 20 + (static_cast<int>((wx + 10) * 60) * 7) % 40; z = static_cast<float>(0.01 / wall);}
            c = std::min(255, c + lift);
            auto* p = &pixels[(y * W + x) * 4]; p[0] = p[1] = p[2] = static_cast<unsigned char>(c); p[3] = 255; d[y * W + x] = z;
        }
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = W; desc.Height = H; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.Usage = D3D11_USAGE_DEFAULT;  // no read view, as GameXr keeps them
        const D3D11_SUBRESOURCE_DATA data{pixels.data(), W * 4, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &image)));
        desc.Format = DXGI_FORMAT_R32_FLOAT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA ddata{d.data(), W * 4, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &ddata, &depth)));
    };
    ComPtr<ID3D11Texture2D> kept_image, kept_depth, fresh_image, fresh_depth;
    render(0.032, -0.05, 0.5, 0, kept_image, kept_depth);   // right eye, a frame ago
    render(-0.032, 0.0, 0.5, 0, fresh_image, fresh_depth);  // left eye, now
    // v811: the hand is drawn and tracked, but its recorded grip did not move while the game drew it 5 cm further right (headset v810
    // dumps: the drawn hands are not the recorded grips): the hand fit finds where the fresh eye sees it, and this eye's own hand goes there
    const WarpCamera kept = camera({}, {0.032, 0, 0}), target = kept, fresh = camera({}, {-0.032, 0, 0});
    // a tracked hand drawn at (x, ahead) in the kept frame whose recorded grip stayed there (the fit must find where it went)
    const auto held = [&](double x, double ahead) {
        WarpCamera k = kept; k.hand_bits = 1; k.hands[0].position = {x, 0, -ahead}; k.hands[0].basis = k.pose.basis;
        return *stale_eye_layers(k, square, k, square, &fresh, square, 2.0);
    };
    const auto layers = held(-0.05, 0.5);
    StaleEyeWarp warp(device.Get());
    const auto id = *stale_eye_homography({}, square, {}, square);
    const auto row = [&](ID3D11Texture2D* out) {
        const auto image = read_back(device.Get(), context.Get(), out);
        std::vector<int> r(W); for (UINT x = 0; x < W; ++x) r[x] = image[((H / 2) * W + x) * 4];
        return r;
    };
    const auto column = [&](double world_x, double eye_x, double ahead) {return ((world_x - eye_x) / ahead + 1) / 2 * W - 0.5;};
    const auto brightest = [](const std::vector<int>& r, int x0, int x1) {int best = -1, at = 0; for (int x = x0; x < x1; ++x) if (r[x] > best) {best = r[x]; at = x;} return at;};
    const auto now = row(warp.apply(kept_image.Get(), id, &layers, {kept_depth.Get(), W, H}, fresh_image.Get(), {fresh_depth.Get(), W, H}));
    const double hand_want = column(0.0, 0.032, 0.5), old_hand = column(-0.05, 0.032, 0.5), wall_want = column(1.0, 0.032, 5.0);
    const int hand_at = brightest(now, 95, 145), wall_at = brightest(now, 145, 170);
    const int was_hand = static_cast<int>(column(-0.08, 0.032, 0.5)), beside = static_cast<int>(column(0.075, 0.032, 0.5));
    std::printf("fresh layer: hand line at %d (want %.1f, kept had %.1f), wall line at %d (want %.1f); where the hand was: %d, beside it: %d\n",
                hand_at, hand_want, old_hand, wall_at, wall_want, now[was_hand], now[beside]);
    // v811: the fit's samples lie about 2 px apart over a flat, untextured square: within 2 px (13 px without it)
    CHECK(std::abs(hand_at - hand_want) <= 2 && std::abs(wall_at - wall_want) <= 1);
    CHECK(now[was_hand] < 100 && now[beside] < 100);  // wall (dim) where the hand was and beside it, not the hand's 120 fill
    CHECK(warp.fresh_passes == 1 && warp.fresh_refusals == 0);
    // near range 0 (v796): the kept image alone, the hand where it was
    const auto alone = *stale_eye_layers(kept, square, target, square, &fresh, square, 0.0);
    const auto before = row(warp.apply(kept_image.Get(), id, &alone, {kept_depth.Get(), W, H}, fresh_image.Get(), {fresh_depth.Get(), W, H}));
    CHECK(std::abs(brightest(before, 95, 145) - old_hand) <= 1 && warp.fresh_passes == 1);
    // headset v798: a hand 0.28 m away, turned 22 degrees, 4 cm further left a frame ago: it lands whole where it is now (a fixed point
    // started at 0.4 m left it in stripes)
    ComPtr<ID3D11Texture2D> near_kept, near_kept_depth, near_fresh, near_fresh_depth;
    render(0.032, -0.04, 0.28, 0.4, near_kept, near_kept_depth);
    render(-0.032, 0.0, 0.28, 0.4, near_fresh, near_fresh_depth);
    const auto near_layers = held(-0.04, 0.28);
    const auto close = row(warp.apply(near_kept.Get(), id, &near_layers, {near_kept_depth.Get(), W, H}, near_fresh.Get(), {near_fresh_depth.Get(), W, H}));
    const double close_want = column(0.0, 0.032, 0.28);
    const int a = static_cast<int>(std::ceil(column(-0.045, 0.032, 0.28 + 0.4 * -0.045))), b = static_cast<int>(column(0.045, 0.032, 0.28 + 0.4 * 0.045));
    int covered = 0; for (int x = a; x <= b; ++x) {covered += close[x] >= 90; if (close[x] < 90) std::printf("  column %d: %d\n", x, close[x]);}
    std::printf("near hand: line at %d (want %.1f), hand over %d of %d columns (%d..%d)\n", brightest(close, a, b + 1), close_want, covered, b - a + 1, a, b);
    CHECK(std::abs(brightest(close, a, b + 1) - close_want) <= 1 && covered == b - a + 1);
    // headset v807 dump ("the hands warp; holes in the hand"): the sword hand held 17-25 cm from the eyes, 3 cm further left a frame ago.
    // Nearer than the march's old 0.25 m start, it was never met and its old place took the scenery: it must land whole where it is now.
    ComPtr<ID3D11Texture2D> face_kept, face_kept_depth, face_fresh, face_fresh_depth;
    render(0.032, -0.03, 0.18, 0, face_kept, face_kept_depth);
    render(-0.032, 0.0, 0.18, 0, face_fresh, face_fresh_depth);
    const auto face_layers = held(-0.03, 0.18);
    const auto face = row(warp.apply(face_kept.Get(), id, &face_layers, {face_kept_depth.Get(), W, H}, face_fresh.Get(), {face_fresh_depth.Get(), W, H}));
    const double face_want = column(0.0, 0.032, 0.18);
    const int fa = static_cast<int>(std::ceil(column(-0.055, 0.032, 0.18))), fb = static_cast<int>(column(0.055, 0.032, 0.18));
    int face_covered = 0; for (int x = fa; x <= fb; ++x) {face_covered += face[x] >= 90; if (face[x] < 90) std::printf("  column %d: %d\n", x, face[x]);}
    std::printf("hand at 18 cm: line at %d (want %.1f), hand over %d of %d columns (%d..%d)\n", brightest(face, fa, fb + 1), face_want, face_covered, fb - fa + 1, fa, fb);
    CHECK(std::abs(brightest(face, fa, fb + 1) - face_want) <= 1 && face_covered == fb - fa + 1);
    // headset v800 ("double silhouettes of the hands, in both eyes"): the hand moved 6 cm left since the kept frame, so its old place
    // lies beside it where only this eye sees the wall (the fresh eye looks behind the hand there). v799 kept the old hand there, a second
    // hand beside the first; the old place must show wall.
    ComPtr<ID3D11Texture2D> moved_kept, moved_kept_depth;
    render(0.032, 0.06, 0.5, 0, moved_kept, moved_kept_depth);
    const auto moved_layers = held(0.06, 0.5);
    const auto moved = row(warp.apply(moved_kept.Get(), id, &moved_layers, {moved_kept_depth.Get(), W, H}, fresh_image.Get(), {fresh_depth.Get(), W, H}));
    const int beside0 = static_cast<int>(std::ceil(column(0.06, 0.032, 0.5))) + 1, beside1 = static_cast<int>(column(0.12, 0.032, 0.5)) - 1;
    int ghost = 0; for (int x = beside0; x <= beside1; ++x) {ghost += moved[x] >= 100; if (moved[x] >= 100) std::printf("  column %d: %d\n", x, moved[x]);}
    std::printf("moved hand: line at %d (want %.1f), its old place beside it shows the hand in %d of %d columns (%d..%d)\n",
                brightest(moved, 95, 135), hand_want, ghost, beside1 - beside0 + 1, beside0, beside1);
    CHECK(std::abs(brightest(moved, 95, 135) - hand_want) <= 2 && ghost == 0);  // v811: the fit on a flat square, within 2 px
    // v810: the same 6 cm move of a drawn hand whose poses were recorded at both Setups: its own motion layer moves this eye's kept hand
    // there, so it shows in this eye's own shading (the fresh eye here 30 levels brighter), whole, and its old place shows wall.
    {
        ComPtr<ID3D11Texture2D> lit, lit_depth;
        render(-0.032, 0.0, 0.5, 0, lit, lit_depth, 30);
        WarpCamera tracked_kept = kept, tracked_target = target;
        const auto at = [](double x) {WorldTrackedPose p; p.position = {x, 0, -0.5}; p.basis = {Vec3{0, 0, -1}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}}; return p;};
        tracked_kept.hands[1] = at(0.06); tracked_target.hands[1] = at(0.0); tracked_kept.hand_bits = tracked_target.hand_bits = 2u;
        const auto tracked_layers = *stale_eye_layers(tracked_kept, square, tracked_target, square, &fresh, square, 2.0);
        CHECK(tracked_layers.hand[1] && !tracked_layers.hand[0]);
        const auto own_hand = row(warp.apply(moved_kept.Get(), id, &tracked_layers, {moved_kept_depth.Get(), W, H}, lit.Get(), {lit_depth.Get(), W, H}));
        const int h0 = static_cast<int>(std::ceil(column(-0.055, 0.032, 0.5))), h1 = static_cast<int>(column(0.055, 0.032, 0.5));
        // inside the hand (its edge pixels and the line's anti-aliasing apart: a depth texel cannot settle a silhouette to the pixel)
        const int line = static_cast<int>(std::lround(hand_want));
        int lit_columns = 0, holes = 0;
        for (int x = h0 + 1; x <= h1 - 1; ++x) if (std::abs(x - line) > 1) {lit_columns += own_hand[x] > 135; holes += own_hand[x] < 100;}
        int old_ghost = 0; for (int x = beside0 + 1; x <= beside1; ++x) old_ghost += own_hand[x] >= 100;
        std::printf("tracked hand: line at %d (want %.1f); over %d..%d %d columns in the other eye's shading, %d holes; its old place shows the hand in %d of %d columns\n",
                    brightest(own_hand, 95, 135), hand_want, h0, h1, lit_columns, holes, old_ghost, beside1 - beside0 + 1);
        CHECK(std::abs(brightest(own_hand, 95, 135) - hand_want) <= 1 && lit_columns == 0 && holes == 0 && old_ghost == 0);
    }
    // headset v801 ("exactly the same double silhouettes"): each eye alternates between its own frame and this one, and the other eye shades
    // things a little differently (here 30 levels brighter). The hand held still and the wall is 1.8 m away: this
    // frame must be this eye's own, hand and wall alike (v801 took the wall from the fresh eye around a strip of the kept one, outlining the
    // hand). v810: the hands-still flag no longer decides; nothing moved here, so with it off too the frame stays this eye's own.
    ComPtr<ID3D11Texture2D> lit_fresh, lit_fresh_depth, still_kept, still_kept_depth;
    render(-0.032, 0.0, 0.5, 0, lit_fresh, lit_fresh_depth, 30, 1.8);
    render(0.032, 0.0, 0.5, 0, still_kept, still_kept_depth, 0, 1.8);
    auto still_layers = *stale_eye_layers(kept, square, target, square, &fresh, square, 2.0);
    const auto kept_row = row(still_kept.Get());
    still_layers.near_from_kept = true;
    const auto own = row(warp.apply(still_kept.Get(), id, &still_layers, {still_kept_depth.Get(), W, H}, lit_fresh.Get(), {lit_fresh_depth.Get(), W, H}));
    int differ = 0; for (UINT x = 0; x < W; ++x) if (std::abs(own[x] - kept_row[x]) > 8) {++differ; std::printf("  column %u: %d (own %d)\n", x, own[x], kept_row[x]);}
    still_layers.near_from_kept = false;
    const auto moving = row(warp.apply(still_kept.Get(), id, &still_layers, {still_kept_depth.Get(), W, H}, lit_fresh.Get(), {lit_fresh_depth.Get(), W, H}));
    const int inside = static_cast<int>(column(0.03, 0.032, 0.5)), near_wall = static_cast<int>(column(0.5, 0.032, 1.8));
    std::printf("still hand: %d of %d columns differ from this eye's own frame; hand %d (own %d), wall %d (own %d); hands moving: hand %d, wall %d\n",
                differ, W, own[inside], kept_row[inside], own[near_wall], kept_row[near_wall], moving[inside], moving[near_wall]);
    CHECK(differ == 0 && std::abs(own[inside] - kept_row[inside]) <= 2 && std::abs(own[near_wall] - kept_row[near_wall]) <= 2);
    CHECK(std::abs(moving[inside] - kept_row[inside]) <= 2 && std::abs(moving[near_wall] - kept_row[near_wall]) <= 2);
    // v811 (headset v810: "many artifacts on the hands, very unstable"; a still hand showed a grain of pixels from the other hand's,
    // the head's and the world's motions): the left hand holds still in view while the right one, out of view, was recorded moving 3 cm.
    // Only the left hand's own motion may move its pixels: the frame stays this eye's own.
    {
        WarpCamera two_kept = kept, two_target = target;
        const auto at = [](double x) {WorldTrackedPose p; p.position = {x, 0, -0.5}; p.basis = {Vec3{0, 0, -1}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}}; return p;};
        two_kept.hands = {at(0.0), at(0.3)}; two_target.hands = {at(0.0), at(0.33)}; two_kept.hand_bits = two_target.hand_bits = 3u;
        const auto two_layers = *stale_eye_layers(two_kept, square, two_target, square, &fresh, square, 2.0);
        CHECK(two_layers.hand[0] && two_layers.hand[1] && two_layers.palms[0][3] > 0 && two_layers.palms[1][3] > 0);
        const auto two = row(warp.apply(still_kept.Get(), id, &two_layers, {still_kept_depth.Get(), W, H}, lit_fresh.Get(), {lit_fresh_depth.Get(), W, H}));
        int grain = 0; for (UINT x = 0; x < W; ++x) grain += std::abs(two[x] - kept_row[x]) > 8;
        std::printf("still hand beside a moving one: %d of %d columns differ from this eye's own frame\n", grain, W);
        CHECK(grain == 0);
    }
    // headset v812 (snap turns: "artifacts on the hands"): the hand's recorded grip turned in place while the game moved the drawn hand out
    // of that part of the view, and v811 showed this eye's own hand pixels wherever the hand's motion put them: a dark ghost of hand and
    // sleeve for a frame. Here the recorded motion says the hand stayed, but the fresh eye sees only wall there now: wall, not the hand.
    {
        ComPtr<ID3D11Texture2D> stayed_kept, stayed_kept_depth, empty_fresh, empty_fresh_depth;
        render(0.032, 0.0, 0.5, 0, stayed_kept, stayed_kept_depth);
        render(-0.032, 10.0, 0.5, 0, empty_fresh, empty_fresh_depth);  // the hand far out of view
        const auto stayed_layers = held(0.0, 0.5);
        const auto left = row(warp.apply(stayed_kept.Get(), id, &stayed_layers, {stayed_kept_depth.Get(), W, H}, empty_fresh.Get(), {empty_fresh_depth.Get(), W, H}));
        const int g0 = static_cast<int>(std::ceil(column(-0.055, 0.032, 0.5))), g1 = static_cast<int>(column(0.055, 0.032, 0.5));
        int ghost_columns = 0; for (int x = g0; x <= g1; ++x) {ghost_columns += left[x] >= 100; if (left[x] >= 100) std::printf("  column %d: %d\n", x, left[x]);}
        std::printf("hand its motion misplaced: the hand shows in %d of %d columns (%d..%d)\n", ghost_columns, g1 - g0 + 1, g0, g1);
        CHECK(ghost_columns == 0);
    }
    // headset v804 ("door frames and railings double; shadows behind the fingers"): v804 showed everything beyond 2 m through the fresh
    // eye, whose own shading (here 30 levels brighter) then patched this eye's frames. Beyond reach the rebuilt frame is this eye's own
    // again, hands still or moving: a guard 3 m away that walked 20 cm, in front of a wall 8 m away, stays where the kept frame had it.
    ComPtr<ID3D11Texture2D> guard_kept, guard_kept_depth, guard_fresh, guard_fresh_depth;
    render(0.032, -0.2, 3.0, 0, guard_kept, guard_kept_depth, 0, 8.0);
    render(-0.032, 0.0, 3.0, 0, guard_fresh, guard_fresh_depth, 30, 8.0);
    const auto guard_own = row(guard_kept.Get());
    for (const bool still : {true, false}) {
        still_layers.near_from_kept = still;
        const auto guard = row(warp.apply(guard_kept.Get(), id, &still_layers, {guard_kept_depth.Get(), W, H}, guard_fresh.Get(), {guard_fresh_depth.Get(), W, H}));
        int patched = 0; for (UINT x = 0; x < W; ++x) if (std::abs(guard[x] - guard_own[x]) > 8) {++patched; std::printf("  column %u: %d (own %d)\n", x, guard[x], guard_own[x]);}
        std::printf("far guard, hands %s: %d of %d columns differ from this eye's own frame\n", still ? "still" : "moving", patched, W);
        CHECK(patched == 0);
    }
    // headset v803 ("the hands double only while walking"): the walk carried the head and the hand sideways together since the kept frame
    // (15 cm here, so the strip spans 11 of these 256 columns; the headset's spanned 35 of 2432 px), in front of a wall 1.8 m away: beside
    // the hand's trailing edge this eye now sees wall its kept frame had behind the hand. That strip must show wall, not a second hand, and
    // (v805, headset v804: a band behind each finger) this eye's own wall (20-59 here), not the other eye's (60 levels brighter).
    ComPtr<ID3D11Texture2D> walk_fresh, walk_fresh_depth;
    render(-0.032, 0.0, 0.5, 0, walk_fresh, walk_fresh_depth, 60, 1.8);
    const int hand_left = static_cast<int>(std::floor(column(-0.06, 0.032, 0.5))), hand_right = static_cast<int>(std::ceil(column(0.06, 0.032, 0.5)));
    for (const double step : {0.15, -0.15}) {
        ComPtr<ID3D11Texture2D> walk_kept, walk_kept_depth;
        render(0.032 - step, -step, 0.5, 0, walk_kept, walk_kept_depth, 0, 1.8);
        const auto walk_layers = *stale_eye_layers(camera({}, {0.032 - step, 0, 0}), square, target, square, &fresh, square, 2.0);
        const auto walk = row(warp.apply(walk_kept.Get(), id, &walk_layers, {walk_kept_depth.Get(), W, H}, walk_fresh.Get(), {walk_fresh_depth.Get(), W, H}));
        const int x0 = step > 0 ? hand_left - 11 : hand_right + 1, x1 = step > 0 ? hand_left - 1 : hand_right + 11;
        int wrong = 0; for (int x = x0; x <= x1; ++x) if (walk[x] >= 70) {++wrong; std::printf("  column %d: %d\n", x, walk[x]);}
        std::printf("walking %s: hand line at %d (want %.1f); beside its trailing edge (%d..%d) other than this eye's own wall in %d columns\n",
                    step > 0 ? "right" : "left", brightest(walk, hand_left, hand_right), hand_want, x0, x1, wrong);
        CHECK(std::abs(brightest(walk, hand_left, hand_right) - hand_want) <= 1 && wrong == 0);
    }
}

// v807 (headset v806: "the hands jump about when I turn with the right stick"): a 30 degree snap turn between the kept frame and now. The
// part of this eye's view beyond the kept frame's edge must come from the fresh eye (turned already; here 30 levels brighter), not the
// kept edge smeared across it; the part the kept frame has stays this eye's own.
static void gpu_turn() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    constexpr UINT W = 256, H = 64;
    constexpr double wall = 5.0;
    const auto shade = [](double wx) {return 40 + static_cast<int>(std::lround(15 * std::sin(wx * 2)));}; // smooth: the wall is seen at a grazing angle too
    // a wall `wall` metres ahead of the origin (world z = -wall), seen from p turned by `turn` about +Y (left); lift: the other eye's shading
    const auto render = [&](Vec3 p, double turn, ComPtr<ID3D11Texture2D>& image, ComPtr<ID3D11Texture2D>& depth, int lift) {
        std::vector<unsigned char> pixels(W * H * 4); std::vector<float> d(W * H);
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            const double tx = (x + 0.5) / W * 2 - 1, ty = 1 - (y + 0.5) / H * 2;
            const double dx = tx * std::cos(turn) - std::sin(turn), dz = -tx * std::sin(turn) - std::cos(turn); // view ray (tx, ty, -1) turned
            const double ahead = (-wall - p.z) / dz, wx = p.x + ahead * dx;   // ahead: the view-space distance (the ray's z is -1)
            const int c = std::min(255, shade(wx) + lift);
            auto* px = &pixels[(y * W + x) * 4]; px[0] = px[1] = px[2] = static_cast<unsigned char>(c); px[3] = 255; d[y * W + x] = static_cast<float>(0.01 / ahead);
            (void)ty;
        }
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = W; desc.Height = H; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.Usage = D3D11_USAGE_DEFAULT;
        const D3D11_SUBRESOURCE_DATA data{pixels.data(), W * 4, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &image)));
        desc.Format = DXGI_FORMAT_R32_FLOAT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA ddata{d.data(), W * 4, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &ddata, &depth)));
    };
    const double turn = 30 * 3.14159265358979 / 180;
    const auto eye = [&](double side) {return Vec3{side * std::cos(turn), 0, -side * std::sin(turn)};}; // the eyes turn with the head
    ComPtr<ID3D11Texture2D> kept_image, kept_depth, fresh_image, fresh_depth, truth_image, truth_depth;
    render({0.032, 0, 0}, 0, kept_image, kept_depth, 0);           // right eye, before the snap
    render(eye(-0.032), turn, fresh_image, fresh_depth, 30);       // left eye, now
    render(eye(0.032), turn, truth_image, truth_depth, 0);         // what the right eye sees now
    const WarpCamera kept = camera({}, {0.032, 0, 0}), target = camera(yaw(turn), eye(0.032)), fresh = camera(yaw(turn), eye(-0.032));
    auto layers = *stale_eye_layers(kept, square, target, square, &fresh, square, 2.0);
    layers.border = {0, 0};  // v1167: these synthetic images have no bright edge band (the game's eye images do: the default inset)
    StaleEyeWarp warp(device.Get());
    const auto h = *stale_eye_homography(yaw(0), square, yaw(turn), square);
    const auto row = [&](ID3D11Texture2D* t) {
        const auto image = read_back(device.Get(), context.Get(), t);
        std::vector<int> r(W); for (UINT x = 0; x < W; ++x) r[x] = image[((H / 2) * W + x) * 4];
        return r;
    };
    const auto out = row(warp.apply(kept_image.Get(), h, &layers, {kept_depth.Get(), W, H}, fresh_image.Get(), {fresh_depth.Get(), W, H}));
    const auto truth = row(truth_image.Get());
    const auto near_value = [&](int value, int x, int lift) { // the wall as it is, within a column (its sawtooth jumps between neighbours)
        int lo = 255, hi = 0; for (int i = std::max(0, x - 1); i <= std::min<int>(W - 1, x + 1); ++i) {lo = std::min(lo, std::min(255, truth[i] + lift)); hi = std::max(hi, std::min(255, truth[i] + lift));}
        return value >= lo - 12 && value <= hi + 12;
    };
    int beyond = 0, beyond_wrong = 0, inside = 0, inside_wrong = 0;
    for (int x = 1; x < static_cast<int>(W) - 1; ++x) {
        // where its ray lies in the kept frame (the wall is 5 m away: the turn alone; the view turned left, so its rays lie right of before)
        const double kept_tx = std::tan(std::atan((x + 0.5) / W * 2 - 1) - turn);
        if (std::abs(kept_tx) < 0.97) {++inside; if (!near_value(out[x], x, 0)) {++inside_wrong; std::printf("  inside column %d: %d (truth %d)\n", x, out[x], truth[x]);}}
        else if (std::abs(kept_tx) > 1.03) {++beyond; if (!near_value(out[x], x, 30)) {++beyond_wrong; std::printf("  beyond column %d: %d (fresh %d)\n", x, out[x], std::min(255, truth[x] + 30));}}
    }
    std::printf("snap turn 30 deg: beyond the kept frame %d of %d columns not the fresh eye's view; inside it %d of %d not this eye's own\n", beyond_wrong, beyond, inside_wrong, inside);
    CHECK(beyond > 20 && inside > 20 && beyond_wrong == 0 && inside_wrong == 0);
}

// v808: a canted eye is rendered turned (orientation * cant, its symmetric canted FOV) and submitted in its own located view (the
// orientation without the turn, the runtime's FOV). The resampling homography must take each direction of the located view to the
// same direction in the turned image, and back; the turned image covers the located view to its corners. Several eyes, any headset.
static void uncant_math() {
    const auto uv = [](Quaternion q, WarpFov f, Vec3 d) { // a world direction -> image uv of a view (OpenXR: -z forward, +y up)
        const Vec3 v = rotated(Quaternion{-q.x, -q.y, -q.z, q.w}, d);
        const double tx = v.x / -v.z, ty = v.y / -v.z;
        return std::array<double, 2>{(tx - std::tan(f.left)) / (std::tan(f.right) - std::tan(f.left)), (std::tan(f.up) - ty) / (std::tan(f.up) - std::tan(f.down))};
    };
    const double d2r = 3.14159265358979 / 180;
    const std::array<WarpFov, 3> eyes{WarpFov{static_cast<float>(-54 * d2r), static_cast<float>(40 * d2r), static_cast<float>(44 * d2r), static_cast<float>(-55 * d2r)},
                                      WarpFov{static_cast<float>(-45 * d2r), static_cast<float>(52 * d2r), static_cast<float>(48 * d2r), static_cast<float>(-48 * d2r)},
                                      WarpFov{static_cast<float>(-60 * d2r), static_cast<float>(35 * d2r), static_cast<float>(40 * d2r), static_cast<float>(-50 * d2r)}};
    for (const auto& located : eyes) {
        const auto c = canted_fov(static_cast<float>(located.left), static_cast<float>(located.right), static_cast<float>(located.up), static_cast<float>(located.down));
        CHECK(c);
        const Quaternion s = normalized(multiplied(yaw(0.3), pitch(-0.1)));  // the eye's located orientation (any)
        const Quaternion t = normalized(multiplied(s, Quaternion{c->rotation[0], c->rotation[1], c->rotation[2], c->rotation[3]}));
        const WarpFov turned{-c->half_x, c->half_x, c->half_y, -c->half_y};
        const auto h = stale_eye_homography(t, turned, s, located), back = stale_eye_homography(s, located, t, turned);
        CHECK(h && back);
        for (double a : {0.0, 0.25, 0.5, 0.75, 1.0}) for (double b : {0.0, 0.5, 1.0}) {  // over the located view, its edges and corners
            const double tx = std::tan(located.left) + a * (std::tan(located.right) - std::tan(located.left));
            const double ty = std::tan(located.down) + b * (std::tan(located.up) - std::tan(located.down));
            const Vec3 d = rotated(s, Vec3{tx, ty, -1});
            const auto straight = uv(s, located, d), canted = uv(t, turned, d);
            const auto to_turned = warp_point(*h, straight[0], straight[1]), to_straight = warp_point(*back, canted[0], canted[1]);
            CHECK(to_turned && close_to((*to_turned)[0], canted[0], 1e-6) && close_to((*to_turned)[1], canted[1], 1e-6));
            CHECK(to_straight && close_to((*to_straight)[0], straight[0], 1e-6) && close_to((*to_straight)[1], straight[1], 1e-6));
            CHECK(canted[0] >= -1e-6 && canted[0] <= 1 + 1e-6 && canted[1] >= -1e-6 && canted[1] <= 1 + 1e-6);
        }
    }
    std::printf("uncant: located views of 3 eyes resample from their turned images exactly, corners covered\n");
}

// v1150 (design 2026-11-11 section 3, lab section 4.1): movers from the eyes' motion textures. A striped guard (50 cm wide, 3 m away)
// in front of a wall 8 m away walks 20 cm left between the kept tick and now; narrow views (+-15 deg) so it spans 80 columns. The kept
// eye's motion texture spans the two ticks before the kept tick, the fresh eye's the two ending now (constant velocity: 40 cm each).
static std::uint16_t half_float(float f) {  // normal numbers, truncated; tiny ones flush to zero (enough for the lab)
    std::uint32_t x; std::memcpy(&x, &f, 4);
    const std::uint32_t sign = (x >> 16) & 0x8000u; const int e = static_cast<int>((x >> 23) & 0xffu) - 127 + 15;
    if (e <= 0) return static_cast<std::uint16_t>(sign);
    if (e >= 31) return static_cast<std::uint16_t>(sign | 0x7c00u);
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(e) << 10) | ((x & 0x7fffffu) >> 13));
}
static void gpu_movers() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    constexpr UINT W = 256, H = 64;
    const double T = std::tan(15 * 3.14159265358979 / 180);
    const WarpFov narrow{-std::atan(T), std::atan(T), std::atan(T), -std::atan(T)};
    enum {wall_kind, guard_kind, pillar_kind, hand_kind};
    struct Frame {ComPtr<ID3D11Texture2D> image, depth, motion; std::vector<int> row, kind;};
    // An eye at (eye_x, 0, 0) looking down -z. guard: its centre (NaN: none), moved: its motion texture xyz (world metres); pillar: 26 cm
    // wide, 0.6 m away, at x -0.30..-0.04; hand: a 12 cm square 0.5 m away centred at x (NaN: none); lift: levels added (the other eye's
    // shading); junk: every still pixel's xyz nonzero (w stays 0). kind: what each pixel shows.
    const auto render = [&](double eye_x, double guard, Vec3 moved, bool pillar, double hand, int lift, bool junk = false, bool soft = false) {
        Frame f; f.row.resize(W); f.kind.resize(W * H);
        std::vector<unsigned char> pixels(W * H * 4); std::vector<float> d(W * H); std::vector<std::uint16_t> m(W * H * 4);
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            const double tx = ((x + 0.5) / W * 2 - 1) * T, ty = (1 - (y + 0.5) / H * 2) * T;
            const auto hit = [&](double z, double x0, double x1, double half) {const double wx = eye_x + z * tx; return wx >= x0 && wx <= x1 && std::abs(z * ty) < half;};
            double a = 8; int c = 20 + (static_cast<int>((eye_x + 8 * tx + 10) * 60) * 7) % 40, kind = wall_kind; float w = 0; Vec3 xyz{};
            if (std::isfinite(guard) && hit(3, guard - 0.25, guard + 0.25, 0.9)) {
                a = 3; c = (static_cast<int>(std::floor((eye_x + 3 * tx - guard) / 0.05)) & 1) ? 200 : 150; kind = guard_kind; w = length(moved) > 0 ? 8.f : 0.f; xyz = moved;
            }
            // soft: the guard's anti-aliased edge darkens the 2 wall pixels beside it (60 %, 30 % of the guard's level), which keep the wall's
            // depth and no motion, as the game draws them (v1152 dump 5)
            if (soft && kind == wall_kind && std::isfinite(guard) && std::abs(3 * ty) < 0.9) {
                const double px = W / (2 * T), left = ((guard - 0.25 - eye_x) / 3 / T + 1) / 2 * W, right = ((guard + 0.25 - eye_x) / 3 / T + 1) / 2 * W;
                const double out = std::min(std::abs(x + 0.5 - left), std::abs(x + 0.5 - right)); (void)px;
                if (out < 2) c = static_cast<int>(std::lround(c + (out < 1 ? 0.6 : 0.3) * (175 - c)));
            }
            if (pillar && hit(0.6, -0.30, -0.04, 1)) {a = 0.6; c = 80; kind = pillar_kind; w = 0; xyz = {};}
            if (std::isfinite(hand) && hit(0.5, hand - 0.06, hand + 0.06, 0.06)) {a = 0.5; c = 120; kind = hand_kind; w = 0; xyz = {};}
            if (junk && w == 0) xyz = {0.3, -0.2, 0.5};
            c = std::min(255, c + lift);
            auto* p = &pixels[(y * W + x) * 4]; p[0] = p[1] = p[2] = static_cast<unsigned char>(c); p[3] = 255;
            d[y * W + x] = static_cast<float>(0.01 / a); f.kind[y * W + x] = kind;
            const float v[4]{static_cast<float>(xyz.x), static_cast<float>(xyz.y), static_cast<float>(xyz.z), w};
            for (int i = 0; i < 4; ++i) m[(y * W + x) * 4 + i] = half_float(v[i]);
            if (y == H / 2) f.row[x] = c;
        }
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = W; desc.Height = H; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.Usage = D3D11_USAGE_DEFAULT;  // no read view, as GameXr keeps them
        const D3D11_SUBRESOURCE_DATA data{pixels.data(), W * 4, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &f.image)));
        desc.Format = DXGI_FORMAT_R32_FLOAT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA ddata{d.data(), W * 4, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &ddata, &f.depth)));
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        const D3D11_SUBRESOURCE_DATA mdata{m.data(), W * 8, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &mdata, &f.motion)));
        return f;
    };
    const double none = std::nan("");
    const Vec3 walk{-0.4, 0, 0};  // two ticks at 20 cm a tick, leftward
    const WarpCamera kept_cam = camera({}, {0.032, 0, 0}), fresh_cam = camera({}, {-0.032, 0, 0});
    auto layers = *stale_eye_layers(kept_cam, narrow, kept_cam, narrow, &fresh_cam, narrow, 2.0);
    StaleEyeWarp warp(device.Get());
    const auto id = *stale_eye_homography({}, square, {}, square);
    const auto image = [&](const Frame& k, const Frame& f, const StaleLayers& l, bool motion) {
        return read_back(device.Get(), context.Get(), warp.apply(k.image.Get(), id, &l, {k.depth.Get(), W, H}, f.image.Get(), {f.depth.Get(), W, H},
                                                                   motion ? StaleMotion{k.motion.Get(), W, H} : StaleMotion{}, motion ? StaleMotion{f.motion.Get(), W, H} : StaleMotion{}));
    };
    const auto middle = [&](const std::vector<unsigned char>& i) {std::vector<int> r(W); for (UINT x = 0; x < W; ++x) r[x] = i[((H / 2) * W + x) * 4]; return r;};
    const auto run = [&](const Frame& k, const Frame& f, bool rules) {layers.motion_rules = rules; const auto r = middle(image(k, f, layers, true)); layers.motion_rules = true; return r;};
    // the truth within a column either side (an edge's anti-aliasing) and 12 levels
    const auto near_truth = [&](const std::vector<int>& out, const Frame& truth, int x) {
        int lo = 255, hi = 0; for (int i = std::max(0, x - 1); i <= std::min<int>(W - 1, x + 1); ++i) {lo = std::min(lo, truth.row[i]); hi = std::max(hi, truth.row[i]);}
        return out[x] >= lo - 12 && out[x] <= hi + 12;
    };
    // per class of column (the guard now, the wall it uncovered, the rest of the wall): columns wrong. Uncovered wall: wrong if it shows
    // the guard (>= 130: this eye never saw that wall, so only the fresh eye has it). v1152: with the 2 px fringe beside the guard's old
    // outline, which M2 clears with it (in the game those pixels carry its anti-aliased edge).
    struct Score {int guard{}, guard_wrong{}, uncovered{}, uncovered_wrong{}, wall{}, wall_wrong{};};
    const auto score = [&](const std::vector<int>& out, const Frame& kept, const Frame& truth) {
        Score s;
        const auto kept_guard_near = [&](int x) {
            for (int d = -2; d <= 2; ++d) if (x + d >= 0 && x + d < static_cast<int>(W) && kept.kind[(H / 2) * W + x + d] == guard_kind) return true;
            return false;
        };
        for (int x = 1; x < static_cast<int>(W) - 1; ++x) {
            const int k = truth.kind[(H / 2) * W + x];
            if (k == guard_kind) {++s.guard; s.guard_wrong += !near_truth(out, truth, x);}
            else if (k == wall_kind && kept_guard_near(x)) {++s.uncovered; s.uncovered_wrong += out[x] >= 130;}
            else if (k == wall_kind) {++s.wall; s.wall_wrong += !near_truth(out, truth, x);}
        }
        return s;
    };
    const auto say = [](const char* name, const Score& on, const Score& off) {
        std::printf("%s: guard %d/%d wrong (rules off %d), uncovered wall %d/%d shows the guard (off %d), other wall %d/%d wrong (off %d)\n", name,
                    on.guard_wrong, on.guard, off.guard_wrong, on.uncovered_wrong, on.uncovered, off.uncovered_wrong, on.wall_wrong, on.wall, off.wall_wrong);
    };
    const Frame truth = render(0.032, 0.0, walk, false, none, 0);  // the target eye now, in its own shading
    // a. the guard at its new place in this eye's own kept pixels, wall at its old place
    {
        const Frame kept = render(0.032, 0.2, walk, false, none, 0), fresh = render(-0.032, 0.0, walk, false, none, 0);
        const auto on = score(run(kept, fresh, true), kept, truth), off = score(run(kept, fresh, false), kept, truth);
        say("a. guard walks 20 cm", on, off);
        CHECK(on.guard > 60 && on.uncovered > 20 && on.guard_wrong == 0 && on.uncovered_wrong == 0 && on.wall_wrong == 0);
        CHECK(off.guard_wrong > 20 && off.uncovered_wrong > 20);  // v1132: the guard where the kept frame had it
        CHECK(warp.motion_passes == 1 && warp.motion_refusals == 0);  // the run with the rules off reads no motion
    }
    // b. the fresh eye 30 levels brighter: guard and wall keep this eye's own levels (the guard from its own kept pixels, not the other
    // eye's). The wall the guard uncovered is in no kept pixel: there it is the fresh eye's (reported, not hidden).
    {
        const Frame kept = render(0.032, 0.2, walk, false, none, 0), fresh = render(-0.032, 0.0, walk, false, none, 30);
        const auto out = run(kept, fresh, true);
        const auto on = score(out, kept, truth), off = score(run(kept, fresh, false), kept, truth);
        say("b. other eye 30 brighter", on, off);
        int lifted = 0; for (int x = 0; x < static_cast<int>(W); ++x) if (truth.kind[(H / 2) * W + x] == wall_kind && kept.kind[(H / 2) * W + x] == guard_kind) lifted += out[x] > truth.row[x] + 20;
        std::printf("   uncovered wall in the fresh eye's shading: %d of %d columns\n", lifted, on.uncovered);
        CHECK(on.guard_wrong == 0 && on.uncovered_wrong == 0 && on.wall_wrong == 0);
        CHECK(off.guard_wrong > 20);
    }
    // c. a pillar 0.6 m away hides the guard's left part (x < -7 cm) from the fresh eye; this eye sees all of it. The hidden part moves with
    // the visible part (M3): every guard column within a column of the truth, both sides of the pillar's edge in the fresh view.
    const auto hidden_from_fresh = [&](int x) {  // the guard point on this column lies behind the pillar for the fresh eye
        const double tx = ((x + 0.5) / W * 2 - 1) * T, wx = 0.032 + 3 * tx, f = (wx + 0.032) / 3;
        return f >= (-0.30 + 0.032) / 0.6 && f <= (-0.04 + 0.032) / 0.6;
    };
    const Frame pillar_truth = render(0.032, 0.0, walk, true, none, 0);
    const auto split = [&](const std::vector<int>& out, int& hidden, int& hidden_wrong, int& seen, int& seen_wrong) {
        hidden = hidden_wrong = seen = seen_wrong = 0;
        for (int x = 1; x < static_cast<int>(W) - 1; ++x) if (pillar_truth.kind[(H / 2) * W + x] == guard_kind) {
            const bool bad = !near_truth(out, pillar_truth, x);
            if (hidden_from_fresh(x)) {++hidden; hidden_wrong += bad;} else {++seen; seen_wrong += bad;}
            if (bad) std::printf("   column %d (%s): %d (truth %d)\n", x, hidden_from_fresh(x) ? "hidden" : "seen", out[x], pillar_truth.row[x]);
        }
    };
    {
        const Frame kept = render(0.032, 0.2, walk, true, none, 0), fresh = render(-0.032, 0.0, walk, true, none, 30);
        int h, hw, s, sw, oh, ohw, os, osw;
        split(run(kept, fresh, true), h, hw, s, sw);
        split(run(kept, fresh, false), oh, ohw, os, osw);
        const auto on = score(run(kept, fresh, true), kept, pillar_truth);
        std::printf("c. half hidden by a pillar: hidden part %d/%d columns wrong (rules off %d), seen part %d/%d (off %d); uncovered wall shows the guard in %d/%d\n",
                    hw, h, ohw, sw, s, osw, on.uncovered_wrong, on.uncovered);
        CHECK(h > 15 && s > 40 && hw == 0 && sw == 0 && on.uncovered_wrong == 0);  // tolerance: within a column of the truth (near_truth)
        CHECK(ohw > 10);
    }
    // c'. the guard accelerates: the kept eye's motion says 10 cm over its two ticks (the M3 carry: 5 cm), this tick it walked 20 cm, and the
    // fresh eye's two ticks say 26 cm (6 + 20; fresh_part 0.5 takes 13 cm back for M1's lookup). M1 still draws the seen part where the
    // fresh eye has it (silhouette: guard on every seen guard column, wall where it was); only the hidden part (M3) lags.
    {
        const Frame kept = render(0.032, 0.2, {-0.10, 0, 0}, true, none, 0), fresh = render(-0.032, 0.0, {-0.26, 0, 0}, true, none, 0);
        const auto out = run(kept, fresh, true);
        int h, hw, s, sw; split(out, h, hw, s, sw);
        int silhouette = 0, hidden_lag = 0;
        for (int x = 1; x < static_cast<int>(W) - 1; ++x) {
            const int k = pillar_truth.kind[(H / 2) * W + x];
            const bool off_silhouette = (k == guard_kind && !hidden_from_fresh(x) && out[x] < 130) || (k == wall_kind && out[x] >= 130 && kept.kind[(H / 2) * W + x] == guard_kind);
            if (off_silhouette) {++silhouette; std::printf("   silhouette column %d: %d (truth %d)\n", x, out[x], pillar_truth.row[x]);}
            if (k == guard_kind && hidden_from_fresh(x)) hidden_lag += out[x] < 130;
        }
        std::printf("c'. accelerating guard: seen part off its silhouette in %d columns, %d/%d seen columns not the truth's levels (M1 lookup 7 cm off);"
                    " hidden part: %d/%d wrong, %d without the guard (M3 lags 15 cm)\n", silhouette, sw, s, hw, h, hidden_lag);
        CHECK(silhouette == 0);
    }
    // a'. (v1152 dump 5: a dark line followed a lifted boot's old sole edge) the same walk with soft, anti-aliased guard edges whose 2 px
    // fringe lies on the wall (wall depth, no motion): the wall at the guard's old place shows no line; its trailing fringe columns are wall
    {
        const Frame kept = render(0.032, 0.2, walk, false, none, 0, false, true), fresh = render(-0.032, 0.0, walk, false, none, 0, false, true);
        const Frame soft_truth = render(0.032, 0.0, walk, false, none, 0, false, true);
        const auto out = run(kept, fresh, true);
        const double old_right = ((0.2 + 0.25 - 0.032) / 3 / T + 1) / 2 * W;  // the old trailing edge (the guard walks left)
        int fringe = 0, line = 0;
        for (int x = static_cast<int>(old_right); x < static_cast<int>(old_right) + 3; ++x) {
            ++fringe; const bool bad = std::abs(out[x] - soft_truth.row[x]) > 12; line += bad;
            if (bad) std::printf("   column %d: %d (truth %d, kept %d)\n", x, out[x], soft_truth.row[x], kept.row[x]);
        }
        const auto on = score(out, kept, soft_truth);
        std::printf("a'. soft edges: old trailing fringe %d of %d columns not the wall; guard %d/%d wrong, uncovered wall shows the guard in %d/%d\n",
                    line, fringe, on.guard_wrong, on.guard, on.uncovered_wrong, on.uncovered);
        CHECK(line == 0 && on.uncovered_wrong == 0);
    }
    // d. the guard is gone in the fresh eye (left the view between the ticks): no afterimage
    {
        const Frame kept = render(0.032, 0.2, walk, false, none, 0), fresh = render(-0.032, none, walk, false, none, 0);
        int ghost = 0, ghost_off = 0;
        const auto on = run(kept, fresh, true), off = run(kept, fresh, false);
        for (UINT x = 0; x < W; ++x) {ghost += on[x] >= 130; ghost_off += off[x] >= 130;}
        std::printf("d. guard gone: it shows in %d columns (rules off %d)\n", ghost, ghost_off);
        CHECK(ghost == 0 && ghost_off > 40);
    }
    // e. anti-goal: a motion texture with w 0 everywhere (its xyz junk) changes not one byte against no motion texture, on a walk with a
    // tracked hand in front of the guard and the wall (and the rules switched off with real motion, likewise)
    {
        WarpCamera k = camera({}, {-0.068, 0, 0}), now = kept_cam;  // walked 10 cm right since the kept tick
        const auto at = [](double x) {WorldTrackedPose p; p.position = {x, 0, -0.5}; p.basis = {Vec3{0, 0, -1}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}}; return p;};
        k.hand_bits = now.hand_bits = 1u; k.hands[0] = at(-0.1); now.hands[0] = at(0.0);
        const auto walk_layers = *stale_eye_layers(k, narrow, now, narrow, &fresh_cam, narrow, 2.0);
        const Frame still_kept = render(-0.068, 0.0, {}, false, -0.1, 0, true), still_fresh = render(-0.032, 0.0, {}, false, 0.0, 30, true);
        const auto bare = image(still_kept, still_fresh, walk_layers, false), zero = image(still_kept, still_fresh, walk_layers, true);
        const Frame mover_kept = render(-0.068, 0.2, walk, false, -0.1, 0), mover_fresh = render(-0.032, 0.0, walk, false, 0.0, 30);
        auto off_layers = walk_layers; off_layers.motion_rules = false;
        const auto mover_bare = image(mover_kept, mover_fresh, walk_layers, false), mover_off = image(mover_kept, mover_fresh, off_layers, true);
        int differ = 0, differ_off = 0, hand = 0;
        for (std::size_t i = 0; i < bare.size(); ++i) {differ += bare[i] != zero[i]; differ_off += mover_bare[i] != mover_off[i];}
        for (UINT x = 0; x < W; ++x) hand += still_kept.kind[(H / 2) * W + x] == hand_kind;
        std::printf("e. zero motion vs none: %d bytes differ; rules off vs none: %d bytes differ (hand on %d columns)\n", differ, differ_off, hand);
        CHECK(differ == 0 && differ_off == 0 && hand > 10);
    }
    // f. a still, tracked hand in front of the walking guard: the hand's own pixels as without the rules
    {
        WarpCamera k = kept_cam;
        k.hand_bits = 1u; k.hands[0].position = {0.0, 0, -0.5}; k.hands[0].basis = k.pose.basis;
        const auto hand_layers = *stale_eye_layers(k, narrow, k, narrow, &fresh_cam, narrow, 2.0);
        const Frame kept = render(0.032, 0.2, walk, false, 0.0, 0), fresh = render(-0.032, 0.0, walk, false, 0.0, 30);
        auto off_layers = hand_layers; off_layers.motion_rules = false;
        const auto on = image(kept, fresh, hand_layers, true), off = image(kept, fresh, off_layers, true);
        int owned = 0, differ = 0, guard_moved = 0;
        for (UINT p = 0; p < W * H; ++p) if (kept.kind[p] == hand_kind) {++owned; for (int c = 0; c < 4; ++c) differ += on[p * 4 + c] != off[p * 4 + c];}
        const auto row_on = middle(on);
        for (int x = 0; x < static_cast<int>(W); ++x) guard_moved += truth.kind[(H / 2) * W + x] == wall_kind && kept.kind[(H / 2) * W + x] == guard_kind && row_on[x] >= 130;
        std::printf("f. still hand before the guard: %d bytes differ over its %d pixels; guard afterimage beside it in %d columns\n", differ, owned, guard_moved);
        CHECK(owned > 50 && differ == 0 && guard_moved == 0);
    }
}

// v1159 (headset v1158: "artifacts around the body and arms; a lot when the arms come close to objects; white shimmer around the right
// arm at the corner of the view"): the first-person rig masks. v811 gave a drawn hand every kept pixel within 22 cm of its palm: a still
// prop or wall there moved with the hand, and a forearm or a held stick beyond 22 cm tore off it. With the masks the player is exactly the
// masked pixels. Two eyes 6.4 cm apart, the right one rebuilt; the right hand (tracked) moved 5 cm right since the kept frame, the fresh
// (left) eye 30 levels brighter, so any colour it lends shows. Each case runs with the masks and without (the sphere).
static void gpu_rig() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    constexpr UINT W = 256, H = 128;
    enum {wall_kind, hand_kind, arm_kind, stick_kind, prop_kind};
    struct Frame {ComPtr<ID3D11Texture2D> image, depth, rig; std::vector<int> row, kind;};
    // hand, hand2: a 12 cm square 0.5 m ahead centred at x (NaN: none; a bright line at its centre); forearm: a rig bar from the hand's right
    // edge to 40 cm right of its centre; stick: a held rig stick from its left edge to 38 cm left; prop: a still striped object (not rig),
    // 6 cm wide, 0.5 m ahead, from x; wall: the wall's distance; yaw: the camera turned about +Y (canted eyes)
    struct Scene {double hand = std::nan(""), hand2 = std::nan(""); bool forearm = false, stick = false, hood = false; double prop = std::nan(""), wall = 1.8, yaw = 0, hand_y = 0; int lift = 0, band = 0;};
    const auto render = [&](double eye_x, const Scene& s) {
        Frame f; f.row.resize(W); f.kind.resize(W * H);
        std::vector<unsigned char> pixels(W * H * 4), mask(W * H); std::vector<float> d(W * H);
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            const double tx = (x + 0.5) / W * 2 - 1, ty = 1 - (y + 0.5) / H * 2;
            const double dx = tx * std::cos(s.yaw) - std::sin(s.yaw), dz = -tx * std::sin(s.yaw) - std::cos(s.yaw);  // ahead = 1 along the camera
            const auto plane = [&](double depth, double& wx, double& wy) {const double a = -depth / dz; wx = eye_x + a * dx; wy = a * ty; return a;};
            double wx, wy, a = plane(s.wall, wx, wy);
            int c = 20 + (static_cast<int>((wx + 10) * 60) * 7) % 40, kind = wall_kind;
            double px, py; const double pa = plane(0.5, px, py);
            const auto stripes = [](double v, double width, int lo, int hi) {return (static_cast<int>(std::floor(v / width)) & 1) ? hi : lo;};
            if (std::isfinite(s.prop) && px >= s.prop && px <= s.prop + 0.06 && std::abs(py) < 0.04) {a = pa; c = stripes(px - s.prop, 0.015, 180, 240); kind = prop_kind;}
            for (const double h : {s.hand, s.hand2}) if (std::isfinite(h)) {
                if (s.stick && h == s.hand && px >= h - 0.38 && px < h - 0.06 && std::abs(py) < 0.015) {a = pa; c = stripes(px - h, 0.03, 160, 220); kind = stick_kind;}
                if (s.forearm && h == s.hand && px > h + 0.06 && px <= h + 0.40 && std::abs(py) < 0.03) {a = pa; c = stripes(px - h, 0.03, 80, 110); kind = arm_kind;}
                if (std::abs(px - h) <= 0.06 && std::abs(py - (h == s.hand ? s.hand_y : 0)) < 0.06) {a = pa; c = std::abs(px - h) < pa * 2 / W ? 255 : 120; kind = hand_kind;}
            }
            // hood: the player's own hood or collar (rig, head-attached) 0.1 m before the eye at the left edge of the view, striped
            double hx, hy; const double ha = plane(0.1, hx, hy);
            if (s.hood && hx < -0.028) {a = ha; c = stripes(hx, 0.004, 50, 90); kind = arm_kind;}
            c = std::min(255, c + s.lift);
            if (static_cast<int>(y) >= static_cast<int>(H) - s.band) c = 240;  // the eye image's bright edge band (no scene; its depth is the scene's)
            auto* p = &pixels[(y * W + x) * 4]; p[0] = p[1] = p[2] = static_cast<unsigned char>(c); p[3] = 255;
            d[y * W + x] = static_cast<float>(0.01 / a); f.kind[y * W + x] = kind; mask[y * W + x] = kind == wall_kind || kind == prop_kind ? 0 : 255;
            if (y == H / 2) f.row[x] = c;
        }
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = W; desc.Height = H; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.Usage = D3D11_USAGE_DEFAULT;
        const D3D11_SUBRESOURCE_DATA data{pixels.data(), W * 4, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &f.image)));
        desc.Format = DXGI_FORMAT_R32_FLOAT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA ddata{d.data(), W * 4, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &ddata, &f.depth)));
        desc.Format = DXGI_FORMAT_R8_UNORM;
        const D3D11_SUBRESOURCE_DATA mdata{mask.data(), W, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &mdata, &f.rig)));
        return f;
    };
    const auto pose = [](double x) {WorldTrackedPose p; p.position = {x, 0, -0.5}; p.basis = {Vec3{0, 0, -1}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}}; return p;};
    // the right hand (side 1) drawn at kept_hand in the kept frame and at now_hand now; hand2 a still left hand; the eyes turned by yaw
    const auto layers_for = [&](double kept_hand, double now_hand, double hand2 = std::nan(""), double yaw_kept = 0, double yaw_fresh = 0) {
        WarpCamera k = camera(yaw(yaw_kept), {0.032, 0, 0}), f = camera(yaw(yaw_fresh), {-0.032, 0, 0});
        k.hand_bits = std::isfinite(hand2) ? 3u : 2u; k.hands[1] = pose(kept_hand); if (std::isfinite(hand2)) k.hands[0] = pose(hand2);
        WarpCamera t = k; t.hands[1] = pose(now_hand);
        auto l = *stale_eye_layers(k, square, t, square, &f, square, 2.0);
        l.border = {0, 0};  // v1167: no bright edge band in these synthetic images
        return l;
    };
    StaleEyeWarp warp(device.Get());
    const auto id = *stale_eye_homography({}, square, {}, square);
    const auto row = [&](const Frame& k, const Frame& f, const StaleLayers& l, bool masks) {
        const auto image = read_back(device.Get(), context.Get(), warp.apply(k.image.Get(), id, &l, {k.depth.Get(), W, H}, f.image.Get(), {f.depth.Get(), W, H}, {}, {},
                                                                           masks ? StaleRig{k.rig.Get(), W, H} : StaleRig{}, masks ? StaleRig{f.rig.Get(), W, H} : StaleRig{}));
        std::vector<int> r(W); for (UINT x = 0; x < W; ++x) r[x] = image[((H / 2) * W + x) * 4];
        return r;
    };
    const auto near_truth = [&](const std::vector<int>& out, const Frame& truth, int x) {
        int lo = 255, hi = 0; for (int i = std::max(0, x - 1); i <= std::min<int>(W - 1, x + 1); ++i) {lo = std::min(lo, truth.row[i]); hi = std::max(hi, truth.row[i]);}
        return out[x] >= lo - 12 && out[x] <= hi + 12;
    };
    // the world x a column shows on the 0.5 m plane (eyes not turned)
    const auto plane_x = [&](int x) {return 0.032 + 0.5 * ((x + 0.5) / W * 2 - 1);};
    // columns of the truth of `kind` whose 0.5 m point satisfies `where`, wrong (not within a column and 12 levels of the truth)
    const auto wrong = [&](const std::vector<int>& out, const Frame& truth, int kind, auto where, int& total) {
        int bad = 0; total = 0;
        for (int x = 1; x < static_cast<int>(W) - 1; ++x) if (truth.kind[(H / 2) * W + x] == kind && where(plane_x(x))) {++total; bad += !near_truth(out, truth, x);}
        return bad;
    };
    const auto both = [&](const char* name, const Frame& k, const Frame& f, const StaleLayers& l, const Frame& truth, int kind, auto where) {
        int n = 0;
        const int with = wrong(row(k, f, l, true), truth, kind, where, n), sphere = wrong(row(k, f, l, false), truth, kind, where, n);
        std::printf("%s: %d of %d columns wrong with the rig masks, %d with the 22 cm sphere\n", name, with, n, sphere);
        CHECK(n > 4 && with == 0 && sphere > 0);
    };
    // r1. a still striped prop 10 cm beside the palm stays where the world layer puts it
    {
        Scene kept{}, fresh{}, now{}; kept.hand = -0.05; fresh.hand = now.hand = 0.0; kept.prop = fresh.prop = now.prop = 0.10; fresh.lift = 0;  // v1175: near scenery under hand motion comes from the fresh eye; this case tests placement
        both("r1. still prop 10 cm from a moving palm", render(0.032, kept), render(-0.032, fresh), layers_for(-0.05, 0.0), render(0.032, now), prop_kind,
             [](double) {return true;});
    }
    // r2. a hand sliding along a wall 10 cm behind it leaves the wall still (beside the hand, within 22 cm of the palm)
    {
        Scene kept{}, fresh{}, now{}; kept.hand = -0.05; fresh.hand = now.hand = 0.0; kept.wall = fresh.wall = now.wall = 0.6; fresh.lift = 0;  // v1175: as r1
        both("r2. wall 10 cm behind a sliding hand", render(0.032, kept), render(-0.032, fresh), layers_for(-0.05, 0.0), render(0.032, now), wall_kind,
             [](double x) {return (x > 0.09 && x < 0.2) || (x > -0.25 && x < -0.14);});
    }
    // r3. a forearm (rig) reaching 40 cm from the palm moves with the hand, in this eye's own shading, beyond the 22 cm sphere too
    {
        Scene kept{}, fresh{}, now{}; kept.hand = -0.05; fresh.hand = now.hand = 0.0; kept.forearm = fresh.forearm = now.forearm = true; fresh.lift = 30;
        // (v1173: the hand layers, as with rig_from_fresh off; a moving hand is otherwise drawn from the fresh eye: case r10)
        both("r3. forearm 25-40 cm from the palm", render(0.032, kept), render(-0.032, fresh), [&] {auto l = layers_for(-0.05, 0.0); l.rig_from_fresh = false; return l;}(), render(0.032, now), arm_kind,
             [](double x) {return x > 0.25;});
    }
    // r4. a held stick (rig) reaching 38 cm the other way moves with the hand
    {
        Scene kept{}, fresh{}, now{}; kept.hand = -0.05; fresh.hand = now.hand = 0.0; kept.stick = fresh.stick = now.stick = true; fresh.lift = 30;
        both("r4. held stick 25-38 cm from the palm", render(0.032, kept), render(-0.032, fresh), [&] {auto l = layers_for(-0.05, 0.0); l.rig_from_fresh = false; return l;}(), render(0.032, now), stick_kind,
             [](double x) {return x < -0.25;});
    }
    // r5. a still left hand beside the moving right one keeps its own kept pixels exactly (within the sRGB round trip)
    {
        Scene kept{}, fresh{}, now{}; kept.hand = -0.05; fresh.hand = now.hand = 0.0; kept.hand2 = fresh.hand2 = now.hand2 = -0.3; fresh.lift = 30;
        const Frame k = render(0.032, kept), f = render(-0.032, fresh);
        const auto l = layers_for(-0.05, 0.0, -0.3);
        int with = 0, sphere = 0, n = 0;
        const auto on = row(k, f, l, true), off = row(k, f, l, false);
        for (int x = 0; x < static_cast<int>(W); ++x) if (std::abs(plane_x(x) + 0.3) < 0.055) {++n; with += std::abs(on[x] - k.row[x]) > 1; sphere += std::abs(off[x] - k.row[x]) > 1;}
        std::printf("r5. still hand beside a moving one: %d of %d columns off its kept pixels with the masks, %d with the sphere\n", with, n, sphere);
        CHECK(n > 10 && with == 0);
    }
    // r6. (v1163 dumps 1-3) the hood 10 cm before the eye, 41 cm from a palm that moved 5 cm: the head's, not the hand's; it stays
    {
        Scene kept{}, fresh{}, now{}; kept.hand = -0.05; fresh.hand = now.hand = 0.0; kept.hood = fresh.hood = now.hood = true; fresh.lift = 30;
        const Frame k = render(0.032, kept), f = render(-0.032, fresh);
        const auto l = layers_for(-0.05, 0.0);
        const auto on = row(k, f, l, true);
        int n = 0, moved = 0;
        for (int x = 1; x < static_cast<int>(W) - 1; ++x) if (k.kind[(H / 2) * W + x] == arm_kind && plane_x(x) < -0.2) {++n; moved += std::abs(on[x] - k.row[x]) > 8; if (n % 10 == 1) std::printf("   hood column %d: %d (kept %d, fresh %d)%c", x, on[x], k.row[x], f.row[x], 10);}
        std::printf("r6. hood 10 cm before the eye: %d of %d columns off its kept pixels with the masks\n", moved, n);
        CHECK(n > 20 && moved == 0);
    }
    // r9. (v1167, v1165 hold dump 7: a white block from the image's bottom band) the eye images end in an 8-row bright band (no scene); the
    // right hand low in the view moved 3 cm up since the kept frame. The rebuilt rows above the band take nothing from it.
    {
        Scene kept{}, fresh{}, now{}; kept.hand = fresh.hand = now.hand = 0.0; kept.hand_y = -0.23; fresh.hand_y = now.hand_y = -0.20;
        kept.band = fresh.band = now.band = 8;
        const Frame k = render(0.032, kept), f = render(-0.032, fresh), truth = render(0.032, now);
        WarpCamera kc = camera({}, {0.032, 0, 0}), fc = camera({}, {-0.032, 0, 0});
        kc.hand_bits = 2u; kc.hands[1] = pose(0.0); kc.hands[1].position.y = -0.23;
        WarpCamera tc = kc; tc.hands[1].position.y = -0.20;
        auto l = *stale_eye_layers(kc, square, tc, square, &fc, square, 2.0);
        l.border = {0.0, 8.0 / H};
        const auto image = read_back(device.Get(), context.Get(), warp.apply(k.image.Get(), id, &l, {k.depth.Get(), W, H}, f.image.Get(), {f.depth.Get(), W, H}, {}, {},
                                                                           StaleRig{k.rig.Get(), W, H}, StaleRig{f.rig.Get(), W, H}));
        int white = 0, n = 0;
        for (UINT y = H - 40; y < H - 8; ++y) for (UINT x = 0; x < W; ++x) if (truth.kind[y * W + x] != hand_kind) {++n; white += image[(y * W + x) * 4] > 200;}  // the hand has a white centre line

        std::printf("r9. hand moving up from the image's bright bottom band: %d of %d pixels above the band white\n", white, n);
        CHECK(white == 0);
    }
    // r10. (v1173, headset v1172: a halo around moving hands) the drawn hand moved 2.5 cm, its recorded grip 5 cm, beside a still prop over a
    // textured wall: the hand where the fresh eye has it, nothing of it at the over-moved place, prop and wall as they are
    {
        Scene kept{}, fresh{}, now{}; kept.hand = -0.05; fresh.hand = now.hand = -0.025; kept.prop = fresh.prop = now.prop = 0.10; fresh.lift = 0;  // v1175: as r1
        const Frame k = render(0.032, kept), f = render(-0.032, fresh), truth = render(0.032, now);
        const auto out = row(k, f, layers_for(-0.05, 0.0), true);
        int over = 0, n_over = 0, hand_missing = 0, n_hand = 0, scenery = 0, n_scenery = 0;
        for (int x = 1; x < static_cast<int>(W) - 1; ++x) {
            const double px = plane_x(x); const int kind = truth.kind[(H / 2) * W + x];
            if (px > 0.040 && px < 0.058) {++n_over; over += out[x] >= 100 && out[x] <= 160; std::printf("   over-moved column %d: %d (truth %d, kept %d, fresh %d)\n", x, out[x], truth.row[x], k.row[x], f.row[x]);}
            if (kind == hand_kind && std::abs(px + 0.025) < 0.05) {++n_hand; hand_missing += out[x] < 100;}  // the hand where it is now
            if ((kind == prop_kind || (kind == wall_kind && (px < -0.14 || (px > 0.07 && px < 0.20)))) && !(px > -0.09 && px < -0.08)) {
                ++n_scenery; scenery += !near_truth(out, truth, x);                                         // prop and wall beyond the strip
            }
        }
        std::printf("r10. hand moved 2.5 cm, grip 5 cm: hand fill at the over-moved place in %d of %d columns, hand missing in %d of %d, scenery wrong in %d of %d\n",
                    over, n_over, hand_missing, n_hand, scenery, n_scenery);
        CHECK(n_over >= 3 && over == 0 && hand_missing == 0 && n_scenery > 40 && scenery == 0);
    }
    // r11. (v1174, headset v1173: moving hands near the view's edge) canted eyes as in r7: the right hand at the rebuilt eye's outer edge,
    // where the fresh eye has no view, drawn 2.5 cm further out, its recorded grip 5 cm: its kept pixels in place, not shifted by the grip
    {
        Scene kept{}, fresh{}, now{}; kept.hand = 0.78; fresh.hand = now.hand = 0.805; kept.yaw = now.yaw = -0.25; fresh.yaw = 0.25; fresh.lift = 30;
        const Frame k = render(0.032, kept), f = render(-0.032, fresh);
        const auto out = row(k, f, layers_for(0.78, 0.83, std::nan(""), -0.25, 0.25), true);
        int n = 0, moved = 0;
        for (int x = W - 60; x < static_cast<int>(W) - 2; ++x) if (k.kind[(H / 2) * W + x] == hand_kind) {++n; moved += std::abs(out[x] - k.row[x]) > 2;}
        std::printf("r11. moving hand outside the fresh view: %d of %d kept hand columns not shown in place\n", moved, n);
        CHECK(n > 10 && moved == 0);
    }
    // r12. (v1175, headset v1174: artifacts on near objects while the camera moves) the camera stepped 1 cm since the kept tick, the hands
    // still; a near prop 0.5 m away: every prop column from the fresh eye (30 levels brighter) at the prop's true place, none mixed in
    {
        Scene kept{}, fresh{}, now{}; kept.prop = fresh.prop = now.prop = 0.10; fresh.lift = 30; now.lift = 30;
        const Frame k = render(0.022, kept), f = render(-0.032, fresh), lifted = render(0.032, now);
        WarpCamera kc = camera({}, {0.022, 0, 0}), tc = camera({}, {0.032, 0, 0}), fc = camera({}, {-0.032, 0, 0});
        const auto l = *stale_eye_layers(kc, square, tc, square, &fc, square, 2.0);
        CHECK(l.camera_moving);
        const auto image = read_back(device.Get(), context.Get(), warp.apply(k.image.Get(), id, &l, {k.depth.Get(), W, H}, f.image.Get(), {f.depth.Get(), W, H}, {}, {},
                                                                           StaleRig{k.rig.Get(), W, H}, StaleRig{f.rig.Get(), W, H}));
        std::vector<int> out(W); for (UINT x = 0; x < W; ++x) out[x] = image[((H / 2) * W + x) * 4];
        int n = 0, bad = 0;
        for (int x = 2; x < static_cast<int>(W) - 2; ++x) if (lifted.kind[(H / 2) * W + x] == prop_kind && lifted.kind[(H / 2) * W + x - 2] == prop_kind && lifted.kind[(H / 2) * W + x + 2] == prop_kind) {
            ++n; bad += !near_truth(out, lifted, x);
        }
        std::printf("r12. camera stepped 1 cm, near prop: %d of %d prop columns not the fresh eye's prop at its place\n", bad, n);
        CHECK(n > 8 && bad == 0);
    }
    // r7. the view corner (canted eyes turned 14 degrees apart each way): the right hand and its stick at the right edge of the rebuilt
    // eye, 3 cm further out now, where the fresh eye has no view. No black or white there, and this eye's own rig where it was.
    {
        Scene kept{}, fresh{}, now{}; kept.hand = 0.78; fresh.hand = now.hand = 0.81; kept.stick = fresh.stick = now.stick = true;
        kept.yaw = now.yaw = -0.25; fresh.yaw = 0.25; fresh.lift = 30;
        const Frame k = render(0.032, kept), f = render(-0.032, fresh), truth = render(0.032, now);
        const auto l = layers_for(0.78, 0.81, std::nan(""), -0.25, 0.25);
        for (const bool masks : {true, false}) {
            const auto out = row(k, f, l, masks);
            int rig = 0, black = 0, white = 0, off_truth = 0;
            for (int x = W - 60; x < static_cast<int>(W); ++x) {
                const int kind = truth.kind[(H / 2) * W + x];
                if (kind == wall_kind || kind == prop_kind) continue;
                ++rig; black += out[x] < 10; white += out[x] > 250 && truth.row[x] < 250 && k.row[x] < 250;  // v1174: not the hand's own white line where it was (B: shown in place) off_truth += !near_truth(out, truth, x);
                if (!near_truth(out, truth, x)) std::printf("   column %d: %d (truth %d, kept %d)\n", x, out[x], truth.row[x], k.row[x]);
            }
            std::printf("r7. rig at the view corner, %s: %d rig columns, %d black, %d white, %d off the truth\n", masks ? "masks" : "sphere", rig, black, white, off_truth);
            if (masks) CHECK(rig > 20 && black == 0 && white == 0);
        }
    }
}

// v1162 (headset v1158 "white shimmering at the corner of the view", "artifacts around the arms/body"; v1160 dumps 5 and 2): a ray at the
// rebuilt eye's edge leaves the fresh image at near distances, and the near search read the clamped edge texel as a surface there: the
// fresh eye's own sleeve at its edge (dump 2) or the eye image's 1 px white border (dump 5) was drawn over what this eye sees beside the
// sleeve. Here a dark sleeve 0.3 m ahead reaches the fresh (left) eye's right edge but ends before the rebuilt (right) eye's; the rebuilt
// eye's last columns must show its own wall 1.2 m away, neither the sleeve nor white.
static void gpu_edges() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    constexpr UINT W = 256, H = 128;
    struct Frame {ComPtr<ID3D11Texture2D> image, depth; std::vector<int> row;};
    const auto render = [&](double eye_x, bool white_border) {
        Frame f; f.row.resize(W);
        std::vector<unsigned char> pixels(W * H * 4); std::vector<float> d(W * H);
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            const double tx = (x + 0.5) / W * 2 - 1, ty = 1 - (y + 0.5) / H * 2, sx = eye_x + 0.3 * tx;
            double a = 1.2; int c = 100 + (static_cast<int>((eye_x + 1.2 * tx + 10) * 60) * 7) % 40;
            if (sx >= 0.10 && sx <= 0.30 && std::abs(0.3 * ty) < 0.3) {a = 0.3; c = 40;}
            if (white_border && x == W - 1) c = 254;  // the eye image's border column (its depth is the scene's)
            auto* p = &pixels[(y * W + x) * 4]; p[0] = p[1] = p[2] = static_cast<unsigned char>(c); p[3] = 255; d[y * W + x] = static_cast<float>(0.01 / a);
            if (y == H / 2) f.row[x] = c;
        }
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = W; desc.Height = H; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.Usage = D3D11_USAGE_DEFAULT;
        const D3D11_SUBRESOURCE_DATA data{pixels.data(), W * 4, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &f.image)));
        desc.Format = DXGI_FORMAT_R32_FLOAT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA ddata{d.data(), W * 4, 0};
        CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &ddata, &f.depth)));
        return f;
    };
    const WarpCamera kept = camera({}, {0.032, 0, 0}), fresh = camera({}, {-0.032, 0, 0});
    const auto layers = *stale_eye_layers(kept, square, kept, square, &fresh, square, 2.0);
    StaleEyeWarp warp(device.Get());
    const auto id = *stale_eye_homography({}, square, {}, square);
    const Frame k = render(0.032, false);
    const int first = static_cast<int>(std::ceil(((0.30 - 0.032) / 0.3 + 1) / 2 * W)) + 1;  // the rebuilt eye's first column past the sleeve
    for (const bool border : {false, true}) {
        const Frame f = render(-0.032, border);
        const auto image = read_back(device.Get(), context.Get(), warp.apply(k.image.Get(), id, &layers, {k.depth.Get(), W, H}, f.image.Get(), {f.depth.Get(), W, H}));
        int sleeve = 0, white = 0, other = 0, n = 0;
        for (int x = first; x < static_cast<int>(W) - 1; ++x) {
            const int v = image[((H / 2) * W + x) * 4]; ++n;
            sleeve += v < 60; white += v > 200; other += std::abs(v - k.row[x]) > 8;
            if (std::abs(v - k.row[x]) > 8) std::printf("   column %d: %d (own %d)\n", x, v, k.row[x]);
        }
        std::printf("%s: beside a near sleeve at the edge, %d columns: %d the sleeve, %d white, %d not this eye's own wall\n",
                    border ? "a. fresh image's white border" : "b. fresh eye's sleeve at its edge", n, sleeve, white, other);
        CHECK(n >= 8 && sleeve == 0 && white == 0 && other == 0);
    }
    // d. (v1166, v1165 hold dump 2: a camera cut) the kept camera turned 0.6 rad away: the rebuilt eye's right edge lies outside the kept
    // view and beside the near sleeve; the fresh eye sees it. Not black.
    {
        const WarpCamera turned = camera(yaw(0.6), {0.032, 0, 0});
        const auto cut = *stale_eye_layers(turned, square, kept, square, &fresh, square, 2.0);
        const Frame f = render(-0.032, false);
        const auto image = read_back(device.Get(), context.Get(), warp.apply(k.image.Get(), id, &cut, {k.depth.Get(), W, H}, f.image.Get(), {f.depth.Get(), W, H}));
        int black = 0, n = 0;
        for (int x = first; x < static_cast<int>(W) - 1; ++x) if ((x + 0.5) / W * 2 - 1 + 0.064 / 1.2 <= 0.99) {  // the fresh eye sees its wall point
            ++n; black += image[((H / 2) * W + x) * 4] < 5;
        }
        std::printf("d. camera cut, the kept view turned away: %d of %d columns beside the sleeve (in the fresh view) black\n", black, n);
        CHECK(n >= 3 && black == 0);
    }
    // e. (v1171, v1170 sim teleport and v1165 hold 2) the kept frame is another place entirely (here 245 everywhere, a level the fresh eye
    // never shows), its camera 2 m away: a cut. The rebuilt frame takes nothing from it and nothing black; with no cut the kept image is used.
    {
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = W; desc.Height = H; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.Usage = D3D11_USAGE_DEFAULT;
        std::vector<unsigned char> elsewhere(W * H * 4, 245); const D3D11_SUBRESOURCE_DATA data{elsewhere.data(), W * 4, 0};
        ComPtr<ID3D11Texture2D> other; CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &other)));
        const WarpCamera away = camera(yaw(0.4), {2.032, 0, 1.0});
        auto teleport = *stale_eye_layers(away, square, kept, square, &fresh, square, 2.0);
        CHECK(teleport.cut && !layers.cut);
        const Frame f = render(-0.032, true);
        const auto image = read_back(device.Get(), context.Get(), warp.apply(other.Get(), id, &teleport, {k.depth.Get(), W, H}, f.image.Get(), {f.depth.Get(), W, H}));
        int kept_px = 0, black = 0;
        for (UINT i = 0; i < W * H; ++i) {kept_px += image[i * 4] == 245; black += image[i * 4] < 5;}
        std::printf("e. camera cut (teleport 2 m): %d pixels from the kept frame, %d black\n", kept_px, black);
        CHECK(kept_px == 0 && black == 0 && warp.cut_passes == 1);
    }
}

// v1177 AFW v2 (src/afw2.hpp): the rebuilt eye as a forward splat of its own kept frame. A scene of planes seen by cameras looking down -z
// (tan +-0.5): a checkered wall 3 m away (40/60), a striped object 0.5 m away (140/170), a pole 1.5 cm wide 0.6 m away (220), the player's
// hand (rig, 100/120) 0.45 m away, a striped guard 2.5 m away (175/195) with motion textures. Colour at 256 x 160, depth, masks and motion
// at 0.75 of that (render fraction). The fresh eye is 50 levels brighter: any fresh colour shows.
static void gpu_afw2() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    constexpr UINT W = 256, H = 160, RW = 192, RH = 120;
    const double T = 0.5;
    const WarpFov fov{-std::atan(T), std::atan(T), std::atan(T), -std::atan(T)};
    enum {sky_kind, wall_kind, object_kind, pole_kind, hand_kind, guard_kind, beam_kind, stick_kind};
    // slant: the object's sides lean (x shifts by slant * y); aa: colour anti-aliased (4x4 samples a pixel, as the game's edges are)
    struct Scene {double object = std::nan(""), pole = std::nan(""), hand = std::nan(""), guard = std::nan(""); Vec3 walk{}; int lift = 0; double slant = 0; bool aa = false; double beam = std::nan(""), bar = std::nan(""), stick = std::nan(""), plate = std::nan("");};  // plate: a 60 cm board 0.6 m away whose right edge is at x = plate (90)  // stick: a held 2 cm stick 1 m away (rig, 200)  // bar: a 2 cm post 0.3 m away (before the hand)  // beam: a striped plate seen nearly edge-on, from x = beam at 0.2 m away
    struct Frame {ComPtr<ID3D11Texture2D> image, depth, rig, motion; std::vector<int> grey, kind; std::vector<double> ahead;};
    // what camera c (position, looking down -z) sees through uv: kind, colour, distance ahead
    const auto look = [&](Vec3 c, const Scene& s, double u, double v, int& kind, double& ahead) {
        const double tx = (2 * u - 1) * T, ty = (1 - 2 * v) * T;
        kind = wall_kind; ahead = c.z + 3; double wx = c.x + ahead * tx, wy = c.y + ahead * ty;
        int g = 40 + 20 * ((static_cast<int>(std::floor(wx / 0.04)) + static_cast<int>(std::floor(wy / 0.04))) & 1);
        const auto plane = [&](double z, double x0, double x1, double half, int k, int colour, double lean = 0) {
            const double a = c.z - z; if (a <= 0 || a >= ahead) return;
            const double x = c.x + a * tx, y = c.y + a * ty;
            if (x - lean * y >= x0 && x - lean * y <= x1 && std::abs(y) <= half) {ahead = a; kind = k; g = colour; wx = x - lean * y; wy = y;}
        };
        if (std::isfinite(s.guard)) plane(-2.5, s.guard - 0.25, s.guard + 0.25, 0.4, guard_kind, 0);
        if (std::isfinite(s.pole)) plane(-0.6, s.pole - 0.0075, s.pole + 0.0075, 10, pole_kind, 220);
        if (std::isfinite(s.object)) plane(-0.5, s.object - 0.06, s.object + 0.06, 0.06, object_kind, 0, s.slant);
        if (std::isfinite(s.hand)) plane(-0.45, s.hand - 0.05, s.hand + 0.05, 0.05, hand_kind, 0);
        if (std::isfinite(s.bar)) plane(-0.3, s.bar - 0.01, s.bar + 0.01, 10, pole_kind, 230);
        if (std::isfinite(s.stick)) plane(-1.0, s.stick - 0.01, s.stick + 0.01, 0.15, stick_kind, 200);
        if (std::isfinite(s.plate)) plane(-0.6, s.plate - 0.6, s.plate, 0.3, object_kind, 90);
        if (std::isfinite(s.beam)) {  // z = -0.2 - 2.5 (x - beam): it recedes steeply to the right (seen at a grazing angle)
            const double k = 2.5, a = (c.z + 0.2 + k * (c.x - s.beam)) / (1 - k * tx);
            const double x = c.x + a * tx, y = c.y + a * ty;
            if (a > 0 && a < ahead && x >= s.beam && x <= s.beam + 0.3 && std::abs(y) <= 0.05) {ahead = a; kind = beam_kind; wx = x; g = (static_cast<int>(std::floor((x - s.beam) / 0.01)) & 1) ? 230 : 190;}
        }
        if (kind == guard_kind) g = (static_cast<int>(std::floor((wx - s.guard) / 0.05)) & 1) ? 195 : 175;
        if (kind == object_kind && std::isfinite(s.object)) g = (static_cast<int>(std::floor((wx - s.object) / 0.02)) & 1) ? 170 : 140;
        if (kind == hand_kind) g = (static_cast<int>(std::floor((wx - s.hand) / 0.02)) & 1) ? 120 : 100;
        return std::min(255, g + s.lift);
    };
    const auto render = [&](Vec3 c, const Scene& s) {
        Frame f; f.grey.resize(W * H); f.kind.resize(W * H); f.ahead.resize(W * H);
        std::vector<unsigned char> px(W * H * 4), mask(RW * RH); std::vector<float> d(RW * RH); std::vector<std::uint16_t> m(RW * RH * 4, 0);
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
            int k; double a; int g = look(c, s, (x + 0.5) / W, (y + 0.5) / H, k, a);
            if (s.aa) {int sum = 0, kk; double aa; for (int j = 0; j < 16; ++j) sum += look(c, s, (x + (j % 4 + 0.5) / 4) / W, (y + (j / 4 + 0.5) / 4) / H, kk, aa); g = (sum + 8) / 16;}
            auto* p = &px[(y * W + x) * 4]; p[0] = p[1] = p[2] = static_cast<unsigned char>(g); p[3] = 255;
            f.grey[y * W + x] = g; f.kind[y * W + x] = k; f.ahead[y * W + x] = a;
        }
        for (UINT y = 0; y < RH; ++y) for (UINT x = 0; x < RW; ++x) {
            int k; double a; look(c, s, (x + 0.5) / RW, (y + 0.5) / RH, k, a);
            d[y * RW + x] = static_cast<float>(0.01 / a); mask[y * RW + x] = k == hand_kind || k == stick_kind ? 255 : 0;
            if (k == guard_kind && length(s.walk) > 0) {
                const float v[4]{static_cast<float>(s.walk.x), static_cast<float>(s.walk.y), static_cast<float>(s.walk.z), 8.f};
                for (int i = 0; i < 4; ++i) m[(y * RW + x) * 4 + i] = half_float(v[i]);
            }
        }
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = W; desc.Height = H; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.Usage = D3D11_USAGE_DEFAULT;
        const D3D11_SUBRESOURCE_DATA data{px.data(), W * 4, 0}; CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &f.image)));
        desc.Width = RW; desc.Height = RH; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.Format = DXGI_FORMAT_R32_FLOAT; const D3D11_SUBRESOURCE_DATA dd{d.data(), RW * 4, 0}; CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &dd, &f.depth)));
        desc.Format = DXGI_FORMAT_R8_UNORM; const D3D11_SUBRESOURCE_DATA md{mask.data(), RW, 0}; CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &md, &f.rig)));
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; const D3D11_SUBRESOURCE_DATA vd{m.data(), RW * 8, 0}; CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &vd, &f.motion)));
        return f;
    };
    const auto pose = [](double x) {WorldTrackedPose p; p.position = {x, 0, -0.45}; p.basis = {Vec3{0, 0, -1}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}}; return p;};
    StaleEyeWarp warp(device.Get());
    const auto id = *stale_eye_homography({}, fov, {}, fov);
    struct Run {std::vector<unsigned char> out; std::uint32_t holes;};
    // kept camera kc (hand drawn at kept_hand, its grip at grip now), target tc, fresh fc; masks and motion textures on or off
    // v1190: the rig target the engine draws (this tick's rig, depth-only, in the target camera; no scene), emulated: the hand of `s` from c
    ID3D11Texture2D* rig_target = nullptr;
    const auto target_rig = [&](Vec3 c, double hand, double stick = std::nan("")) {
        std::vector<float> t(RW * RH * 2, 0.f); Scene only; only.hand = hand; only.stick = stick;
        for (UINT y = 0; y < RH; ++y) for (UINT x = 0; x < RW; ++x) {
            int k; double a; look(c, only, (x + 0.5) / RW, (y + 0.5) / RH, k, a);
            if (k == hand_kind || k == stick_kind) {t[(y * RW + x) * 2] = static_cast<float>(0.01 / a); t[(y * RW + x) * 2 + 1] = 255.f;}
        }
        D3D11_TEXTURE2D_DESC desc{}; desc.Width = RW; desc.Height = RH; desc.MipLevels = 1; desc.ArraySize = 1; desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_R32G32_FLOAT; desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA data{t.data(), RW * 8, 0}; ComPtr<ID3D11Texture2D> tex; CHECK(SUCCEEDED(device->CreateTexture2D(&desc, &data, &tex)));
        return tex;
    };
    const auto run = [&](bool afw2, Vec3 kc, Vec3 tc, Vec3 fc, const Frame& k, const Frame& f, double kept_hand, double grip, bool masks, bool motion) {
        WarpCamera kcam = camera({}, kc), tcam = camera({}, tc), fcam = camera({}, fc);
        if (std::isfinite(kept_hand)) {kcam.hand_bits = tcam.hand_bits = 2u; kcam.hands[1] = pose(kept_hand); tcam.hands[1] = pose(grip);}
        auto l = *stale_eye_layers(kcam, fov, tcam, fov, &fcam, fov, 2.0);
        l.afw2 = afw2; l.rig_target = rig_target != nullptr;
        const auto out = read_back(device.Get(), context.Get(), warp.apply(k.image.Get(), id, &l, {k.depth.Get(), RW, RH}, f.image.Get(), {f.depth.Get(), RW, RH},
            motion ? StaleMotion{k.motion.Get(), RW, RH} : StaleMotion{}, motion ? StaleMotion{f.motion.Get(), RW, RH} : StaleMotion{},
            masks ? StaleRig{k.rig.Get(), RW, RH} : StaleRig{}, masks ? StaleRig{f.rig.Get(), RW, RH} : StaleRig{}, StaleRig{rig_target, RW, RH}));
        return Run{out, afw2 && warp.afw2 ? warp.afw2->holes() : 0u};
    };
    // a scored run: wrong = outside the truth's 3x3 range +-12 (silhouettes within a pixel of a depth step skipped); uncovered = truth pixels
    // the kept camera cannot see (hidden there): `fore` counts those showing a nearer kind's colour (a doubled edge), black any black pixel
    struct Score {int n{}, wrong{}, lifted{}, uncovered{}, fore{}, black{};};
    const auto score = [&](const std::vector<unsigned char>& out, const Frame& truth, const Frame& lifted_truth, Vec3 kc, const Scene& kept_scene, int only_kind = -1) {
        Score s;
        for (int y = 1; y < static_cast<int>(H) - 1; ++y) for (int x = 1; x < static_cast<int>(W) - 1; ++x) {
            const int i = y * W + x, o = out[i * 4]; s.black += o < 5;
            bool edge = false; int lo = 255, hi = 0, llo = 255, lhi = 0;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int j = (y + dy) * W + x + dx;
                edge = edge || std::abs(truth.ahead[j] - truth.ahead[i]) > 0.05 * truth.ahead[i];
                lo = std::min(lo, truth.grey[j]); hi = std::max(hi, truth.grey[j]); llo = std::min(llo, lifted_truth.grey[j]); lhi = std::max(lhi, lifted_truth.grey[j]);
            }
            if (edge || (only_kind >= 0 && truth.kind[i] != only_kind)) continue;
            // the truth point, seen from the kept camera: is that what the kept camera sees there?
            const double tx = (2 * (x + 0.5) / W - 1) * T, ty = (1 - 2 * (y + 0.5) / H) * T, a = truth.ahead[i];
            const Vec3 tc{0.032, 0, 0}; const Vec3 p{tc.x + a * tx, a * ty, tc.z - a};
            const double ka = kc.z - p.z, ku = ((p.x - kc.x) / ka / T + 1) / 2, kv = (1 - p.y / ka / T) / 2;
            int kk; double kahead; look(kc, kept_scene, ku, kv, kk, kahead);
            if (ku < 0 || ku > 1 || kv < 0 || kv > 1 || std::abs(kahead - ka) > 0.02 + 0.03 * ka) {
                ++s.uncovered; s.fore += truth.kind[i] == wall_kind && o >= 125;
                if (truth.kind[i] == wall_kind && o >= 125 && s.fore <= 12 && std::getenv("AFW2_VERBOSE")) std::printf("   fore (%d,%d): %d\n", x, y, o);
                continue;
            }
            ++s.n; const bool bad = o < lo - 12 || o > hi + 12; s.wrong += bad; s.lifted += bad && o >= llo - 12 && o <= lhi + 12;
            if (bad && s.wrong <= 12 && std::getenv("AFW2_VERBOSE")) std::printf("   wrong (%d,%d): %d, truth %d..%d kind %d ahead %.2f\n", x, y, o, lo, hi, truth.kind[i], a);
        }
        return s;
    };
    const auto say = [](const char* name, const Score& a, const Score& b, std::uint32_t holes) {
        std::printf("%s: AFW2 %d/%d wrong (%d in the fresh shading), strip %d px: %d foreground, %d black, %u holes after the fill | today %d/%d wrong (%d fresh), strip %d foreground\n",
                    name, a.wrong, a.n, a.lifted, a.uncovered, a.fore, a.black, holes, b.wrong, b.n, b.lifted, b.fore);
    };
    const Vec3 fresh_at{-0.032, 0, 0}, here{0.032, 0, 0};
    // A. a near object before the wall, the camera stepping 2, 5 and 15 cm sideways and forward
    for (const bool forward : {false, true}) for (const double step : {0.02, 0.05, 0.15}) {
        Scene s; s.object = 0.05; Scene lit = s; lit.lift = 50;
        const Vec3 kc = forward ? Vec3{0.032, 0, step} : Vec3{0.032 - step, 0, 0};
        const Frame k = render(kc, s), f = render(fresh_at, lit), truth = render(here, s), lifted = render(here, lit);
        const auto a = run(true, kc, here, fresh_at, k, f, std::nan(""), 0, true, true), b = run(false, kc, here, fresh_at, k, f, std::nan(""), 0, true, true);
        const Score sa = score(a.out, truth, lifted, kc, s), sb = score(b.out, truth, lifted, kc, s);
        char name[96]; std::snprintf(name, sizeof name, "afw2 A. object 0.5 m, wall 3 m, camera %s %.0f cm", forward ? "forward" : "sideways", step * 100);
        say(name, sa, sb, a.holes);
        // (one texel at the strip's far edge may take the stretch a render texel off: 1 in 10 000)
        CHECK(sa.n > 15000 && sa.wrong * 10000 <= sa.n && sa.lifted == 0 && sa.fore == 0 && sa.black == 0 && a.holes == 0);
    }
    // B. a thin pole (1.5 cm, 0.6 m) and the object, the camera 5 cm sideways
    {
        Scene s; s.pole = -0.08; s.object = 0.1; Scene lit = s; lit.lift = 50;
        const Vec3 kc{0.032 - 0.05, 0, 0};
        const Frame k = render(kc, s), f = render(fresh_at, lit), truth = render(here, s), lifted = render(here, lit);
        const auto a = run(true, kc, here, fresh_at, k, f, std::nan(""), 0, true, true), b = run(false, kc, here, fresh_at, k, f, std::nan(""), 0, true, true);
        const Score sa = score(a.out, truth, lifted, kc, s), sb = score(b.out, truth, lifted, kc, s), pole = score(a.out, truth, lifted, kc, s, pole_kind);
        say("afw2 B. thin pole 1.5 cm at 0.6 m, camera 5 cm", sa, sb, a.holes);
        std::printf("   pole pixels: %d/%d wrong\n", pole.wrong, pole.n);
        if (std::getenv("AFW2_VERBOSE")) {
            const auto kept = read_back(device.Get(), context.Get(), k.image.Get());
            for (int x = 70; x < 110; ++x) std::printf("   col %d: out %d truth %d (kind %d) kept %d\n", x, a.out[(80 * W + x) * 4], truth.grey[80 * W + x], truth.kind[80 * W + x], kept[(80 * W + x) * 4]);
        }
        CHECK(pole.n > 300 && pole.wrong == 0 && sa.wrong == 0 && sa.fore == 0 && a.holes == 0);
    }
    // C. a still frame (identical cameras, a still hand drawn, zero motion, masks): this eye's kept image byte for byte
    {
        Scene s; s.object = 0.05; s.hand = -0.1; s.guard = 0.3; Scene lit = s; lit.lift = 50;
        const Frame k = render(here, s), f = render(fresh_at, lit);
        const auto a = run(true, here, here, fresh_at, k, f, -0.1, -0.1, true, true);
        const auto kept = read_back(device.Get(), context.Get(), k.image.Get());
        int differ = 0; for (std::size_t i = 0; i < kept.size(); ++i) differ += a.out[i] != kept[i];
        std::printf("afw2 C. still frame: %d bytes differ from the kept image\n", differ);
        CHECK(differ == 0);
    }
    // D. a moving hand (rig mask), the camera 2 cm sideways: drawn 5 cm right of its kept place; (a) the grip says the same: the hand where it
    // is, in this eye's own shading, wall at its old place; (b) the grip over-moved 10 cm (v1173): the fresh eye's hand where it is, no ghost
    for (const double grip : {0.0, 0.05}) {
        Scene ks; ks.hand = -0.05; ks.object = 0.12; Scene ns = ks; ns.hand = 0.0; Scene lit = ns; lit.lift = 50;
        const Vec3 kc{0.032 - 0.02, 0, 0};
        const Frame k = render(kc, ks), f = render(fresh_at, lit), truth = render(here, ns), lifted = render(here, lit);
        const auto a = run(true, kc, here, fresh_at, k, f, -0.05, grip, true, false);
        const Score sa = score(a.out, truth, lifted, kc, ks);
        int hand = 0, hand_wrong = 0, ghost = 0;
        // within a pixel of the truth (its stripes and outline; render-sized depth settles an outline to a render texel), in either eye's
        // shading where the grip over-moved; ghost: the hand's levels on wall 2 px clear of the hand
        for (int y = 2; y < static_cast<int>(H) - 2; ++y) for (int x = 2; x < static_cast<int>(W) - 2; ++x) {
            const int i = y * W + x, o = a.out[i * 4];
            int lo = 255, hi = 0, llo = 255, lhi = 0; bool clear = true;
            for (int d = -2; d <= 2; ++d) clear = clear && truth.kind[i + d] == wall_kind && truth.kind[i + d * W] == wall_kind;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int j = i + dy * W + dx; lo = std::min(lo, truth.grey[j]); hi = std::max(hi, truth.grey[j]); llo = std::min(llo, lifted.grey[j]); lhi = std::max(lhi, lifted.grey[j]);
            }
            if (truth.kind[i] == hand_kind && truth.kind[i - 1] == hand_kind && truth.kind[i + 1] == hand_kind) {
                ++hand; hand_wrong += !((o >= lo - 12 && o <= hi + 12) || (grip > 0 && o >= llo - 12 && o <= lhi + 12));
            }
            if (clear && o >= 95 && o <= 125) ++ghost;
        }
        std::printf("afw2 D. moving hand, grip %s: hand %d/%d wrong, hand colour on the wall in %d px; scene %d/%d wrong (%d fresh), %u holes\n",
                    grip > 0 ? "over-moved" : "right", hand_wrong, hand, ghost, sa.wrong, sa.n, sa.lifted, a.holes);
        if (std::getenv("AFW2_VERBOSE")) {
            const auto kept = read_back(device.Get(), context.Get(), k.image.Get());
            for (int x = 90; x < 150; ++x) std::printf("   col %d: out %d truth %d (kind %d) kept %d fresh %d\n", x, a.out[(80 * W + x) * 4], truth.grey[80 * W + x], truth.kind[80 * W + x], kept[(80 * W + x) * 4], f.grey[80 * W + x]);
        }
        CHECK(hand > 300 && hand_wrong == 0 && ghost == 0 && a.holes == 0);
        if (grip == 0.0) CHECK(sa.lifted == 0);
    }
    // F. (v1186 in-game: silhouettes stair-stepped under a 7 mm step) a leaning object 0.5 m away before the wall, anti-aliased like the
    // game's edges; the camera steps 1 mm (under a texel) and 2 cm sideways and up: along its outline (its own pixels within a pixel of the
    // wall in the truth) the rebuilt edge must follow the truth's blend, not render-texel steps (mean error, and pixels 30+ levels off)
    for (const double step : {0.001, 0.02}) {
        Scene s; s.object = 0.05; s.slant = 0.45; s.aa = true; Scene lit = s; lit.lift = 50;
        const Vec3 kc{0.032 - step, -step, 0};
        const Frame k = render(kc, s), f = render(fresh_at, lit), truth = render(here, s);
        const auto a = run(true, kc, here, fresh_at, k, f, std::nan(""), 0, true, true), b = run(false, kc, here, fresh_at, k, f, std::nan(""), 0, true, true);
        const auto edge = [&](const std::vector<unsigned char>& out, double& mean, int& steps) {
            int n = 0; double sum = 0; steps = 0;
            for (int y = 2; y < static_cast<int>(H) - 2; ++y) for (int x = 2; x < static_cast<int>(W) - 2; ++x) {
                bool blend = false;
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    const int j = (y + dy) * W + x + dx;
                    blend = blend || (truth.kind[j] != truth.kind[y * W + x]);
                }
                if (!blend || truth.kind[y * W + x] != object_kind) continue;  // the object's side of its outline (the wall's side may be uncovered)
                const int e = std::abs(out[(y * W + x) * 4] - truth.grey[y * W + x]);
                ++n; sum += e; steps += e >= 30;
            }
            mean = n ? sum / n : 0;
        };
        double ma, mb; int sa, sb; edge(a.out, ma, sa); edge(b.out, mb, sb);
        std::printf("afw2 F. leaning anti-aliased edge, camera %.0f mm sideways and up: outline error AFW2 %.1f levels (%d px 30+ off) | today %.1f (%d)\n",
                    step * 1000, ma, sa, mb, sb);
        CHECK(ma < 12 && sa < 40);
    }
    // G. (v1183 street 11: a beam 16 cm from the eye splatted as dots) a striped plate seen nearly edge-on 0.2-1 m away, the camera 5 mm
    // forward and sideways: every beam pixel of the truth shows the beam (no wall through it)
    for (const bool forward : {false, true}) {
        Scene s; s.beam = 0.05; Scene lit = s; lit.lift = 50;
        const Vec3 kc = forward ? Vec3{0.032, 0, 0.005} : Vec3{0.027, 0, 0};
        const Frame k = render(kc, s), f = render(fresh_at, lit), truth = render(here, s);
        const auto a = run(true, kc, here, fresh_at, k, f, std::nan(""), 0, true, true);
        int n = 0, through = 0;
        for (int y = 2; y < static_cast<int>(H) - 2; ++y) for (int x = 2; x < static_cast<int>(W) - 2; ++x) {
            bool inner = true; for (int d = -2; d <= 2; ++d) inner = inner && truth.kind[y * W + x + d] == beam_kind && truth.kind[(y + d) * W + x] == beam_kind;
            if (!inner) continue;
            ++n; through += a.out[(y * W + x) * 4] < 120;  // the wall (40/60) instead of the beam (190/230)
        }
        std::printf("afw2 G. plate seen edge-on, camera 5 mm %s: wall through the plate in %d of %d px\n", forward ? "forward" : "sideways", through, n);
        CHECK(n > 200 && through == 0);
    }
    // H. (v1183 street 21) a hand at the rebuilt eye's right edge, where the fresh eye has no view, drawn 3 cm on, its grip 8 cm (it stands
    // down): its pixels outside the fresh view stay this eye's own hand, not the wall behind it
    {
        Scene ks; ks.hand = 0.21; Scene ns = ks; ns.hand = 0.24; Scene lit = ns; lit.lift = 50;
        const Frame k = render(here, ks), f = render(fresh_at, lit), truth = render(here, ns);
        const auto a = run(true, here, here, fresh_at, k, f, 0.21, 0.29, true, false);
        int n = 0, wall = 0;
        for (int y = 2; y < static_cast<int>(H) - 2; ++y) for (int x = 2; x < static_cast<int>(W) - 2; ++x) {
            const double tx = (2 * (x + 0.5) / W - 1) * T, fresh_tx = (0.032 + 0.45 * tx + 0.032) / 0.45;  // the hand point, seen from the fresh eye
            if (truth.kind[y * W + x] != hand_kind || k.kind[y * W + x] != hand_kind || std::abs(fresh_tx) <= T) continue;
            ++n; wall += a.out[(y * W + x) * 4] < 80;
        }
        std::printf("afw2 H. moving hand outside the fresh view, grip over-moved: wall instead of the hand in %d of %d px\n", wall, n);
        CHECK(n > 50 && wall == 0);
    }
    // I. (v1187 thresholds) a guard 2.5 m away walking 1.5 cm a tick (2.3 render px over the eye's two ticks, under the old 3 px gate),
    // the camera still: at its new place, the wall at its old place
    {
        Scene ks; ks.guard = 0.015; ks.walk = {-0.03, 0, 0}; Scene ns = ks; ns.guard = 0.0; Scene lit = ns; lit.lift = 50;
        const Frame k = render(here, ks), f = render(fresh_at, lit), truth = render(here, ns);
        const auto a = run(true, here, here, fresh_at, k, f, std::nan(""), 0, true, true);
        int n = 0, off = 0;
        for (int y = 2; y < static_cast<int>(H) - 2; ++y) for (int x = 2; x < static_cast<int>(W) - 2; ++x) {
            const int i = y * W + x;
            if (truth.kind[i] != guard_kind || truth.kind[i - 2] != guard_kind || truth.kind[i + 2] != guard_kind) continue;
            int lo = 255, hi = 0; for (int d = -1; d <= 1; ++d) {lo = std::min(lo, truth.grey[i + d]); hi = std::max(hi, truth.grey[i + d]);}
            ++n; off += a.out[i * 4] < lo - 6 || a.out[i * 4] > hi + 6;
        }
        std::printf("afw2 I. guard walking 1.5 cm a tick: %d of %d guard px off its stripes at its new place\n", off, n);
        CHECK(n > 1000 && off * 100 <= n);
    }
    // R. (v1190) the rig target: a hand drawn 5 cm on (camera 2 cm sideways), its recorded grip right, over-moved (10 cm) or still, a post
    // 0.3 m away before part of it; both colour orders. The hand where the target has it, in this eye's own shading where its kept texels
    // are confirmed (order 0, grip right), no hand outside the target silhouette, the post in front, nothing of the wall inside the hand.
    for (const int order : {0, 1}) for (const double grip : {0.0, 0.05, -0.05}) {
        Scene ks; ks.hand = -0.05; ks.bar = -0.02; Scene ns = ks; ns.hand = 0.0; Scene lit = ns; lit.lift = 50;
        const Vec3 kc{0.032 - 0.02, 0, 0};
        const Frame k = render(kc, ks), f = render(fresh_at, lit), truth = render(here, ns), lifted = render(here, lit);
        const auto rt = target_rig(here, ns.hand); rig_target = rt.Get();
        if (!warp.afw2) run(true, kc, here, fresh_at, k, f, -0.05, grip, true, false);  // (makes the pass)
        warp.afw2->rig_order = order;
        const auto a = run(true, kc, here, fresh_at, k, f, -0.05, grip, true, false);
        rig_target = nullptr; warp.afw2->rig_order = 0;
        int hand = 0, wrong = 0, off = 0, own = 0, seen = 0, ghost = 0, bar = 0, bar_wrong = 0;
        for (int y = 2; y < static_cast<int>(H) - 2; ++y) for (int x = 2; x < static_cast<int>(W) - 2; ++x) {
            const int i = y * W + x, o = a.out[i * 4];
            int lo = 255, hi = 0, llo = 255, lhi = 0; bool clear = true, inner = true;
            for (int d = -2; d <= 2; ++d) {clear = clear && truth.kind[i + d] != hand_kind && truth.kind[i + d * W] != hand_kind; inner = inner && truth.kind[i + d] == truth.kind[i] && truth.kind[i + d * W] == truth.kind[i];}
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int j = i + dy * W + dx; lo = std::min(lo, truth.grey[j]); hi = std::max(hi, truth.grey[j]); llo = std::min(llo, lifted.grey[j]); lhi = std::max(lhi, lifted.grey[j]);
            }
            if (truth.kind[i] == hand_kind && inner) {
                ++hand; const bool kept_ok = o >= lo - 12 && o <= hi + 12, fresh_ok = o >= llo - 12 && o <= lhi + 12;
                // wrong: not the hand at all (another surface's colour); off: the hand, but not its stripe here (where both eyes' views of
                // this spot are hidden behind the post, the hand's nearest own pixels stand in)
                const bool handish = (o >= 88 && o <= 132) || (o >= 138 && o <= 182);
                wrong += !handish && !kept_ok && !fresh_ok; off += handish && !kept_ok && !fresh_ok;
                // this hand point a tick ago (the hand 5 cm left), seen from the kept camera: did the kept frame show it?
                const double tx = (2 * (x + 0.5) / W - 1) * T, ty = (1 - 2 * (y + 0.5) / H) * T;
                const double px = here.x + 0.45 * tx - 0.05, py = 0.45 * ty;
                int kk; double ka; look(kc, ks, ((px - kc.x) / 0.45 / T + 1) / 2, (1 - py / 0.45 / T) / 2, kk, ka);
                if (kk == hand_kind) {++seen; own += kept_ok;}
                if (!handish && !kept_ok && !fresh_ok && wrong <= 12 && std::getenv("AFW2_VERBOSE")) std::printf("   R wrong (%d,%d): %d truth %d kept %d\n", x, y, o, truth.grey[i], k.grey[i]);
            }
            if (clear && truth.kind[i] == wall_kind && (std::abs(o - 100) <= 3 || std::abs(o - 120) <= 3) && !(o >= lo - 12 && o <= hi + 12) && !(o >= llo - 12 && o <= lhi + 12)) ++ghost;  // (the hand's levels, not the wall's in either eye)
            if (truth.kind[i] == pole_kind && inner) {++bar; bar_wrong += o < 200;}
        }
        std::printf("afw2 R. rig target, order %s, grip %s: hand %d/%d not the hand (%d off its stripe), of %d the kept frame showed %d in this eye's shading; hand on the wall %d px; post in front %d/%d wrong\n",
                    order ? "fresh first" : "kept first", grip > 0 ? "over-moved" : grip < 0 ? "still" : "right", wrong, hand, off, seen, own, ghost, bar_wrong, bar);
        if (std::getenv("AFW2_VERBOSE")) for (int x = 90; x < 160; ++x) std::printf("   col %d: out %d truth %d (kind %d) lifted %d kept %d\n", x, a.out[(80 * W + x) * 4], truth.grey[80 * W + x], truth.kind[80 * W + x], lifted.grey[80 * W + x], k.grey[80 * W + x]);
        // (over-moved: the hand stands down; where the post hides it from the fresh eye too, its nearest rig pixels stand in, stripes and all)
        CHECK(hand > 300 && (wrong == 0 || (grip != 0.0 && wrong * 50 <= hand)) && (off * 20 <= hand || grip != 0.0) && ghost == 0 && bar > 50 && bar_wrong == 0);
        if (order == 0 && grip == 0.0) CHECK(seen > 300 && own * 100 >= seen * 95);
    }
    // S. (v1190 swing 7: a block of stretched sword colour beside the sleeve) the hand moves 6 cm left before a board 0.6 m away; a held stick
    // 1 m away shows beside the board's edge: the place the hand left shows the board, never the stick's colour stretched into it (the
    // farthest fill seed there is the stick: a rig texel is no fill source)
    {
        Scene ks; ks.hand = 0.0; ks.stick = 0.11; ks.plate = 0.06; Scene ns = ks; ns.hand = -0.06; Scene lit = ns; lit.lift = 50;
        const Frame k = render(here, ks), f = render(fresh_at, lit), truth = render(here, ns);
        const auto rt = target_rig(here, ns.hand, ns.stick); rig_target = rt.Get();
        const auto a = run(true, here, here, fresh_at, k, f, 0.0, -0.06, true, false);
        rig_target = nullptr;
        int n = 0, stick = 0;
        for (int y = 2; y < static_cast<int>(H) - 2; ++y) for (int x = 2; x < static_cast<int>(W) - 2; ++x) {
            const int i = y * W + x;
            if (k.kind[i] != hand_kind || truth.kind[i] == hand_kind || truth.kind[i] == stick_kind) continue;
            ++n; stick += a.out[i * 4] >= 150;
        }
        std::printf("afw2 S. place a hand left beside a held stick: stick colour in %d of %d px\n", stick, n);
        CHECK(n > 300 && stick == 0);
    }
    // P. (v1191 in-game swing 8: a phantom second sword in the rig target) the target holds a held stick that neither eye ever drew: its
    // pixels show the world behind (the wall), never inpainted rig colour or noise
    {
        Scene ks; ks.hand = -0.05; Scene ns = ks; ns.hand = 0.0; Scene lit = ns; lit.lift = 50;
        const Vec3 kc{0.032 - 0.01, 0, 0};
        const Frame k = render(kc, ks), f = render(fresh_at, lit), truth = render(here, ns);
        const auto rt = target_rig(here, ns.hand, 0.2); rig_target = rt.Get();  // the hand, and a stick 1 m away at x 0.2 nobody drew
        const auto a = run(true, kc, here, fresh_at, k, f, -0.05, 0.0, true, false);
        rig_target = nullptr;
        Scene only; only.stick = 0.2; const Frame phantom = render(here, only);
        int n = 0, bad = 0;
        for (int y = 2; y < static_cast<int>(H) - 2; ++y) for (int x = 2; x < static_cast<int>(W) - 2; ++x) {
            const int i = y * W + x;
            if (phantom.kind[i] != stick_kind || truth.kind[i] != wall_kind) continue;
            int lo = 255, hi = 0; for (int d = -1; d <= 1; ++d) {lo = std::min({lo, truth.grey[i + d], truth.grey[i + d * W]}); hi = std::max({hi, truth.grey[i + d], truth.grey[i + d * W]});}
            ++n; bad += a.out[i * 4] < lo - 12 || a.out[i * 4] > hi + 12;
        }
        std::printf("afw2 P. a phantom stick in the rig target: %d of %d of its px not the wall behind it\n", bad, n);
        CHECK(n > 200 && bad == 0);
    }
    // W. (v1197 in-game, street 4-9 and room 3/4/6/10/12/14/15: the engine's rig target drawn ~290 texels off, a camera one IPD beyond the
    // fresh eye; both hands came out as striped floor) a still hand, the camera 1 cm sideways, the target drawn from a camera 12 cm to the
    // side: the target as a whole disagrees with this eye's rig, so it is not used; the hand stays this eye's own, no hand where the target
    // put one
    {
        Scene ks; ks.hand = 0.0; Scene ns = ks; Scene lit = ns; lit.lift = 50;
        const Vec3 kc{0.032 - 0.01, 0, 0};
        const Frame k = render(kc, ks), f = render(fresh_at, lit), truth = render(here, ns);
        const auto rt = target_rig(Vec3{here.x + 0.12, here.y, here.z}, ns.hand); rig_target = rt.Get();
        const auto a = run(true, kc, here, fresh_at, k, f, 0.0, 0.0, true, false);
        rig_target = nullptr;
        int hand = 0, wrong = 0, stray = 0, wall = 0;
        for (int y = 2; y < static_cast<int>(H) - 2; ++y) for (int x = 2; x < static_cast<int>(W) - 2; ++x) {
            const int i = y * W + x, o = a.out[i * 4];
            int lo = 255, hi = 0; bool inner = true;
            for (int d = -2; d <= 2; ++d) inner = inner && truth.kind[i + d] == truth.kind[i] && truth.kind[i + d * W] == truth.kind[i];
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {lo = std::min(lo, truth.grey[i + dy * W + dx]); hi = std::max(hi, truth.grey[i + dy * W + dx]);}
            if (!inner) continue;
            if (truth.kind[i] == hand_kind) {++hand; wrong += o < lo - 12 || o > hi + 12;}
            if (truth.kind[i] == wall_kind) {++wall; stray += o < lo - 12 || o > hi + 12;}
        }
#ifdef AFW2_NO_STATS
        const std::array<std::uint32_t, 4> st{2, 0, 0, 0};   // (the committed pass before v1197's stats: the image checks only)
#else
        const auto st = warp.afw2->rig_stats_wait();
#endif
        std::printf("afw2 W. rig target drawn from the wrong camera: hand %d of %d px wrong, wall %d of %d px wrong; stats: target %s, %u sampled target px, %u to the world\n",
                    wrong, hand, stray, wall, st[0] == 1 ? "used" : st[0] == 2 ? "ignored" : "none", st[1], st[2]);
        CHECK(hand > 300 && wrong * 100 <= hand && stray * 200 <= wall && st[0] == 2);
    }
#ifndef AFW2_NO_STATS
    // W'. the right target in the same scene: used, almost none of its pixels resolved to the world
    {
        Scene ks; ks.hand = 0.0; Scene ns = ks; Scene lit = ns; lit.lift = 50;
        const Vec3 kc{0.032 - 0.01, 0, 0};
        const Frame k = render(kc, ks), f = render(fresh_at, lit);
        const auto rt = target_rig(here, ns.hand); rig_target = rt.Get();
        run(true, kc, here, fresh_at, k, f, 0.0, 0.0, true, false);
        rig_target = nullptr;
        const auto st = warp.afw2->rig_stats_wait();
        std::printf("afw2 W'. the right rig target: %s, %u sampled target px, %u to the world\n", st[0] == 1 ? "used" : st[0] == 2 ? "ignored" : "none", st[1], st[2]);
        CHECK(st[0] == 1 && st[1] > 20 && st[2] * 50 <= st[1]);
    }
#endif
    // E. a guard 2.5 m away walking 20 cm a tick (motion textures, both eyes), the camera 2 cm sideways: at its new place in this eye's own
    // shading, the wall at its old place. E'. the kept eye's motion texture says nothing (a vector it lacks: the guard just started): the fresh
    // eye's motion is handed to the kept texels it came from (scatter) and confirmed the same way
    for (const bool kept_vectors : {true, false}) {
        Scene ks; ks.guard = 0.2; ks.walk = {-0.4, 0, 0}; Scene ns = ks; ns.guard = 0.0; Scene lit = ns; lit.lift = 50;
        const Vec3 kc{0.032 - 0.02, 0, 0};
        Scene kv = ks; if (!kept_vectors) kv.walk = {};
        const Frame k = render(kc, kv), f = render(fresh_at, lit), truth = render(here, ns), lifted = render(here, lit);
        const auto a = run(true, kc, here, fresh_at, k, f, std::nan(""), 0, true, true), b = run(false, kc, here, fresh_at, k, f, std::nan(""), 0, true, true);
        const Score sa = score(a.out, truth, lifted, kc, ks), sb = score(b.out, truth, lifted, kc, ks);
        int guard = 0, guard_wrong = 0, ghost = 0;
        for (int y = 1; y < static_cast<int>(H) - 1; ++y) for (int x = 2; x < static_cast<int>(W) - 2; ++x) {
            const int i = y * W + x, o = a.out[i * 4];
            if (truth.kind[i] == guard_kind && truth.kind[i - 2] == guard_kind && truth.kind[i + 2] == guard_kind && truth.kind[i - 2 * W] == guard_kind && truth.kind[i + 2 * W] == guard_kind) {
                int lo = 255, hi = 0; for (int d = -1; d <= 1; ++d) {lo = std::min({lo, truth.grey[i + d], truth.grey[i + d * W]}); hi = std::max({hi, truth.grey[i + d], truth.grey[i + d * W]});}
                ++guard; guard_wrong += o < lo - 12 || o > hi + 12;  // within a pixel of the truth's stripes, this eye's levels
                if ((o < lo - 12 || o > hi + 12) && std::getenv("AFW2_VERBOSE") && guard_wrong < 25) std::printf("   guard (%d,%d): %d truth %d..%d\n", x, y, o, lo, hi);
            }
            if (truth.kind[i] == wall_kind && truth.kind[i - 2] == wall_kind && truth.kind[i + 2] == wall_kind && o >= 150) ++ghost;
        }
        say(kept_vectors ? "afw2 E. guard walking 20 cm, camera 2 cm" : "afw2 E'. the same, no kept vectors (scatter)", sa, sb, a.holes);
        std::printf("   guard %d/%d off its own shading at its place, guard colour on the wall in %d px\n", guard_wrong, guard, ghost);
        if (std::getenv("AFW2_VERBOSE") && !kept_vectors)
            for (int x = 90; x < 200; x += 1) std::printf("   col %d: out %d truth %d (kind %d) kept %d\n", x, a.out[(80 * W + x) * 4], truth.grey[80 * W + x], truth.kind[80 * W + x], k.grey[80 * W + x]);
        CHECK(guard > 1000 && guard_wrong == 0 && ghost == 0 && a.holes == 0);
    }
}

// (gpu_afw2 F is inside gpu_afw2: see "F.")
// v1196 (an in-game AFW v2 pass failed on first use: a shader that only that DLL compiled): the game's path at the game's sizes. Every AFW2
// shader is compiled by the same class with the same flags; here all its inputs are bound at the headset sizes (eye images 2496 x 2688,
// render-sized depth, motion, rig masks and rig target 1922 x 2070, camera moving, a hand moving, rig target on) for one dry pass each way
// (rig target on and off, moving and still), and the pass must run (no throw, no fallback) and leave no black.
static void gpu_afw2_full() {
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    CHECK(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)));
    constexpr UINT W = 2496, H = 2688, RW = 1922, RH = 2070;
    const auto make = [&](UINT w, UINT h, DXGI_FORMAT f, UINT bpp, const void* fill) {
        std::vector<unsigned char> data(static_cast<std::size_t>(w) * h * bpp);
        for (std::size_t i = 0; i < data.size(); i += bpp) std::memcpy(&data[i], fill, bpp);
        D3D11_TEXTURE2D_DESC d{}; d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.SampleDesc.Count = 1; d.Format = f; d.Usage = D3D11_USAGE_DEFAULT;
        d.BindFlags = f == DXGI_FORMAT_R8G8B8A8_UNORM ? 0 : D3D11_BIND_SHADER_RESOURCE;  // eye images without a read view, as GameXr keeps them
        const D3D11_SUBRESOURCE_DATA sd{data.data(), w * bpp, 0}; ComPtr<ID3D11Texture2D> t; CHECK(SUCCEEDED(device->CreateTexture2D(&d, &sd, &t))); return t;
    };
    const unsigned char grey[4]{90, 90, 90, 255}; const float depth = 0.01f / 2.0f; const std::uint16_t still[4]{0, 0, 0, 0};
    const unsigned char rig = 255; const float target[2]{0.01f / 0.5f, 255.f};
    const auto kept = make(W, H, DXGI_FORMAT_R8G8B8A8_UNORM, 4, grey), fresh = make(W, H, DXGI_FORMAT_R8G8B8A8_UNORM, 4, grey);
    const auto kd = make(RW, RH, DXGI_FORMAT_R32_FLOAT, 4, &depth), fd = make(RW, RH, DXGI_FORMAT_R32_FLOAT, 4, &depth);
    const auto km = make(RW, RH, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, still), fm = make(RW, RH, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, still);
    const auto kr = make(RW, RH, DXGI_FORMAT_R8_UNORM, 1, &rig), fr = make(RW, RH, DXGI_FORMAT_R8_UNORM, 1, &rig);
    const auto rt = make(RW, RH, DXGI_FORMAT_R32G32_FLOAT, 8, target);
    const double T = 0.8; const WarpFov fov{-std::atan(T), std::atan(T), std::atan(T), -std::atan(T)};
    const auto pose = [](double x) {WorldTrackedPose p; p.position = {x, 0, -0.45}; p.basis = {Vec3{0, 0, -1}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}}; return p;};
    StaleEyeWarp warp(device.Get());
    const auto id = *stale_eye_homography({}, fov, {}, fov);
    int runs = 0;
    for (const bool moving : {true, false}) for (const bool with_target : {true, false}) {
        WarpCamera kc = camera({}, {moving ? 0.027 : 0.032, 0, 0}), tc = camera({}, {0.032, 0, 0}), fc = camera({}, {-0.032, 0, 0});
        kc.hand_bits = tc.hand_bits = 2u; kc.hands[1] = pose(0.0); tc.hands[1] = pose(moving ? 0.03 : 0.0);
        auto l = *stale_eye_layers(kc, fov, tc, fov, &fc, fov, 2.0);
        l.afw2 = true; l.rig_target = with_target;
        const auto passes = warp.afw2 ? warp.afw2->passes : 0;
        ID3D11Texture2D* out = warp.apply(kept.Get(), id, &l, {kd.Get(), RW, RH}, fresh.Get(), {fd.Get(), RW, RH}, {km.Get(), RW, RH}, {fm.Get(), RW, RH},
                                          {kr.Get(), RW, RH}, {fr.Get(), RW, RH}, with_target ? StaleRig{rt.Get(), RW, RH} : StaleRig{});
        CHECK(out && warp.afw2 && warp.afw2->passes == passes + 1);
        const auto img = read_back(device.Get(), context.Get(), out);
        std::size_t black = 0; for (std::size_t i = 0; i < img.size(); i += 4) black += img[i] < 5;
        CHECK(black == 0);
        ++runs;
    }
    std::printf("afw2 full size (2496 x 2688, render 1922 x 2070): %d dry passes, all inputs bound, rig target on and off, no fallback, no black\n", runs);
}

// v802: the drawn hands held still from the kept frame's Setup to the target's (what is within reach may show from the kept image)
static void hands_still() {
    WarpCamera a = camera({}, {0, 0, 0}), b = a;
    a.hand_bits = b.hand_bits = 3u;
    for (unsigned side = 0; side < 2; ++side) {a.hands[side].position = {0.2 * side, 1.1, -0.4}; a.hands[side].basis = {Vec3{0, 0, -1}, Vec3{-1, 0, 0}, Vec3{0, 1, 0}};}
    b.hands = a.hands;
    CHECK(stale_hands_still(a, b));
    b.hands[1].position.x += 0.003; CHECK(stale_hands_still(a, b));   // 3 mm: still
    {const auto m = stale_hands_motion(a, b); CHECK(close_to(m[0], 0.003, 1e-9) && close_to(m[1], 0, 1e-6));}
    b.hands[1].position.x += 0.002; CHECK(!stale_hands_still(a, b));  // 5 mm: moved
    b.hands = a.hands; const double turn = 3.0 * 3.14159265358979 / 180;
    b.hands[0].basis[0] = {std::sin(turn), 0, -std::cos(turn)}; CHECK(!stale_hands_still(a, b));  // turned 3 degrees
    b.hands = a.hands; b.hand_bits = 1u; CHECK(!stale_hands_still(a, b));  // a hand drawn in only one frame
    CHECK(!std::isfinite(stale_hands_motion(a, b)[0]));
    a.hand_bits = b.hand_bits = 0u; CHECK(stale_hands_still(a, b));         // no hands drawn
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // (a crash leaves every line it printed)
    if (argc > 1 && std::strcmp(argv[1], "afw2") == 0) {gpu_afw2_full();gpu_afw2(); std::puts("PASS: afw2"); return 0;}  // v1177: the AFW2 cases alone
    math();
    gpu();
    parallax_math();
    gpu_parallax();
    fresh_math();
    gpu_fresh();
    gpu_turn();
    gpu_movers();
    gpu_rig();
    gpu_edges();
    try {gpu_afw2_full();} catch (const std::exception& e) {std::fprintf(stderr, "gpu_afw2_full threw: %s\n", e.what()); return 1;}
    try {gpu_afw2();} catch (const std::exception& e) {std::fprintf(stderr, "gpu_afw2 threw: %s\n", e.what()); return 1;}
    uncant_math();
    hands_still();
    std::puts("PASS: stale eye warp (homography: identity, yaw, pitch, relative rotation, round trip, behind, invalid; WARP: identity copy, line shift on both read paths;"
              " v796 parallax: still, turn, side and forward steps, reticle round trip, refusals; WARP: two depths, no depth, anchor turn, wrong format, floor sprint;"
              " v798 fresh layer: a point now through the other eye, near range 0; WARP: hand now, wall where it was and beside it, near range 0;"
              " v801 moved hand's old place; v802 still hand = this eye's own frame, moving hand from the fresh eye, hands-still test;"
              " v805 beyond reach this eye's own frame, walking hand without its second outline, in this eye's own wall;"
              " v806 canted fresh layer; v807 snap turn: beyond the kept frame the fresh eye; v808 uncant to the located view;"
              " v809 hand at 18 cm; v810 tracked hand moved with its grip in this eye's own shading;"
              " v811 each kept pixel moves with its owner alone, a still hand beside a moving one, hands where the fresh eye sees them (fit);"
              " v813 no hand where its recorded motion put it and the fresh eye sees past it;"
              " v1150 movers: a walking guard at its new place in its own shading, wall where it was, other eye brighter, half hidden by a pillar (M3),"
              " accelerating, gone; zero motion and rules off byte-identical; a still hand unchanged;"
              " v1159 rig masks: a still prop and a wall beside a moving palm stay, a forearm and a held stick move whole, a still hand exact, the view corner;"
              " v1162 a ray leaving the fresh image meets nothing there (no sleeve, no white border beside a near edge))");
}
