#pragma once
#include "native_wrist_hud.hpp"
#include "tracked_pose.hpp"
#include "grab_beam.hpp"
#include <algorithm>
#include <vector>
#include <string>

namespace d2vr {
// face_sign: the grip X direction (+1/-1) that must point toward the eyes; 0 = no wrist-side requirement. Grip +X points
// out of the back of the right hand and into the palm of the left (WebXR/OpenXR grip convention), so the inner (palm)
// side is +1 for the left hand and -1 for the right.
inline std::optional<TrackedPose> wrist_hud_pose(const TrackedPose& hand,const TrackedPose& head,int face_sign=0) {
    if(!finite(hand.position) || !finite(head.position)) return {};
    if(face_sign) {
        const auto dorsal=rotated(normalized(hand.orientation),{static_cast<double>(face_sign),0,0});
        const auto to_head=head.position-hand.position;const auto d=length(to_head);
        if(d<1e-3 || dot(dorsal,to_head*(1/d))<.3) return {}; // back of the hand roughly toward the eyes
    }
    // Watch face on the forearm: grip -Z points toward the fingers, so +Z runs
    // up the forearm. Sit behind the wrist, lifted toward the viewer, and spin
    // about the forearm so the face looks at the head; no wrist-twist gate.
    const auto back=rotated(normalized(hand.orientation),{0,0,1});
    // v232: .14 up the forearm (clear of the hand/controller model) and .07 off the axis (clear of the sleeve/forearm mesh).
    const auto centre=hand.position+back*.14;
    const auto toward=head.position-centre;const auto distance=length(toward);
    if(distance<.15 || distance>.80 || hand.position.y<head.position.y-.75) return {};
    const auto across=toward-back*dot(toward,back);const auto across_length=length(across);
    if(across_length<.35*distance) return {}; // looking straight down the forearm
    const auto gaze=rotated(normalized(head.orientation),{0,0,-1});
    if(dot(gaze,toward*(-1/distance))<.8) return {}; // only when glanced at (~37 degrees)
    const auto normal=across*(1/across_length);
    auto up=rotated(normalized(head.orientation),{0,1,0});up=up-normal*dot(up,normal);
    if(length(up)<.2) up=back-normal*dot(back,normal);
    up=unit(up);
    return TrackedPose{centre+normal*.07,basis_quaternion(cross(up,normal),up,normal)};
}
// Display only values supplied by native HUD calls. Ratios are percentages;
// they are not invented absolute health/mana maximums or inventory counts.
inline std::vector<std::uint8_t> wrist_hud_image(const WristHudSnapshot& data,unsigned side) {
    constexpr int w=512,h=192;
    struct Bitmap {
        HDC dc{};HBITMAP bitmap{};HGDIOBJ previous{};void* bits{};
        Bitmap() {
            dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;
            info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            if(dc && bitmap) previous=SelectObject(dc,bitmap);
        }
        ~Bitmap() {if(previous) SelectObject(dc,previous);if(bitmap) DeleteObject(bitmap);if(dc) DeleteDC(dc);}
    } image;
    if(!image.dc || !image.bitmap || side>1) throw std::runtime_error("Cannot create wrist HUD bitmap");
    RECT whole{0,0,w,h};auto brush=CreateSolidBrush(RGB(0,0,0));FillRect(image.dc,&whole,brush);DeleteObject(brush);
    const auto panel=RGB(18,22,25);auto pen=CreatePen(PS_SOLID,1,panel);auto old_pen=SelectObject(image.dc,pen);
    brush=CreateSolidBrush(panel);auto old_brush=SelectObject(image.dc,brush);
    RoundRect(image.dc,8,8,w-8,h-8,22,22);SelectObject(image.dc,old_brush);SelectObject(image.dc,old_pen);DeleteObject(brush);DeleteObject(pen);
    const auto text=[&](const std::wstring& value,int top,int bottom,int size) {
        auto font=CreateFontW(-size,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        auto previous=SelectObject(image.dc,font);SetTextColor(image.dc,RGB(235,226,207));SetBkMode(image.dc,TRANSPARENT);
        RECT r{24,top,w-24,bottom};DrawTextW(image.dc,value.c_str(),-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        SelectObject(image.dc,previous);DeleteObject(font);
    };
    const auto meter=[&](const wchar_t* label,double value,bool valid,int top,COLORREF colour) {
        text(std::wstring(label)+(valid?L"  "+std::to_wstring(static_cast<int>(std::lround(std::clamp(value,0.,1.)*100)))+L"%":L"  —"),top,top+42,29);
        RECT r{24,top+48,w-24,top+62};auto b=CreateSolidBrush(RGB(48,51,52));FillRect(image.dc,&r,b);DeleteObject(b);
        if(valid) {r.right=r.left+static_cast<int>((w-48)*std::clamp(value,0.,1.));b=CreateSolidBrush(colour);FillRect(image.dc,&r,b);DeleteObject(b);}
    };
    if(side==1) {
        meter(L"HEALTH",data.health_fill,data.health_valid,20,RGB(184,78,65));
        meter(L"MANA",data.mana_fill,data.mana_valid,104,RGB(77,131,177));
    } else {
        text(data.ammo_valid?L"AMMO  "+std::to_wstring(data.ammo_count):L"AMMO  —",22,90,38);
        text(data.ammo_valid?L"LOADED  "+std::to_wstring(data.loaded_count):L"LOADED  —",96,168,30);
    }
    GdiFlush();const auto* raw=static_cast<const unsigned char*>(image.bits);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w)*h*4);
    for(std::size_t i=0;i<pixels.size();i+=4) {
        pixels[i]=raw[i+2];pixels[i+1]=raw[i+1];pixels[i+2]=raw[i];
        pixels[i+3]=(raw[i]|raw[i+1]|raw[i+2])?242:0;
    }
    return pixels;
}
} // namespace d2vr
