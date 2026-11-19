// Spacewarp plan step 0a (docs/SPACEWARP-PLAN-2026-11-19.md): how close does a frame extrapolated forward come to the next real frame?
//
//   spacewarp_replay <dump dir> <out dir> [N ...]
//
// Input: performance-mode stale-warp dumps (capture_stale_warp in src/dxgi_probe.cpp, knob build/stale-warp-dump "... full"). Of the
// rebuilt eye E a dump holds E's render at N-1 (-kept.rgba; its depth, motion texture and rig mask at render size) and E's next render
// at N+1 (-next.rgba). Every method below makes E's image at N+1 from the N-1 render alone and is scored against -next.rgba. That is an
// extrapolation over two Presents (22 ms at 90 Hz); spacewarp at half rate needs one, so these scores are the harder case.
//
// Methods:
//   held  the kept image unchanged
//   rot   rotation-only reprojection, every pixel a direction (a runtime compositor without app depth)
//   pos   positional reprojection: every kept texel splatted as still world at its depth (a runtime that takes app depth)
//   sw    own spacewarp: as pos, plus every texel with its own motion (world movers; rig texels of class 255) advanced by its motion
//         texture (needs kept_camera). The rig (arms, hands, held items) by SW_RIG:
//           auto (default): hands when the dump has exact cameras (v1415+), else play;
//           hands: each drawn hand rigidly from its kept to its next drawn pose (real spacewarp has the controllers at the slot's
//                  time), the rest of the rig by its own motion, else with the play space (play_kept -> next_play origin);
//           play: by the camera's step but not its turn (tracked hands go along with locomotion, not with a head turn; exact when the
//                 head does not move within the play space and there is no smooth turn);
//           world: left in the world; carried: kept at its place in the view.
//         2026-11-19: carried lost on still-standing dumps (head turns), world lost while walking (the sleeve left behind).
//   swo   sw with the rig fitted to the next frame: three rigid groups (each drawn hand, the rest of the rig) aligned like the camera.
//         An oracle for the exact hand poses real spacewarp has (the controllers at the slot's time), so an upper bound for the rig.
// pos and sw splat at render size (nearest texel, depth test on 1/z), fill holes from the farthest neighbour (the background is
// stretched, never the foreground), then resolve each display pixel by its cell's depth back into the kept image (bilinear, linear
// light); a filled cell shows its seed's own kept colour.
//
// The camera at N+1: exact when the meta has kept_camera and next_camera (v1415+: world poses, rows forward, left, up as WarpCamera).
// Else estimated: the world layer's kept -> N step repeated (constant velocity), then refined by direct photometric alignment of the
// still world (non-rig, non-mover texels with depth) between kept and next: 6 parameters, Gauss-Newton with Huber weights, coarse to
// fine. The alignment runs either way and reports its error against exact cameras. Movers need the world -> camera rotation
// (kept_camera); without it they are treated as still (the dump's lines say so).
// SW_A=<a>: the share of the motion texture's span to advance movers (default 1: the texture spans E's previous render to N-1, two
// Presents, the same as N-1 to N+1).
//
// Scores: mean |R - next| in 8-bit levels per region of the target image (disjoint, first wins): border (48 px frame), rig and movers
// (sw's splat of the kept rig mask / movers, dilated 4 px), edge (within 6 px of a depth step > 15 %), near (< 1.5 m), far; all;
// ghost % = of the pixels where kept and next differ by > 16 levels, those where R stays 8+ levels closer to kept than to next.
// Sheets (<out>/spacewarp-N.png): kept | rot | pos | sw | swo | next at the 4 worst 64 px tiles of sw, the outer ring of tiles left out
// (what turned into view: no extrapolation can know it).
//
// Read-only: it reads dumps and writes into <out dir>; nothing touches the game.
#define _CRT_SECURE_NO_WARNINGS  // std::getenv
#include "replay_io.hpp"
#include <array>
#include <cmath>
#include <map>
#include <numeric>

