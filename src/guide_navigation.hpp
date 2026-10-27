#pragma once
#include <algorithm>
#include <cmath>

namespace d2vr {
inline int guide_page_step(int page,int delta,int count=7) noexcept {return std::clamp(page+(delta>0?1:delta<0?-1:0),0,std::max(0,count-1));}
struct GuideNavigationState {bool armed=true,blocked=false;};
struct GuideNavigationResult {GuideNavigationState state;int delta{};bool consume{};};
// User's illustrated guide: one flick, both axes neutral to rearm; closed gameplay waits for neutral and click release.
inline GuideNavigationResult guide_navigation_update(GuideNavigationState state,bool open,float x,float y,bool click,bool active=true) noexcept {
    const bool valid=active && std::isfinite(x) && std::isfinite(y);
    const bool neutral=valid && std::abs(x)<.25f && std::abs(y)<.25f;
    GuideNavigationResult out{state,0,open || state.blocked};
    if(open) {
        out.state.blocked=true;
        if(neutral) out.state.armed=true;
        else if(valid && out.state.armed && std::abs(x)>=.65f) {out.delta=x>0?1:-1;out.state.armed=false;}
    } else if(state.blocked && neutral && !click) {out.state.blocked=false;out.state.armed=true;}
    return out;
}
class GuideNavigation {
    GuideNavigationState state_;
public:
    GuideNavigationResult update(bool open,float x,float y,bool click,bool active=true) noexcept {
        const auto out=guide_navigation_update(state_,open,x,y,click,active);state_=out.state;return out;
    }
    bool blocked() const noexcept {return state_.blocked;}
    void claim() noexcept {state_.blocked=true;state_.armed=false;} // opening always starts at page 1, even with a pre-held stick
};
}
