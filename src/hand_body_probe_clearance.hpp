#pragma once
#include "tracked_pose.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>
namespace d2vr {
struct HandBodyProbeAnswer {
    std::uint64_t request_id{},tick{};Vec3 from{},to{};double radius{};std::uintptr_t exclude{};bool blocked{};
};
// Three distinct completed probes, for this epoch, exact palm and exclusion. Pending/replayed work cannot
// become a clear answer. Completion time is retained so a render loop cannot keep old clearance alive.
class HandBodyProbeClearance {
    std::uint64_t epoch_{},retired_{},seen_{},tick_{};unsigned count_{};Vec3 at_{};std::uintptr_t exclude_{};
public:
    void reset(std::uint64_t retired=0) {epoch_=0;retired_=retired;seen_=retired;tick_=0;count_=0;}
    bool observe(std::uint64_t epoch,std::uint64_t now,Vec3 palm,std::uintptr_t exclude,double radius,
                 std::uint64_t issued_through,const std::optional<HandBodyProbeAnswer>& answer) {
        if(epoch_!=epoch) {reset(issued_through);epoch_=epoch;}
        if(!epoch || !finite(palm) || !std::isfinite(radius) || radius<=0) {count_=0;return false;}
        if(!tick_ || now<tick_ || now-tick_>100 || length(palm-at_)>.005 || exclude!=exclude_)count_=0;
        if(answer && answer->request_id>retired_ && answer->request_id>seen_) {
            seen_=answer->request_id;
            const bool exact=answer->tick && now>=answer->tick && now-answer->tick<=100 &&
                finite(answer->from) && finite(answer->to) && length(answer->to-palm)<=.005 &&
                length(answer->from-(palm+Vec3{0,0,.01}))<=.005 &&
                std::isfinite(answer->radius) && std::abs(answer->radius-radius)<=1e-9 && answer->exclude==exclude;
            if(exact && !answer->blocked) {count_=std::min(count_+1,3u);tick_=answer->tick;at_=palm;exclude_=exclude;}
            else {count_=0;tick_=0;}
        }
        return count_>=3;
    }
};
}