using V3 = std::array<double, 3>;
using M3 = std::array<double, 9>;  // row-major
static V3 add(const V3& a, const V3& b) {return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};}
static V3 sub(const V3& a, const V3& b) {return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};}
static V3 scale(const V3& a, double s) {return {a[0] * s, a[1] * s, a[2] * s};}
static double dot(const V3& a, const V3& b) {return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];}
static double norm(const V3& a) {return std::sqrt(dot(a, a));}
static V3 mulv(const M3& m, const V3& v) {return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2], m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};}
static V3 mulvt(const M3& m, const V3& v) {return {m[0] * v[0] + m[3] * v[1] + m[6] * v[2], m[1] * v[0] + m[4] * v[1] + m[7] * v[2], m[2] * v[0] + m[5] * v[1] + m[8] * v[2]};}
static M3 mul(const M3& a, const M3& b) {M3 r{}; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) for (int k = 0; k < 3; ++k) r[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j]; return r;}
static M3 rodrigues(const V3& w) {  // rotation by the vector w (radians)
    const double t = norm(w); if (t < 1e-15) return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    const V3 k = scale(w, 1 / t); const double c = std::cos(t), s = std::sin(t), C = 1 - c;
    return {c + k[0] * k[0] * C, k[0] * k[1] * C - k[2] * s, k[0] * k[2] * C + k[1] * s,
            k[1] * k[0] * C + k[2] * s, c + k[1] * k[1] * C, k[1] * k[2] * C - k[0] * s,
            k[2] * k[0] * C - k[1] * s, k[2] * k[1] * C + k[0] * s, c + k[2] * k[2] * C};
}
static M3 orthonormal(M3 m) {  // Gram-Schmidt on the columns
    V3 a{m[0], m[3], m[6]}, b{m[1], m[4], m[7]};
    a = scale(a, 1 / norm(a)); b = sub(b, scale(a, dot(a, b))); b = scale(b, 1 / norm(b));
    const V3 c{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    return {a[0], b[0], c[0], a[1], b[1], c[1], a[2], b[2], c[2]};
}
static double turn_deg(const M3& m) {return std::acos(std::clamp((m[0] + m[4] + m[8] - 1) / 2, -1.0, 1.0)) * 180 / 3.14159265358979;}

// OpenXR camera axes (x right, y up, z back: -z ahead); image u, v in 0..1, v down; tangents of the FOV.
struct Fov {double l, r, u, d;};
static V3 ray(const Fov& f, double u, double v) {return {f.l + u * (f.r - f.l), f.u + v * (f.d - f.u), -1};}  // point = distance ahead * ray
static bool to_image(const Fov& f, const V3& q, double& u, double& v) {
    if (!(q[2] < -1e-6)) return false;
    u = (q[0] / -q[2] - f.l) / (f.r - f.l); v = (q[1] / -q[2] - f.u) / (f.d - f.u); return std::isfinite(u) && std::isfinite(v);
}
// The target camera in kept camera coordinates: columns of m = its axes, c = its centre. A kept point P is Q = m^T (P - c) there.
struct Pose {M3 m{1, 0, 0, 0, 1, 0, 0, 0, 1}; V3 c{};};
static V3 to_target(const Pose& p, const V3& P) {return mulvt(p.m, sub(P, p.c));}
static V3 to_kept(const Pose& p, const V3& Q) {return add(mulv(p.m, Q), p.c);}

// ---- images ----
static float lin_of[256];
static unsigned char encode(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    const float s = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1 / 2.4f) - 0.055f;
    return static_cast<unsigned char>(std::lround(s * 255));
}
struct Image {
    int w{}, h{}; std::vector<float> lin; std::vector<unsigned char> rgb;  // linear RGB, 8-bit encoded RGB
    void from_rgba(const std::vector<unsigned char>& raw, int W, int H) {
        w = W; h = H; CHECK(raw.size() == static_cast<std::size_t>(W) * H * 4);
        lin.resize(static_cast<std::size_t>(W) * H * 3); rgb.resize(lin.size());
        for (std::size_t i = 0, n = static_cast<std::size_t>(W) * H; i < n; ++i) for (int c = 0; c < 3; ++c) {rgb[i * 3 + c] = raw[i * 4 + c]; lin[i * 3 + c] = lin_of[raw[i * 4 + c]];}
    }
    void sample(double u, double v, float out[3]) const {  // bilinear in linear light, clamped at the edges
        const double x = std::clamp(u * w - 0.5, 0.0, w - 1.0), y = std::clamp(v * h - 0.5, 0.0, h - 1.0);
        const int x0 = static_cast<int>(x), y0 = static_cast<int>(y), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
        const float fx = static_cast<float>(x - x0), fy = static_cast<float>(y - y0);
        const float* a = &lin[(static_cast<std::size_t>(y0) * w + x0) * 3]; const float* b = &lin[(static_cast<std::size_t>(y0) * w + x1) * 3];
        const float* c = &lin[(static_cast<std::size_t>(y1) * w + x0) * 3]; const float* d = &lin[(static_cast<std::size_t>(y1) * w + x1) * 3];
        for (int k = 0; k < 3; ++k) out[k] = (a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + d[k] * fx) * fy;
    }
};
struct Map {  // one float channel, bilinear
    int w{}, h{}; std::vector<float> v;
    float at(double u, double vv) const {
        const double x = std::clamp(u * w - 0.5, 0.0, w - 1.0), y = std::clamp(vv * h - 0.5, 0.0, h - 1.0);
        const int x0 = static_cast<int>(x), y0 = static_cast<int>(y), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
        const float fx = static_cast<float>(x - x0), fy = static_cast<float>(y - y0);
        return (v[static_cast<std::size_t>(y0) * w + x0] * (1 - fx) + v[static_cast<std::size_t>(y0) * w + x1] * fx) * (1 - fy) +
               (v[static_cast<std::size_t>(y1) * w + x0] * (1 - fx) + v[static_cast<std::size_t>(y1) * w + x1] * fx) * fy;
    }
};
static Map luminance(const Image& im) {  // of the encoded values, 0..255: the alignment works on what the scores compare
    Map m{im.w, im.h, std::vector<float>(static_cast<std::size_t>(im.w) * im.h)};
    for (std::size_t i = 0; i < m.v.size(); ++i) m.v[i] = 0.2126f * im.rgb[i * 3] + 0.7152f * im.rgb[i * 3 + 1] + 0.0722f * im.rgb[i * 3 + 2];
    return m;
}
static Map blurred(const Map& m, int r) {  // separable box, radius r
    if (r <= 0) return m;
    Map t = m, o = m;
    for (int y = 0; y < m.h; ++y) {
        double s = 0; int n = 0; const float* row = &m.v[static_cast<std::size_t>(y) * m.w];
        for (int x = -r; x < m.w + r; ++x) {
            if (x + r < m.w) {s += row[std::max(0, x + r)]; ++n;}
            if (x - r - 1 >= 0) {s -= row[x - r - 1]; --n;}
            if (x >= 0 && x < m.w) t.v[static_cast<std::size_t>(y) * m.w + x] = static_cast<float>(s / std::max(n, 1));
        }
    }
    for (int x = 0; x < m.w; ++x) {
        double s = 0; int n = 0;
        for (int y = -r; y < m.h + r; ++y) {
            if (y + r < m.h) {s += t.v[static_cast<std::size_t>(std::max(0, y + r)) * m.w + x]; ++n;}
            if (y - r - 1 >= 0) {s -= t.v[static_cast<std::size_t>(y - r - 1) * m.w + x]; --n;}
            if (y >= 0 && y < m.h) o.v[static_cast<std::size_t>(y) * m.w + x] = static_cast<float>(s / std::max(n, 1));
        }
    }
    return o;
}

