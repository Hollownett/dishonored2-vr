#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2vr {
// A Quest controller reference card belonging only to the mod's OpenXR panel.
// The card is laid out at 512x384 and scaled to the requested bitmap size.
inline std::vector<std::uint8_t> controls_hint_image(unsigned width=512,unsigned height=384) {
    constexpr int w=512,h=384;
    const int cw=static_cast<int>(width),ch=static_cast<int>(height);
    struct Bitmap {
        HDC dc{};HBITMAP bitmap{};HGDIOBJ previous{};void* bits{};
        Bitmap(int width,int height) {
            dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;
            info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            if(dc && bitmap) previous=SelectObject(dc,bitmap);
        }
        ~Bitmap() {if(previous) SelectObject(dc,previous);if(bitmap) DeleteObject(bitmap);if(dc) DeleteDC(dc);}
    } image(cw,ch);
    if(cw<=0 || ch<=0 || !image.dc || !image.bitmap) throw std::runtime_error("Cannot allocate VR controls bitmap");
    const double scale_x=static_cast<double>(cw)/w,scale_y=static_cast<double>(ch)/h;
    const auto px=[&](int value) {return static_cast<int>(std::lround(value*scale_x));};
    const auto py=[&](int value) {return static_cast<int>(std::lround(value*scale_y));};
    const auto point=[&](int size) {return std::max(1,static_cast<int>(std::lround(size*scale_y)));};
    const auto white=RGB(236,241,245),muted=RGB(170,189,201),cyan=RGB(93,211,233),card=RGB(17,24,32);
    RECT all{0,0,cw,ch};auto background=CreateSolidBrush(RGB(0,0,0));FillRect(image.dc,&all,background);DeleteObject(background);
    // Only the dark card interior is opaque; the untouched surround stays clear.
    auto pen=CreatePen(PS_SOLID,1,card);auto old_pen=SelectObject(image.dc,pen);
    auto brush=CreateSolidBrush(card);auto old_brush=SelectObject(image.dc,brush);
    RoundRect(image.dc,px(8),py(8),px(504),py(376),px(26),py(26));
    SelectObject(image.dc,old_brush);SelectObject(image.dc,old_pen);DeleteObject(brush);DeleteObject(pen);
    const auto text=[&](const std::wstring& value,int left,int top,int right,int bottom,int size,COLORREF colour) {
        auto font=CreateFontW(-point(size),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        auto previous=SelectObject(image.dc,font);SetTextColor(image.dc,colour);SetBkMode(image.dc,TRANSPARENT);
        RECT rect{px(left),py(top),px(right),py(bottom)};DrawTextW(image.dc,value.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(image.dc,previous);DeleteObject(font);
    };
    const auto paragraph=[&](const wchar_t* value,int left,int top,int right,int size,COLORREF colour) {
        auto font=CreateFontW(-point(size),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        auto previous=SelectObject(image.dc,font);SetTextColor(image.dc,colour);SetBkMode(image.dc,TRANSPARENT);
        RECT rect{px(left),top,px(right),top};
        DrawTextW(image.dc,value,-1,&rect,DT_LEFT|DT_WORDBREAK|DT_CALCRECT);
        const int used=rect.bottom-rect.top;RECT draw{px(left),top,px(right),top+used};
        DrawTextW(image.dc,value,-1,&draw,DT_LEFT|DT_WORDBREAK);
        SelectObject(image.dc,previous);DeleteObject(font);return used;
    };
    text(L"Quest controls",24,12,488,48,26,white);
    text(L"LEFT",24,52,250,80,20,cyan);text(L"RIGHT",262,52,488,80,20,cyan);
    const wchar_t* left_lines[]={L"Stick - move (click: sprint)",L"X hold - weapon wheel",L"Y + right stick - lean",
        L"Trigger - power / weapon",L"Grip - grab, interact, fist",L"Menu - pause (hold: journal)"};
    const wchar_t* right_lines[]={L"Stick - turn (down: crouch, click: zoom)",L"A - jump / mantle",L"B - holster weapons",
        L"Swing - sword attack",L"Grip - grab, interact, block",L"Trigger or sword sideways - block"};
    int body=py(90);
    for(int i=0;i<6;++i) {
        const int used=std::max(paragraph(left_lines[i],24,body,250,18,white),paragraph(right_lines[i],262,body,488,18,white));
        body+=used+py(8);
    }
    text(L"The left hand aims powers and weapons",24,344,488,368,17,muted);
    GdiFlush();const auto raw=static_cast<const unsigned char*>(image.bits);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(cw)*ch*4);
    for(std::size_t i=0;i<pixels.size();i+=4) {
        pixels[i]=raw[i+2];pixels[i+1]=raw[i+1];pixels[i+2]=raw[i];
        pixels[i+3]=(raw[i]|raw[i+1]|raw[i+2])?255:0;
    }
    return pixels;
}
} // namespace d2vr
