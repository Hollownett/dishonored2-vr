#include "../src/xr_floor_datum.hpp"
#include <cassert>
int main() {
    XrSpaceLocation floor{};floor.type=XR_TYPE_SPACE_LOCATION;floor.pose.orientation.w=1;floor.pose.position.y=-1.4f;
    assert(!d2vr::xr_floor_datum(floor));
    floor.locationFlags=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
    assert(std::abs(*d2vr::xr_floor_datum(floor)+1.4)<1e-6);
    floor.pose.orientation.x=.5f;assert(!d2vr::xr_floor_datum(floor));
    floor.pose.orientation={0,0,0,0};assert(!d2vr::xr_floor_datum(floor));
}