// ---- direct photometric alignment: the kept -> N+1 camera from the still world ----
struct Align {Pose pose; double before{}, after{}; std::size_t points{};};
static Align align(const Pose& start, const Fov& f, const std::vector<V3>& pts, const std::vector<std::array<double, 2>>& uvs,
                   const Map& kept_l, const Map& next_l, bool fit = true) {  // fit false: only the starting residual
    Align out{start}; out.points = pts.size();
    std::vector<float> r(pts.size()), w(pts.size());
    const auto residuals = [&](const Pose& p, const Map& K, const Map& X, std::vector<float>& res, std::vector<float>& wt) {
        for (std::size_t i = 0; i < pts.size(); ++i) {
            double u, v; wt[i] = 0; res[i] = 0;
            if (!to_image(f, to_target(p, pts[i]), u, v) || u < 0.01 || u > 0.99 || v < 0.01 || v > 0.99) continue;
            res[i] = X.at(u, v) - K.at(uvs[i][0], uvs[i][1]); wt[i] = 1;
        }
    };
    const auto median_abs = [&](const Pose& p) {
        std::vector<float> a; a.reserve(pts.size()); residuals(p, kept_l, next_l, r, w);
        for (std::size_t i = 0; i < pts.size(); ++i) if (w[i] > 0) a.push_back(std::abs(r[i]));
        if (a.size() < 50) return static_cast<double>(INFINITY); std::nth_element(a.begin(), a.begin() + a.size() / 2, a.end()); return static_cast<double>(a[a.size() / 2]);
    };
    out.before = median_abs(start);
    if (!fit) {out.after = out.before; return out;}
    const auto perturbed = [](const Pose& p, const std::array<double, 6>& t) {Pose q; q.m = orthonormal(mul(p.m, rodrigues({t[0], t[1], t[2]}))); q.c = add(p.c, {t[3], t[4], t[5]}); return q;};
    const double huber = 10;
    const auto cost = [&](const Pose& p, const Map& K, const Map& X, std::size_t& valid) {  // mean Huber cost over the points in view
        std::vector<float> rr(pts.size()), ww(pts.size()); residuals(p, K, X, rr, ww); double c = 0; valid = 0;
        for (std::size_t i = 0; i < pts.size(); ++i) if (ww[i] > 0) {const double e = std::abs(rr[i]); c += e <= huber ? e * e / 2 : huber * (e - huber / 2); ++valid;}
        return valid ? c / static_cast<double>(valid) : INFINITY;
    };
    Pose p = start;
    for (const int radius : {24, 12, 6, 2, 0}) {
        const Map K = blurred(kept_l, radius), X = blurred(next_l, radius);
        for (int it = 0; it < 8; ++it) {
            residuals(p, K, X, r, w);
            std::array<std::vector<float>, 6> J;
            const std::array<double, 6> h{2e-5, 2e-5, 2e-5, 2e-5, 2e-5, 2e-5};
            for (int k = 0; k < 6; ++k) {
                std::array<double, 6> t{}; t[k] = h[k]; std::vector<float> rk(pts.size()), wk(pts.size());
                residuals(perturbed(p, t), K, X, rk, wk);
                J[k].resize(pts.size()); for (std::size_t i = 0; i < pts.size(); ++i) J[k][i] = wk[i] > 0 && w[i] > 0 ? static_cast<float>((rk[i] - r[i]) / h[k]) : 0.f;
            }
            double A[6][6]{}, b[6]{};
            for (std::size_t i = 0; i < pts.size(); ++i) {
                if (w[i] <= 0) continue;
                const double e = std::abs(r[i]), wt = e <= huber ? 1 : huber / e;
                for (int a = 0; a < 6; ++a) {b[a] -= wt * J[a][i] * r[i]; for (int c = a; c < 6; ++c) A[a][c] += wt * J[a][i] * J[c][i];}
            }
            for (int a = 0; a < 6; ++a) {for (int c = 0; c < a; ++c) A[a][c] = A[c][a]; A[a][a] *= 1.0 + 1e-3; A[a][a] += 1e-9;}
            double x[6]{}; {  // Gaussian elimination with partial pivoting
                double M[6][7]; for (int a = 0; a < 6; ++a) {for (int c = 0; c < 6; ++c) M[a][c] = A[a][c]; M[a][6] = b[a];}
                for (int c = 0; c < 6; ++c) {
                    int piv = c; for (int a = c + 1; a < 6; ++a) if (std::abs(M[a][c]) > std::abs(M[piv][c])) piv = a;
                    for (int k = 0; k < 7; ++k) std::swap(M[c][k], M[piv][k]);
                    if (std::abs(M[c][c]) < 1e-30) continue;
                    for (int a = 0; a < 6; ++a) if (a != c) {const double q = M[a][c] / M[c][c]; for (int k = c; k < 7; ++k) M[a][k] -= q * M[c][k];}
                }
                for (int a = 0; a < 6; ++a) x[a] = std::abs(M[a][a]) > 1e-30 ? M[a][6] / M[a][a] : 0;
            }
            std::array<double, 6> t{}; for (int a = 0; a < 6; ++a) t[a] = std::clamp(x[a], -0.02, 0.02);
            // accept a step only if it lowers the cost without losing many points from view; else halve it (three times at most)
            std::size_t v0 = 0, v1 = 0; const double c0 = cost(p, K, X, v0); bool taken = false;
            for (int half = 0; half < 4 && !taken; ++half) {
                const Pose q = perturbed(p, t);
                if (cost(q, K, X, v1) < c0 && v1 * 10 >= v0 * 9) {p = q; taken = true;} else for (double& e : t) e /= 2;
            }
            if (!taken) break;
            if (std::abs(t[0]) + std::abs(t[1]) + std::abs(t[2]) < 1e-7 && std::abs(t[3]) + std::abs(t[4]) + std::abs(t[5]) < 1e-6) break;
        }
    }
    out.pose = p; out.after = median_abs(p); return out;
}

