#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/controls_guide_image.hpp"
#include "../src/guide_navigation.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

static void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace d2vr;
        GuideNavigation opened;opened.claim();require(opened.update(true,1.f,0.f,false).delta==0,"Opening ignores a pre-held stick");
        opened.update(true,0.f,0.f,false);require(opened.update(true,1.f,0.f,false).delta==1,"Opening neutral arms navigation");
        // User request: page gestures are pure and leave gameplay blocked through the neutral release tick.
        GuideNavigationState state{};
        auto nav=guide_navigation_update(state,true,.65f,0.f,false);require(nav.delta==1 && nav.consume,"Threshold flick");
        nav=guide_navigation_update(nav.state,true,1.f,0.f,true);require(nav.delta==0 && nav.consume,"No held repeat or click leak");
        nav=guide_navigation_update(nav.state,true,0.f,.3f,false);require(!nav.state.armed,"Both axes must be neutral");
        nav=guide_navigation_update(nav.state,true,.24f,.24f,false);require(nav.state.armed,"Neutral rearms");
        nav=guide_navigation_update(nav.state,true,-.65f,0.f,false);require(nav.delta==-1,"Left flick");
        require(guide_page_step(0,-1)==0 && guide_page_step(6,1)==6 && guide_page_step(3,-1)==2,"Clamp page ends");
        nav=guide_navigation_update(nav.state,false,1.f,0.f,true);require(nav.consume,"Closing retains ownership");
        nav=guide_navigation_update(nav.state,false,0.f,0.f,true);require(nav.consume && nav.state.blocked,"Held click blocked");
        nav=guide_navigation_update(nav.state,false,0.f,0.f,false);require(nav.consume && !nav.state.blocked,"Neutral release consumed");
        nav=guide_navigation_update(nav.state,false,0.f,0.f,false);require(!nav.consume,"Gameplay restored after neutral");
        std::ifstream in("docs/VR-CONTROLS.md",std::ios::binary);require(static_cast<bool>(in),"Guide source missing");std::ostringstream text;text<<in.rdbuf();unsigned errors=0;
        const auto items=parse_controls_guide(text.str(),&errors);int pages=0,figures=0,bullets=0,sections=0;
        for(const auto& item:items) {pages+=item.kind==GuideItem::page;figures+=item.kind==GuideItem::figure;sections+=item.kind==GuideItem::section;
            if(item.kind==GuideItem::bullet) {require(item.bullet_index==static_cast<std::size_t>(bullets++),"Stable bullet identity");require(!item.key.empty() && !item.text.empty(),"Complete bullet text");}}
        require(!errors && pages==7 && figures==11 && sections==12 && bullets>=40,"All source directives and instructions parse");
        require(parse_controls_guide(text.str()+"\n<!-- unrelated -->\n").size()==items.size(),"Ignore unrelated comments");
        const auto invalid=parse_controls_guide("<!-- vr-page:../../outside|Bad -->\n<!-- vr-figure:unknown|Bad -->\n<!-- vr-page:moving -->\n",&errors);
        require(invalid.empty() && errors==3,"Reject unknown ids and malformed directives");
        std::cout<<"PASS: seven pages, eleven figures, "<<bullets<<" complete bullets; safe parser; flick thresholds, no repeat, clamped ends and neutral ownership\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
