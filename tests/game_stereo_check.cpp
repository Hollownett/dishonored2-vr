#include "../src/game_stereo.hpp"
#include <iostream>
#include <limits>

int main() {
    constexpr std::uintptr_t base=0x7ff712340000;
    std::array<std::byte,80> data{};
    for(int eye=0;eye<2;++eye) {
        auto at=data.data()+eye*40;
        std::uintptr_t label=base+0x1ab2848;
        std::array<float,4> viewport{eye==0?0.0f:0.5f,0.0f,0.5f,1.0f};
        float side=eye==0?-1.0f:1.0f;
        std::memcpy(at,&label,8);std::memcpy(at+12,viewport.data(),16);
        std::memcpy(at+32,&side,4);at[28]=at[29]=std::byte{1};
    }
    auto require=[](bool result) {if(!result) {std::cerr << "Stereo metadata guard failed\n";return false;}return true;};
    d2vr::NativeEyePair pair;
    if(!require(!pair.begin(-1) && !pair.begin(0) && !pair.complete() && pair.begin(1) && pair.complete())) return 1;
    if(!require(!pair.begin(-1) && pair.complete())) return 1;
    pair.reset();
    if(!require(!pair.complete() && !pair.begin(1) && !pair.begin(0) && !pair.complete())) return 1;
    pair.reset();pair.begin(0);pair.begin(0);pair.begin(1);
    if(!require(!pair.complete())) return 1;
    pair.reset();pair.begin(0);pair.begin(1);pair.begin(1);
    if(!require(!pair.complete())) return 1;
    pair.reset();pair.begin(0);pair.reset();pair.begin(1);
    if(!require(!pair.complete())) return 1; // Never combine different frames.
    pair.reset();pair.begin(2);
    if(!require(!pair.complete())) return 1;
    if(!require(d2vr::valid_stereo_metadata(data,base))) return 1;
    if(!require(!d2vr::valid_stereo_metadata(data,base+0x10000))) return 1;
    auto wrong=data;wrong[28]=std::byte{0};
    if(!require(!d2vr::valid_stereo_metadata(wrong,base))) return 1;
    std::array<float,16> native_projection{};native_projection[0]=1.19f;native_projection[5]=2.12f;
    native_projection[10]=1.42857334e-6f;native_projection[11]=.0100000137f;native_projection[14]=-1;
    d2vr::EyeFov optical{-.8f,.6f,.75f,-.9f};
    auto symmetric=d2vr::enclosing_symmetric_fov(optical);
    if(!require(symmetric.has_value())) return 1;
    auto symmetric_projection=d2vr::eye_projection(native_projection,*symmetric);
    if(!require(symmetric_projection && (*symmetric_projection)[2]==0 && (*symmetric_projection)[6]==0 &&
        symmetric->left<=optical.left && symmetric->right>=optical.right &&
        symmetric->up>=optical.up && symmetric->down<=optical.down)) return 1;
    // A pass reconstructing rays from scalar FOV must now agree with the matrix.
    if(!require(std::abs((*symmetric_projection)[0]*std::tan((symmetric->right-symmetric->left)*.5f)-1)<1e-5f &&
        std::abs((*symmetric_projection)[5]*std::tan((symmetric->up-symmetric->down)*.5f)-1)<1e-5f)) return 1;
    if(!require(!d2vr::enclosing_symmetric_fov({.1f,.6f,.75f,-.9f}) &&
        !d2vr::enclosing_symmetric_fov({-.8f,.6f,.75f,std::numeric_limits<float>::quiet_NaN()}))) return 1;
    auto projection=d2vr::eye_projection(native_projection,optical);
    if(!require(projection.has_value())) return 1;
    auto ndc=[&](float x,float y,float z,int row) {
        return ((*projection)[row*4]*x+(*projection)[row*4+1]*y+(*projection)[row*4+2]*z+(*projection)[row*4+3])/(-z);
    };
    if(!require(std::abs(ndc(std::tan(optical.left),0,-1,0)+1)<1e-5f &&
                std::abs(ndc(std::tan(optical.right),0,-1,0)-1)<1e-5f &&
                std::abs(ndc(0,std::tan(optical.down),-1,1)+1)<1e-5f &&
                std::abs(ndc(0,std::tan(optical.up),-1,1)-1)<1e-5f)) return 1;
    float near=native_projection[11]/(native_projection[10]+1),far=native_projection[11]/native_projection[10];
    if(!require(std::abs(ndc(0,0,-near,2)-1)<1e-5f && std::abs(ndc(0,0,-far,2))<1e-5f)) return 1;
    if(!require(!d2vr::eye_projection(native_projection,{.8f,-.8f,.8f,-.8f}))) return 1;
    auto jittered=native_projection;jittered[2]=.0004f;jittered[6]=-.0003f;
    auto kept=d2vr::eye_projection(jittered,optical);
    if(!require(kept && std::abs((*kept)[2]-(*projection)[2]-.0004f)<1e-6f && std::abs((*kept)[6]-(*projection)[6]+.0003f)<1e-6f)) return 1;
    jittered[2]=.2f; // an off-centre frustum, not jitter
    if(!require(d2vr::eye_projection(jittered,optical)->at(2)==(*projection)[2])) return 1;
    // Injected TAA jitter must move the image the way the native TAA dejitters it: content shifts by
    // -j*(1,-1) in UV (y down), so its sample at uv - cb_jittervectors.xy*(1,-1) lands on the pixel.
    const auto j=d2vr::engine_taa_jitter(13,2496,2688); // counter%8 == 5: (-7,1)/16 px
    if(!require(std::abs(j.x+7.f/16/2496)<1e-9f && std::abs(j.y-1.f/16/2688)<1e-9f)) return 1;
    auto taa_projection=*symmetric_projection;d2vr::apply_taa_jitter(taa_projection,j);
    const float centre_u=.5f-taa_projection[2]*.5f,centre_v=.5f+taa_projection[6]*.5f; // view ray (0,0,-1)
    if(!require(std::abs((centre_u-.5f)+j.x)<2e-7f && std::abs((centre_v-.5f)-j.y)<2e-7f)) return 1;
    native_projection[14]=1;
    if(!require(!d2vr::eye_projection(native_projection,optical))) return 1;
    wrong=data;std::uint32_t split_screen=1;std::memcpy(wrong.data()+48,&split_screen,4);
    if(!require(!d2vr::valid_stereo_metadata(wrong,base))) return 1;
    wrong=data;float nan=std::numeric_limits<float>::quiet_NaN();std::memcpy(wrong.data()+52,&nan,4);
    if(!require(!d2vr::valid_stereo_metadata(wrong,base))) return 1;
    std::cout << "PASS: stereo metadata and native eye ordering, duplicate/partial/cross-frame rejection\n";
}
