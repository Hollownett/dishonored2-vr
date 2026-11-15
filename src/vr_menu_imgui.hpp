#pragma once
// The VR settings panel drawn with Dear ImGui (third_party/imgui, v1.92.8, MIT) instead of GDI (settings_panel_image.hpp). The layout is
// the SAME 640x560 panel and the same row rectangles (vr_settings.hpp settings_row_top/pitch/height), so the controller pointer hit test,
// stick navigation and the panel placement in the eyes are unchanged; only the image is new. VrSettings stays the one model: ImGui is
// only the renderer (no ImGui input, so the game's own XInput polling can never navigate it a second time).
// The calibration, measurement and hand-fit pages keep the GDI image (GameXr falls back to it there).
// tools/vr_menu_preview.cpp renders this same function to a PNG without the game.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include "imgui.h"
#include "backends/imgui_impl_dx11.h"
#include "vr_settings.hpp"

namespace d2vr {
inline std::string menu_utf8(const std::wstring& w) {
    if(w.empty()) return {};
    const int n=WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);
    std::string s(static_cast<std::size_t>(n),'\0');
    WideCharToMultiByte(CP_UTF8,0,w.data(),static_cast<int>(w.size()),s.data(),n,nullptr,nullptr);return s;
}
struct VrMenuRow {std::string name,value;double slider{-1};std::string help;};
// The 14 rows in panel order (mirrors settings_panel_image.hpp).
inline std::array<VrMenuRow,settings_row_count> vr_menu_rows(const VrSettings& s,double effective,UINT width,UINT height) {
    const auto split=[](const wchar_t* all,double slider,const char* help) {
        const std::wstring a(all);const auto at=a.find(L": ");
        return VrMenuRow{menu_utf8(a.substr(0,at)),at==std::wstring::npos?std::string():menu_utf8(a.substr(at+2)),slider,help};};
    char scale[32];std::snprintf(scale,sizeof scale,"%.1fx",s.requested());
    char size[96];std::snprintf(size,sizeof size,"%u x %u per eye",width,height);
    std::string size_text=size;if(effective+.01<s.requested()) {char lim[32];std::snprintf(lim,sizeof lim," (%.2fx limit)",effective);size_text+=lim;}
    const auto t=s.turning();char turn[48];
    if(t.mode==TurnControl::Mode::snap) std::snprintf(turn,sizeof turn,"Snap %g degrees",t.snap_degrees);
    else if(t.mode==TurnControl::Mode::smooth) std::snprintf(turn,sizeof turn,"Smooth %g deg/s",t.smooth_degrees_per_second);
    else std::snprintf(turn,sizeof turn,"Native game");
    std::string renderer_help="Native stereo renders both eyes every frame. Alternate eye renders one eye per frame and re-projects the other (built-in warp). "
        "AFW (alternate frame warping) rebuilds the other eye from the fresh one (needs PDAFWPlugin.dll next to the game and an FSR or DLSS anti-aliasing mode). "
        "Choose with the stick, then press A to apply: it switches at once, no restart.";
    if(s.renderer_pending() && !s.renderer_available(s.renderer())) renderer_help+=" Unavailable this session: alternate eye did not start (see the log).";
    if(!s.afw_status().empty() && s.renderer()==2) renderer_help+=" Status: "+s.afw_status()+".";
    return {{
        {"Resolution",scale,(s.requested()-.5)/1.5,"Pixels each eye renders. Higher is sharper but slower; the mod caps it to what the GPU can hold. Now "+size_text+"."},
        split(s.temporal_aa_text(),-1,"Smooths shimmering edges over time. FSR Native and DLAA use full resolution; Quality and Balanced upscale. AFW needs an FSR or DLSS mode."),
        {"Edge anti-aliasing",s.antialiasing()?"SMAA":"Native only",-1,"SMAA smooths edges on its own when Anti-aliasing is Off. Skipped in a temporal mode."},
        split(s.mirror_text(),-1,"What the desktop monitor mirrors: both eyes, one eye, or off (fastest). The headset image is unchanged."),
        split(s.sharpness_text(),s.sharpness(),"Contrast sharpening after anti-aliasing. Raise it if the image looks soft, lower it if edges crawl."),
        split(s.hud_text(),(s.hud_percent()-55)/45.,"Scales the HUD and prompts. 55% sits in the centre of your view; 100% keeps the original layout."),
        {"Controls & gestures","Open",-1,"Controller pictures and motion hints. Right stick: pages."},
        split(s.crosshair_text(),-1,"Shows the game's crosshair where your gun or power aims."),
        {"Turning",turn,-1,"Native uses the original stick turn. Snap rotates in fixed steps; Smooth turns continuously."},
        split(s.scripted_recenter_text(),-1,"Re-aims the view at scripted moments so you keep facing the action."),
        split(s.cutscene_mode_text(),-1,"VR comfort limits camera motion; VR full follows the camera exactly; Theater uses a flat screen."),
        {"Calibrate avatar","Recenter view",-1,"Left: measure shoulders, then arm reach. Right: re-align the view to where you face now."},
        {"Renderer",menu_utf8(VrSettings::renderer_name(s.renderer())),-1,renderer_help},
        {"Test: unlock all","Back to game menu",-1,"Left: 100 runes for every power, infinite coins and every Black Market item. Right closes this menu."}}};
}

