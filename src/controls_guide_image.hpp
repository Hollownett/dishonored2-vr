#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>
#include "game_art.hpp"

namespace d2vr {
// User request: seven illustrated pages share the launcher's Markdown, with fixed readable type and safe art keys.
inline std::string& controls_guide_markdown() {static std::string text;return text;}
inline std::wstring widen_utf8(const std::string& s) {
    if(s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring w(static_cast<std::size_t>(std::max(n,0)),L'\0');
    if(n>0) MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;
}
inline constexpr std::array<const char*,7> guide_page_ids{{"signature","sword","moving","hands","bodies-weapons","wheel-journal","menus-settings"}};
inline constexpr std::array<const wchar_t*,7> guide_page_names{{L"Throw & grab",L"Sword & stealth",L"Moving",L"Hands",L"Bodies & weapons",L"Wheel & journal",L"Menus & settings"}};
inline constexpr std::array<const char*,11> guide_figure_ids{{"sword_throw","enemy_grab","sword_draw_block","stealth","move_jump_lean","hands_push","bodies_drag_carry","left_hip","wheel_watch","temples","menu_point"}};
struct GuideItem {enum Kind {title,intro,section,bullet,page,figure} kind;std::wstring key,text;unsigned page_index{};std::size_t bullet_index{};};
inline std::vector<GuideItem> parse_controls_guide(const std::string& markdown,unsigned* errors=nullptr) {
    std::vector<GuideItem> out;std::size_t at=0,bullet_index=0;unsigned current_page=0,invalid=0;bool in_comment=false,have_section=false;
    std::array<bool,7> seen_pages{};std::array<bool,11> seen_figures{};
    const auto trim=[](std::string s) {const auto first=s.find_first_not_of(" \t\r");if(first==std::string::npos) return std::string{};const auto last=s.find_last_not_of(" \t\r");return s.substr(first,last-first+1);};
    while(at<=markdown.size()) {
        auto end=markdown.find('\n',at);if(end==std::string::npos) end=markdown.size();std::string line=trim(markdown.substr(at,end-at));at=end+1;
        if(line.empty()) continue;
        if(in_comment) {if(line.find("-->")!=std::string::npos) in_comment=false;continue;}
        if(line.rfind("<!--",0)==0) {
            const bool pg=line.rfind("<!-- vr-page:",0)==0,fg=line.rfind("<!-- vr-figure:",0)==0;
            if(pg || fg) {
                const auto close=line.find("-->");const std::size_t start=pg?13u:15u;const auto bar=line.find('|',start);
                if(close==std::string::npos || close+3!=line.size() || bar==std::string::npos || bar>=close) {++invalid;continue;}
                const auto id=trim(line.substr(start,bar-start)),label=trim(line.substr(bar+1,close-bar-1));
                if(label.empty()) {++invalid;continue;}
                if(pg) {
                    const auto found=std::find(guide_page_ids.begin(),guide_page_ids.end(),id);
                    if(found==guide_page_ids.end()) {++invalid;continue;}current_page=static_cast<unsigned>(found-guide_page_ids.begin());
                    if(seen_pages[current_page]) ++invalid;seen_pages[current_page]=true;have_section=false;
                    out.push_back({GuideItem::page,widen_utf8(id),widen_utf8(label),current_page});
                } else {
                    const auto found=std::find(guide_figure_ids.begin(),guide_figure_ids.end(),id);
                    if(found==guide_figure_ids.end() || !have_section) {++invalid;continue;}
                    const auto n=static_cast<std::size_t>(found-guide_figure_ids.begin());if(seen_figures[n]) ++invalid;seen_figures[n]=true;
                    out.push_back({GuideItem::figure,widen_utf8(id),widen_utf8(label),current_page});
                }
            } else {if(line.rfind("<!-- vr-page",0)==0 || line.rfind("<!-- vr-figure",0)==0) ++invalid;in_comment=line.find("-->")==std::string::npos;}
            continue;
        }
        if(line.rfind("## ",0)==0) {out.push_back({GuideItem::section,{},widen_utf8(line.substr(3)),current_page});have_section=true;}
        else if(line.rfind("# ",0)==0) out.push_back({GuideItem::title,{},widen_utf8(line.substr(2)),current_page});
        else if(line.rfind("- ",0)==0) {
            GuideItem item{GuideItem::bullet,{},widen_utf8(line.substr(2)),current_page,bullet_index++};
            if(line.rfind("- **",0)==0) {const auto close=line.find("**",4);if(close!=std::string::npos) {
                auto rest=line.substr(close+2);for(const char* sep:{" \xE2\x80\x94 "," - ",": "}) if(rest.rfind(sep,0)==0) {rest=rest.substr(std::string(sep).size());break;}
                item.key=widen_utf8(line.substr(4,close-4));item.text=widen_utf8(rest);}}
            out.push_back(std::move(item));
        } else out.push_back({GuideItem::intro,{},widen_utf8(line),current_page});
    }
    if(errors) *errors=invalid;return out;
}
struct GuideLayout {int bullets{},drawn{};bool overflow{},out_of_bounds{};std::vector<std::size_t> drawn_bullet_indices;};
inline std::vector<std::uint8_t> controls_guide_image(const std::string& markdown,unsigned width=1920,unsigned height=1200,
    GuideLayout* layout=nullptr,unsigned page=0,bool emily=false,bool use_art=true) {
    constexpr int w=1920,h=1200;page=std::min(page,6u);if(!width || !height) throw std::runtime_error("Cannot allocate VR controls bitmap");
    struct Bitmap {HDC dc{};HBITMAP bitmap{};HGDIOBJ previous{};void* bits{};
        Bitmap() {dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(dc && bitmap) previous=SelectObject(dc,bitmap);}
        ~Bitmap() {if(previous) SelectObject(dc,previous);if(bitmap) DeleteObject(bitmap);if(dc) DeleteDC(dc);}} image;
    if(!image.dc || !image.bitmap) throw std::runtime_error("Cannot allocate VR controls bitmap");
    auto* pixels=static_cast<std::uint32_t*>(image.bits);std::fill_n(pixels,w*h,0u);const HDC dc=image.dc;
    const auto white=RGB(232,227,216),muted=RGB(198,192,180),cyan=RGB(124,202,218),gold=RGB(206,174,112),card=RGB(16,15,18);
    const auto fill=[&](int x,int y,int rw,int rh,COLORREF colour) {RECT r{x,y,x+rw,y+rh};auto brush=CreateSolidBrush(colour);FillRect(dc,&r,brush);DeleteObject(brush);};
    fill(16,16,1888,1168,card);
    const auto art=[&](const std::string& key) {return use_art?game_art().get(key):std::shared_ptr<const GameArt>{};};
    const auto crop=[&](const GameArt& a,int x,int y,int rw,int rh,double opacity) {
        const double aspect=double(rw)/rh;int aw=static_cast<int>(a.width),ah=static_cast<int>(a.height),sx=0,sy=0;
        if(double(aw)/ah>aspect) {const int n=static_cast<int>(ah*aspect);sx=(aw-n)/2;aw=n;} else {const int n=static_cast<int>(aw/aspect);sy=(ah-n)/2;ah=n;}
        GdiFlush();blit_art_part(pixels,w,h,a,sx,sy,sx+aw,sy+ah,x,y,x+rw,y+rh,opacity);};
    if(auto a=art("dark_page")) crop(*a,16,16,1888,1168,.24);
    fill(48,244,1824,824,card);if(auto a=art("menu_background")) crop(*a,48,244,1824,824,.18);
    // A further translucent dark backing keeps the material below the instructions.
    GdiFlush();for(int y=244;y<1068;++y) for(int x=48;x<1872;++x) {auto& p=pixels[y*w+x];p=((p>>16&255)*3/4+16/4)<<16|((p>>8&255)*3/4+15/4)<<8|((p&255)*3/4+18/4);}
    struct Font {HFONT f{};Font(int size,int weight,const wchar_t* face=L"Segoe UI") {
        const auto make=[&](const wchar_t* name) {return CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,name);};f=make(face);
        if(std::wcscmp(face,L"Palatino Linotype")==0) {auto probe=CreateCompatibleDC(nullptr);auto old=SelectObject(probe,f);wchar_t actual[LF_FACESIZE]{};GetTextFaceW(probe,LF_FACESIZE,actual);SelectObject(probe,old);DeleteDC(probe);
            if(_wcsicmp(actual,face)!=0) {DeleteObject(f);f=make(L"Georgia");}}}~Font() {if(f) DeleteObject(f);}};
    const Font title_font(56,FW_NORMAL,L"Palatino Linotype"),heading_font(38,FW_NORMAL),body_font(32,FW_NORMAL),key_font(32,FW_SEMIBOLD),selector_font(26,FW_NORMAL),nav_font(28,FW_NORMAL);
    SetBkMode(dc,TRANSPARENT);GuideLayout result{};
    const auto text=[&](const std::wstring& value,int x,int y,int rw,int rh,HFONT font,COLORREF colour,UINT align=DT_LEFT) {
        SelectObject(dc,font);SetTextColor(dc,colour);RECT r{x,y,x+rw,y+rh};RECT measured=r;DrawTextW(dc,value.c_str(),-1,&measured,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
        if(measured.right-measured.left>rw || measured.bottom-measured.top>rh) result.out_of_bounds=true;
        DrawTextW(dc,value.c_str(),-1,&r,align|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);}; // NOPREFIX: "Throw & grab" is not a menu accelerator
    struct Run {const std::wstring* text;HFONT font;COLORREF colour;};
    const auto flow=[&](const std::vector<Run>& runs,int x,int y,int rw,int bottom,bool draw) {
        int cx=x,cy=y;bool first=true;for(const auto& run:runs) {SelectObject(dc,run.font);SetTextColor(dc,run.colour);SIZE space{};GetTextExtentPoint32W(dc,L" ",1,&space);
            for(std::size_t i=0;i<run.text->size();) {while(i<run.text->size() && (*run.text)[i]==L' ') ++i;auto j=run.text->find(L' ',i);if(j==std::wstring::npos) j=run.text->size();if(i==j) break;
                SIZE word{};GetTextExtentPoint32W(dc,run.text->c_str()+i,static_cast<int>(j-i),&word);if(!first && cx+word.cx>x+rw) {cx=x;cy+=40;}
                if(draw) {if(word.cx>rw || cy+40>bottom) result.out_of_bounds=true;TextOutW(dc,cx,cy,run.text->c_str()+i,static_cast<int>(j-i));}cx+=word.cx+space.cx;first=false;i=j;}}
        return cy-y+40;};
    const auto items=parse_controls_guide(markdown);std::wstring page_name=guide_page_names[page];
    for(const auto& item:items) if(item.kind==GuideItem::page && item.page_index==page) page_name=item.text;
    for(const auto& item:items) if(item.kind==GuideItem::title) {text(item.text,64,36,1120,80,title_font.f,white);break;}
    const std::wstring label=page_name+L"   "+std::to_wstring(page+1)+L" / 7";text(label,1200,54,656,48,body_font.f,muted,DT_RIGHT);
    for(const auto& item:items) if(item.kind==GuideItem::intro) {const int used=flow({{&item.text,body_font.f,muted}},64,116,1792,200,true);if(used>84) result.overflow=true;break;}
    // The game's divider art is near-black on the dark page (it vanished in the v650 preview): a plain gold rule reads.
    const auto rule=[&](int y,int rh,COLORREF colour) {fill(64,y+rh/2,1792,2,colour);};rule(216,20,gold);rule(1080,10,RGB(120,102,70));
    for(unsigned i=0;i<7;++i) {const int x=64+static_cast<int>(i)*256;if(i==page) {if(auto a=art("highlight_brush")) {GdiFlush();blit_art_band(pixels,w,h,*a,36,x,1098,x+256,1142);}else fill(x,1098,256,44,white);}
        text(guide_page_names[i],x,1098,256,44,selector_font.f,i==page?card:muted,DT_CENTER);}
    text(L"Right stick \x2190 / \x2192: pages \x00B7 Right temple + grip: close",64,1144,1792,40,nav_font.f,muted,DT_CENTER);
    struct Section {std::size_t first,end;const GuideItem* figure{};};std::vector<Section> sections;
    for(std::size_t i=0;i<items.size();++i) if(items[i].kind==GuideItem::section && items[i].page_index==page) {std::size_t e=i+1;while(e<items.size() && items[e].kind!=GuideItem::section && items[e].kind!=GuideItem::page) ++e;
        Section s{i,e};for(auto k=i+1;k<e;++k) if(items[k].kind==GuideItem::figure) s.figure=&items[k];sections.push_back(s);}
    const bool two=page==0 || page==1 || page==4 || page==5;
    const auto figure=[&](const GuideItem* f,int x,int y,int rw,int rh) {
        if(!f) return;std::string id;for(wchar_t c:f->key) id.push_back(static_cast<char>(c));const auto a=art("guide_"+id+(emily?"_emily":"_corvo"));
        if(a && a->width==static_cast<unsigned>(rw) && a->height==static_cast<unsigned>(rh)) {GdiFlush();blit_art(pixels,w,h,*a,x,y,x+rw,y+rh,1,false);}
        else {fill(x,y,rw,rh,RGB(41,39,35));const int used=flow({{&f->text,body_font.f,white}},x+32,0,rw-64,10000,false);const int top=y+(rh-used)/2;
            if(top<y+16 || top+used>y+rh-16) result.overflow=true;flow({{&f->text,body_font.f,white}},x+32,top,rw-64,y+rh,true);}};
    int one_y=260;for(std::size_t n=0;n<sections.size();++n) {const auto& s=sections[n];const int x=two?(n==0?64:992):992;int y=two?260:one_y;
        if(two && n>=2) {result.overflow=true;continue;}text(items[s.first].text,x,y,864,56,heading_font.f,gold);
        if(two) {figure(s.figure,x,324,864,232);y=580;}else {if(n==0) figure(s.figure,64,260,864,804);y+=64;}
        for(auto k=s.first+1;k<s.end;++k) if(items[k].kind==GuideItem::bullet) {const auto& b=items[k];++result.bullets;static const std::wstring dash=L"\x2014";std::vector<Run> runs;
            if(!b.key.empty()) {runs.push_back({&b.key,key_font.f,cyan});runs.push_back({&dash,body_font.f,muted});}runs.push_back({&b.text,body_font.f,white});
            const int used=flow(runs,x,y,864,1064,false);if(y+used>1064) {result.overflow=true;continue;}flow(runs,x,y,864,1064,true);y+=used+8;++result.drawn;result.drawn_bullet_indices.push_back(b.bullet_index);}
        one_y=y+20;}
    if(layout) *layout=result;SelectObject(dc,GetStockObject(SYSTEM_FONT));GdiFlush();std::vector<std::uint8_t> out(static_cast<std::size_t>(width)*height*4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {const int sx=static_cast<int>(std::uint64_t(x)*w/width),sy=static_cast<int>(std::uint64_t(y)*h/height);const auto p=pixels[sy*w+sx];const auto i=(static_cast<std::size_t>(y)*width+x)*4;
        const int dx=sx<40?40-sx:(sx>=1880?sx-1879:0),dy=sy<40?40-sy:(sy>=1160?sy-1159:0);const bool inside=sx>=16 && sx<1904 && sy>=16 && sy<1184 && (!dx || !dy || dx*dx+dy*dy<=24*24);
        out[i]=static_cast<std::uint8_t>(p>>16);out[i+1]=static_cast<std::uint8_t>(p>>8);out[i+2]=static_cast<std::uint8_t>(p);out[i+3]=inside?255:0;}
    return out;
}
} // namespace d2vr
