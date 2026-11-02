#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/note_eye_art.hpp"
#include "../src/render_size.hpp"
#include <cassert>
#include <iostream>
using Microsoft::WRL::ComPtr;
int main() {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    auto texture=[&](unsigned width,unsigned height,std::array<unsigned char,4> color) {
        std::vector<unsigned char> pixels(width*height*4);for(unsigned i=0;i<width*height;++i)std::copy(color.begin(),color.end(),pixels.begin()+i*4);
        D3D11_TEXTURE2D_DESC d{};d.Width=width;d.Height=height;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{pixels.data(),width*4,0};ComPtr<ID3D11Texture2D> result;assert(SUCCEEDED(device->CreateTexture2D(&d,&data,&result)));return result;
    };
    const auto hand=[](double left,double right,double depth) {
        auto p=std::make_shared<std::vector<d2vr::Vec3>>(std::initializer_list<d2vr::Vec3>{{left,-.2,depth},{right,-.2,depth},{left,.2,depth},{left,.2,depth},{right,-.2,depth},{right,.2,depth}});return p;
    };
    std::array<d2vr::NoteHandReceipt,2> hands{{{hand(-.3,-.05,-.5),100,1,2,3},{hand(.05,.3,-1.5),100,1,2,3}}};
    assert(hands[0].current(100,1,2,3));assert(!hands[0].current(201,1,2,3));assert(!hands[0].current(100,2,2,3));assert(!hands[0].current(100,1,3,3));assert(!hands[0].current(100,1,2,4));
    d2vr::NoteEyeArt compositor(device.Get());d2vr::NotePanelReceipt panel{{{0,0,-1},{0,0,0,1}},.8,.8,100,1,2,3};
    auto art=texture(8,8,{220,15,30,255});XrView view{XR_TYPE_VIEW};view.pose.orientation.w=1;view.fov={-.78539816f,.78539816f,.78539816f,-.78539816f};
    auto pixel=[](const d2vr::RgbaPixels& image,unsigned x,unsigned y){const auto i=(y*image.width+x)*4;return std::array<unsigned,4>{std::to_integer<unsigned>(image.bytes[i]),std::to_integer<unsigned>(image.bytes[i+1]),std::to_integer<unsigned>(image.bytes[i+2]),std::to_integer<unsigned>(image.bytes[i+3])};};
    for(float eye_x:{-.032f,.032f}) {
        auto eye=texture(128,128,{10,180,40,255});view.pose.position.x=eye_x;
        compositor.draw(eye.Get(),art.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,hands);
        const auto image=d2vr::readback_rgba(eye.Get());
        assert((pixel(image,48,64)==std::array<unsigned,4>{10,180,40,255})); // front hand is never painted over
        assert((pixel(image,76,64)==std::array<unsigned,4>{220,15,30,255})); // page covers the hand behind it
        assert((pixel(image,8,8)==std::array<unsigned,4>{10,180,40,255}));
    }
    // Premultiplied transparency preserves the native eye, including fingers.
    auto clear_art=texture(8,8,{0,0,0,0}),eye=texture(128,128,{10,180,40,255});
    compositor.draw(eye.Get(),clear_art.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,hands);
    assert((pixel(d2vr::readback_rgba(eye.Get()),76,64)==std::array<unsigned,4>{10,180,40,255}));
    // The captured native movie is gamma-space bytes in a typeless target: an sRGB eye shows them as the quad's byte copy would,
    // never encoded twice (v670: a dark 12/255 page became ~70/255 and its faint red ink loud).
    {
        auto typeless=[&](unsigned size,std::array<unsigned char,4> color) {
            std::vector<unsigned char> pixels(size*size*4);for(unsigned i=0;i<size*size;++i)std::copy(color.begin(),color.end(),pixels.begin()+i*4);
            D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=size;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{pixels.data(),size*4,0};ComPtr<ID3D11Texture2D> result;assert(SUCCEEDED(device->CreateTexture2D(&d,&data,&result)));return result;
        };
        auto dark=typeless(8,{40,10,15,255}),srgb_eye=typeless(128,{10,180,40,255});
        compositor.draw(srgb_eye.Get(),dark.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,{0,0,0,8,8,1},view,panel,hands);
        const auto got=pixel(d2vr::readback_rgba(srgb_eye.Get()),76,64);
        const unsigned want[3]{40,10,15};
        for(unsigned c=0;c<3;++c)assert(got[c]+1>=want[c] && got[c]<=want[c]+1);
    }
    // v776 (headset v775): the dual-wield hand wheel ring goes into the eyes like a page: the movie's dim full-screen layer is un-composited, a hand in
    // front of the ring stays over it, the ring is over a hand behind it, and a missing hand leaves the ring over everything (draw(...,ring=true)).
    {
        const auto typeless_texture=[&](unsigned size,std::array<unsigned char,4> color) {
            std::vector<unsigned char> pixels(size*size*4);for(unsigned i=0;i<size*size;++i)std::copy(color.begin(),color.end(),pixels.begin()+i*4);
            D3D11_TEXTURE2D_DESC d{};d.Width=d.Height=size;d.MipLevels=d.ArraySize=d.SampleDesc.Count=1;d.Format=DXGI_FORMAT_R8G8B8A8_TYPELESS;d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
            D3D11_SUBRESOURCE_DATA data{pixels.data(),size*4,0};ComPtr<ID3D11Texture2D> result;assert(SUCCEEDED(device->CreateTexture2D(&d,&data,&result)));return result;
        };
        const float saved_x=view.pose.position.x;
        auto dim=texture(8,8,{12,13,14,128}),opaque=texture(8,8,{220,15,30,255}),half_red=texture(8,8,{133,6,7,191}); // the dim layer; red art; 50% red art over the dim layer
        const std::array<unsigned,4> world{10,180,40,255},red{220,15,30,255};
        for(float eye_x:{-.032f,.032f}) {
            view.pose.position.x=eye_x;
            auto ring_eye=texture(128,128,{10,180,40,255});
            compositor.draw(ring_eye.Get(),dim.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,hands,true);
            auto image=d2vr::readback_rgba(ring_eye.Get());
            const std::array<std::array<unsigned,2>,5> probes{{{48,64},{76,64},{64,64},{64,80},{8,8}}};
            for(const auto& at:probes)assert(pixel(image,at[0],at[1])==world); // the dim layer is gone
            ring_eye=texture(128,128,{10,180,40,255});
            compositor.draw(ring_eye.Get(),opaque.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,hands,true);
            image=d2vr::readback_rgba(ring_eye.Get());
            assert(pixel(image,48,64)==world);                                         // a hand in front of the ring stays over it
            assert(pixel(image,76,64)==red && pixel(image,64,80)==red);                // the ring is over a hand behind it, and over the world
            assert(pixel(image,8,8)==world);                                           // nothing outside the ring's quad
            std::array<d2vr::NoteHandReceipt,2> none{},front_only{{hands[0],{}}};      // no hand at all / one hand: the ring needs none
            ring_eye=texture(128,128,{10,180,40,255});
            compositor.draw(ring_eye.Get(),opaque.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,none,true);
            image=d2vr::readback_rgba(ring_eye.Get());
            assert(pixel(image,48,64)==red && pixel(image,76,64)==red && pixel(image,8,8)==world);
            ring_eye=texture(128,128,{10,180,40,255});
            compositor.draw(ring_eye.Get(),opaque.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,front_only,true);
            image=d2vr::readback_rgba(ring_eye.Get());
            assert(pixel(image,48,64)==world && pixel(image,76,64)==red);
        }
        view.pose.position.x=saved_x;
        // 50% red art over the dim layer: the art's own coverage and colour come back, premultiplied (about (.496,0,0) at alpha .496), blended as the quad's would be.
        const float alpha=(191/255.f-.502f)/.498f,k[3]{.046f,.050f,.056f},source[3]{133/255.f,6/255.f,7/255.f},base[3]{10/255.f,180/255.f,40/255.f};
        float premultiplied[3];for(unsigned c=0;c<3;++c)premultiplied[c]=std::min(std::max(source[c]-k[c]*(1-alpha),0.f),alpha);
        const auto close=[](const std::array<unsigned,4>& got,const float want[3]) {for(unsigned c=0;c<3;++c)if(std::abs(static_cast<float>(got[c])-want[c]*255.f)>2.01f)return false;return true;};
        float gamma_blend[3];for(unsigned c=0;c<3;++c)gamma_blend[c]=premultiplied[c]+base[c]*(1-alpha);
        auto ring_eye=texture(128,128,{10,180,40,255});
        compositor.draw(ring_eye.Get(),half_red.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,hands,true);
        assert(close(pixel(d2vr::readback_rgba(ring_eye.Get()),64,80),gamma_blend));       // a plain eye blends the bytes
        const auto lin=[](float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);};
        const auto enc=[](float c){return c<=.0031308f?c*12.92f:1.055f*std::pow(c,1/2.4f)-.055f;};
        float srgb_blend[3];for(unsigned c=0;c<3;++c)srgb_blend[c]=enc(lin(premultiplied[c])+lin(base[c])*(1-alpha));
        auto srgb_eye=typeless_texture(128,{10,180,40,255});                               // the game's typeless sRGB eye: the quad layer's linear blend
        compositor.draw(srgb_eye.Get(),half_red.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,{0,0,0,8,8,1},view,panel,hands,true);
        assert(close(pixel(d2vr::readback_rgba(srgb_eye.Get()),64,80),srgb_blend));
        srgb_eye=typeless_texture(128,{10,180,40,255});
        compositor.draw(srgb_eye.Get(),dim.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,{0,0,0,8,8,1},view,panel,hands,true);
        assert(pixel(d2vr::readback_rgba(srgb_eye.Get()),64,80)==world);                  // the dim layer is gone in an sRGB eye too
        auto typeless_art=typeless_texture(8,{220,15,30,255});srgb_eye=typeless_texture(128,{10,180,40,255}); // a typeless movie (as the game's) is read as raw bytes
        compositor.draw(srgb_eye.Get(),typeless_art.Get(),DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,{0,0,0,8,8,1},view,panel,hands,true);
        const auto opaque_pixel=pixel(d2vr::readback_rgba(srgb_eye.Get()),64,80);
        for(unsigned c=0;c<3;++c)assert(opaque_pixel[c]+1>=red[c] && opaque_pixel[c]<=red[c]+1);
    }
    bool rejected=false;hands[1].triangles.reset();try{compositor.draw(eye.Get(),art.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,hands);}catch(const std::invalid_argument&){rejected=true;}assert(rejected);
    {   // the page still needs both hands; the ring (v776) does not
        auto one_eye=texture(128,128,{10,180,40,255});auto art2=texture(8,8,{220,15,30,255});
        compositor.draw(one_eye.Get(),art2.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,{0,0,0,8,8,1},view,panel,hands,true);
        assert((pixel(d2vr::readback_rgba(one_eye.Get()),76,64)==std::array<unsigned,4>{220,15,30,255}));
    }
    std::cout<<"note eye art: stereo front/back hand occlusion, alpha, receipt rejection and the hand wheel ring passed\n";
}
