#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/controls_guide_image.hpp"
#include "../src/guide_navigation.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace d2vr;namespace fs=std::filesystem;
        std::ifstream in("docs/VR-CONTROLS.md",std::ios::binary);require(static_cast<bool>(in),"docs/VR-CONTROLS.md missing (run from the repo root)");
        std::ostringstream text;text<<in.rdbuf();const auto md=text.str();unsigned errors=0;
        const auto items=parse_controls_guide(md,&errors);
        int sections=0,bullets=0,pages=0,figures=0;
        for(const auto& i:items) {sections+=i.kind==GuideItem::section;bullets+=i.kind==GuideItem::bullet;pages+=i.kind==GuideItem::page;figures+=i.kind==GuideItem::figure;
            if(i.kind==GuideItem::bullet) require(!i.key.empty() && !i.text.empty(),"A bullet lost its bold input or description");}
        require(!errors && !items.empty() && items[0].kind==GuideItem::title && sections==5 && bullets==26 && pages==3 && figures==2,"Guide directives, headings or bullets did not parse");
        require(parse_controls_guide(md+"\n<!-- unrelated comment -->\n").size()==items.size(),"Unrelated comments must be ignored");
        errors=0;const auto invalid=parse_controls_guide("<!-- vr-page:../../outside|Bad -->\n<!-- vr-figure:unknown|Bad -->\n<!-- vr-page:moving -->\n",&errors);
        require(errors==3 && invalid.empty(),"Unknown and malformed directive ids must be rejected safely");
        {   // The page's controller anchors must match the renderer's record (tools/render_touch_plus.py).
            std::ifstream json("launcher/assets/touch-plus.anchors.json");std::ostringstream j;j<<json.rdbuf();const auto a=j.str();
            const auto check=[&](const char* side,const std::array<const char*,6>& keys,const auto& want) {
                const auto base=a.find(std::string("\"")+side+"\"");require(base!=std::string::npos,"Controller anchors missing");
                for(std::size_t i=0;i<want.size();++i) {const auto at=a.find(std::string("\"")+keys[i]+"\": [",base);require(at!=std::string::npos,"Controller anchor missing");
                    char* end=nullptr;const double x=std::strtod(a.c_str()+a.find('[',at)+1,&end),y=std::strtod(end+1,nullptr);
                    require(std::abs(x-want[i][0])<=1 && std::abs(y-want[i][1])<=1,"Controller anchors differ from touch-plus.anchors.json");}};
            check("left",{"y","x","thumbstick","menu","trigger","grip"},touch_plus_left_anchors);
            check("right",{"b","a","thumbstick","trigger","grip",""},touch_plus_right_anchors);
        }
        const fs::path previews="artifacts/guide-pages",imported="artifacts/game-art";fs::create_directories(previews);
        const auto validate=[&](const std::vector<std::uint8_t>& pixels,const GuideLayout& layout,std::vector<unsigned>& seen) {
            require(pixels.size()==1920u*1200*4,"Guide bitmap has wrong dimensions");
            require(layout.drawn==layout.bullets && !layout.overflow && !layout.out_of_bounds,"A page omitted a bullet, overflowed or drew out of bounds");
            require(layout.drawn_bullet_indices.size()==static_cast<std::size_t>(layout.drawn),"Bullet identity stats must match drawing stats");
            for(auto id:layout.drawn_bullet_indices) {require(id<seen.size(),"Drawn bullet id is invalid");++seen[id];}
            const auto alpha=[&](unsigned x,unsigned y) {return pixels[(y*1920+x)*4+3];};
            require(alpha(2,2)==0 && alpha(1919,1199)==0 && alpha(16,16)==0,"Card corners must stay clear");
            for(unsigned y=40;y<1160;++y) for(unsigned x=40;x<1880;++x) require(alpha(x,y)==255,"Card interior must be fully opaque");
        };
        // User request: all pages retain every instruction with missing, complete, or partial imported art.
        const auto render_set=[&](const std::string& prefix,bool emily,bool art,bool save) {
            std::vector<unsigned> seen(static_cast<std::size_t>(bullets));
            for(unsigned p=0;p<3;++p) {
                GuideLayout layout{};const auto pixels=controls_guide_image(md,1920,1200,&layout,p,emily,art);
                if(save) {std::ofstream out(previews/(prefix+"-"+std::to_string(p+1)+".rgba"),std::ios::binary);out.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(pixels.size()));require(static_cast<bool>(out),"Could not write RGBA page preview");}
                if(layout.drawn!=layout.bullets || layout.overflow || layout.out_of_bounds) std::cout<<prefix<<" page "<<p+1<<": drawn "<<layout.drawn<<" of "<<layout.bullets<<(layout.overflow?", overflow":"")<<(layout.out_of_bounds?", out of bounds":"")<<'\n';
                validate(pixels,layout,seen);
                require(pixels==controls_guide_image(md,1920,1200,nullptr,p,emily,art),"Guide image is not deterministic");
                std::cout<<prefix<<" page "<<p+1<<": "<<layout.drawn<<" bullets, no overflow or out-of-bounds\n";
            }
            for(auto count:seen) require(count==1,"Every source bullet must be drawn exactly once across all pages");
        };
        game_art().folder(imported);render_set("plain",false,false,true);
        render_set("corvo-art",false,true,true);render_set("emily-art",true,true,true);
        const auto partial=previews/"partial-art";fs::create_directories(partial);
        if(fs::exists(imported/"divider.rgba")) fs::copy_file(imported/"divider.rgba",partial/"divider.rgba",fs::copy_options::overwrite_existing);
        {std::ofstream out(partial/"guide_sword_draw_block_corvo.rgba",std::ios::binary);const std::uint32_t wh[2]={864,232};out.write(reinterpret_cast<const char*>(wh),8);
            const std::array<unsigned char,4> colour={173,97,61,255};for(unsigned i=0;i<864u*232;++i) out.write(reinterpret_cast<const char*>(colour.data()),4);}
        game_art().folder(partial);render_set("partial-corvo",false,true,true);render_set("partial-emily",true,true,false);
        const auto corvo=controls_guide_image(md,1920,1200,nullptr,2,false,true),emily=controls_guide_image(md,1920,1200,nullptr,2,true,true);
        const auto at=(400u*1920+400)*4;
        require(corvo[at]==173 && corvo[at+1]==97 && corvo[at+2]==61,"A figure must use its exact slot and 1:1 pixels");
        require(corvo!=emily,"Character-specific figures must use the selected character; a missing variant keeps the fallback");
        fs::remove(partial/"publication-check.rgba");fs::remove(partial/"generation.txt");
        GameArtCache published(partial);published.refresh();require(!published.get("publication-check"),"Fixture publication key must begin missing");
        const auto generation=published.generation();
        fs::copy_file(partial/"guide_sword_draw_block_corvo.rgba",partial/"publication-check.rgba",fs::copy_options::overwrite_existing);
        {std::ofstream marker(partial/"generation.txt");marker<<"test publication";}
        require(published.refresh()>generation && published.get("publication-check"),"Publishing art must invalidate cached misses and advance generation");
        fs::remove(partial/"publication-check.rgba");fs::remove(partial/"generation.txt");
        game_art().folder(imported);
        GuideNavigationState state{};
        auto nav=guide_navigation_update(state,true,.65f,0.f,false);require(nav.delta==1 && nav.consume,"A threshold flick changes a page and consumes the stick");state=nav.state;
        nav=guide_navigation_update(state,true,1.f,0.f,true);require(nav.delta==0 && nav.consume,"A held stick must not repeat or leak its click");state=nav.state;
        nav=guide_navigation_update(state,true,0.f,.3f,false);require(!nav.state.armed,"Both axes must be neutral before rearming");
        nav=guide_navigation_update(nav.state,true,.24f,.24f,false);require(nav.state.armed,"Neutral rearms the page gesture");
        nav=guide_navigation_update(nav.state,true,-.65f,0.f,false);require(nav.delta==-1,"A left flick changes one page back");
        require(guide_page_step(0,-1)==0 && guide_page_step(2,1)==2 && guide_page_step(2,-1)==1,"Page navigation must stop at both ends");
        nav=guide_navigation_update(nav.state,false,1.f,0.f,true);require(nav.consume,"Closing must retain ownership until neutral");
        nav=guide_navigation_update(nav.state,false,0.f,0.f,true);require(nav.consume && nav.state.blocked,"A held click must not leak after closing");
        nav=guide_navigation_update(nav.state,false,0.f,0.f,false);require(nav.consume && !nav.state.blocked,"The neutral release tick is still consumed");
        nav=guide_navigation_update(nav.state,false,0.f,0.f,false);require(!nav.consume,"Gameplay regains the stick after neutral");
        std::cout<<"PASS: three pages; every bullet exactly once; plain/imported/partial art; both characters; bounds, opacity, determinism and navigation\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
