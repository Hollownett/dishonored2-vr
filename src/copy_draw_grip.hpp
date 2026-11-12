#pragma once
#include "weapon_draw_place.hpp"

namespace d2vr::weapon_draw {
// v1221 (headset v1216: "the crossbow rendered wrong in the arm" after a hand-to-hand toss; the left crossbow copy's bound frame sat 119 deg /
// 64 mm off the copy's own engine frame, both in v1209 and v1216). A copy's offset is relative to the CONTROLLER grip (the copy service places
// it there). A hand holding a copy plays its kind's gun clip, and that authored socket rotates the drawn joint 59 away from the controller
// (v1104), so joint 59 * inverse(grip_to_attachment) is not the grip under a clip and the copy was bound rotated by the clip. The offset is
// re-expressed against the drawn joint 59 through this render's own image grip: G59 = inverse(J59) * grip * offset. The bound frame
// J59 * G59 then sits where the copy service put the copy relative to the grip, and still follows the drawn joint (its clamp) between learns.
// Without a clip (J59 = grip * grip_to_attachment) it is the old binding exactly. A singular joint gives a zero matrix (valid_grip fails).
inline Mat copy_joint_grip(const Mat& joint59,const Mat& grip,const Mat& offset) noexcept {
    Mat inv{};if(!inverse(joint59,inv)) return Mat{};
    return mul(mul(inv,grip),offset);
}
}
