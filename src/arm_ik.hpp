#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace d2vr {

// Right-handed, metres. Convert the game's coordinates at the integration seam.
struct Vec3 {
    double x{}, y{}, z{};
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
};
inline double dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline double length(Vec3 v) { return std::hypot(v.x, v.y, v.z); }
inline bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline Vec3 unit(Vec3 v) { return v * (1.0 / length(v)); }

struct ArmPose {
    Vec3 shoulder, elbow, wrist;
    bool reach_clamped;
};

// pole_direction selects the elbow side; it is a direction, not a world point.
// This solves joint positions. Rig bone rotations and wrist orientation belong
// to the game's skeleton integration, which has not been reverse-engineered.
inline ArmPose solve_arm(Vec3 shoulder, Vec3 tracked_wrist, Vec3 pole_direction,
                         double upper_arm, double forearm) {
    if (!finite(shoulder) || !finite(tracked_wrist) || !finite(pole_direction) ||
        !std::isfinite(upper_arm) || !std::isfinite(forearm) ||
        upper_arm < 0.001 || forearm < 0.001 || upper_arm > 2 || forearm > 2) {
        throw std::invalid_argument("Invalid arm pose or uncalibrated limb lengths");
    }
    Vec3 delta = tracked_wrist - shoulder;
    double requested = length(delta);
    if (!std::isfinite(requested)) throw std::invalid_argument("Arm position overflow");
    Vec3 direction = requested > 1e-9 ? unit(delta) : Vec3{0, 0, -1};
    double distance = std::clamp(requested, std::max(1e-6, std::abs(upper_arm-forearm)),
                               upper_arm + forearm);
    double pole_length = length(pole_direction);
    if (!std::isfinite(pole_length)) throw std::invalid_argument("Elbow pole overflow");
    Vec3 pole = pole_length > 1e-9 ? unit(pole_direction) : Vec3{};
    Vec3 bend = pole - direction * dot(pole, direction);
    if (length(bend) < 1e-6) {
        // Choose the least parallel axis for a deterministic singularity fallback.
        Vec3 axis = std::abs(direction.x) <= std::abs(direction.y) &&
                    std::abs(direction.x) <= std::abs(direction.z) ? Vec3{1,0,0} :
                    std::abs(direction.y) <= std::abs(direction.z) ? Vec3{0,1,0} : Vec3{0,0,1};
        bend = axis - direction * dot(axis, direction);
    }
    bend = unit(bend);
    double along = (upper_arm*upper_arm - forearm*forearm + distance*distance) /
                   (2*distance);
    double height = std::sqrt(std::max(0.0, upper_arm*upper_arm - along*along));
    return {shoulder, shoulder + direction*along + bend*height,
            shoulder + direction*distance, std::abs(distance-requested) > 1e-9};
}

} // namespace d2vr
