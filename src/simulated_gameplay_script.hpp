#pragma once
#include "controller_input.hpp"
#include <array>
#include <istream>
#include <string>
#include <vector>

namespace d2vr {
// Developer commands only. Native pad composition still applies its release gates.
class SimulatedGameplayScript {
    struct Segment {UINT64 start{},end{};ControllerInput input{};float head_pitch_degrees{},hand_pitch_degrees{};std::array<float,9> offsets{};};
    std::vector<Segment> segments_;
public:
    void load(std::istream& source,unsigned seconds) {
        std::string magic;unsigned version{},duration{},count{};
        if(!(source>>magic>>version>>duration>>count) || magic!="D2VR_GAMEPLAY_SCRIPT" || (version<1 || version>4) ||
           seconds<10 || seconds>120 || duration!=seconds*1000 || count==0 || count>128)
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
            if(version==4 && (!(source>>s.hand_pitch_degrees) || !std::isfinite(s.hand_pitch_degrees) || std::abs(s.hand_pitch_degrees)>89))
                throw std::invalid_argument("Invalid gameplay script hand pitch");
            parsed.push_back(s);previous=s.end;
        }
        std::string extra;if(source>>extra) throw std::invalid_argument("Trailing gameplay script data");
        segments_=std::move(parsed);
    }
    ControllerInput sample(UINT64 elapsed,bool gameplay,bool focused,int* index=nullptr,float* head_pitch_degrees=nullptr) const noexcept {
        if(index) *index=-1;
        if(head_pitch_degrees) *head_pitch_degrees=0;
        ControllerInput neutral{};neutral.active=focused;
        if(!gameplay || !focused) return neutral;
        for(std::size_t i=0;i<segments_.size();++i) {
            const auto& s=segments_[i];
            if(elapsed>=s.start && elapsed<s.end) {
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
    float hand_pitch(UINT64 elapsed) const noexcept {
        for(const auto& s:segments_) if(elapsed>=s.start && elapsed<s.end) return s.hand_pitch_degrees;
        return 0;
    }
    std::size_t size() const noexcept {return segments_.size();}
    // OpenXR -Z forward: negative rotation about +X looks down, as scripted_pitch does.
    static std::array<float,4> pitch_orientation(float degrees) noexcept {
        const float half=degrees*0.008726646259971648f;
        return {std::sin(half),0,0,std::cos(half)};
    }
};
} // namespace d2vr