// Draws the main page into the current ImGui frame (a 640x560 display). Pure ImGui: no device, so the preview tool shares it.
// tabs: the panel is page 1 of the combined VR menu (then the Controls & gestures guide's Body / Controllers / Gestures pages, right stick
// left/right): a tab strip and a paging hint. Without tabs it is the stand-alone panel (opened from the pause menu).
inline void draw_vr_menu(const VrSettings& s,double effective,UINT width,UINT height,std::optional<PanelPointer> pointer,ImFont* font,bool tabs=true) {
    ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({640,560});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::Begin("##vrmenu",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoInputs);
    ImDrawList* d=ImGui::GetWindowDrawList();
    const auto col=[](int r,int g,int b,int a=255) {return IM_COL32(r,g,b,a);};
    const ImU32 bg=col(12,13,19),row_bg=col(22,23,32),row_edge=col(44,46,58),text=col(214,218,226),muted=col(140,146,162),
                accent=col(201,164,96),accent_dark=col(28,22,14),value_bg=col(30,26,40),value_edge=col(92,84,112);
    const auto label=[&](const std::string& t,float x,float y,float size,ImU32 c) {d->AddText(font,size,{x,y},c,t.c_str());};
    const auto measure=[&](const std::string& t,float size) {return font->CalcTextSizeA(size,FLT_MAX,0,t.c_str()).x;};
    const auto fit=[&](std::string t,float size,float room) { // ellipsize to the room
        if(measure(t,size)<=room) return t;
        while(!t.empty() && measure(t+"...",size)>room) t.pop_back();
        return t+"...";};
    d->AddRectFilledMultiColor({0,0},{640,560},col(16,17,26),col(16,17,26),bg,bg);
    // Title and a brass rule.
    label("VR Settings",24,10,34,col(150,154,172));
    d->AddLine({22,52},{420,52},accent,1.5f);d->AddCircleFilled({424,52},2.5f,accent);
    if(tabs) { // VR SETTINGS | BODY | CONTROLLERS | GESTURES, this page highlighted
        const char* names[4]={"SETTINGS","BODY","CONTROLS","GESTURES"};float x=616;
        for(int t=3;t>=0;--t) {const float tw=measure(names[t],12)+14;x-=tw;
            if(t==0) {d->AddRectFilled({x,16},{x+tw,38},accent,3);label(names[t],x+7,20,12,accent_dark);}
            else {d->AddRect({x,16},{x+tw,38},col(70,72,86),3);label(names[t],x+7,20,12,muted);}
            x-=4;}
        label("1 / 4",616-measure("1 / 4",12),42,12,muted);
    } else {const std::string hint="Right temple + grip in play";label(hint,616-measure(hint,13),24,13,muted);}
    const auto rows=vr_menu_rows(s,effective,width,height);
    const int selected=static_cast<int>(std::min<unsigned>(s.selected(),settings_row_count-1));
    for(int i=0;i<settings_row_count;++i) {
        const float top=static_cast<float>(settings_row_top+settings_row_pitch*i),bot=top+settings_row_height,cy=(top+bot)/2;
        const bool sel=i==selected;const auto& r=rows[static_cast<std::size_t>(i)];
        if(sel) {d->AddRectFilled({16,top-1},{624,bot+1},accent,3);d->AddRect({16,top-1},{624,bot+1},col(240,214,160),3.f,ImDrawFlags_None,1.f);}
        else {d->AddRectFilled({20,top},{620,bot},row_bg,2);d->AddLine({20,top},{620,top},row_edge);}
        const ImU32 ink=sel?accent_dark:text;const float fs=16,ty=cy-fs/2-1;
        if(i==11 || i==13) { // two actions share the row
            label(r.name,28,ty,fs,ink);d->AddLine({320,top+5},{320,bot-5},sel?col(120,96,60):col(62,64,78));label(r.value,332,ty,fs,ink);continue;
        }
        label(fit(r.name,fs,236),30,ty,fs,ink);
        const float vl=290,vr=596;
        const ImU32 arrow=sel?accent_dark:col(176,180,192);
        d->AddTriangleFilled({272,cy},{283,cy-6},{283,cy+6},arrow);d->AddTriangleFilled({614,cy},{603,cy-6},{603,cy+6},arrow);
        const auto box=[&](float l,float rr) {d->AddRectFilled({l,top+3},{rr,bot-3},sel?col(236,206,150):value_bg,4);d->AddRect({l,top+3},{rr,bot-3},sel?col(120,96,60):value_edge,4);};
        const ImU32 vink=sel?accent_dark:col(246,246,250);
        if(r.slider>=0) {
            const float tl=vl+6,tr=458,fx=tl+(tr-tl)*static_cast<float>(std::clamp(r.slider,0.,1.));
            d->AddRectFilled({tl,cy-2},{tr,cy+2},sel?col(150,118,66):col(52,46,70),2);d->AddRectFilled({tl,cy-2},{fx,cy+2},sel?accent_dark:col(220,224,228),2);
            d->AddCircleFilled({fx,cy},7,sel?accent_dark:col(230,232,236));d->AddCircle({fx,cy},7,sel?col(240,214,160):col(110,114,130));
            std::string v=r.value;if(const auto at=v.find(" (");at!=std::string::npos) v.resize(at);
            box(466,vr);label(fit(v,13,vr-480),474,cy-7,13,vink);
        } else if(i==12 && s.renderer_pending()) { // Renderer: the selection, then the Apply chip (A applies it, live)
            const bool ok=s.renderer_available(s.renderer());const float cl=ok?528.f:470.f;
            box(vl,cl-6);label(fit(r.value,13,cl-vl-26),vl+10,cy-7,13,vink);
            if(ok) {d->AddRectFilled({cl,top+3},{vr,bot-3},sel?col(40,120,70):col(46,140,80),4);label("A: APPLY",cl+8,cy-7,13,col(240,250,240));}
            else {d->AddRectFilled({cl,top+3},{vr,bot-3},col(110,40,40),4);label("unavailable",cl+8,cy-7,13,col(250,230,230));}
        } else {box(vl,vr);label(fit(r.value,13,vr-vl-20),vl+10,cy-7,13,vink);}
    }
    // Description plate: the selected row's name and help, word-wrapped.
    const float pt=settings_plate_top,pb=pt+settings_plate_height;
    d->AddRectFilled({20,pt},{620,pb},col(17,18,26),3);d->AddRect({20,pt},{620,pb},col(52,54,68),3);
    const auto& cur=rows[static_cast<std::size_t>(selected)];
    label(cur.name,30,pt+5,19,col(226,230,238));d->AddLine({30,pt+28},{610,pt+28},col(70,74,92));
    d->AddText(font,13,{30,pt+34},col(186,190,204),cur.help.c_str(),nullptr,580);
    // Footer: key boxes.
    const float ft=settings_footer_top,fb=ft+settings_footer_height;
    const auto key=[&](float& right,const char* k,const char* what) {
        const float lw=measure(what,15),kw=measure(k,13)+14,lx=right-lw,kx=lx-kw-6;
        d->AddRectFilled({kx,ft+4},{kx+kw,fb-4},col(40,42,54),3);d->AddRect({kx,ft+4},{kx+kw,fb-4},col(88,90,106),3);
        label(k,kx+7,ft+8,13,text);label(what,lx,ft+6,15,text);right=kx-18;};
    float right=616;key(right,"B","Back");key(right,"A / Trigger","Change");key(right,"L-Stick","Select");if(tabs) key(right,"R-Stick < >","Pages");
    if(pointer) { // where the controller ray meets the panel
        const ImVec2 p{static_cast<float>(pointer->u*640),static_cast<float>(pointer->v*560)};
        d->AddCircleFilled(p,5,col(255,255,255,220));d->AddCircle(p,7,col(201,164,96,255),0,2);
    }
    ImGui::End();ImGui::PopStyleVar(2);
}