// ---- forward splat at render size ----
enum : std::uint8_t {cell_empty, cell_world, cell_mover, cell_rig, cell_filled, cell_sky};
struct Cell {float invz{-1}; float su{}, sv{}; float dx{}, dy{}, dz{}; std::uint8_t cls{cell_empty};};
struct Grid {int w{}, h{}; std::vector<Cell> c; std::size_t cracks{}, filled{}, left_empty{};};
struct Texels {  // the kept render's texels
    int w{}, h{}; std::vector<float> depth; std::vector<std::array<float, 4>> motion; std::vector<std::uint8_t> rig; double m10{}, m11{};
    double ahead(std::size_t i) const {return depth[i] > 1e-7f ? m11 / (depth[i] + m10) : INFINITY;}
};
// disp: per texel the kept-camera displacement to add before projecting (movers, rig), and its class.
static Grid splat(const Texels& T, const Fov& f, const Pose& p, const std::vector<V3>& disp, const std::vector<std::uint8_t>& cls) {
    Grid g{T.w, T.h, std::vector<Cell>(static_cast<std::size_t>(T.w) * T.h)};
    for (int y = 0; y < T.h; ++y) for (int x = 0; x < T.w; ++x) {
        const std::size_t i = static_cast<std::size_t>(y) * T.w + x;
        const double u = (x + 0.5) / T.w, v = (y + 0.5) / T.h;
        double ut, vt; V3 q; float invz;
        if (T.depth[i] <= 1e-7f) {q = mulvt(p.m, ray(f, u, v)); invz = 0;}  // sky: a direction
        else {const V3 P = add(scale(ray(f, u, v), T.ahead(i)), disp[i]); q = to_target(p, P); invz = static_cast<float>(1 / std::max(-q[2], 1e-6));}
        if (!to_image(f, q, ut, vt)) continue;
        const int cx = static_cast<int>(std::floor(ut * T.w)), cy = static_cast<int>(std::floor(vt * T.h));
        if (cx < 0 || cy < 0 || cx >= T.w || cy >= T.h) continue;
        Cell& c = g.c[static_cast<std::size_t>(cy) * T.w + cx];
        if (c.cls != cell_empty && invz <= c.invz) continue;
        c.invz = invz; c.su = static_cast<float>(u); c.sv = static_cast<float>(v);
        c.dx = static_cast<float>(disp[i][0]); c.dy = static_cast<float>(disp[i][1]); c.dz = static_cast<float>(disp[i][2]);
        c.cls = T.depth[i] <= 1e-7f ? cell_sky : cls[i];
    }
    // cracks (AFW v2's Crack stage): a cell between two cells of one surface (opposite neighbours at distance 1 or 2, depths within
    // 10 %) that is empty, or farther than both by more than 10 % (background that slid into the seam of a magnified near surface),
    // takes the nearer of them, so a magnified surface does not show the background through its seams
    for (int pass = 0; pass < 2; ++pass) {
        std::vector<std::pair<std::size_t, std::size_t>> take;
        for (int y = 0; y < T.h; ++y) for (int x = 0; x < T.w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * T.w + x;
            const double zi = g.c[i].cls == cell_empty ? INFINITY : g.c[i].invz > 0 ? 1 / g.c[i].invz : INFINITY;
            std::size_t best = SIZE_MAX;
            for (int d = 1; d <= 2 && best == SIZE_MAX; ++d) for (const auto& o : {std::array<int, 2>{1, 0}, std::array<int, 2>{0, 1}, std::array<int, 2>{1, 1}, std::array<int, 2>{1, -1}}) {
                const int ax = x - o[0] * d, ay = y - o[1] * d, bx = x + o[0] * d, by = y + o[1] * d;
                if (ax < 0 || ay < 0 || bx < 0 || by < 0 || ax >= T.w || bx >= T.w || ay >= T.h || by >= T.h) continue;
                const std::size_t ia = static_cast<std::size_t>(ay) * T.w + ax, ib = static_cast<std::size_t>(by) * T.w + bx;
                const Cell& a = g.c[ia]; const Cell& b = g.c[ib];
                if (a.cls == cell_empty || b.cls == cell_empty || a.cls == cell_sky || b.cls == cell_sky || a.cls == cell_filled || b.cls == cell_filled) continue;
                const double za = 1 / a.invz, zb = 1 / b.invz; if (std::abs(za - zb) > 0.1 * std::min(za, zb) || !(zi > 1.1 * std::max(za, zb))) continue;
                const std::size_t n = a.invz >= b.invz ? ia : ib; if (best == SIZE_MAX || g.c[n].invz > g.c[best].invz) best = n;
            }
            if (best != SIZE_MAX) take.emplace_back(i, best);
        }
        for (const auto& [i, j] : take) g.c[i] = g.c[j];  // keeps the class: resolved by its depth like its neighbour
        g.cracks += take.size();
    }
    // holes: each empty cell takes its farthest non-empty 8-neighbour (the background), pass by pass outward
    std::vector<std::size_t> empty; for (std::size_t i = 0; i < g.c.size(); ++i) if (g.c[i].cls == cell_empty) empty.push_back(i);
    for (int pass = 0; pass < 512 && !empty.empty(); ++pass) {
        std::vector<std::pair<std::size_t, std::size_t>> take; std::vector<std::size_t> still;
        for (const std::size_t i : empty) {
            const int x = static_cast<int>(i % T.w), y = static_cast<int>(i / T.w); std::size_t best = SIZE_MAX;
            for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx, ny = y + dy; if ((!dx && !dy) || nx < 0 || ny < 0 || nx >= T.w || ny >= T.h) continue;
                const std::size_t j = static_cast<std::size_t>(ny) * T.w + nx; const Cell& n = g.c[j];
                if (n.cls == cell_empty) continue;
                if (best == SIZE_MAX || n.invz < g.c[best].invz) best = j;
            }
            if (best == SIZE_MAX) still.push_back(i); else take.emplace_back(i, best);
        }
        if (take.empty()) break;
        for (const auto& [i, j] : take) {Cell c = g.c[j]; if (c.cls != cell_sky) c.cls = cell_filled; g.c[i] = c;}  // a sky seed stays a direction
        g.filled += take.size(); empty.swap(still);
    }
    g.left_empty = empty.size();
    return g;
}
// Each display pixel from its cell: its depth back into the kept image (minus the cell's displacement); a filled cell shows its seed's
// own kept colour; a back-projection that lands more than two texels from the cell's source falls back to that source.
static Image resolve(const Grid& g, const Image& kept, const Fov& f, const Pose& p) {
    Image o; o.w = kept.w; o.h = kept.h; o.rgb.resize(static_cast<std::size_t>(o.w) * o.h * 3);
    for (int y = 0; y < o.h; ++y) for (int x = 0; x < o.w; ++x) {
        const double ut = (x + 0.5) / o.w, vt = (y + 0.5) / o.h;
        const int cx = std::min(static_cast<int>(ut * g.w), g.w - 1), cy = std::min(static_cast<int>(vt * g.h), g.h - 1);
        const Cell& c = g.c[static_cast<std::size_t>(cy) * g.w + cx];
        double su = c.su, sv = c.sv;
        if (c.cls == cell_sky) {double u, v; if (to_image(f, mulv(p.m, ray(f, ut, vt)), u, v)) {su = u; sv = v;}}
        else if (c.cls != cell_filled && c.cls != cell_empty) {
            const V3 P = sub(to_kept(p, scale(ray(f, ut, vt), 1 / c.invz)), {c.dx, c.dy, c.dz});
            double u, v;
            if (to_image(f, P, u, v) && std::abs(u - c.su) * g.w < 2 && std::abs(v - c.sv) * g.h < 2) {su = u; sv = v;}
        }
        float rgb[3]; kept.sample(su, sv, rgb);
        for (int k = 0; k < 3; ++k) o.rgb[(static_cast<std::size_t>(y) * o.w + x) * 3 + k] = encode(rgb[k]);
    }
    return o;
}
static Image rotate_only(const Image& kept, const Fov& f, const Pose& p) {
    Image o; o.w = kept.w; o.h = kept.h; o.rgb.resize(static_cast<std::size_t>(o.w) * o.h * 3);
    for (int y = 0; y < o.h; ++y) for (int x = 0; x < o.w; ++x) {
        double u = (x + 0.5) / o.w, v = (y + 0.5) / o.h;
        if (!to_image(f, mulv(p.m, ray(f, u, v)), u, v)) {u = 0.5; v = 0.5;}
        float rgb[3]; kept.sample(u, v, rgb);
        for (int k = 0; k < 3; ++k) o.rgb[(static_cast<std::size_t>(y) * o.w + x) * 3 + k] = encode(rgb[k]);
    }
    return o;
}

