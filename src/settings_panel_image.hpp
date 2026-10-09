#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include "vr_settings.hpp"

namespace d2vr {
// A bitmap belonging only to the mod's OpenXR panel. This does not inspect or
// automate the desktop. The runtime image is uploaded by the Present thread.
inline std::vector<unsigned char> settings_panel_image(const VrSettings& settings,double effective,
    unsigned width,unsigned height,std::optional<PanelPointer> pointer) {
    constexpr int w=640,h=560;
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
    if(!image.dc || !image.bitmap) throw std::runtime_error("Cannot allocate VR settings bitmap");
    const auto box=[&](int left,int top,int right,int bottom,COLORREF colour) {
        RECT rect{left,top,right,bottom};auto brush=CreateSolidBrush(colour);FillRect(image.dc,&rect,brush);DeleteObject(brush);
    };
    const auto text=[&](const std::wstring& value,int left,int top,int right,int bottom,int size,COLORREF colour) {
        auto font=CreateFontW(-size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        auto previous=SelectObject(image.dc,font);SetTextColor(image.dc,colour);SetBkMode(image.dc,TRANSPARENT);
        RECT rect{left,top,right,bottom};DrawTextW(image.dc,value.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        SelectObject(image.dc,previous);DeleteObject(font);
    };
    box(0,0,w,h,RGB(17,24,32));const auto white=RGB(236,241,245),muted=RGB(170,189,201),cyan=RGB(93,211,233);
    if(settings.calibration_page()) {
        text(L"Calibrate avatar",24,20,616,72,34,white);
        text(L"Stand or sit upright and look forward.",12,96,628,136,25,white);
        text(L"Stretch both arms sideways at shoulder height.",8,144,632,184,23,white);
        text(L"Squeeze and HOLD BOTH GRIPS closed.",8,192,632,232,25,white);
        text(L"Keep arms level and hold still until the line fills.",8,236,632,272,21,white);
        const wchar_t* status=L"Press A to start";
        switch(settings.calibration_state()) {
            case AvatarCalibrationState::hold_pose:status=L"Hold on - keep both grips closed...";break;
            case AvatarCalibrationState::grips_released:status=L"Grip released - hold BOTH grips to begin";break;
            case AvatarCalibrationState::pose_moving:status=L"Movement reset - hold your T-pose steady";break;
            case AvatarCalibrationState::tracking_lost:status=L"Tracking lost - show both controllers";break;
            case AvatarCalibrationState::pose_invalid:status=L"Pose reset - stretch arms sideways and level";break;
            case AvatarCalibrationState::dimensions_unsupported:status=L"Pose size unsupported - press A to retry";break;
            case AvatarCalibrationState::ready:status=settings.calibration_runtime()==AvatarFitRuntimeStatus::unmeasured?L"Measurement saved - fit not active":settings.calibration_runtime_text();break;
            case AvatarCalibrationState::reference_changed:status=L"View recentered - press A to recalibrate";break;
            case AvatarCalibrationState::rig_changed:status=L"Avatar changed - press A to recalibrate";break;
            case AvatarCalibrationState::cancelled:status=L"Capture cancelled - press A to start";break;
            default:break;
        }
        text(status,8,284,632,326,23,cyan);
        box(48,342,592,358,RGB(38,48,60));
        const auto progress=settings.calibration_progress();
        if(progress>0) box(48,342,48+static_cast<int>(544*progress),358,cyan);
        text(std::to_wstring(static_cast<int>(progress*100))+L"% stable hold (1 second)",8,366,632,398,21,cyan);
        text(L"Fits arm reach. Floor height is not measured.",8,414,632,444,20,muted);
        text(L"B: return to VR settings",8,464,632,520,24,white);
    } else {
    text(L"VR settings",24,0,616,36,27,white);
    text(L"Resolution",24,36,616,60,17,muted);
    std::wostringstream scale;scale<<std::fixed<<std::setprecision(1)<<settings.requested()<<L"x";
    text(scale.str(),215,60,425,104,32,white);
    box(28,60,184,104,settings.selected()==0?RGB(36,92,111):RGB(38,48,60));
    box(456,60,612,104,settings.selected()==0?RGB(36,92,111):RGB(38,48,60));
    text(L"-",28,60,184,104,32,white);text(L"+",456,60,612,104,32,white);
    std::wostringstream size;size<<width<<L" x "<<height<<L" per eye";
    if(effective+.01<settings.requested()) size<<L" ("<<std::fixed<<std::setprecision(2)<<effective<<L"x limit)";
    text(size.str(),24,104,616,124,15,muted);
    // Ten 32 px rows on a 36 px pitch: resolution, temporal AA, edge AA, monitor
    // view, sharpening, aim reticle, controls hint, turning, calibrate/recenter, back.
    const auto row=[&](int index) {return 128+36*index;};
    box(28,row(1),612,row(1)+32,settings.selected()==1?RGB(36,92,111):RGB(38,48,60));
    text(settings.temporal_aa_text(),28,row(1),612,row(1)+32,20,white);
    box(28,row(2),612,row(2)+32,settings.selected()==2?RGB(36,92,111):RGB(38,48,60));
    text(settings.antialiasing()?L"Edge AA: SMAA Ultra":L"Edge AA: native only",28,row(2),612,row(2)+32,20,white);
    box(28,row(3),612,row(3)+32,settings.selected()==3?RGB(36,92,111):RGB(38,48,60));
    text(settings.mirror_text(),28,row(3),612,row(3)+32,20,white);
    box(28,row(4),612,row(4)+32,settings.selected()==4?RGB(36,92,111):RGB(38,48,60));
    text(settings.sharpness_text(),28,row(4),612,row(4)+32,20,white);
    box(28,row(5),612,row(5)+32,settings.selected()==5?RGB(36,92,111):RGB(38,48,60));
    text(settings.aim_reticle_text(),28,row(5),612,row(5)+32,20,white);
    box(28,row(6),612,row(6)+32,settings.selected()==6?RGB(36,92,111):RGB(38,48,60));
    text(settings.controls_hint_text(),28,row(6),612,row(6)+32,20,white);
    box(28,row(7),612,row(7)+32,settings.selected()==7?RGB(36,92,111):RGB(38,48,60));
    const auto turning=settings.turning();std::wostringstream turn;
    if(turning.mode==TurnControl::Mode::snap) turn<<L"Turning: Snap "<<turning.snap_degrees<<L" degrees";
    else if(turning.mode==TurnControl::Mode::smooth) turn<<L"Turning: Smooth "<<turning.smooth_degrees_per_second<<L" deg/s";
    else turn<<L"Turning: Native game";
    text(turn.str(),28,row(7),612,row(7)+32,20,white);
    box(28,row(8),612,row(8)+32,settings.selected()==8?RGB(36,92,111):RGB(38,48,60));
    const bool fit_notice=settings.calibration_required() || settings.calibration_runtime_notice();
    text(L"Calibrate avatar",28,row(8),310,fit_notice?row(8)+22:row(8)+32,19,white);
    if(fit_notice) {
        const auto reason=settings.calibration_state()==AvatarCalibrationState::rig_changed?L"Avatar changed - fit required":
            settings.calibration_state()==AvatarCalibrationState::reference_changed?L"Recentered - fit required":L"Capture cancelled - retry";
        text(settings.calibration_runtime_notice()?settings.calibration_runtime_text():reason,28,row(8)+22,310,row(8)+32,12,cyan);
    }
    text(L"Recenter view",322,row(8),612,row(8)+32,19,white);
    box(28,row(9),612,row(9)+32,settings.selected()==9?RGB(36,92,111):RGB(38,48,60));
    text(L"Back to game menu",28,row(9),612,row(9)+32,20,white);
    text(L"Point + trigger  /  stick + A  /  B to close",8,492,632,516,16,muted);
    text(L"In play: hold Menu for Journal; Menu + right stick for shortcuts",8,520,632,544,15,muted);
    }
    if(pointer) {
        const int x=static_cast<int>(pointer->u*w),y=static_cast<int>(pointer->v*h);
        auto pen=CreatePen(PS_SOLID,3,cyan);auto old_pen=SelectObject(image.dc,pen);
        auto old_brush=SelectObject(image.dc,GetStockObject(HOLLOW_BRUSH));Ellipse(image.dc,x-8,y-8,x+8,y+8);
        SelectObject(image.dc,old_brush);SelectObject(image.dc,old_pen);DeleteObject(pen);
    }
    GdiFlush();const auto raw=static_cast<const unsigned char*>(image.bits);
    std::vector<unsigned char> pixels(w*h*4);
    for(std::size_t i=0;i<pixels.size();i+=4) {pixels[i]=raw[i+2];pixels[i+1]=raw[i+1];pixels[i+2]=raw[i];pixels[i+3]=255;}
    return pixels;
}
} // namespace d2vr