// Loads the panel font: Segoe UI from Windows, else ImGui's built-in font.
inline ImFont* vr_menu_font(ImGuiIO& io) {
    wchar_t dir[MAX_PATH]{};GetWindowsDirectoryW(dir,MAX_PATH);
    const std::string path=menu_utf8(std::wstring(dir)+L"\\Fonts\\segoeui.ttf");
    ImFontConfig cfg{};cfg.OversampleH=2;
    if(FILE* f=nullptr;fopen_s(&f,path.c_str(),"rb")==0 && f) {std::fclose(f);if(auto* font=io.Fonts->AddFontFromFileTTF(path.c_str(),17.f,&cfg)) return font;}
    return io.Fonts->AddFontDefault();
}

// GPU side: an ImGui context and a 640x560 render target on the game's device.
class VrMenuImgui {
    ImGuiContext* imgui_{};ImFont* font_{};ID3D11DeviceContext* context_{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target_;Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtv_;
public:
    VrMenuImgui(ID3D11Device* device,ID3D11DeviceContext* context):context_(context) {
        ImGuiContext* prior=ImGui::GetCurrentContext();
        imgui_=ImGui::CreateContext();ImGui::SetCurrentContext(imgui_);
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.LogFilename=nullptr;io.DisplaySize={640,560};
        font_=vr_menu_font(io);
        const bool ok=ImGui_ImplDX11_Init(device,context);
        ImGui::SetCurrentContext(prior);
        if(!ok) throw std::runtime_error("ImGui DX11 backend");
        D3D11_TEXTURE2D_DESC d{};d.Width=640;d.Height=560;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.SampleDesc.Count=1;
        d.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        if(FAILED(device->CreateTexture2D(&d,nullptr,&target_)) || FAILED(device->CreateRenderTargetView(target_.Get(),nullptr,&rtv_))) throw std::runtime_error("ImGui menu target");
    }
    ~VrMenuImgui() {ImGuiContext* prior=ImGui::GetCurrentContext();ImGui::SetCurrentContext(imgui_);ImGui_ImplDX11_Shutdown();ImGui::DestroyContext(imgui_);ImGui::SetCurrentContext(prior==imgui_?nullptr:prior);}
    VrMenuImgui(const VrMenuImgui&)=delete;VrMenuImgui& operator=(const VrMenuImgui&)=delete;
    ID3D11Texture2D* texture() const noexcept {return target_.Get();}
    void render(const VrSettings& s,double effective,UINT width,UINT height,std::optional<PanelPointer> pointer,bool tabs=true) {
        ImGuiContext* prior=ImGui::GetCurrentContext();ImGui::SetCurrentContext(imgui_);
        auto& io=ImGui::GetIO();io.DisplaySize={640,560};io.DeltaTime=1.f/90;
        ImGui_ImplDX11_NewFrame();ImGui::NewFrame();
        draw_vr_menu(s,effective,width,height,pointer,font_,tabs);
        ImGui::Render();
        // The backend restores its pipeline state but not the output merger targets: keep the game's.
        ID3D11RenderTargetView* old_rtv[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};ID3D11DepthStencilView* old_dsv{};
        context_->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,old_rtv,&old_dsv);
        const float clear[4]{0,0,0,1};context_->ClearRenderTargetView(rtv_.Get(),clear);
        ID3D11RenderTargetView* rtv=rtv_.Get();context_->OMSetRenderTargets(1,&rtv,nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        context_->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,old_rtv,old_dsv);
        for(auto* v:old_rtv) if(v) v->Release();
        if(old_dsv) old_dsv->Release();
        ImGui::SetCurrentContext(prior);
    }
};
} // namespace d2vr
