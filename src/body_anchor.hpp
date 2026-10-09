#pragma once
#include "native_player_camera.hpp"
#include "tracked_pose.hpp"

namespace d2vr {
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
