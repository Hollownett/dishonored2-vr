// Adapter for the VR Stereo Hub AFW (modified). In Dishonored 1 this was a D3D9 probe of the scene target; in Dishonored 2 the
// mod registers, per captured eye (grab serial), a texture whose ALPHA is linear view depth in metres and an R mask (1 = the
// player's arms/weapon, drawn in the engine's near depth range). The AFW reads them by serial at note_capture.
#pragma once
#include <stdint.h>
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;
namespace dvr::depthprobe {
struct Frame { uint32_t serial = 0; ID3D11ShaderResourceView* depth = nullptr; ID3D11ShaderResourceView* mask = nullptr; uint32_t w = 0, h = 0, maskDraws = 0; };
inline Frame& current() { static Frame f; return f; }
inline ID3D11ShaderResourceView* depth_srv_for(uint32_t grabSerial, unsigned* w, unsigned* h) {
    const Frame& f = current();
    if (f.serial != grabSerial || !f.depth) return nullptr;
    if (w) *w = f.w; if (h) *h = f.h;
    return f.depth;
}
inline ID3D11ShaderResourceView* fgmask_srv_for(uint32_t grabSerial, uint32_t* draws, uint32_t* w, uint32_t* h) {
    const Frame& f = current();
    if (f.serial != grabSerial || !f.mask) return nullptr;
    if (draws) *draws = f.maskDraws; if (w) *w = f.w; if (h) *h = f.h;
    return f.mask;
}
inline ID3D11ShaderResourceView* prefg_srv_for(uint32_t, bool* saw) { if (saw) *saw = false; return nullptr; }
inline bool prefg_ready() { return false; }
inline void set_prefg_wanted(unsigned, bool) {}
inline void read_done(ID3D11DeviceContext*) {}
}
