#pragma once
#include "controller_input.hpp"
#include <array>
#include <cmath>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

namespace d2vr {
// Developer commands only. Native pad composition still applies its release gates.
class SimulatedGameplayScript {
    struct Segment {UINT64 start{},end{};ControllerInput input{};float head_pitch_degrees{},hand_pitch_degrees{};std::array<float,9> offsets{};unsigned mask{};std::array<std::array<float,7>,3> poses{};unsigned when{};unsigned touch{};};
    std::vector<Segment> segments_;
public:
    void load(std::istream& source,unsigned seconds) {
        std::string magic;unsigned version{},duration{},count{};
        if(!(source>>magic>>version>>duration>>count) || magic!="D2VR_GAMEPLAY_SCRIPT" || (version<1 || version>7) ||
           seconds<10 || seconds>120 || duration!=seconds*1000 || count==0 || count>(version>=5?4096u:128u))
            throw std::invalid_argument("Invalid gameplay script header");
        std::vector<Segment> parsed;UINT64 previous{};
        for(unsigned i=0;i<count;++i) {
            Segment s{};s.input.active=true;
            if(!(source>>s.start>>s.end) || s.start<previous || s.start>=s.end || s.end>duration)
                throw std::invalid_argument("Invalid gameplay script interval");
            const std::array<float*,8> analog{&s.input.move_x,&s.input.move_y,&s.input.turn_x,&s.input.turn_y,
                &s.input.left_trigger,&s.input.right_trigger,&s.input.left_grip,&s.input.right_grip};
            for(unsigned j=0;j<analog.size();++j)
                if(!(source>>*analog[j]) || !std::isfinite(*analog[j]) || *analog[j]<(j<4?-1.f:0.f) || *analog[j]>1.f)
                    throw std::invalid_argument("Invalid gameplay script analog value");
            const std::array<bool*,7> buttons{&s.input.a,&s.input.b,&s.input.x,&s.input.y,&s.input.left_click,&s.input.right_click,&s.input.menu};
            for(auto* button:buttons) {int value{};if(!(source>>value) || value<0 || value>1) throw std::invalid_argument("Invalid gameplay script button");*button=value==1;}
            if(version>=2 && (!(source>>s.head_pitch_degrees) || !std::isfinite(s.head_pitch_degrees) ||
                s.head_pitch_degrees<-80 || s.head_pitch_degrees>80)) throw std::invalid_argument("Invalid gameplay script head pitch");
            // Version 3: head, left hand, right hand offsets in metres (XR local space).
            if(version>=3) for(auto& value:s.offsets)
                if(!(source>>value) || !std::isfinite(value) || std::abs(value)>2) throw std::invalid_argument("Invalid gameplay script pose offset");
            // Version 4: both hands pitched (degrees, negative points down) so tests can aim at the floor.
            if(version>=4 && (!(source>>s.hand_pitch_degrees) || !std::isfinite(s.hand_pitch_degrees) || std::abs(s.hand_pitch_degrees)>89))
                throw std::invalid_argument("Invalid gameplay script hand pitch");
            // Version 5: absolute tracking-space poses (x y z qx qy qz qw, e.g. a recorded headset trace) for the masked parts: 1 head, 2 left grip, 4 right grip.
            if(version>=5) {
                if(!(source>>s.mask) || s.mask>7) throw std::invalid_argument("Invalid gameplay script pose mask");
                for(unsigned k=0;k<3;++k) if(s.mask&(1u<<k)) {
                    auto& p=s.poses[k];bool ok=true;
                    for(float& v:p) ok=(source>>v) && std::isfinite(v) && ok;
                    const float n=ok?std::sqrt(p[3]*p[3]+p[4]*p[4]+p[5]*p[5]+p[6]*p[6]):0.f;
                    if(!ok || std::abs(p[0])>10 || std::abs(p[1])>10 || std::abs(p[2])>10 || !(n>.99f && n<1.01f)) throw std::invalid_argument("Invalid gameplay script pose");
                    for(unsigned j=3;j<7;++j) p[j]/=n;
                }
            }
            // Version 6: when the segment applies: 0 gameplay only (earlier versions), 1 menus/non-gameplay only, 2 always.
            if(version>=6 && (!(source>>s.when) || s.when>2)) throw std::invalid_argument("Invalid gameplay script when");
            // Version 7: ten capacitive-touch flags, left then right, each [trigger,thumbstick,first,second,thumbrest] (visual finger curl tests; the buttons above are untouched).
            if(version>=7) for(unsigned k=0;k<10;++k) {int value{};if(!(source>>value) || value<0 || value>1) throw std::invalid_argument("Invalid gameplay script touch");s.touch|=static_cast<unsigned>(value)<<k;}
            parsed.push_back(s);previous=s.end;
        }
        std::string extra;if(source>>extra) throw std::invalid_argument("Trailing gameplay script data");
        segments_=std::move(parsed);
    }
    ControllerInput sample(UINT64 elapsed,bool gameplay,bool focused,int* index=nullptr,float* head_pitch_degrees=nullptr) const noexcept {
        if(index) *index=-1;
        if(head_pitch_degrees) *head_pitch_degrees=0;
        ControllerInput neutral{};neutral.active=focused;
        if(!focused) return neutral;
        for(std::size_t i=0;i<segments_.size();++i) {
            const auto& s=segments_[i];
            if(elapsed>=s.start && elapsed<s.end) {
                if(!((s.when==0 && gameplay) || (s.when==1 && !gameplay) || s.when==2)) return neutral;
                if(index) *index=static_cast<int>(i);
                if(head_pitch_degrees) *head_pitch_degrees=s.head_pitch_degrees;
                return s.input;
            }
        }
        return neutral;
    }
    std::array<float,9> offsets(UINT64 elapsed) const noexcept {
        for(const auto& s:segments_) if(elapsed>=s.start && elapsed<s.end) return s.offsets;
        return {};
    }
    std::array<std::array<bool,5>,2> touch(UINT64 elapsed) const noexcept {
        std::array<std::array<bool,5>,2> out{};
        for(const auto& s:segments_) if(elapsed>=s.start && elapsed<s.end) {for(unsigned k=0;k<10;++k) out[k/5][k%5]=(s.touch>>k&1u)!=0;break;}
        return out;
    }
    float hand_pitch(UINT64 elapsed) const noexcept {
        for(const auto& s:segments_) if(elapsed>=s.start && elapsed<s.end) return s.hand_pitch_degrees;
        return 0;
    }
    struct Poses {unsigned mask{};std::array<std::array<float,7>,3> pose{};}; // head, left grip, right grip
    // Version 5 poses at `elapsed`, lerped toward the next back-to-back segment so a 20 Hz trace plays smoothly (swing speed needs it).
    Poses poses(UINT64 elapsed) const noexcept {
        for(std::size_t i=0;i<segments_.size();++i) {
            const auto& s=segments_[i];if(elapsed<s.start || elapsed>=s.end) continue;
            Poses out{s.mask,s.poses};
            if(i+1<segments_.size() && segments_[i+1].start==s.end) {
                const auto& n=segments_[i+1];const float t=static_cast<float>(elapsed-s.start)/static_cast<float>(s.end-s.start);
                for(unsigned k=0;k<3;++k) if((s.mask&n.mask)&(1u<<k)) {
                    const auto& a=s.poses[k];const auto& b=n.poses[k];auto& o=out.pose[k];
                    const float sign=a[3]*b[3]+a[4]*b[4]+a[5]*b[5]+a[6]*b[6]<0?-1.f:1.f; // shortest arc
                    for(unsigned j=0;j<7;++j) o[j]=a[j]+t*((j<3?1.f:sign)*b[j]-a[j]);
                    const float len=std::sqrt(o[3]*o[3]+o[4]*o[4]+o[5]*o[5]+o[6]*o[6]);
                    for(unsigned j=3;j<7;++j) o[j]/=len;
                }
            }
            return out;
        }
        return {};
    }
    std::size_t size() const noexcept {return segments_.size();}
    // OpenXR -Z forward: negative rotation about +X looks down, as scripted_pitch does.
    static std::array<float,4> pitch_orientation(float degrees) noexcept {
        const float half=degrees*0.008726646259971648f;
        return {std::sin(half),0,0,std::cos(half)};
    }
};
} // namespace d2vr
