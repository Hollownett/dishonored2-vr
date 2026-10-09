#pragma once
#include "finger_pose.hpp"
#include "hand_bind.hpp"
#include <istream>
#include <cstring>

namespace d2vr::rig {
struct NativeHandReference {
    FingerBinding fingers{};
    FingerChains chains{};
    FingerPalmHelpers palm_helpers{69,69};
    std::array<std::array<float,12>,69> inverse{};
};
inline NativeHandReference native_hand_reference(const std::array<Transform,69>& open,
    const std::array<Transform,69>& closed,const ArmHierarchy& parents,const FingerChains& chains,
    const std::array<std::array<float,12>,69>& inverse,FingerPalmHelpers palm_helpers={69,69}) {
    for(const auto& rows:inverse) (void)bind_transform(rows);
    return {native_finger_binding(open,closed,parents,chains,palm_helpers),chains,palm_helpers,inverse};
}
// Private references contain exact-build native palettes. Never distribute them
// as generic poses or reuse them on another skeleton/inverse-bind identity.
inline std::array<NativeHandReference,2> read_native_hand_references(std::istream& stream) {
    static_assert(sizeof(Transform)==64 && sizeof(ArmHierarchy)==138 && sizeof(FingerChains)==60 && sizeof(FingerPalmHelpers)==8);
    const auto read=[&](void* data,std::size_t bytes) {
        if(!stream.read(static_cast<char*>(data),static_cast<std::streamsize>(bytes)))
            throw std::invalid_argument("Truncated native hand references");
    };
    std::array<char,8> magic{};read(magic.data(),magic.size());
    std::array<char,64> game_hash{};read(game_hash.data(),game_hash.size());
    constexpr char pinned[]="b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180";
    if(std::memcmp(magic.data(),"D2VRHP01",8) || std::memcmp(game_hash.data(),pinned,64))
        throw std::invalid_argument("Native hand references belong to another executable");
    std::array<NativeHandReference,2> result{};
    for(unsigned side=0;side<2;++side) {
        std::uint32_t recorded_side{};read(&recorded_side,sizeof(recorded_side));
        if(recorded_side!=side) throw std::invalid_argument("Native hand reference side differs");
        ArmHierarchy parents{};FingerChains chains{};FingerPalmHelpers helpers{};
        std::array<std::array<float,12>,69> inverse{};
        std::array<Transform,69> open{},closed{};
        read(parents.data(),sizeof(parents));read(chains.data(),sizeof(chains));read(helpers.data(),sizeof(helpers));
        read(inverse.data(),sizeof(inverse));read(open.data(),sizeof(open));read(closed.data(),sizeof(closed));
        result[side]=native_hand_reference(open,closed,parents,chains,inverse,helpers);
    }
    if(stream.peek()!=std::char_traits<char>::eof()) throw std::invalid_argument("Unexpected native hand reference trailing data");
    return result;
}
} // namespace d2vr::rig
