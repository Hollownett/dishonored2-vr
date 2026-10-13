#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2vr {
// BC7 mode 6 encoder: one subset, combined RGBA endpoints at 7 bits per channel
// with a unique p-bit per endpoint, and 16 four-bit indices. The bitstream, LSB
// first, is mode (bit 6 set), R0 R1 G0 G1 B0 B1 A0 A1 at 7 bits each, P0 P1,
// then the anchor index in 3 bits (its high bit is always zero) followed by
// fifteen 4-bit indices. Endpoints come from the block's per-channel min/max,
// the p-bits are the pair that minimises endpoint error, and every pixel takes
// the nearest of the 16 palette entries built from the BC7 4-bit weight table.
// A decoded endpoint channel is (e7 << 1) | p and the palette interpolates
// c = ((64 - w) * e0 + w * e1 + 32) >> 6. Blocks past the right or bottom edge
// replicate the last pixel, so every width and height is accepted. Input rows
// are width * 4 straight RGBA8 bytes; the output is ceil(w/4) * ceil(h/4)
// 16-byte blocks in row-major order.
inline std::vector<std::uint8_t> encode_bc7_mode6(const std::uint8_t* rgba,unsigned width,unsigned height) {
    constexpr unsigned weights[16]={0,4,9,13,17,21,26,30,34,38,43,47,51,55,60,64};
    std::vector<std::uint8_t> blocks;
    if(!rgba || !width || !height) return blocks;
    const unsigned across=(width+3)/4,down=(height+3)/4;
    blocks.resize(static_cast<std::size_t>(across)*down*16);
    for(unsigned by=0;by<down;++by) for(unsigned bx=0;bx<across;++bx) {
        unsigned pixels[16][4];
        for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
            const unsigned sx=std::min(bx*4+x,width-1),sy=std::min(by*4+y,height-1);
            const auto* source=rgba+(static_cast<std::size_t>(sy)*width+sx)*4;
            for(unsigned c=0;c<4;++c) pixels[y*4+x][c]=source[c];
        }
        unsigned low[4],high[4];
        for(unsigned c=0;c<4;++c) {low[c]=255;high[c]=0;}
        for(unsigned i=0;i<16;++i) for(unsigned c=0;c<4;++c) {
            low[c]=std::min(low[c],pixels[i][c]);high[c]=std::max(high[c],pixels[i][c]);
        }
        // The p-bit is shared by the four channels of one endpoint, so pick the
        // bit whose quantised endpoints carry the lower total squared error.
        unsigned endpoint[2][4],pbit[2];
        for(unsigned e=0;e<2;++e) {
            const unsigned* target=e?high:low;
            unsigned best=0,best_error=~0u;
            for(unsigned p=0;p<2;++p) {
                unsigned error=0;
                for(unsigned c=0;c<4;++c) {
                    const int step=(static_cast<int>(target[c])-static_cast<int>(p)+1)/2;
                    const int decoded=static_cast<int>((static_cast<unsigned>(std::max(0,std::min(127,step)))<<1)|p);
                    const int difference=decoded-static_cast<int>(target[c]);
                    error+=static_cast<unsigned>(difference*difference);
                }
                if(error<best_error) {best_error=error;best=p;}
            }
            pbit[e]=best;
            for(unsigned c=0;c<4;++c) {
                const int step=(static_cast<int>(target[c])-static_cast<int>(best)+1)/2;
                endpoint[e][c]=(static_cast<unsigned>(std::max(0,std::min(127,step)))<<1)|best;
            }
        }
        unsigned indices[16];
        for(unsigned i=0;i<16;++i) {
            unsigned best=0,best_error=~0u;
            for(unsigned k=0;k<16;++k) {
                unsigned error=0;
                for(unsigned c=0;c<4;++c) {
                    const unsigned value=((64-weights[k])*endpoint[0][c]+weights[k]*endpoint[1][c]+32)>>6;
                    const int difference=static_cast<int>(value)-static_cast<int>(pixels[i][c]);
                    error+=static_cast<unsigned>(difference*difference);
                }
                if(error<best_error) {best_error=error;best=k;}
            }
            indices[i]=best;
        }
        // The anchor pixel's index high bit is implicit and must be zero: swap
        // the endpoints (with their p-bits) and mirror every index if it is not.
        if(indices[0]>=8) {
            for(unsigned c=0;c<4;++c) std::swap(endpoint[0][c],endpoint[1][c]);
            std::swap(pbit[0],pbit[1]);
            for(unsigned i=0;i<16;++i) indices[i]=15-indices[i];
        }
        std::uint64_t low_bits=0x40,high_bits=0;unsigned position=7;
        const auto emit=[&](unsigned value,unsigned count) {
            for(unsigned bit=0;bit<count;++bit) {
                if((value>>bit)&1u) {
                    if(position<64) low_bits|=1ull<<position;else high_bits|=1ull<<(position-64);
                }
                ++position;
            }
        };
        for(unsigned c=0;c<4;++c) for(unsigned e=0;e<2;++e) emit(endpoint[e][c]>>1,7);
        emit(pbit[0],1);emit(pbit[1],1);
        for(unsigned i=0;i<16;++i) emit(indices[i],i?4u:3u);
        auto* block=blocks.data()+(static_cast<std::size_t>(by)*across+bx)*16;
        for(unsigned i=0;i<8;++i) {
            block[i]=static_cast<std::uint8_t>(low_bits>>(8*i));
            block[8+i]=static_cast<std::uint8_t>(high_bits>>(8*i));
        }
    }
    return blocks;
}
} // namespace d2vr
