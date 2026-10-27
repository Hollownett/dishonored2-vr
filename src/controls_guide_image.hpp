#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2vr {
// The VR controls and gestures page (user, after v646: "a page with VR control and gestures ... in launcher, VR settings and when
// user grabs right temple"). One source: docs/VR-CONTROLS.md (installed by the launcher as <mod home>/VR-CONTROLS.md; the
// launcher's Controls page and the README show the same file). Format: "# title", one intro line, "## section", bullets
// "- **input** — what it does". Drawn once into a dark card, three columns, laid out at 1920x1200 and scaled to the bitmap.
inline std::string& controls_guide_markdown() {static std::string text;return text;}
inline std::wstring widen_utf8(const std::string& s) {
    if(s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring w(static_cast<std::size_t>(std::max(n,0)),L'\0');
    if(n>0) MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);
    return w;
}
struct GuideItem {enum Kind {title,intro,section,bullet} kind;std::wstring key,text;};
inline std::vector<GuideItem> parse_controls_guide(const std::string& markdown) {
    std::vector<GuideItem> out;std::size_t at=0;
    while(at<=markdown.size()) {
        auto end=markdown.find('\n',at);if(end==std::string::npos) end=markdown.size();
        std::string line=markdown.substr(at,end-at);at=end+1;
        while(!line.empty() && (line.back()=='\r' || line.back()==' ')) line.pop_back();
        if(line.empty()) continue;
        if(line.rfind("## ",0)==0) out.push_back({GuideItem::section,{},widen_utf8(line.substr(3))});
        else if(line.rfind("# ",0)==0) out.push_back({GuideItem::title,{},widen_utf8(line.substr(2))});
        else if(line.rfind("- **",0)==0) {
            const auto close=line.find("**",4);if(close==std::string::npos) {out.push_back({GuideItem::bullet,{},widen_utf8(line.substr(2))});continue;}
            auto rest=line.substr(close+2);
            for(const char* sep:{" \xE2\x80\x94 "," - ",": "}) if(rest.rfind(sep,0)==0) {rest=rest.substr(std::string(sep).size());break;}
            out.push_back({GuideItem::bullet,widen_utf8(line.substr(4,close-4)),widen_utf8(rest)});
        } else out.push_back({GuideItem::intro,{},widen_utf8(line)});
    }
    return out;
}
struct GuideLayout {int bullets{},drawn{};bool overflow{};};
inline std::vector<std::uint8_t> controls_guide_image(const std::string& markdown,unsigned width=1920,unsigned height=1200,GuideLayout* layout=nullptr) {
    constexpr int w=1920,h=1200,margin=48,gap=40,columns=3;
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
    const double sx=static_cast<double>(cw)/w,sy=static_cast<double>(ch)/h;
    const auto px=[&](double v) {return static_cast<int>(std::lround(v*sx));};
    const auto py=[&](double v) {return static_cast<int>(std::lround(v*sy));};
    const auto white=RGB(236,241,245),muted=RGB(170,189,201),cyan=RGB(93,211,233),gold=RGB(222,184,108),card=RGB(17,24,32);
    RECT all{0,0,cw,ch};auto background=CreateSolidBrush(RGB(0,0,0));FillRect(image.dc,&all,background);DeleteObject(background);
    {   // Only the dark card interior is opaque; the untouched surround stays clear.
        auto pen=CreatePen(PS_SOLID,1,card);auto old_pen=SelectObject(image.dc,pen);auto brush=CreateSolidBrush(card);auto old_brush=SelectObject(image.dc,brush);
        RoundRect(image.dc,px(8),py(8),px(w-8),py(h-8),px(40),py(40));
        SelectObject(image.dc,old_brush);SelectObject(image.dc,old_pen);DeleteObject(brush);DeleteObject(pen);
    }
    struct Font {HFONT f{};explicit Font(int size,int weight) {f=CreateFontW(-size,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");} ~Font() {if(f) DeleteObject(f);}};
    const Font title_font(py(44),FW_SEMIBOLD),section_font(py(28),FW_SEMIBOLD),key_font(py(22),FW_SEMIBOLD),body_font(py(22),FW_NORMAL),intro_font(py(22),FW_NORMAL);
    SetBkMode(image.dc,TRANSPARENT);
    const int line=py(29),indent=px(18);
    // Word-wraps runs of (text, font, colour) from x across `span` pixels; draw=false only measures. Returns the height used.
    struct Run {const std::wstring* text;HFONT font;COLORREF colour;};
    const auto flow=[&](const std::vector<Run>& runs,int x,int y,int span,bool draw) {
        int cx=x,cy=y;bool first_word=true;
        for(const auto& run:runs) {
            SelectObject(image.dc,run.font);SetTextColor(image.dc,run.colour);
            SIZE space{};GetTextExtentPoint32W(image.dc,L" ",1,&space);
            std::size_t i=0;const auto& t=*run.text;
            while(i<t.size()) {
                while(i<t.size() && t[i]==L' ') ++i;
                auto j=t.find(L' ',i);if(j==std::wstring::npos) j=t.size();
                if(j==i) break;
                SIZE word{};GetTextExtentPoint32W(image.dc,t.c_str()+i,static_cast<int>(j-i),&word);
                if(!first_word && cx+word.cx>x+span) {cx=x+indent;cy+=line;}
                if(draw) TextOutW(image.dc,cx,cy,t.c_str()+i,static_cast<int>(j-i));
                cx+=word.cx+space.cx;first_word=false;i=j;
            }
        }
        return cy-y+line;
    };
    const auto items=parse_controls_guide(markdown);
    int top=py(36);
    for(const auto& item:items) if(item.kind==GuideItem::title) {
        SelectObject(image.dc,title_font.f);SetTextColor(image.dc,white);
        RECT r{px(margin),top,px(w-margin),top+py(60)};DrawTextW(image.dc,item.text.c_str(),-1,&r,DT_LEFT|DT_SINGLELINE|DT_VCENTER);top+=py(62);break;}
    for(const auto& item:items) if(item.kind==GuideItem::intro) {top+=flow({{&item.text,intro_font.f,muted}},px(margin),top,px(w-2*margin),true)+py(14);break;}
    const int col_w=(px(w-2*margin)-px(gap)*(columns-1))/columns,bottom=py(h-36);
    // A section's height, measured: header plus its bullets.
    const auto bullet_runs=[&](const GuideItem& b) {
        static const std::wstring dash=L"\x2014";
        std::vector<Run> runs;if(!b.key.empty()) {runs.push_back({&b.key,key_font.f,cyan});runs.push_back({&dash,body_font.f,muted});}
        runs.push_back({&b.text,body_font.f,white});return runs;};
    std::vector<std::pair<std::size_t,std::size_t>> sections; // [first item, end)
    for(std::size_t i=0;i<items.size();++i) if(items[i].kind==GuideItem::section) {
        std::size_t e=i+1;while(e<items.size() && items[e].kind==GuideItem::bullet) ++e;sections.push_back({i,e});}
    GuideLayout result{};for(const auto& it:items) if(it.kind==GuideItem::bullet) ++result.bullets;
    int column=0,y=top;
    const auto column_x=[&](int c) {return px(margin)+c*(col_w+px(gap));};
    for(const auto& [first,end]:sections) {
        const int need=line+py(10)+(first+1<end?flow(bullet_runs(items[first+1]),column_x(column),0,col_w,false):0);
        if(y>top && y+need>bottom && column+1<columns) {++column;y=top;}  // a section starts where its header and first bullet fit, then flows on
        SelectObject(image.dc,section_font.f);SetTextColor(image.dc,gold);
        TextOutW(image.dc,column_x(column),y,items[first].text.c_str(),static_cast<int>(items[first].text.size()));y+=line+py(10);
        for(std::size_t k=first+1;k<end;++k) {
            const auto runs=bullet_runs(items[k]);const int used=flow(runs,column_x(column),y,col_w,false);
            if(y+used>bottom) {if(column+1<columns) {++column;y=top;} else {result.overflow=true;continue;}}
            flow(runs,column_x(column),y,col_w,true);y+=used+py(6);++result.drawn;
        }
        y+=py(14);
    }
    if(layout) *layout=result;
    GdiFlush();const auto raw=static_cast<const unsigned char*>(image.bits);
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(cw)*ch*4);
    for(std::size_t i=0;i<pixels.size();i+=4) {
        pixels[i]=raw[i+2];pixels[i+1]=raw[i+1];pixels[i+2]=raw[i];
        pixels[i+3]=(raw[i]|raw[i+1]|raw[i+2])?255:0;
    }
    return pixels;
}
} // namespace d2vr