// ---- scores ----
static const char* region_names[] = {"border", "rig", "movers", "edge", "near", "far"};
constexpr int regions = 6;
static std::vector<std::uint8_t> region_map(const Grid& g, int W, int H) {
    // per display pixel: 0 border, 1 rig, 2 movers, 3 edge, 4 near, 5 far (from sw's splat, so in the target image)
    std::vector<std::uint8_t> cls(static_cast<std::size_t>(W) * H, 5);
    const auto cell = [&](int x, int y) -> const Cell& {return g.c[static_cast<std::size_t>(std::min(y * g.h / H, g.h - 1)) * g.w + std::min(x * g.w / W, g.w - 1)];};
    std::vector<std::uint8_t> rig(cls.size()), mover(cls.size()), step(cls.size());
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const Cell& c = cell(x, y); const std::size_t i = static_cast<std::size_t>(y) * W + x;
        rig[i] = c.cls == cell_rig; mover[i] = c.cls == cell_mover;
        if (c.invz > 0 && 1 / c.invz < 1.5) cls[i] = 4;
    }
    for (int gy = 0; gy < g.h; ++gy) for (int gx = 0; gx < g.w; ++gx) {  // depth steps > 15 % in the 3x3 of a cell
        double lo = INFINITY, hi = 0;
        for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
            const int x = std::clamp(gx + dx, 0, g.w - 1), y = std::clamp(gy + dy, 0, g.h - 1); const Cell& c = g.c[static_cast<std::size_t>(y) * g.w + x];
            const double a = c.invz > 0 ? 1 / c.invz : 1e4; lo = std::min(lo, a); hi = std::max(hi, a);
        }
        if (hi > 1.15 * lo) for (int y = gy * H / g.h; y < (gy + 1) * H / g.h; ++y) for (int x = gx * W / g.w; x < (gx + 1) * W / g.w; ++x) step[static_cast<std::size_t>(y) * W + x] = 1;
    }
    const auto dilate = [&](std::vector<std::uint8_t> m, int r) {  // square, radius r
        std::vector<std::uint8_t> t(m.size()), o(m.size());
        for (int y = 0; y < H; ++y) {int last = -1000000; for (int x = 0; x < W + r; ++x) {if (x < W && m[static_cast<std::size_t>(y) * W + x]) last = x; const int ox = x - r; if (ox >= 0 && ox < W) t[static_cast<std::size_t>(y) * W + ox] = x - last <= 2 * r;}}
        for (int x = 0; x < W; ++x) {int last = -1000000; for (int y = 0; y < H + r; ++y) {if (y < H && t[static_cast<std::size_t>(y) * W + x]) last = y; const int oy = y - r; if (oy >= 0 && oy < H) o[static_cast<std::size_t>(oy) * W + x] = y - last <= 2 * r;}}
        return o;
    };
    rig = dilate(rig, 4); mover = dilate(mover, 4); step = dilate(step, 6);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        const std::size_t i = static_cast<std::size_t>(y) * W + x;
        if (x < 48 || y < 48 || x >= W - 48 || y >= H - 48) cls[i] = 0;
        else if (rig[i]) cls[i] = 1; else if (mover[i]) cls[i] = 2; else if (step[i]) cls[i] = 3;
    }
    return cls;
}
struct Score {std::array<double, regions> sum{}; std::array<std::size_t, regions> n{}; double all{}; std::size_t ghost{}, changed{};};
static Score score(const Image& R, const Image& K, const Image& X, const std::vector<std::uint8_t>& cls) {
    Score s; const std::size_t n = static_cast<std::size_t>(X.w) * X.h;
    for (std::size_t i = 0; i < n; ++i) {
        double e = 0, ek = 0, tk = 0;
        for (int c = 0; c < 3; ++c) {e += std::abs(R.rgb[i * 3 + c] - X.rgb[i * 3 + c]); ek += std::abs(R.rgb[i * 3 + c] - K.rgb[i * 3 + c]); tk += std::abs(X.rgb[i * 3 + c] - K.rgb[i * 3 + c]);}
        e /= 3; ek /= 3; tk /= 3;
        s.sum[cls[i]] += e; ++s.n[cls[i]]; s.all += e;
        if (tk > 16) {++s.changed; if (ek + 8 < e) ++s.ghost;}
    }
    s.all /= static_cast<double>(n); return s;
}

// World poses as the dump meta writes them (write_world_pose in src/dxgi_probe.cpp: WarpCamera and play-space poses): position, basis
// rows forward, left, up.
struct WPose {V3 p{}; std::array<V3, 3> rows{}; bool ok{};};
static WPose wpose(const Json& j) {
    WPose w; if (!j.has("position") || !j.has("basis")) return w;
    w.p = {j["position"].at(0), j["position"].at(1), j["position"].at(2)};
    for (std::size_t r = 0; r < 3; ++r) w.rows[r] = {j["basis"].a.at(r).at(0), j["basis"].a.at(r).at(1), j["basis"].a.at(r).at(2)};
    w.ok = norm(w.rows[0]) > 0.5; return w;
}
static V3 move_rigid(const WPose& a, const WPose& b, const V3& X) {  // a world point that moves with a body from pose a to pose b
    const V3 d = sub(X, a.p), l{dot(a.rows[0], d), dot(a.rows[1], d), dot(a.rows[2], d)};
    return add(b.p, add(add(scale(b.rows[0], l[0]), scale(b.rows[1], l[1])), scale(b.rows[2], l[2])));
}
struct CameraFrame {  // a camera in the world: its centre and OpenXR axes (right, up, back)
    V3 c{}; std::array<V3, 3> f{};
    V3 world(const V3& P) const {return add(c, add(add(scale(f[0], P[0]), scale(f[1], P[1])), scale(f[2], P[2])));}
    V3 local(const V3& X) const {const V3 d = sub(X, c); return {dot(f[0], d), dot(f[1], d), dot(f[2], d)};}
};
static CameraFrame camera_frame(const WPose& w) {return {w.p, {scale(w.rows[1], -1), w.rows[2], scale(w.rows[0], -1)}};}
static Pose pose_between(const CameraFrame& k, const CameraFrame& t) {  // camera t in camera k's coordinates
    Pose p; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) p.m[i * 3 + j] = dot(k.f[i], t.f[j]);
    p.c = k.local(t.c); return p;
}

