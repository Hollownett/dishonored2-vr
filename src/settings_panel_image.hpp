#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include "vr_settings.hpp"
#include "game_art.hpp"

namespace d2vr {
// Content bitmap for the native menu's VR Settings page. The Present thread
// composites it into the native menu image's content area.
// Drawn at 3x with the game's own menu art when the user extracted it (tools/extract_game_art.py -> artifacts/game-art: ink
// panel, brush highlight, arrows, slider, divider, character portraits), else procedurally in the same style; then
// box-downsampled.
// Layout is fixed at 640x560 (game_xr.hpp owns the swapchain size); row i occupies
// y=settings_row_top+settings_row_pitch*i, height settings_row_height (vr_settings.hpp constants) - vr_settings.hpp pointer hit-testing uses the same rows.
inline std::vector<unsigned char> settings_panel_image(const VrSettings& settings,double effective,
    unsigned width,unsigned height,std::optional<PanelPointer> pointer) {
    constexpr int w=640,h=560,S=3,W=w*S,H=h*S;
    struct Bitmap {
        HDC dc{};HBITMAP bitmap{};HGDIOBJ previous{};void* bits{};
        Bitmap() {
            dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=W;info.bmiHeader.biHeight=-H;info.bmiHeader.biPlanes=1;
            info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
            bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            if(dc && bitmap) previous=SelectObject(dc,bitmap);
        }
        ~Bitmap() {if(previous) SelectObject(dc,previous);if(bitmap) DeleteObject(bitmap);if(dc) DeleteDC(dc);}
    } image;
    if(!image.dc || !image.bitmap) throw std::runtime_error("Cannot allocate VR settings bitmap");
    const HDC dc=image.dc;
    // Ink ground: dark slate with a soft vignette and faint grain (written straight into the DIB).
    {
        auto* px=static_cast<std::uint32_t*>(image.bits);
        for(int y=0;y<H;++y) for(int x=0;x<W;++x) {
            const double fx=(x/double(W)-.42)*1.2,fy=(y/double(H)-.45)*1.3,v=std::min(1.,std::sqrt(fx*fx+fy*fy));
            const int grain=static_cast<int>((static_cast<unsigned>(x)*73856093u^static_cast<unsigned>(y)*19349663u)>>13&3);
            const int r=static_cast<int>(30-14*v)+grain,g=static_cast<int>(32-14*v)+grain,b=static_cast<int>(42-16*v)+grain;
            px[static_cast<std::size_t>(y)*W+x]=static_cast<std::uint32_t>(r<<16|g<<8|b);
        }
    }
    auto& art=game_art();auto* const px=static_cast<std::uint32_t*>(image.bits);
    const auto ink_panel=art.get("menu_background"),brush=art.get("highlight_brush"),arrow_l=art.get("arrow_left"),arrow_r=art.get("arrow_right"),
        track=art.get("slider_track"),knob=art.get("slider_knob"),divider=art.get("divider"),shard_art=art.get("glass_shard"),
        frame_art=art.get("frame"),glow=art.get("highlight_glow_bar");
    const auto put=[&](const std::shared_ptr<const GameArt>& a,double l,double t,double r,double b,double opacity=1,bool fit=false,std::uint32_t tint=0xffffff) {
        GdiFlush();blit_art(px,W,H,*a,l*S,t*S,r*S,b*S,opacity,fit,tint);};
    if(ink_panel) put(ink_panel,-40,-30,680,590,.95);   // the game's ink panel behind menu lists
    const auto rgb=[](int r,int g,int b) {return RGB(r,g,b);};
    const auto pen=[&](int width_px,COLORREF c) {return CreatePen(PS_SOLID,std::max(1,width_px),c);};
    const auto fill=[&](int l,int t,int r,int b,COLORREF c) {
        RECT rc{l*S,t*S,r*S,b*S};auto br=CreateSolidBrush(c);FillRect(dc,&rc,br);DeleteObject(br);};
    const auto line=[&](double x0,double y0,double x1,double y1,double thick,COLORREF c) {
        auto p=pen(static_cast<int>(thick*S),c);auto o=SelectObject(dc,p);
        MoveToEx(dc,static_cast<int>(x0*S),static_cast<int>(y0*S),nullptr);LineTo(dc,static_cast<int>(x1*S),static_cast<int>(y1*S));
        SelectObject(dc,o);DeleteObject(p);};
    const auto poly=[&](const std::vector<POINT>& pts,COLORREF in,COLORREF edge,double thick) {   // points in 3x space
        auto p=thick>0?pen(static_cast<int>(thick*S),edge):static_cast<HPEN>(GetStockObject(NULL_PEN));
        auto b=in==CLR_INVALID?static_cast<HBRUSH>(GetStockObject(NULL_BRUSH)):CreateSolidBrush(in);
        auto op=SelectObject(dc,p);auto ob=SelectObject(dc,b);Polygon(dc,pts.data(),static_cast<int>(pts.size()));
        SelectObject(dc,op);SelectObject(dc,ob);if(thick>0) DeleteObject(p);if(in!=CLR_INVALID) DeleteObject(b);};
    const auto tri=[&](int x,int y,int dx,int dy,int ex,int ey,COLORREF in,COLORREF edge) {
        poly({{x*S,y*S},{dx*S,dy*S},{ex*S,ey*S}},in,edge,.6);};
    // Fonts: native lettering is hand-drawn italic serif. Script face for titles, italic serif for rows.
    const auto face=[&](std::initializer_list<const wchar_t*> names,bool italic) {
        for(auto name:names) {
            auto font=CreateFontW(-20,0,0,0,FW_NORMAL,italic,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,name);
            auto o=SelectObject(dc,font);wchar_t got[LF_FACESIZE]{};GetTextFaceW(dc,LF_FACESIZE,got);SelectObject(dc,o);DeleteObject(font);
            if(!_wcsicmp(got,name)) return name;
        }
        return static_cast<const wchar_t*>(L"Segoe UI");
    };
    const wchar_t* const serif=face({L"Palatino Linotype",L"Book Antiqua",L"Georgia",L"Times New Roman"},true);
    const wchar_t* const script=face({L"Gabriola",L"Monotype Corsiva",L"Palatino Linotype",L"Georgia"},true);
    // kind 0 = italic serif, 1 = script.
    const auto text=[&](const std::wstring& value,int left,int top,int right,int bottom,int size,COLORREF colour,int kind=0,UINT flags=DT_CENTER|DT_VCENTER|DT_SINGLELINE) {
        auto font=CreateFontW(-size*S,0,0,0,FW_NORMAL,kind==0?TRUE:FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,kind==0?serif:script);
        auto previous=SelectObject(dc,font);SetTextColor(dc,colour);SetBkMode(dc,TRANSPARENT);
        RECT rect{left*S,top*S,right*S,bottom*S};DrawTextW(dc,value.c_str(),-1,&rect,flags);
        SelectObject(dc,previous);DeleteObject(font);
    };
    const COLORREF white=rgb(236,241,245),muted=rgb(160,168,184),cyan=rgb(93,211,233),ink=rgb(24,26,36);
    // Faint diagonal hairlines and glass shards, as on the native menu ground.
    if(!ink_panel) for(const auto& l:{std::array<int,4>{0,150,260,0},{0,330,300,110},{380,560,640,300},{470,0,640,120},{250,560,420,420},{560,560,640,470},{0,470,70,380}})
        line(l[0],l[1],l[2],l[3],.7,rgb(58,61,76));
    const auto shard=[&](std::initializer_list<POINT> pts,int lum) {
        std::vector<POINT> v(pts);for(auto& q:v) {q.x*=S;q.y*=S;}
        poly(v,rgb(lum*3/5,lum*3/5,lum*2/3),rgb(lum,lum,lum+8),.5);
        std::vector<POINT> hi{v[0],v[1],{(v[1].x+v[2].x)/2,(v[1].y+v[2].y)/2}};poly(hi,rgb(lum,lum,lum+6),CLR_INVALID,0);};
    if(shard_art) {put(shard_art,588,6,624,46,.8,true);put(shard_art,10,506,40,540,.6,true);}
    else {shard({{590,10},{616,22},{598,38}},170);shard({{500,30},{516,26},{510,44}},120);
        shard({{14,516},{40,508},{34,534}},150);shard({{58,540},{70,534},{66,552}},105);shard({{600,470},{628,476},{610,498}},90);}
    // Brush band with jagged ends for the selected row, cyan glow underneath.
    const auto hash=[](unsigned a,unsigned b) {unsigned v=a*2654435761u^b*40503u;v^=v>>13;v*=1274126177u;return (v>>8&255)/255.;};
    const auto band=[&](int x0,int y0,int x1,int y1,unsigned seed) {
        std::vector<POINT> v;
        const auto jit=[&](unsigned k,double amp) {return static_cast<int>((hash(seed,k)-.5)*2*amp*S);};
        v.push_back({(x0-12)*S,(y0+y1)/2*S});
        for(int x=x0,k=1;x<=x1;x+=12,++k) v.push_back({x*S,y0*S+jit(static_cast<unsigned>(k),2.2)});
        v.push_back({(x1+12)*S,(y0+y1)/2*S});
        for(int x=x1,k=100;x>=x0;x-=12,--k) v.push_back({x*S,y1*S+jit(static_cast<unsigned>(k),2.2)});
        for(auto [gw,c]:{std::pair<int,COLORREF>{9,rgb(24,58,78)},{6,rgb(40,108,136)},{3,rgb(120,208,230)}}) poly(v,CLR_INVALID,c,gw/3.);
        poly(v,rgb(212,216,215),rgb(236,240,240),.6);
        auto rgn=CreatePolygonRgn(v.data(),static_cast<int>(v.size()),WINDING);SelectClipRgn(dc,rgn);
        for(unsigned k=0;k<260;++k) {   // paper mottling
            const int sx=static_cast<int>(hash(seed+7,k)*(x1-x0+24))+x0-12,sy=static_cast<int>(hash(seed+9,k)*(y1-y0))+y0;
            const int len=static_cast<int>(2+hash(seed+11,k)*10),shade=static_cast<int>(190+hash(seed+13,k)*18);
            fill(sx,sy,sx+len,sy+1+(k%3==0),rgb(shade,shade+3,shade+3));
        }
        SelectClipRgn(dc,nullptr);DeleteObject(rgn);
    };
    // Rows: name, value, slider fraction (-1 none), description.
    struct Row {std::wstring name,value;double slider;std::wstring help;};
    const auto split=[&](const std::wstring& all,double slider,const wchar_t* help) {
        const auto at=all.find(L": ");return Row{all.substr(0,at),at==std::wstring::npos?L"":all.substr(at+2),slider,help};};
    std::wostringstream scale;scale<<std::fixed<<std::setprecision(1)<<settings.requested()<<L"x";
    std::wostringstream size;size<<width<<L" x "<<height<<L" per eye";
    if(effective+.01<settings.requested()) size<<L" ("<<std::fixed<<std::setprecision(2)<<effective<<L"x limit)";
    const auto turning=settings.turning();std::wostringstream turn;
    if(turning.mode==TurnControl::Mode::snap) turn<<L"Snap "<<turning.snap_degrees<<L" degrees";
    else if(turning.mode==TurnControl::Mode::smooth) turn<<L"Smooth "<<turning.smooth_degrees_per_second<<L" deg/s";
    else turn<<L"Native game";
    const auto hud_fraction=(settings.hud_percent()-55)/45.;
    const std::array<Row,settings_row_count> rows{{
        {L"Resolution",scale.str(),(settings.requested()-.5)/1.5,L"Sets how many pixels each eye renders. Higher is sharper but slower; the mod caps it to what the GPU can hold. Now "+size.str()+L"."},
        split(settings.temporal_aa_text(),-1,settings.dlss_available()?
            L"Smooths shimmering edges over time. Game TAA is the original; FSR Native and DLAA use full resolution; Quality and Balanced render fewer pixels and upscale. DLAA and DLSS Quality are powered by NVIDIA DLSS. The label shows the active backend.":
            L"Smooths shimmering edges over time. Game TAA is the original; FSR Native uses full resolution; Quality and Balanced render fewer pixels and upscale. The label shows the active backend."),
        {L"Edge anti-aliasing",settings.antialiasing()?L"SMAA Ultra":L"Native only",-1,L"SMAA Ultra cleans up jagged edges that the temporal pass misses. Native only keeps the game's own edge handling."},
        split(settings.mirror_text(),-1,L"Chooses what the desktop monitor mirrors: both eyes side by side, one eye alone, or off (the fastest). It does not change the headset image."),
        split(settings.sharpness_text(),settings.sharpness(),L"Contrast sharpening applied after anti-aliasing. Raise it if the image looks soft, lower it if edges crawl."),
        split(settings.hud_text(),hud_fraction,L"Scales the HUD and on-screen prompts shown in VR. 55% sits in the centre of your view; 100% keeps the original game layout."),
        split(settings.controls_hint_text(),-1,L"Shows controller button hints in the world during play. Turn it off once you know the controls."),
        split(settings.crosshair_text(),-1,L"Shows the game's crosshair where your gun or power aims. Off hides it."),
        {L"Turning",turn.str(),-1,L"Native game uses the original stick turn. Snap rotates in fixed steps and is comfortable; Smooth turns continuously at a set speed."},
        {L"Calibrate avatar",L"",-1,L"Measure shoulders, then arm reach. Enter chest width separately in Measurements. Recenter re-aligns the view to where you face now."},
        {L"Back to game menu",L"",-1,L"Test unlock (left): 100 runes for every power, infinite coins and every Black Market item. Back (right) closes this menu."}}};
    const auto strip=[](std::wstring v) {const auto at=v.find(L" (");return at==std::wstring::npos?v:v.substr(0,at);};
    const int selected=static_cast<int>(settings.selected());
    const auto footer=[&](bool change) {   // framed key boxes, bottom right: "B Back", "A / Trigger Change"
        int right=624;
        const auto key=[&](const wchar_t* k,const wchar_t* label,int kw,int lw) {
            const int lx=right-lw,kx=lx-kw-6;
            fill(kx-3,508,right+4,538,rgb(14,15,22));
            fill(kx,512,kx+kw,534,rgb(46,48,60));fill(kx+1,513,kx+kw-1,533,rgb(26,27,36));
            text(k,kx,512,kx+kw,534,13,white);text(label,lx,510,right,536,17,white,1);right=kx-18;};
        key(L"B",L"Back",20,40);if(change) key(L"A / Trigger",L"Change",84,56);
    };
    if(settings.calibration_page() && settings.measurements_page()) {
        text(L"Player measurements",24,6,616,58,35,rgb(150,154,170),1);
        text(L"Enter measured values in cm; leave captures blank",8,62,632,94,17,white);
        for(unsigned row=0;row<5;++row) {
            const int top=110+static_cast<int>(row)*56;const bool measurement_selected=settings.measurement_selected()==row;
            if(measurement_selected)fill(18,top,622,top+48,cyan);
            text(settings.measurement_label(row),28,top+4,430,top+44,19,measurement_selected?rgb(10,20,30):white,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            const auto value=settings.measurement_value(row);
            const auto label=value?std::to_wstring(static_cast<int>(std::lround(*value*100)))+L" cm":
                row==2?std::wstring(L"Not measured"):row==0?std::wstring(L"Runtime floor"):std::wstring(L"Auto capture");
            text(label,430,top+4,604,top+44,20,measurement_selected?rgb(10,20,30):cyan,0,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        }
        text(L"Stick: select / change 1 cm     X: clear selected value",8,394,632,426,17,muted);
        text(L"A: start guided capture",8,438,632,480,24,cyan);
        text(L"Chest width is side to side, not circumference",8,484,632,514,17,muted);
        text(L"B or Y: return",8,524,632,558,18,white);
    } else if(settings.calibration_page() && settings.hand_fit_page()) {
        text(L"Hand fit",24,6,616,58,35,rgb(150,154,170),1);
        text(L"Hand fit: hold the controllers naturally. Left stick moves the hand, right stick tilts it. Trigger confirms that hand. B returns.",20,62,620,150,18,white,0,DT_LEFT|DT_WORDBREAK);
        for(unsigned side=0;side<2;++side) {
            const int top=170+static_cast<int>(side)*96;const bool active=settings.hand_fit_side()==side;
            if(active) fill(18,top,622,top+88,cyan);
            const COLORREF ink2=active?rgb(10,20,30):white,value_ink=active?rgb(10,20,30):cyan;
            text(side?L"Right hand":L"Left hand",28,top+4,300,top+44,22,ink2,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            text(active?L"stick moves this hand":L"A selects this hand",300,top+4,612,top+44,15,ink2,0,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
            if(const auto v=settings.palm_preview(side)) {
                std::wostringstream o;o<<std::fixed<<std::setprecision(1)<<L"forward "<<v->translation.x*100<<L" cm   left "<<v->translation.y*100<<L" cm   up "<<v->translation.z*100<<L" cm";
                text(o.str(),28,top+44,612,top+64,16,value_ink,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
                std::wostringstream t;t<<std::fixed<<std::setprecision(1)<<L"pitch "<<v->pitch_deg<<L" deg   yaw "<<v->yaw_deg<<L" deg";
                text(t.str(),28,top+64,612,top+84,16,value_ink,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            }
        }
        text(L"Empty hands use the tilt; a hand holding a weapon keeps the position only.",8,372,632,402,16,muted);
        text(L"A: switch hand   Trigger: confirm   B: return",8,518,632,548,17,white,0,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    } else if(settings.calibration_page()) {
        text(settings.calibration_stage()==AvatarCalibrationStage::shoulders?L"1. Measure shoulders":L"2. Measure arm reach",24,6,616,58,35,rgb(150,154,170),1);
        text(settings.calibration_stage()==AvatarCalibrationStage::shoulders?L"Grip centres at shoulder joints; hold both grips and keep still":L"Release grips, then hold both with arms straight out and level",4,62,636,92,16,white);
        // The avatar shows the exact pose to take, rendered on the player's own body (tools/render_calibration_poses.py); then the
        // character's painted portrait; the drawn figure only without extracted art (user: "avatar should demonstrate the pose").
        const std::string who=game_character.load()==2?"emily":"corvo";
        const bool shoulder_pose=settings.calibration_stage()==AvatarCalibrationStage::shoulders;
        if(const auto pose=art.get(std::string(shoulder_pose?"pose_shoulders_":"pose_tpose_")+who)) put(pose,shoulder_pose?200:60,94,shoulder_pose?440:580,338,1,true);
        else if(const auto portrait=art.get("character_"+who)) put(portrait,170,96,470,336,1,true);
        else {   // Procedural Corvo figure, drawn directly into the 3x surface (then downsampled with the panel).
            constexpr int fy=94;SetViewportOrgEx(dc,0,fy*S,nullptr);
            constexpr COLORREF bone=RGB(226,219,198),coat=RGB(46,58,70),glove=RGB(30,38,46),grip=RGB(93,211,233);
            const auto gpen=[&](int width_px,COLORREF c) {LOGBRUSH lb{BS_SOLID,c,0};return ExtCreatePen(PS_GEOMETRIC|PS_SOLID|PS_ENDCAP_ROUND|PS_JOIN_ROUND,width_px*S,&lb,0,nullptr);};
            const auto draw=[&](auto&& shape,COLORREF edge,COLORREF in,int width_px) {
                auto p=gpen(width_px,edge);auto b=CreateSolidBrush(in);auto op=SelectObject(dc,p);auto ob=SelectObject(dc,b);
                shape();SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(p);DeleteObject(b);
            };
            const auto gpoly=[&](std::initializer_list<POINT> pts,COLORREF in,int width_px=2,COLORREF edge=RGB(226,219,198)) {
                std::vector<POINT> v(pts);for(auto& q:v) {q.x*=S;q.y*=S;}
                draw([&] {Polygon(dc,v.data(),static_cast<int>(v.size()));},edge,in,width_px);};
            const auto ell=[&](int x0,int y0,int x1,int y1,COLORREF in,int width_px=2,COLORREF edge=RGB(226,219,198)) {
                draw([&] {Ellipse(dc,x0*S,y0*S,x1*S,y1*S);},edge,in,width_px);};
            const auto seg=[&](int x0,int y0,int x1,int y1,int width_px,COLORREF c) {
                auto p=gpen(width_px,c);auto op=SelectObject(dc,p);MoveToEx(dc,x0*S,y0*S,nullptr);LineTo(dc,x1*S,y1*S);SelectObject(dc,op);DeleteObject(p);};
            constexpr int cx=320,sh=74;   // centre line, shoulder height
            for(int side:{-1,1}) {        // legs/boots first, coat flares over them
                seg(cx+side*16,170,cx+side*20,222,20,bone);seg(cx+side*16,170,cx+side*20,222,16,glove);
                gpoly({{cx+side*8,218},{cx+side*34,218},{cx+side*44,236},{cx+side*6,236}},glove);
            }
            for(int side:{-1,1}) {        // arms straight out: bone edge then coat sleeve
                if(settings.calibration_stage()==AvatarCalibrationStage::shoulders) {
                    seg(cx+side*36,sh,cx+side*72,sh+64,24,bone);seg(cx+side*36,sh,cx+side*72,sh+64,20,coat);
                    seg(cx+side*72,sh+64,cx+side*36,sh,24,bone);seg(cx+side*72,sh+64,cx+side*36,sh,20,coat);
                } else {
                    seg(cx+side*36,sh,cx+side*196,sh,24,bone);seg(cx+side*36,sh,cx+side*196,sh,20,coat);
                    seg(cx+side*196,sh,cx+side*206,sh,26,bone);seg(cx+side*196,sh,cx+side*206,sh,22,glove);
                }
            }
            gpoly({{cx-38,64},{cx-14,58},{cx+14,58},{cx+38,64},{cx+30,150},{cx+72,212},{cx+66,222},{cx,214},{cx-66,222},{cx-72,212},{cx-30,150}},coat);
            seg(cx,96,cx,214,2,bone);seg(cx-30,150,cx+30,150,5,bone);
            for(int y:{104,122}) {ell(cx-9,y,cx-4,y+5,bone,1);ell(cx+4,y,cx+9,y+5,bone,1);}
            gpoly({{cx-26,38},{cx-12,70},{cx,60},{cx+12,70},{cx+26,38},{cx+14,42},{cx-14,42}},RGB(70,84,98));  // high collar
            ell(cx-17,6,cx+17,44,glove);ell(cx-11,16,cx+11,44,bone,1);    // hood, face
            gpoly({{cx-11,26},{cx+11,26},{cx+9,34},{cx-9,34}},glove,1);   // mask band
            seg(cx-6,29,cx-3,29,2,bone);seg(cx+3,29,cx+6,29,2,bone);
            for(int side:{-1,1}) {        // controllers held in fists, grip highlighted, squeeze marks
                const int x=cx+side*(settings.calibration_stage()==AvatarCalibrationStage::shoulders?36:204);
                gpoly({{x-6,sh-30},{x+6,sh-30},{x+7,sh+34},{x-7,sh+34}},RGB(120,134,146),1);
                gpoly({{x-6,sh+4},{x+6,sh+4},{x+7,sh+34},{x-7,sh+34}},grip,1,grip);
                ell(x-12,sh-12,x+12,sh+12,glove,3);
                for(int k=0;k<3;k++) seg(x+side*(20+k*7),sh-12+k*2,x+side*(20+k*7),sh+12-k*2,2,grip);
            }
            SetViewportOrgEx(dc,0,0,nullptr);
        }
        fill(46,340,594,360,rgb(12,13,18));fill(47,341,593,359,rgb(70,74,92));
        fill(48,342,592,358,rgb(38,48,60));
        const auto progress=settings.calibration_progress();
        if(progress>0 && glow) put(glow,46,338,46+548*progress,362);   // the game's glowing bar
        else if(progress>0) fill(48,342,48+static_cast<int>(544*progress),358,cyan);
        text(settings.calibration_status_text(),8,366,632,406,22,cyan);
        text(std::to_wstring(static_cast<int>(progress*100))+L"% stable hold (1 second)",8,410,632,442,19,cyan);
        text(settings.player_measurements().torso_width_metres?L"Chest width uses your entered measurement":L"Y: enter chest width and optional measured dimensions",8,450,632,478,17,muted);
        text(settings.calibration_height_text(),8,480,632,508,16,muted);
        text(L"X: hand fit   Y: measurements   B: VR settings",8,518,500,548,17,white,0,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
        footer(false);
    } else {
        text(L"VR Settings",24,2,400,58,44,rgb(140,144,162),1,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        if(divider) put(divider,20,50,420,62,.9);
        for(int i=0;i<settings_row_count;++i) {
            const int top=settings_row_top+settings_row_pitch*i,bot=top+settings_row_height;const auto& r=rows[static_cast<std::size_t>(i)];const bool sel=i==selected;
            if(sel && brush) {GdiFlush();blit_art_band(px,W,H,*brush,36,12.*S,(top-3.)*S,628.*S,(bot+3.)*S);}
            else if(sel) band(18,top-2,622,bot+2,static_cast<unsigned>(i)+3);
            else {fill(20,top,620,bot,rgb(21,22,31));fill(20,top,620,top+1,rgb(44,46,58));fill(20,bot-1,620,bot,rgb(15,16,23));}
            const COLORREF label=sel?ink:rgb(206,210,220);
            if(i==9) {   // two actions share the row: calibrate (left) and recenter (right)
                const bool fit_notice=settings.calibration_required() || settings.calibration_runtime_notice();
                text(L"Calibrate avatar",24,top,316,fit_notice?top+22:bot,17,label);
                if(fit_notice) {
                    const auto reason=settings.calibration_state()==AvatarCalibrationState::rig_changed?L"Avatar changed - fit required":
                        settings.calibration_state()==AvatarCalibrationState::reference_changed?L"Recentered - fit required":L"Capture cancelled - retry";
                    text(settings.calibration_runtime_notice()?settings.calibration_runtime_text():reason,24,top+21,316,bot,11,sel?rgb(10,92,116):cyan);
                }
                line(320,top+5,320,bot-5,.8,sel?rgb(120,124,130):rgb(56,58,72));
                text(L"Recenter view",324,top,616,bot,17,label);
                continue;
            }
            if(i==10) {   // test unlock (left) and close (right)
                text(L"Test: unlock all",24,top,316,bot,17,label);
                line(320,top+5,320,bot-5,.8,sel?rgb(120,124,130):rgb(56,58,72));
                text(r.name,324,top,616,bot,17,label);continue;
            }
            text(r.name,34,top,262,bot,17,label,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
            const COLORREF arrow=sel?rgb(246,248,248):rgb(176,180,192),edge=sel?rgb(50,52,64):rgb(70,72,86);
            const int cy=top+15;
            const COLORREF value_colour=sel && frame_art?ink:white; // the game's frame is open: dark ink on the brush stroke
            if(arrow_l && arrow_r) {const std::uint32_t t=sel?0x181a24u:0xffffffu; // dark ink on the brush stroke
                put(arrow_l,270,cy-10,284,cy+10,1,true,t);put(arrow_r,602,cy-10,616,cy+10,1,true,t);}
            else {tri(272,cy,284,cy-8,284,cy+8,arrow,edge);tri(614,cy,602,cy-8,602,cy+8,arrow,edge);}
            const int vl=290,vr=596;
            const auto box=[&](int bl,int br) {
                if(frame_art) {GdiFlush();blit_art_band(px,W,H,*frame_art,24,(bl+.0)*S,(top+2.)*S,(br+.0)*S,(bot-2.)*S);return;} // the game's value frame
                poly({{(bl+8)*S,(top+3)*S},{(br-8)*S,(top+3)*S},{br*S,cy*S},{(br-8)*S,(bot-3)*S},{(bl+8)*S,(bot-3)*S},{bl*S,cy*S}},rgb(26,22,38),rgb(86,80,108),.7);};
            if(r.slider>=0 && track && knob) { // the game's slider track and knob
                const int tl=vl+6,tr=458,fx=tl+static_cast<int>((tr-tl)*std::clamp(r.slider,0.,1.));
                put(track,tl,cy-4,tr,cy+4);put(knob,fx-11,cy-9,fx+11,cy+9,1,true);
                box(466,vr);text(strip(r.value),474,top+3,vr-8,bot-3,14,value_colour);
            } else if(r.slider>=0) {   // track, filled part, knob, value box
                const int tl=vl+6,tr=458,fx=tl+static_cast<int>((tr-tl)*std::clamp(r.slider,0.,1.));
                fill(tl,cy-2,tr,cy+2,rgb(48,42,66));
                for(int x=fx;x<tr;x+=4) fill(x,cy-1,x+2,cy+1,rgb(120,104,150));
                fill(tl,cy-2,fx,cy+2,rgb(214,218,222));
                {auto p=pen(S,edge);auto b=CreateSolidBrush(rgb(224,228,230));auto op=SelectObject(dc,p);auto ob=SelectObject(dc,b);
                 Ellipse(dc,(fx-8)*S,(cy-8)*S,(fx+8)*S,(cy+8)*S);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(p);DeleteObject(b);}
                {auto p=pen(S/2,rgb(120,124,136));auto b=static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));auto op=SelectObject(dc,p);auto ob=SelectObject(dc,b);
                 Ellipse(dc,(fx-4)*S,(cy-4)*S,(fx+4)*S,(cy+4)*S);SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(p);}
                box(466,vr);text(strip(r.value),474,top+3,vr-8,bot-3,14,value_colour);
            } else {box(vl,vr);text(r.value,vl+10,top+3,vr-10,bot-3,14,value_colour);}
        }
        // Description plate for the selected row, titled with the setting's name.
        fill(20,428,620,500,rgb(17,18,26));fill(20,428,620,429,rgb(52,54,68));fill(20,499,620,500,rgb(10,11,16));
        const auto& cur=rows[static_cast<std::size_t>(std::min(selected,settings_row_count-1))];
        text(cur.name,24,430,616,458,22,rgb(220,224,232),1);
        if(divider) put(divider,170,452,470,462,.9);else line(180,456,460,456,.7,rgb(70,74,92));
        text(cur.help,36,459,604,498,13,rgb(176,181,196),0,DT_LEFT|DT_TOP|DT_WORDBREAK);
        text(L"In play: hold Menu for Journal; Menu + right stick for shortcuts",20,540,420,558,12,muted,0,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
        footer(true);
    }
    if(pointer) {
        const int x=static_cast<int>(pointer->u*W),y=static_cast<int>(pointer->v*H);
        auto p=CreatePen(PS_SOLID,3*S,cyan);auto old_pen=SelectObject(dc,p);
        auto old_brush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));Ellipse(dc,x-8*S,y-8*S,x+8*S,y+8*S);
        SelectObject(dc,old_brush);SelectObject(dc,old_pen);DeleteObject(p);
    }
    GdiFlush();const auto raw=static_cast<const unsigned char*>(image.bits);
    std::vector<unsigned char> pixels(static_cast<std::size_t>(w)*h*4);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {   // 3x3 box downsample, BGRA to RGBA
        unsigned sum[3]{};
        for(int j=0;j<S;++j) for(int i=0;i<S;++i) {const auto* q=raw+((static_cast<std::size_t>(y)*S+j)*W+x*S+i)*4;sum[0]+=q[2];sum[1]+=q[1];sum[2]+=q[0];}
        auto* o=&pixels[(static_cast<std::size_t>(y)*w+x)*4];o[0]=static_cast<unsigned char>(sum[0]/9);o[1]=static_cast<unsigned char>(sum[1]/9);o[2]=static_cast<unsigned char>(sum[2]/9);o[3]=255;
    }
    return pixels;
}
} // namespace d2vr
