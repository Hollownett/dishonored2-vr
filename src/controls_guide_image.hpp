#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include "game_art.hpp"
#include "guide_navigation.hpp"

namespace d2vr {
// User request: three journal pages (body map, controllers, tips) share the launcher's Markdown, with fixed readable type and safe art keys.
inline std::string& controls_guide_markdown() {static std::string text;return text;}
inline std::wstring widen_utf8(const std::string& s) {
    if(s.empty()) return {};
    const int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
    std::wstring w(static_cast<std::size_t>(std::max(n,0)),L'\0');
    if(n>0) MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),w.data(),n);return w;
}
inline constexpr std::array<const char*,guide_page_count> guide_page_ids{{"body","controllers","tips"}};
inline constexpr std::array<const wchar_t*,guide_page_count> guide_page_names{{L"Body",L"Controllers",L"Gestures"}};
inline constexpr std::array<const char*,2> guide_figure_ids{{"sword_draw_block","enemy_grab"}};
// Visible button centres in launcher/assets/touch-plus-{left,right}.png (340x600), from touch-plus.anchors.json, in the
// order of the Markdown bullets: left Y, X, Stick, Menu, Trigger, Grip; right B, A, Stick, Trigger, Grip.
inline constexpr std::array<std::array<int,2>,6> touch_plus_left_anchors{{{160,162},{100,201},{99,100},{54,182},{307,152},{155,366}}};
inline constexpr std::array<std::array<int,2>,5> touch_plus_right_anchors{{{180,162},{239,201},{241,100},{33,152},{185,366}}};
struct GuideItem {enum Kind {title,intro,section,bullet,page,figure} kind;std::wstring key,text;unsigned page_index{};std::size_t bullet_index{};};
inline std::vector<GuideItem> parse_controls_guide(const std::string& markdown,unsigned* errors=nullptr) {
    std::vector<GuideItem> out;std::size_t at=0,bullet_index=0;unsigned current_page=0,invalid=0;bool in_comment=false,have_section=false;
    std::array<bool,guide_page_count> seen_pages{};std::array<bool,2> seen_figures{};
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
    constexpr int w=1920,h=1200;page=std::min(page,unsigned(guide_page_count-1));if(!width || !height) throw std::runtime_error("Cannot allocate VR controls bitmap");
    struct Bitmap {HDC dc{};HBITMAP bitmap{};HGDIOBJ previous{};void* bits{};
        Bitmap() {dc=CreateCompatibleDC(nullptr);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
            info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);if(dc && bitmap) previous=SelectObject(dc,bitmap);}
        ~Bitmap() {if(previous) SelectObject(dc,previous);if(bitmap) DeleteObject(bitmap);if(dc) DeleteDC(dc);}} image;
    if(!image.dc || !image.bitmap) throw std::runtime_error("Cannot allocate VR controls bitmap");
    auto* pixels=static_cast<std::uint32_t*>(image.bits);std::fill_n(pixels,w*h,0u);const HDC dc=image.dc;
    // The game's own menu look: bone-white text on smoked black, brass ornaments and spaced serif capitals.
    const auto white=RGB(234,226,208),muted=RGB(184,175,158),brass=RGB(214,180,116),gold=RGB(206,174,112),card=RGB(16,15,18),dim_gold=RGB(120,102,70);
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
    const Font title_font(50,FW_NORMAL,L"Palatino Linotype"),heading_font(36,FW_NORMAL,L"Palatino Linotype"),body_font(32,FW_NORMAL),key_font(32,FW_SEMIBOLD),
        selector_font(26,FW_NORMAL,L"Palatino Linotype"),label_font(28,FW_NORMAL,L"Palatino Linotype"),nav_font(26,FW_NORMAL);
    SetBkMode(dc,TRANSPARENT);GuideLayout result{};
    const auto text=[&](const std::wstring& value,int x,int y,int rw,int rh,HFONT font,COLORREF colour,UINT align=DT_LEFT) {
        SelectObject(dc,font);SetTextColor(dc,colour);RECT r{x,y,x+rw,y+rh};RECT measured=r;DrawTextW(dc,value.c_str(),-1,&measured,DT_CALCRECT|DT_SINGLELINE|DT_NOPREFIX);
        if(measured.right-measured.left>rw || measured.bottom-measured.top>rh) result.out_of_bounds=true;
        DrawTextW(dc,value.c_str(),-1,&r,align|DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);}; // NOPREFIX: "Throw & grab" is not a menu accelerator
    // Menu headings: upper case with the letter spacing of the game's menu items.
    const auto caps=[&](std::wstring value,int x,int y,int rw,int rh,HFONT font,COLORREF colour,int spacing,UINT align=DT_LEFT) {
        for(auto& c:value) c=static_cast<wchar_t>(std::towupper(c));SetTextCharacterExtra(dc,spacing);text(value,x,y,rw,rh,font,colour,align);SetTextCharacterExtra(dc,0);};
    const auto diamond=[&](int cx,int cy,int r,COLORREF colour) {auto b=CreateSolidBrush(colour);auto ob=SelectObject(dc,b);auto np=SelectObject(dc,GetStockObject(NULL_PEN));
        const POINT p[4]{{cx,cy-r},{cx+r,cy},{cx,cy+r},{cx-r,cy}};Polygon(dc,p,4);SelectObject(dc,np);SelectObject(dc,ob);DeleteObject(b);};
    const auto heading=[&](const std::wstring& value,int x,int y) {diamond(x+7,y+28,7,brass);caps(value,x+26,y,838,56,heading_font.f,gold,3);};
    struct Run {const std::wstring* text;HFONT font;COLORREF colour;};
    const auto flow=[&](const std::vector<Run>& runs,int x,int y,int rw,int bottom,bool draw) {
        int cx=x,cy=y;bool first=true;for(const auto& run:runs) {SelectObject(dc,run.font);SetTextColor(dc,run.colour);SIZE space{};GetTextExtentPoint32W(dc,L" ",1,&space);
            for(std::size_t i=0;i<run.text->size();) {while(i<run.text->size() && (*run.text)[i]==L' ') ++i;auto j=run.text->find(L' ',i);if(j==std::wstring::npos) j=run.text->size();if(i==j) break;
                SIZE word{};GetTextExtentPoint32W(dc,run.text->c_str()+i,static_cast<int>(j-i),&word);if(!first && cx+word.cx>x+rw) {cx=x;cy+=40;}
                if(draw) {if(word.cx>rw || cy+40>bottom) result.out_of_bounds=true;TextOutW(dc,cx,cy,run.text->c_str()+i,static_cast<int>(j-i));}cx+=word.cx+space.cx;first=false;i=j;}}
        return cy-y+40;};
    const auto items=parse_controls_guide(markdown);std::wstring page_name=guide_page_names[page];
    for(const auto& item:items) if(item.kind==GuideItem::page && item.page_index==page) page_name=item.text;
    for(const auto& item:items) if(item.kind==GuideItem::title) {caps(item.text,64,36,1120,80,title_font.f,white,4);break;}
    const std::wstring label=page_name+L"   "+std::to_wstring(page+1)+L" / "+std::to_wstring(guide_page_count);caps(label,1200,54,656,48,label_font.f,muted,0,DT_RIGHT); // spaced capitals mis-measure under DT_RIGHT
    for(const auto& item:items) if(item.kind==GuideItem::intro) {const int used=flow({{&item.text,body_font.f,muted}},64,116,1792,200,true);if(used>84) result.overflow=true;break;}
    // The game's divider art is near-black on the dark page (it vanished in the v650 preview): a plain gold rule reads.
    const auto rule=[&](int y,int rh,COLORREF colour) {fill(64,y+rh/2,1792,2,colour);};rule(216,20,gold);rule(1080,10,dim_gold);
    for(int x:{64,1855}) diamond(x,227,6,gold); // the launcher's ornament: diamonds on the brass rule
    constexpr int cell=1792/guide_page_count;
    for(unsigned i=0;i<guide_page_count;++i) {const int x=64+static_cast<int>(i)*cell;if(i==page) {if(auto a=art("highlight_brush")) {GdiFlush();blit_art_band(pixels,w,h,*a,36,x,1098,x+cell,1142);}else fill(x,1098,cell,44,white);}
        caps(guide_page_names[i],x,1098,cell,44,selector_font.f,i==page?card:muted,3,DT_CENTER);}
    text(L"Right stick \x2190 / \x2192: pages   \x25C6   Right temple + grip: close",64,1144,1792,40,nav_font.f,muted,DT_CENTER);
    struct Section {std::size_t first,end;const GuideItem* figure{};};std::vector<Section> sections;
    for(std::size_t i=0;i<items.size();++i) if(items[i].kind==GuideItem::section && items[i].page_index==page) {std::size_t e=i+1;while(e<items.size() && items[e].kind!=GuideItem::section && items[e].kind!=GuideItem::page) ++e;
        Section s{i,e};for(auto k=i+1;k<e;++k) if(items[k].kind==GuideItem::figure) s.figure=&items[k];sections.push_back(s);}
    const bool two=page>=1;
    // Game-menu drawings (page 1 body map, page 2 controllers): vector shapes plus callouts. Each Markdown bullet is one callout.
    struct Call {const GuideItem* b;std::array<int,2> ay;std::array<int,2> ax;int n;int cy;int stop{};}; // stop: the leader ends this far short of its anchor (on a ring)
    const auto bullet_runs=[&](const GuideItem& b,std::vector<Run>& runs) {static const std::wstring dash=L"—";
        if(!b.key.empty()) {runs.push_back({&b.key,key_font.f,brass});runs.push_back({&dash,body_font.f,muted});}runs.push_back({&b.text,body_font.f,white});};
    // Stack callouts at x (width rw) as near their target y as they fit; a leader runs from the label edge (lx) to each anchor.
    // arrange: buttons packed close together (a controller) try every label order and keep the one whose leaders neither
    // cross each other nor run through another button's ring, nearest their buttons' heights (at most 6! orders per side).
    const auto callouts=[&](std::vector<Call>& calls,int x,int rw,int lx,int top,int bottom,bool arrange=false) {
        std::vector<int> hs;
        for(auto& c:calls) {std::vector<Run> runs;bullet_runs(*c.b,runs);hs.push_back(flow(runs,x,0,rw,100000,false));}
        const auto place=[&](const std::vector<std::size_t>& order,int& over) {std::vector<int> ys(order.size());int prev=top;
            for(std::size_t i=0;i<order.size();++i) {ys[i]=std::max(calls[order[i]].cy-hs[order[i]]/2,prev);prev=ys[i]+hs[order[i]]+14;}
            over=prev-14-bottom;for(std::size_t i=order.size();i-->0;) {if(over<=0) break;const int room=i? ys[i]-(ys[i-1]+hs[order[i-1]]+14):ys[i]-top;const int d=std::min(over,std::max(room,0));ys[i]-=d;over-=d;}
            return ys;};
        std::vector<std::size_t> order(calls.size());for(std::size_t i=0;i<order.size();++i) order[i]=i;
        if(arrange && calls.size()<=7) {
            using P=std::array<double,2>;
            const auto cross=[](P a,P b,P c) {return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);};
            const auto meet=[&](P a,P b,P c,P d) {return cross(a,b,c)*cross(a,b,d)<0 && cross(c,d,a)*cross(c,d,b)<0;};
            const auto near_ring=[](P a,P b,P p,double r) {const double vx=b[0]-a[0],vy=b[1]-a[1],t=std::clamp(((p[0]-a[0])*vx+(p[1]-a[1])*vy)/std::max(1.,vx*vx+vy*vy),0.,1.);
                return std::hypot(a[0]+t*vx-p[0],a[1]+t*vy-p[1])<r;};
            std::sort(order.begin(),order.end(),[&](std::size_t a,std::size_t b) {return calls[a].cy<calls[b].cy;});
            const auto base=order;auto best=order;double best_cost=1e300;
            std::vector<std::size_t> perm(order.size());for(std::size_t i=0;i<perm.size();++i) perm[i]=i;
            do {for(std::size_t i=0;i<perm.size();++i) order[i]=base[perm[i]]; // the first order tried (and kept on a tie) is button height order
                int over{};const auto ys=place(order,over);double cost=over>0?1e9:0;std::vector<std::array<P,2>> seg;
                for(std::size_t i=0;i<order.size();++i) {const auto& c=calls[order[i]];seg.push_back({P{double(lx),ys[i]+20.},P{double(c.ax[0]),double(c.ay[0])}});cost+=std::abs(ys[i]+20.-c.ay[0]);}
                for(std::size_t i=0;i<seg.size();++i) for(std::size_t j=0;j<seg.size();++j) if(i!=j) {
                    if(i<j && meet(seg[i][0],seg[i][1],seg[j][0],seg[j][1])) cost+=1e6;
                    if(near_ring(seg[i][0],seg[i][1],seg[j][1],calls[order[j]].stop+4.)) cost+=1e4;}
                if(cost<best_cost) {best_cost=cost;best=order;}
            } while(std::next_permutation(perm.begin(),perm.end()));
            order=best;
        }
        {std::vector<Call> sorted;for(auto i:order) sorted.push_back(calls[i]);std::vector<int> h2;for(auto i:order) h2.push_back(hs[i]);calls=sorted;hs=h2;}
        for(std::size_t i=0;i<order.size();++i) order[i]=i;
        int over{};const auto ys=place(order,over);
        if(over>0) result.overflow=true;
        for(std::size_t i=0;i<calls.size();++i) {const auto& c=calls[i];std::vector<Run> runs;bullet_runs(*c.b,runs);
            const int mid=ys[i]+20;
            auto pen=CreatePen(PS_SOLID,2,brass);auto old=SelectObject(dc,pen);
            std::array<std::array<int,2>,2> end{};
            for(int k=0;k<c.n;++k) {const double dx=lx-c.ax[k],dy=mid-c.ay[k],d=std::max(1.,std::hypot(dx,dy));
                end[k]={c.ax[k]+static_cast<int>(std::lround(dx*c.stop/d)),c.ay[k]+static_cast<int>(std::lround(dy*c.stop/d))};
                MoveToEx(dc,lx,mid,nullptr);LineTo(dc,end[k][0],end[k][1]);}
            SelectObject(dc,old);DeleteObject(pen);
            for(int k=0;k<c.n;++k) diamond(end[k][0],end[k][1],c.stop?5:7,brass);
            diamond(lx,mid,8,brass);
            flow(runs,x,ys[i],rw,bottom,true);++result.bullets;++result.drawn;result.drawn_bullet_indices.push_back(c.b->bullet_index);}};
    const auto shape=[&](COLORREF edge,COLORREF fill_colour,int edge_w) {return std::pair<HPEN,HBRUSH>{CreatePen(PS_SOLID,edge_w,edge),fill_colour==CLR_INVALID?static_cast<HBRUSH>(GetStockObject(NULL_BRUSH)):CreateSolidBrush(fill_colour)};};
    const auto draw_shape=[&](COLORREF edge,COLORREF fill_colour,int edge_w,const std::function<void()>& body) {
        auto [pen,brush]=shape(edge,fill_colour,edge_w);auto op=SelectObject(dc,pen),ob=SelectObject(dc,brush);body();SelectObject(dc,op);SelectObject(dc,ob);DeleteObject(pen);if(fill_colour!=CLR_INVALID) DeleteObject(brush);};
    const auto limb=[&](int x0,int y0,int x1,int y1,int width,COLORREF edge,COLORREF fill_colour) { // thick capsule: wide edge stroke under a narrower fill stroke
        for(int pass=0;pass<2;++pass) {LOGBRUSH lb{BS_SOLID,pass?fill_colour:edge,0};auto pen=ExtCreatePen(PS_GEOMETRIC|PS_SOLID|PS_ENDCAP_ROUND|PS_JOIN_ROUND,pass?width-6:width,&lb,0,nullptr);
            auto op=SelectObject(dc,pen);MoveToEx(dc,x0,y0,nullptr);LineTo(dc,x1,y1);SelectObject(dc,op);DeleteObject(pen);}};
    const auto ring=[&](int cx,int cy,int r,COLORREF colour,int width) {draw_shape(colour,CLR_INVALID,width,[&]{Ellipse(dc,cx-r,cy-r,cx+r,cy+r);});};
    const auto figure=[&](const GuideItem* f,int x,int y,int rw,int rh) {
        if(!f) return;std::string id;for(wchar_t c:f->key) id.push_back(static_cast<char>(c));const auto a=art("guide_"+id+(emily?"_emily":"_corvo"));
        draw_shape(dim_gold,CLR_INVALID,2,[&]{Rectangle(dc,x-5,y-5,x+rw+5,y+rh+5);}); // a plate in a thin brass frame
        if(a && a->width==static_cast<unsigned>(rw) && a->height==static_cast<unsigned>(rh)) {GdiFlush();blit_art(pixels,w,h,*a,x,y,x+rw,y+rh,1,false);}
        else {fill(x,y,rw,rh,RGB(41,39,35));const int used=flow({{&f->text,body_font.f,white}},x+32,0,rw-64,10000,false);const int top=y+(rh-used)/2;
            if(top<y+16 || top+used>y+rh-16) result.overflow=true;flow({{&f->text,body_font.f,white}},x+32,top,rw-64,y+rh,true);}};
    int one_y=260;
    if(page==0 && !sections.empty()) { // front-view body map: every reach zone is a ring on the figure, each bullet a callout
        const COLORREF fillc=RGB(58,54,50),edge=RGB(150,130,92);heading(items[sections[0].first].text,64,260);
        // The launcher's render of the selected character (520x816 at 700,252); its zones match GuideFigures.BodyMapAnchors.
        const auto body=art(emily?"guide_body_emily":"guide_body_corvo");const bool real=body && body->width==520 && body->height==816;
        if(real) {GdiFlush();blit_art(pixels,w,h,*body,700,252,1220,1068,1,true);
            for(const auto& r:{std::array<int,3>{928,284,26},{893,393,34},{819,648,28},{992,284,26},{1019,630,34},{1101,648,28}}) ring(r[0],r[1],r[2],brass,4);}
        else {
        limb(798,296,824,352,16,RGB(20,18,16),RGB(150,118,76));limb(796,364,858,330,12,RGB(20,18,16),RGB(210,206,196));limb(824,352,944,604,17,RGB(20,18,16),RGB(210,206,196)); // sword behind the right shoulder
        limb(925,670,915,1030,70,edge,fillc);limb(995,670,1005,1030,70,edge,fillc);limb(915,1030,880,1040,34,edge,fillc);limb(1005,1030,1040,1040,34,edge,fillc);
        limb(850,435,806,628,44,edge,fillc);limb(1070,435,1114,628,44,edge,fillc);
        draw_shape(edge,fillc,4,[&]{RoundRect(dc,852,392,1068,672,56,56);});limb(850,425,1070,425,64,edge,fillc);limb(960,372,960,405,36,edge,fillc);
        draw_shape(edge,fillc,4,[&]{Ellipse(dc,910,264,1010,384);Ellipse(dc,784,626,828,672);Ellipse(dc,1092,626,1136,672);RoundRect(dc,1022,612,1058,684,14,14);});
        for(const auto& r:{std::array<int,3>{914,320,28},{1006,320,28},{840,402,44},{806,640,30},{1114,640,30},{1040,648,44}}) ring(r[0],r[1],r[2],brass,4);}
        std::vector<Call> left,right;std::size_t first=sections[0].first;std::vector<const GuideItem*> b;
        for(auto k=first+1;k<sections[0].end;++k) if(items[k].kind==GuideItem::bullet) b.push_back(&items[k]);
        if(b.size()==5) { // bodies are carried by hand only: no shoulder zone (user, 2026-10-28)
            if(real) {left={{b[0],{284},{902},1,300},{b[1],{393},{859},1,430},{b[2],{648},{791},1,650}};
                right={{b[3],{284},{1018},1,300},{b[4],{630},{1053},1,680}};}
            else {left={{b[0],{320},{886},1,330},{b[1],{412},{796},1,480},{b[2],{640},{776},1,650}};
                right={{b[3],{320},{1034},1,330},{b[4],{688},{1050},1,700}};}
            callouts(left,64,570,672,324,1064);callouts(right,1272,584,1248,324,1064);
        } else result.overflow=true;
    } else if(page==1) { // two controllers, each button labelled with its action
        for(std::size_t n=0;n<sections.size() && n<2;++n) {
            const auto& s=sections[n];const int x0=n?992:64,cx=x0+135;const bool lefthand=n==0;
            const bool real_pad=[&]{const auto pad=art(lefthand?"controller_left":"controller_right");return pad && pad->width==340 && pad->height==600;}();
            heading(items[s.first].text,real_pad && !lefthand?1370:x0,260); // above its label column
            // Renders of the real Touch Plus side by side in the middle (launcher writes them from launcher/assets); each button's
            // label sits on the controller's outer side, joined to the ring on the button by a leader, as on the Body page.
            if(const auto pad=art(lefthand?"controller_left":"controller_right");pad && pad->width==340 && pad->height==600) {
                const int ix=lefthand?590:990,iy=380;GdiFlush();blit_art(pixels,w,h,*pad,ix,iy,ix+340,iy+600,1,false);
                std::vector<const GuideItem*> b;for(auto k2=s.first+1;k2<s.end;++k2) if(items[k2].kind==GuideItem::bullet) b.push_back(&items[k2]);
                const auto* at=lefthand?touch_plus_left_anchors.data():touch_plus_right_anchors.data();
                if(b.size()!=(lefthand?touch_plus_left_anchors.size():touch_plus_right_anchors.size())) {result.overflow=true;continue;}
                std::vector<Call> calls;
                for(std::size_t i=0;i<b.size();++i) {const int ax=ix+at[i][0],ay=iy+at[i][1];ring(ax,ay,20,brass,3);calls.push_back({b[i],{ay},{ax},1,ay,22});}
                if(lefthand) callouts(calls,64,486,570,324,1064,true);else callouts(calls,1370,486,1350,324,1064,true);
                continue;
            }
            const COLORREF fillc=RGB(58,54,50),edge=RGB(150,130,92);
            ring(cx,425,105,RGB(88,84,78),16);
            draw_shape(edge,fillc,4,[&]{RoundRect(dc,cx-64,600,cx+64,1030,60,60);RoundRect(dc,cx-95,400,cx+95,690,70,70);});fill(cx-58,620,116,50,fillc);
            const auto key=[&](int bx,int by,int r,const wchar_t* label) {draw_shape(brass,RGB(84,80,74),3,[&]{Ellipse(dc,bx-r,by-r,bx+r,by+r);});if(*label) text(label,bx-r,by-r,2*r,2*r,selector_font.f,white,DT_CENTER);};
            key(cx+38,455,26,lefthand?L"Y":L"B");key(cx+38,525,26,lefthand?L"X":L"A");
            draw_shape(brass,RGB(84,80,74),3,[&]{Ellipse(dc,cx-68,552,cx+12,632);});draw_shape(RGB(120,116,108),RGB(40,38,35),2,[&]{Ellipse(dc,cx-46,574,cx-10,610);});
            if(lefthand) key(cx,655,14,L"");
            draw_shape(brass,RGB(84,80,74),3,[&]{RoundRect(dc,cx+66,700,cx+112,800,24,24);RoundRect(dc,cx+66,840,cx+100,960,20,20);});
            std::vector<Call> calls;std::vector<const GuideItem*> b;for(auto k=s.first+1;k<s.end;++k) if(items[k].kind==GuideItem::bullet) b.push_back(&items[k]);
            const int ys[6]={455,525,590,655,750,900},ax[6]={cx+64,cx+64,cx+12,cx+14,cx+112,cx+100},ay[6]={455,525,592,655,750,900};
            // Left controller has Menu; the right one has no equivalent row.
            const int rows_left[6]={0,1,2,3,4,5},rows_right[5]={0,1,2,4,5};
            if(b.size()!=(lefthand?6u:5u)) {result.overflow=true;continue;}
            for(std::size_t i=0;i<b.size();++i) {const int r=lefthand?rows_left[i]:rows_right[i];calls.push_back({b[i],{ay[r]},{ax[r]},1,ys[r]});}
            callouts(calls,x0+330,534,x0+318,324,1064);
        }
    } else
    for(std::size_t n=0;n<sections.size();++n) {const auto& s=sections[n];const int x=two?(n==0?64:992):992;int y=two?260:one_y;
        if(two && n>=2) {result.overflow=true;continue;}heading(items[s.first].text,x,y);
        if(two) {figure(s.figure,x,324,864,232);y=580;}else {if(n==0) figure(s.figure,64,260,864,804);y+=64;}
        for(auto k=s.first+1;k<s.end;++k) if(items[k].kind==GuideItem::bullet) {const auto& b=items[k];++result.bullets;static const std::wstring dash=L"\x2014";std::vector<Run> runs;
            if(!b.key.empty()) {runs.push_back({&b.key,key_font.f,brass});runs.push_back({&dash,body_font.f,muted});}runs.push_back({&b.text,body_font.f,white});
            const int used=flow(runs,x,y,864,1064,false);if(y+used>1064) {result.overflow=true;continue;}flow(runs,x,y,864,1064,true);y+=used+8;++result.drawn;result.drawn_bullet_indices.push_back(b.bullet_index);}
        one_y=y+20;}
    if(layout) *layout=result;SelectObject(dc,GetStockObject(SYSTEM_FONT));GdiFlush();std::vector<std::uint8_t> out(static_cast<std::size_t>(width)*height*4);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {const int sx=static_cast<int>(std::uint64_t(x)*w/width),sy=static_cast<int>(std::uint64_t(y)*h/height);const auto p=pixels[sy*w+sx];const auto i=(static_cast<std::size_t>(y)*width+x)*4;
        const int dx=sx<40?40-sx:(sx>=1880?sx-1879:0),dy=sy<40?40-sy:(sy>=1160?sy-1159:0);const bool inside=sx>=16 && sx<1904 && sy>=16 && sy<1184 && (!dx || !dy || dx*dx+dy*dy<=24*24);
        out[i]=static_cast<std::uint8_t>(p>>16);out[i+1]=static_cast<std::uint8_t>(p>>8);out[i+2]=static_cast<std::uint8_t>(p);out[i+3]=inside?255:0;}
    return out;
}
} // namespace d2vr
