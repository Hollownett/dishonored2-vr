#pragma once
// Comfort telemetry: numbers that let camera-wobble and view-spin fixes be
// measured before and after. Fed once per frame; no allocation, no locks of its own
// (the caller serialises feed/json). Known undercount: callers that return early
// before feeding (anchor/calibration failures) simply produce no sample.
// view_yaw_* counts ALL anchor yaw, including the player's own turning;
// unrequested_yaw_* counts only frames with turning=false (wobble/spin signal).
#include "arm_ik.hpp"
#include <cmath>
#include <sstream>
#include <string>

namespace d2vr {
struct ComfortSample {double t_s{};Vec3 anchor_pos{};double anchor_yaw{};double hmd_yaw{};bool authored{};bool turning{};Vec3 view_pos{};bool has_view{};}; // view_pos: the smoothed anchor (has_view=false when no smoother ran)

class ComfortMeter {
    static constexpr double max_dt=.25,jerk_limit=30.,yaw_limit=1.5;
    bool have_prev_{},have_v_{},prev_authored_{};
    ComfortSample prev_{};
    double prev_v_{};
    bool have_vv_{};double prev_vv_{},view_v_peak_{};unsigned long long view_jerk_events_{};
    unsigned long long smoother_resets_{}; // set by the caller (AnchorSmoother::resets)
    double authored_s_{};
    unsigned long long entries_{},jerk_events_{},yaw_events_{},unreq_events_{},skipped_{},samples_{};
    double v_peak_{},yaw_peak_{},unreq_peak_{};
    unsigned long long authored_snaps_{}; // set by the caller (AuthoredHeading::snaps); not derived from samples
    static double wrap(double a) noexcept {
        const double pi=3.14159265358979323846;
        while(a>pi) a-=2*pi;
        while(a<-pi) a+=2*pi;
        return a;
    }
public:
    void set_authored_snaps(unsigned long long n) noexcept {authored_snaps_=n;}
    void set_smoother_resets(unsigned long long n) noexcept {smoother_resets_=n;}
    void feed(const ComfortSample& s) noexcept {
        if(!finite(s.anchor_pos) || !std::isfinite(s.t_s) || !std::isfinite(s.anchor_yaw) || !std::isfinite(s.hmd_yaw)) {++skipped_;return;}
        if(!have_prev_) {prev_=s;prev_authored_=s.authored;have_prev_=true;if(s.authored) ++entries_;return;}
        const double dt=s.t_s-prev_.t_s;
        if(!(dt>0)) {++skipped_;return;}
        if(dt>max_dt) { // a stall: rebase on this sample so the meter recovers
            ++skipped_;prev_=s;prev_authored_=s.authored;have_v_=false;have_vv_=false;if(s.authored) ++entries_;return;
        }
        ++samples_;
        if(s.authored) {authored_s_+=dt;if(!prev_authored_) ++entries_;}
        const double v=(s.anchor_pos.z-prev_.anchor_pos.z)/dt;
        v_peak_=std::fmax(v_peak_,std::fabs(v));
        if(have_v_ && std::fabs((v-prev_v_)/dt)>jerk_limit) ++jerk_events_;
        prev_v_=v;have_v_=true;
        if(s.has_view && std::isfinite(s.view_pos.z)) { // same jerk test on the smoothed view height, so raw and smoothed compare directly
            if(prev_.has_view) {
                const double vv=(s.view_pos.z-prev_.view_pos.z)/dt;
                view_v_peak_=std::fmax(view_v_peak_,std::fabs(vv));
                if(have_vv_ && std::fabs((vv-prev_vv_)/dt)>jerk_limit) ++view_jerk_events_;
                prev_vv_=vv;have_vv_=true;
            }
        } else have_vv_=false;
        const double yr=std::fabs(wrap(s.anchor_yaw-prev_.anchor_yaw))/dt;
        yaw_peak_=std::fmax(yaw_peak_,yr);
        if(yr>yaw_limit) ++yaw_events_;
        if(!s.turning) {unreq_peak_=std::fmax(unreq_peak_,yr);if(yr>yaw_limit) ++unreq_events_;}
        prev_=s;prev_authored_=s.authored;
    }
    std::string json() const {
        std::ostringstream o;
        o<<"{\"authored_ms\":"<<static_cast<long long>(std::llround(authored_s_*1000.))<<",\"authored_entries\":"<<entries_
         <<",\"anchor_v_peak\":"<<v_peak_<<",\"anchor_jerk_events\":"<<jerk_events_
         <<",\"view_v_peak\":"<<view_v_peak_<<",\"view_jerk_events\":"<<view_jerk_events_<<",\"smoother_resets\":"<<smoother_resets_
         <<",\"view_yaw_rate_peak\":"<<yaw_peak_<<",\"view_yaw_events\":"<<yaw_events_
         <<",\"unrequested_yaw_rate_peak\":"<<unreq_peak_<<",\"unrequested_yaw_events\":"<<unreq_events_
         <<",\"samples\":"<<samples_<<",\"skipped\":"<<skipped_<<",\"authored_snaps\":"<<authored_snaps_<<'}';
        return o.str();
    }
};
} // namespace d2vr
