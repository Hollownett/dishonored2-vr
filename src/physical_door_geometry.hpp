#pragma once
#include "native_door_contact.hpp"
#include "physical_door_control.hpp"
#include <algorithm>
#include <array>
#include <optional>

namespace d2vr {
class PhysicalDoorGeometry {
public:
    struct Frame {Vec3 hinge{},normal{},leaf{},up{};};
    struct Sample {
        float current_angle_radians{},target_angle_radians{},inward_speed{};
        PhysicalDoorController::Pressure pressure{PhysicalDoorController::Pressure::None};
    };
    static std::optional<Frame> frame(const std::array<float,12>& transform) noexcept {
        Frame result{{transform[0],transform[1],transform[2]},
            {transform[3],transform[4],transform[5]},
            {transform[6],transform[7],transform[8]},
            {transform[9],transform[10],transform[11]}};
        if(!finite(result.hinge) || !axis(result.normal) || !axis(result.leaf) || !axis(result.up) ||
           std::abs(dot(result.normal,result.leaf))>.01 || std::abs(dot(result.normal,result.up))>.01 ||
           std::abs(dot(result.leaf,result.up))>.01 || dot(cross(result.normal,result.leaf),result.up)<.99 || result.up.z<.99)return {};
        return result;
    }
    static std::optional<Sample> sample(const Frame& frame,const NativeDoorContact::Surface& surface,
            Vec3 tracked_point,Vec3 velocity,double radius,bool swing_ccw) noexcept {
        if(!valid(frame) || !surface.entity || (surface.full_body&0xffffffu)==0xffffffu || !finite(surface.point) || !finite(surface.normal) || !axis(surface.normal) ||
           !std::isfinite(surface.depth) || surface.depth<=0 || surface.depth>.08 || !finite(tracked_point) || !finite(velocity) ||
           !std::isfinite(radius) || radius<=.0005 || radius>.08 || std::abs(dot(surface.normal,frame.up))>.1)return {};
        const auto hit=surface.point-frame.hinge,tracked=tracked_point-frame.hinge;
        const double hit_x=dot(hit,frame.normal),hit_y=dot(hit,frame.leaf),tracked_x=dot(tracked,frame.normal),tracked_y=dot(tracked,frame.leaf);
        const double hit_radius=std::hypot(hit_x,hit_y),tracked_radius=std::hypot(tracked_x,tracked_y);
        if(hit_radius<.2 || hit_radius>2.5 || tracked_radius<.2 || tracked_radius>2.5)return {};
        const double sense=swing_ccw?1.:-1.;
        auto normal=surface.normal;
        double current=std::atan2(sense*dot(normal,frame.leaf),dot(normal,frame.normal));
        if(current<-.05 || current>maximum_angle) {
            normal=normal*-1.;
            current=std::atan2(sense*dot(normal,frame.leaf),dot(normal,frame.normal));
        }
        if(current<-.05 || current>maximum_angle)return {};
        const double radial_angle=std::atan2(sense*hit_x,-hit_y);
        if(std::abs(radial_angle-current)>std::atan2(.1,hit_radius) ||
           std::abs(dot(hit,normal))>.1)return {};
        const auto push=surface.normal*-1.;
        const double torque=sense*(hit_x*dot(push,frame.leaf)-hit_y*dot(push,frame.normal));
        if(!std::isfinite(torque) || std::abs(torque)<.01)return {};
        const auto pressure=torque>0?PhysicalDoorController::Pressure::Opening:PhysicalDoorController::Pressure::Closing;
        const double margin=std::asin(std::min(radius/tracked_radius,1.));
        const double target=std::atan2(sense*tracked_x,-tracked_y)+(torque>0?margin:-margin);
        const double inward=-dot(velocity,surface.normal);
        if(!std::isfinite(target) || !std::isfinite(inward) || std::abs(inward)>15)return {};
        return Sample{static_cast<float>(std::clamp(current,0.,right_angle)),static_cast<float>(std::clamp(target,0.,right_angle)),
            static_cast<float>(inward),pressure};
    }
private:
    inline static constexpr double right_angle=1.5707963267948966,maximum_angle=1.7453292519943295;
    static bool axis(Vec3 value) noexcept {return finite(value) && std::abs(dot(value,value)-1.)<.02;}
    static bool valid(const Frame& value) noexcept {
        return finite(value.hinge) && axis(value.normal) && axis(value.leaf) && axis(value.up) && value.up.z>=.99 &&
            std::abs(dot(value.normal,value.leaf))<=.01 && std::abs(dot(value.normal,value.up))<=.01 &&
            std::abs(dot(value.leaf,value.up))<=.01 && dot(cross(value.normal,value.leaf),value.up)>=.99;
    }
};
}