int main(int argc, char** argv) {
    if (argc < 3) {std::fprintf(stderr, "usage: spacewarp_replay <dump dir> <out dir> [N ...]\n"); return 2;}
    for (int i = 0; i < 256; ++i) {const double s = i / 255.0; lin_of[i] = static_cast<float>(s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4));}
    const std::string dir = argv[1], out = argv[2];
    std::vector<int> dumps; for (int i = 3; i < argc; ++i) dumps.push_back(std::atoi(argv[i]));
    if (dumps.empty()) for (int n = 1; n <= 200; ++n) if (std::ifstream(dir + "/stale-warp-" + std::to_string(n) + "-meta.json")) dumps.push_back(n);
    const double a_share = std::getenv("SW_A") ? std::atof(std::getenv("SW_A")) : 1.0;
    const std::string rig_mode = std::getenv("SW_RIG") ? std::getenv("SW_RIG") : "auto";
    CHECK(rig_mode == "auto" || rig_mode == "hands" || rig_mode == "play" || rig_mode == "world" || rig_mode == "carried");
    std::printf("sw rig: %s\n", rig_mode.c_str());
    int sheet_region = -1;  // SW_SHEET_REGION=<border|rig|movers|edge|near|far>: the sheet's worst tiles counted in that region only
    if (const char* s = std::getenv("SW_SHEET_REGION")) {for (int g = 0; g < regions; ++g) if (std::string(s) == region_names[g]) sheet_region = g; CHECK(sheet_region >= 0);}
    std::printf("Columns: per method the mean |R - next| (levels) in border rig movers edge near far | all | ghost %%\n");
    std::map<std::string, Score> totals; std::size_t total_pixels = 0;
    for (const int n : dumps) {
        const std::string stem = dir + "/stale-warp-" + std::to_string(n) + "-";
        if (!std::ifstream(stem + "next.rgba")) {std::printf("dump %d: no -next.rgba, skipped\n", n); continue;}
        const auto meta_text = load(stem + "meta.json"); const std::string meta_s(meta_text.begin(), meta_text.end()); const char* mp = meta_s.c_str();
        const Json meta = parse(mp);
        const int W = static_cast<int>(meta["image"].at(0)), H = static_cast<int>(meta["image"].at(1));
        for (const char* box : {"box", "kept_depth_box"}) CHECK(meta[box].at(0) == 0 && meta[box].at(1) == 0);  // full dumps only
        CHECK(meta["box"].at(2) == W && meta["box"].at(3) == H);
        Texels T; T.w = static_cast<int>(meta["kept_depth_region"].at(0)); T.h = static_cast<int>(meta["kept_depth_region"].at(1));
        CHECK(meta["kept_depth_box"].at(2) == T.w && meta["kept_depth_box"].at(3) == T.h);
        T.m10 = meta["world"]["m10"].n; T.m11 = meta["world"]["m11"].n;
        const std::size_t texels = static_cast<std::size_t>(T.w) * T.h;
        {const auto d = load(stem + "kept-depth.r32"); CHECK(d.size() == texels * 4); T.depth.resize(texels); std::memcpy(T.depth.data(), d.data(), d.size());}
        T.motion.assign(texels, {0, 0, 0, 0});
        if (std::ifstream(stem + "kept-motion.rgba16f")) {
            const auto m = load(stem + "kept-motion.rgba16f"); CHECK(m.size() == texels * 8);
            for (std::size_t i = 0; i < texels; ++i) for (int c = 0; c < 4; ++c) {std::uint16_t h; std::memcpy(&h, &m[i * 8 + c * 2], 2); T.motion[i][c] = half_to_float(h);}
        }
        T.rig.assign(texels, 0);
        if (std::ifstream(stem + "kept-rig.r8")) {const auto r = load(stem + "kept-rig.r8"); CHECK(r.size() == texels); T.rig = r;}
        Image K, X; K.from_rgba(load(stem + "kept.rgba"), W, H); X.from_rgba(load(stem + "next.rgba"), W, H);
        const auto& kf = meta["kept_fov"]; const Fov f{std::tan(kf.at(0)), std::tan(kf.at(1)), std::tan(kf.at(2)), std::tan(kf.at(3))};

        // the camera at N+1: exact from the meta's kept_camera and next_camera when both were recorded. The alignment runs either way;
        // with exact cameras it shows how far off the estimate is.
        const WPose kc = meta.has("kept_camera") && meta["kept_camera"]["valid"].b ? wpose(meta["kept_camera"]["pose"]) : WPose{};
        const WPose nc = meta.has("next_camera") && meta["next_camera"]["valid"].b ? wpose(meta["next_camera"]["pose"]) : WPose{};
        const bool exact = kc.ok && nc.ok, have_axes = kc.ok;
        const CameraFrame kcam = camera_frame(kc);
        M3 world_to_kept{}; for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) world_to_kept[i * 3 + j] = kcam.f[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
        Pose pose; Align al{};
        {
            // the world layer: world = I m a (target pixel -> kept pixel), shift = the target centre in kept image terms (u w, v w, w)
            const double l = f.l, r = f.r, u = f.u, d = f.d;
            const M3 I{1 / (r - l), 0, l / (r - l), 0, 1 / (d - u), u / (d - u), 0, 0, -1}, A{r - l, 0, l, 0, d - u, u, 0, 0, -1};
            M3 world{}; for (int i = 0; i < 9; ++i) world[i] = meta["world"]["world"].at(i);
            const M3 m1 = orthonormal(mul(mul(A, world), I));
            const auto& sh = meta["world"]["shift"]; const double z = -sh.at(2);
            const V3 s1{(sh.at(0) - I[2] * z) / I[0], (sh.at(1) - I[5] * z) / I[4], z};
            Pose guess; guess.m = orthonormal(mul(m1, m1)); guess.c = add(s1, mulv(m1, s1));  // constant velocity: the N-1 -> N step once more
            std::vector<V3> pts; std::vector<std::array<double, 2>> uvs;
            const Map kl = luminance(K), xl = luminance(X);
            for (int y = 2; y < T.h - 2; y += 2) for (int x = 2; x < T.w - 2; x += 2) {
                const std::size_t i = static_cast<std::size_t>(y) * T.w + x;
                if (T.depth[i] <= 1e-7f || T.rig[i] || T.motion[i][3] >= 0.5f || std::abs(T.motion[i][0]) + std::abs(T.motion[i][1]) + std::abs(T.motion[i][2]) > 0.002f) continue;
                const double u0 = (x + 0.5) / T.w, v0 = (y + 0.5) / T.h, du = 1.0 / W, dv = 1.0 / H;
                const double gx = kl.at(u0 + du, v0) - kl.at(u0 - du, v0), gy = kl.at(u0, v0 + dv) - kl.at(u0, v0 - dv);
                if (std::abs(gx) + std::abs(gy) < 6) continue;  // flat texels say nothing about the motion
                pts.push_back(scale(ray(f, u0, v0), T.ahead(i))); uvs.push_back({u0, v0});
            }
            if (pts.size() > 250000) {const std::size_t k = pts.size() / 250000 + 1; std::vector<V3> p2; std::vector<std::array<double, 2>> u2; for (std::size_t i = 0; i < pts.size(); i += k) {p2.push_back(pts[i]); u2.push_back(uvs[i]);} pts.swap(p2); uvs.swap(u2);}
            al = align(guess, f, pts, uvs, kl, xl); pose = al.pose;
            std::printf("dump %d (eye %d, frame %d): N-1 -> N %.2f deg %.1f mm; guess N+1 %.2f deg %.1f mm; aligned %.2f deg %.1f mm "
                        "(%zu points, median |residual| %.2f -> %.2f levels)\n", n, static_cast<int>(meta["eye"].n), static_cast<int>(meta["frame"].n),
                        turn_deg(m1), norm(s1) * 1000, turn_deg(guess.m), norm(guess.c) * 1000, turn_deg(pose.m), norm(pose.c) * 1000, al.points, al.before, al.after);
        }
        if (exact) {
            const Pose aligned = pose; pose = pose_between(kcam, camera_frame(nc));
            std::printf("dump %d: exact cameras, N-1 -> N+1 %.2f deg %.1f mm; the alignment was off by %.2f deg %.1f mm\n", n, turn_deg(pose.m), norm(pose.c) * 1000,
                        turn_deg(mul({aligned.m[0], aligned.m[3], aligned.m[6], aligned.m[1], aligned.m[4], aligned.m[7], aligned.m[2], aligned.m[5], aligned.m[8]}, pose.m)),
                        norm(sub(aligned.c, pose.c)) * 1000);
        }

        // each drawn hand owns the rig texels within its palm's reach (meta palms, kept camera); 2 = the rest of the rig
        std::array<V3, 2> palm{}; std::array<double, 2> reach{};
        for (std::size_t s = 0; s < 2; ++s) {const auto& p = meta["palms"].a.at(s); palm[s] = {p.at(0), p.at(1), -p.at(2)}; reach[s] = p.at(3);}  // (right, up, forward) -> OpenXR
        const auto group_of = [&](const V3& P) {
            int g = 2; for (int s = 0; s < 2; ++s) if (reach[s] > 0 && norm(sub(P, palm[s])) <= reach[s] && (g == 2 || norm(sub(P, palm[s])) < norm(sub(P, palm[g])))) g = s;
            return g;
        };
        // SW_RIG=auto: hands with exact cameras, else play. hands: each drawn hand rigidly from its kept to its next drawn pose (the
        // controllers at the slot's time), the rest of the rig by its own motion, else with the play space (kept -> next origin).
        std::string mode = rig_mode == "auto" ? (exact ? "hands" : "play") : rig_mode;
        std::array<WPose, 2> hk{}, hn{}; std::array<bool, 2> hand_ok{}; WPose pk, pn; bool play_ok = false;
        if (exact) {
            const unsigned kb = static_cast<unsigned>(meta["kept_camera"]["hand_bits"].n), nb = static_cast<unsigned>(meta["next_camera"]["hand_bits"].n);
            for (std::size_t s = 0; s < 2; ++s) {
                hk[s] = wpose(meta["kept_camera"]["hands"].a.at(s)); hn[s] = wpose(meta["next_camera"]["hands"].a.at(s));
                hand_ok[s] = ((kb & nb) >> s & 1u) && hk[s].ok && hn[s].ok;
            }
            if (meta["play_kept"]["valid"].b && meta.has("next_play") && meta["next_play"]["valid"].b) {pk = wpose(meta["play_kept"]["origin"]); pn = wpose(meta["next_play"]["origin"]); play_ok = pk.ok && pn.ok;}
        }
        if (mode == "hands" && !exact) mode = "play";
        std::printf("  sw rig: %s%s%s%s\n", mode.c_str(), mode == "hands" && hand_ok[0] ? ", hand 0" : "", mode == "hands" && hand_ok[1] ? ", hand 1" : "",
                    mode == "hands" && play_ok ? ", play space" : "");

        // displacements per texel: pos none; sw advances every texel that has its own motion (world movers, and rig texels the motion pass
        // kept motion for: class 255) by its motion texture, and the rig by the mode above
        std::vector<V3> none(texels, V3{0, 0, 0}), sw(texels, V3{0, 0, 0});
        std::vector<std::uint8_t> world_cls(texels, cell_world), sw_cls(texels, cell_world);
        std::size_t movers = 0, rig = 0, rig_moving = 0;
        for (int y = 0; y < T.h; ++y) for (int x = 0; x < T.w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * T.w + x;
            if (T.depth[i] <= 1e-7f) continue;
            const V3 P = scale(ray(f, (x + 0.5) / T.w, (y + 0.5) / T.h), T.ahead(i));
            const bool own = T.motion[i][3] >= (T.rig[i] ? 0.5f : 3.f);  // world: mover_px (afw2 rules); rig: any own motion
            if (T.rig[i]) {sw_cls[i] = cell_rig; ++rig; rig_moving += own;} else if (own) {sw_cls[i] = cell_mover; ++movers;}
            const V3 motion = scale(mulv(world_to_kept, {T.motion[i][0], T.motion[i][1], T.motion[i][2]}), a_share);
            if (!T.rig[i]) {if (own && have_axes) sw[i] = motion; continue;}
            if (mode == "hands") {
                const int g = group_of(P);
                if (g < 2 && hand_ok[static_cast<std::size_t>(g)]) sw[i] = sub(kcam.local(move_rigid(hk[static_cast<std::size_t>(g)], hn[static_cast<std::size_t>(g)], kcam.world(P))), P);
                else if (own) sw[i] = motion;
                else if (play_ok) sw[i] = sub(kcam.local(move_rigid(pk, pn, kcam.world(P))), P);
                else sw[i] = pose.c;
            }
            else if (own && have_axes) sw[i] = motion;
            else if (mode == "carried") sw[i] = sub(to_kept(pose, P), P);  // the same place in the target view
            else if (mode == "play") sw[i] = pose.c;  // the camera's step, not its turn
        }
        if (!have_axes && movers + rig_moving)
            std::printf("  %zu mover and %zu rig texels with own motion kept still: the meta has no kept_camera (world -> camera rotation)\n", movers, rig_moving);
        // swo: sw with the rig fitted to the next frame, an oracle for the exact hand poses real spacewarp has (the controllers at the
        // slot's time). Three rigid groups: each drawn hand (rig texels within its palm's reach, meta palms) and the rest of the rig.
        std::vector<V3> swo = sw;
        {
            std::array<std::vector<V3>, 3> gp; std::array<std::vector<std::array<double, 2>>, 3> gu; std::vector<std::int8_t> group(texels, -1);
            const Map kl = luminance(K), xl = luminance(X);
            for (int y = 0; y < T.h; ++y) for (int x = 0; x < T.w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * T.w + x;
                if (!T.rig[i] || T.depth[i] <= 1e-7f) continue;
                const double u0 = (x + 0.5) / T.w, v0 = (y + 0.5) / T.h; const V3 P = scale(ray(f, u0, v0), T.ahead(i));
                const int g = group_of(P);
                group[i] = static_cast<std::int8_t>(g);
                const double du = 1.0 / W, dv = 1.0 / H;
                if (std::abs(kl.at(u0 + du, v0) - kl.at(u0 - du, v0)) + std::abs(kl.at(u0, v0 + dv) - kl.at(u0, v0 - dv)) < 6) continue;
                gp[g].push_back(P); gu[g].push_back({u0, v0});
            }
            // the group's start: whichever of world (pose), play (the camera's turn only) and carried (no change in the view) fits best
            const Pose play_start{pose.m, {0, 0, 0}}, carried_start{};
            std::array<Pose, 3> gpose{play_start, play_start, play_start};
            for (int g = 0; g < 3; ++g) {
                if (gp[g].size() < 300) continue;
                Pose start = play_start; double best = INFINITY;  // a start with (almost) no points in view is no start
                for (const Pose& s : {pose, play_start, carried_start}) {
                    const double m = align(s, f, gp[g], gu[g], kl, xl, false).before;
                    if (m < best) {best = m; start = s;}
                }
                if (!std::isfinite(best)) {std::printf("  rig %s: out of view from every start, left at play\n", g == 2 ? "rest" : g ? "hand 1" : "hand 0"); continue;}
                const Align ga = align(start, f, gp[g], gu[g], kl, xl); gpose[g] = ga.pose;
                std::printf("  rig %s: %zu points, fitted %.2f deg %.1f mm against the camera (median |residual| %.2f -> %.2f)\n", g == 2 ? "rest" : g ? "hand 1" : "hand 0",
                            ga.points, turn_deg(mul({ga.pose.m[0], ga.pose.m[3], ga.pose.m[6], ga.pose.m[1], ga.pose.m[4], ga.pose.m[7], ga.pose.m[2], ga.pose.m[5], ga.pose.m[8]}, pose.m)),
                            norm(sub(ga.pose.c, pose.c)) * 1000, ga.before, ga.after);
            }
            for (int y = 0; y < T.h; ++y) for (int x = 0; x < T.w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * T.w + x; if (group[i] < 0) continue;
                const V3 P = scale(ray(f, (x + 0.5) / T.w, (y + 0.5) / T.h), T.ahead(i));
                swo[i] = sub(to_kept(pose, to_target(gpose[static_cast<std::size_t>(group[i])], P)), P);  // where pose shows what gpose would
            }
        }
        const Grid gpos = splat(T, f, pose, none, world_cls), gsw = splat(T, f, pose, sw, sw_cls), gswo = splat(T, f, pose, swo, sw_cls);
        std::printf("  texels: %zu rig, %zu movers; cracks closed / holes filled %%: pos %.2f / %.2f, sw %.2f / %.2f, swo %.2f / %.2f\n", rig, movers,
                    100.0 * gpos.cracks / texels, 100.0 * gpos.filled / texels, 100.0 * gsw.cracks / texels, 100.0 * gsw.filled / texels,
                    100.0 * gswo.cracks / texels, 100.0 * gswo.filled / texels);
        std::map<std::string, Image> R;
        R["held"] = K; R["rot"] = rotate_only(K, f, pose); R["pos"] = resolve(gpos, K, f, pose); R["sw"] = resolve(gsw, K, f, pose); R["swo"] = resolve(gswo, K, f, pose);
        const auto cls = region_map(gsw, W, H);
        {   // how big each region is: a small one gives a noisy mean
            std::array<std::size_t, regions> count{}; for (const auto c : cls) ++count[c];
            std::printf("  region share %%:"); for (int g = 0; g < regions; ++g) std::printf(" %s %.1f", region_names[g], 100.0 * count[g] / cls.size()); std::printf("\n");
        }
        total_pixels += static_cast<std::size_t>(W) * H;
        for (const char* name : {"held", "rot", "pos", "sw", "swo"}) {
            const Score s = score(R[name], K, X, cls);
            std::printf("  %-4s", name);
            for (int g = 0; g < regions; ++g) s.n[g] ? std::printf(" %6.2f", s.sum[g] / s.n[g]) : std::printf("     - ");
            std::printf(" | %5.2f | %5.1f\n", s.all, 100.0 * s.ghost / std::max<std::size_t>(s.changed, 1));
            Score& t = totals[name]; for (int g = 0; g < regions; ++g) {t.sum[g] += s.sum[g]; t.n[g] += s.n[g];} t.all += s.all * W * H; t.ghost += s.ghost; t.changed += s.changed;
        }
        {   // the sheet: the 4 worst 64 px tiles of sw, 192 px crops
            const int T64 = 64, tw = W / T64, th = H / T64, S = 192; std::vector<std::pair<double, int>> tiles;
            for (int ty = 1; ty < th - 1; ++ty) for (int tx = 1; tx < tw - 1; ++tx) {  // not the edge band: what turned into view
                double e = 0;
                for (int y = ty * T64; y < (ty + 1) * T64; ++y) for (int x = tx * T64; x < (tx + 1) * T64; ++x) {
                    if (sheet_region >= 0 && cls[static_cast<std::size_t>(y) * W + x] != sheet_region) continue;  // SW_SHEET_REGION: that region only
                    for (int c = 0; c < 3; ++c) {const std::size_t i = (static_cast<std::size_t>(y) * W + x) * 3 + c; e += std::abs(R["sw"].rgb[i] - X.rgb[i]);}
                }
                tiles.emplace_back(-e, ty * tw + tx);
            }
            std::partial_sort(tiles.begin(), tiles.begin() + 4, tiles.end());
            const char* order[] = {"held", "rot", "pos", "sw", "swo"}; const int cols = 6; std::vector<unsigned char> sheet(static_cast<std::size_t>(S) * cols * S * 4 * 3);
            for (int row = 0; row < 4; ++row) {
                const int ty = tiles[row].second / tw, tx = tiles[row].second % tw;
                const int cy = std::clamp(ty * T64 + T64 / 2 - S / 2, 0, H - S), cx = std::clamp(tx * T64 + T64 / 2 - S / 2, 0, W - S);
                for (int col = 0; col < cols; ++col) {
                    const Image& src = col < 5 ? R[order[col]] : X;
                    for (int y = 0; y < S; ++y) std::memcpy(&sheet[((static_cast<std::size_t>(row) * S + y) * S * cols + static_cast<std::size_t>(col) * S) * 3], &src.rgb[(static_cast<std::size_t>(cy + y) * W + cx) * 3], static_cast<std::size_t>(S) * 3);
                }
            }
            write_png(out + "/spacewarp-" + std::to_string(n) + ".png", S * cols, S * 4, sheet);
        }
    }
    if (!total_pixels) return 1;
    std::printf("mean over the dumps (columns as above; the sheet columns are held | rot | pos | sw | swo | next):\n");
    for (const char* name : {"held", "rot", "pos", "sw", "swo"}) {
        const Score& t = totals[name]; std::printf("  %-4s", name);
        for (int g = 0; g < regions; ++g) t.n[g] ? std::printf(" %6.2f", t.sum[g] / t.n[g]) : std::printf("     - ");
        std::printf(" | %5.2f | %5.1f\n", t.all / total_pixels, 100.0 * t.ghost / std::max<std::size_t>(t.changed, 1));
    }
    return 0;
}
