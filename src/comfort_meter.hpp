#pragma once
// Comfort telemetry: numbers that let camera-wobble and view-spin fixes be
// measured before and after. Fed once per frame; no allocation, no locks.
#include "arm_ik.hpp"
#include <cmath>
#include <sstream>
#include <string>

namespace d2vr {
struct ComfortSample {double t_s{};Vec3 anchor_pos{};double anchor_yaw{};double hmd_yaw{};bool authored{};};

class ComfortMeter {
    static constexpr double max_dt=.25,jerk_limit=30.,yaw_limit=1.5;
    bool have_prev_{},have_v_{},prev_authored_{};
    ComfortSample prev_{};
    double prev_v_{};
    double authored_s_{};
    unsigned long long entries_{},jerk_events_{},yaw_events_{},skipped_{},samples_{};
    double v_peak_{},yaw_peak_{};
    static double wrap(double a) noexcept {
        const double pi=3.14159265358979323846;
        while(a>pi) a-=2*pi;
        while(a<-pi) a+=2*pi;
        return a;
    }
public:
    void feed(const ComfortSample& s) noexcept {
        if(!finite(s.anchor_pos) || !std::isfinite(s.t_s) || !std::isfinite(s.anchor_yaw) || !std::isfinite(s.hmd_yaw)) {++skipped_;return;}
        if(!have_prev_) {prev_=s;prev_authored_=s.authored;have_prev_=true;if(s.authored) ++entries_;return;}
        const double dt=s.t_s-prev_.t_s;
        if(!(dt>0)) {++skipped_;return;}
        if(dt>max_dt) { // a stall: rebase on this sample so the meter recovers
            ++skipped_;prev_=s;prev_authored_=s.authored;have_v_=false;if(s.authored) ++entries_;return;
        }
        ++samples_;
        if(s.authored) {authored_s_+=dt;if(!prev_authored_) ++entries_;}
        const double v=(s.anchor_pos.z-prev_.anchor_pos.z)/dt;
        v_peak_=std::fmax(v_peak_,std::fabs(v));
        if(have_v_ && std::fabs((v-prev_v_)/dt)>jerk_limit) ++jerk_events_;
        prev_v_=v;have_v_=true;
        const double yr=std::fabs(wrap(s.anchor_yaw-prev_.anchor_yaw))/dt;
        yaw_peak_=std::fmax(yaw_peak_,yr);
        if(yr>yaw_limit) ++yaw_events_;
        prev_=s;prev_authored_=s.authored;
    }
    std::string json() const {
        std::ostringstream o;
        o<<"{\"authored_ms\":"<<authored_s_*1000.<<",\"authored_entries\":"<<entries_
         <<",\"anchor_v_peak\":"<<v_peak_<<",\"anchor_jerk_events\":"<<jerk_events_
         <<",\"view_yaw_rate_peak\":"<<yaw_peak_<<",\"view_yaw_events\":"<<yaw_events_
         <<",\"samples\":"<<samples_<<",\"skipped\":"<<skipped_<<'}';
        return o.str();
    }
};
} // namespace d2vr
