// Adapter for the VR Stereo Hub AFW (modified): dvr::clarity::depth_scale(), the depth unit in the game's world units. The
// Dishonored 2 mod hands the rebuild linear depth already in metres and sets the world scale to 1 uu per metre, so 1.
#pragma once
namespace dvr::clarity {
inline float depth_scale() { return 1.0f; }
}
