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
    const auto layers = *stale_eye_layers(kept, square, target, square, &fresh, square, 2.0);
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

int main() {
    math();
    gpu();
    parallax_math();
    gpu_parallax();
    fresh_math();
    gpu_fresh();
    gpu_turn();
    gpu_movers();
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
              " accelerating, gone; zero motion and rules off byte-identical; a still hand unchanged)");
}
