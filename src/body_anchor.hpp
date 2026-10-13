#pragma once
#include "native_player_camera.hpp"
#include "tracked_pose.hpp"

namespace d2vr {
// Authored actions use the native camera directly. Only the physical eye offset
// is carried into its frame; roomscale, crouch and tracked head yaw are excluded.
inline std::optional<GameCameraPose> authored_eye_camera(const GameCameraPose& native,
    const TrackedPose& centre,const TrackedPose& eye,double units_per_metre=1) {
    if(!finite(centre.position) || !finite(eye.position) || !std::isfinite(units_per_metre) ||
       units_per_metre<=0 || units_per_metre>1000) return {};
    std::array<Vec3,3> basis{};
    for(unsigned i=0;i<3;++i) {
        basis[i]={native.basis[i*3],native.basis[i*3+1],native.basis[i*3+2]};
        if(!finite(basis[i]) || !std::isfinite(native.position[i])) return {};
        for(unsigned j=0;j<=i;++j) if(std::abs(dot(basis[i],basis[j])-(i==j?1.:0.))>.02) return {};
    }
    if(dot(cross(basis[0],basis[1]),basis[2])<.98) return {};
    try {
        auto inverse=normalized(centre.orientation);inverse={-inverse.x,-inverse.y,-inverse.z,inverse.w};
        const auto offset=rotated(inverse,eye.position-centre.position);
        if(length(offset)>.15) return {}; // Reject stale or mismatched stereo poses.
        const auto world=(basis[0]*(-offset.z)+basis[1]*(-offset.x)+basis[2]*offset.y)*units_per_metre;
        auto out=native;
        out.position={static_cast<float>(native.position[0]+world.x),
            static_cast<float>(native.position[1]+world.y),static_cast<float>(native.position[2]+world.z)};
        return out;
    } catch(const std::invalid_argument&) {return {};}
}

// Apply the same tangent multiplier to rendering AND submitted XR FOV.
// Changing only native hfov/vfov would distort the compositor's projection.
inline double authored_fov_angle(double radians,double tangent_scale=1.05) {
    if(!std::isfinite(radians) || std::abs(radians)>=1.5707963267948966 ||
       !std::isfinite(tangent_scale) || tangent_scale<1 || tangent_scale>1.1)
        throw std::invalid_argument("Invalid authored FOV policy");
    return std::atan(std::tan(radians)*tangent_scale);
}

// Prepared stereo views may span a native camera update during an authored
// action. Never combine one eye's orientation with an unrelated cached origin.
// Freeze the validated native first-person centre for the whole render pair.
class AuthoredCameraAnchor {
    std::uint64_t frame_{},epoch_{};
    std::uintptr_t player_{},camera_{};
    rig::Identity identity_{};
    std::optional<GameCameraPose> pose_;
public:
    void clear() noexcept {pose_.reset();}
    std::optional<GameCameraPose> get(std::uint64_t frame,std::uint64_t epoch,
        const NativePlayerCamera& owner,const GameCameraPose& input,const GameCameraPose& left,const GameCameraPose& right,
        double units_per_metre=1) {
        if(!epoch || !owner.player || !owner.camera || owner.camera_identity.generation==-1 ||
           !std::isfinite(units_per_metre) || units_per_metre<=0 || units_per_metre>1000) {
            clear();return {};
        }
        if(pose_ && frame==frame_ && epoch==epoch_ && player_==owner.player &&
           camera_==owner.camera && identity_==owner.camera_identity) return pose_;
        const TrackedPose zero{{},{0,0,0,1}};
        if(!authored_eye_camera(input,zero,zero,units_per_metre) ||
           !authored_eye_camera(left,zero,zero,units_per_metre) ||
           !authored_eye_camera(right,zero,zero,units_per_metre)) {clear();return {};}
        for(unsigned i=0;i<9;++i) if(std::abs(left.basis[i]-right.basis[i])>.002 ||
            std::abs(input.basis[i]-left.basis[i])>.002) {clear();return {};}
        const Vec3 input_position{input.position[0],input.position[1],input.position[2]};
        const Vec3 left_position{left.position[0],left.position[1],left.position[2]},
            right_position{right.position[0],right.position[1],right.position[2]};
        if(std::min(length(input_position-left_position),length(input_position-right_position))>.001*units_per_metre) {
            clear();return {};
        }
        const Vec3 delta{right.position[0]-left.position[0],right.position[1]-left.position[1],
            right.position[2]-left.position[2]};
        const Vec3 forward{left.basis[0],left.basis[1],left.basis[2]},up{left.basis[6],left.basis[7],left.basis[8]};
        if(length(delta)<.001*units_per_metre || length(delta)>.2*units_per_metre ||
           std::abs(dot(delta,forward))>.005*units_per_metre ||
           std::abs(dot(delta,up))>.005*units_per_metre) {clear();return {};}
        auto centre=input;
        for(unsigned i=0;i<3;++i) centre.position[i]=(left.position[i]+right.position[i])*.5f;
        pose_=centre;frame_=frame;epoch_=epoch;player_=owner.player;camera_=owner.camera;identity_=owner.camera_identity;
        return pose_;
    }
};

// Rendering owns the snapshot. Both eyes use the same gameplay heading and
// interpolated origin. Ordinary gameplay excludes animation camera rotation;
// authored camera modes retain their native movement and leveled yaw.
class BodyAnchor {
    std::uint64_t frame_{},epoch_{};
    std::uintptr_t player_{},camera_{};
    std::int32_t generation_{};
    std::optional<GameCameraPose> pose_;
public:
    void clear() noexcept {pose_.reset();}
    std::optional<GameCameraPose> get(std::uint64_t frame,std::uint64_t epoch,
        const NativePlayerCamera& state,const GameCameraPose& source,double physical_compensation=0) {
        if(!epoch || !state.player || !state.camera || !std::isfinite(physical_compensation)) {
            clear();return {};
        }
        if(pose_ && frame==frame_ && epoch==epoch_ && player_==state.player && camera_==state.camera && generation_==state.camera_identity.generation) return pose_;
        // Keep authored translation/yaw (climb, keyhole, takedown), replacing
        // pitch/roll with tracked head orientation for comfort. It remains a
        // native stereo scene; authored movement never becomes a flat panel.
        GameCameraPose body=source;body.basis=state.authored_view()?source.basis:state.entity_axis;
        std::array<Vec3,3> axes{};
        for(unsigned i=0;i<3;++i) {
            axes[i]={body.basis[i*3],body.basis[i*3+1],body.basis[i*3+2]};
            if(!finite(axes[i]) || !std::isfinite(body.position[i])) {clear();return {};}
            for(unsigned j=0;j<=i;++j) if(std::abs(dot(axes[i],axes[j])-(i==j?1.:0.))>.02) {clear();return {};}
        }
        if(dot(cross(axes[0],axes[1]),axes[2])<.98) {clear();return {};}
        body=level_heading(body);
        if(physical_compensation!=0) {
            const auto heading=std::atan2(body.basis[1],body.basis[0])-physical_compensation;
            body.basis={static_cast<float>(std::cos(heading)),static_cast<float>(std::sin(heading)),0,
                static_cast<float>(-std::sin(heading)),static_cast<float>(std::cos(heading)),0,0,0,1};
        }
        pose_=body;frame_=frame;epoch_=epoch;player_=state.player;camera_=state.camera;generation_=state.camera_identity.generation;
        return pose_;
    }
};
} // namespace d2vr
