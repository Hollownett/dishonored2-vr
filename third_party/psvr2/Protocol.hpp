#pragma once
#include <cstdint>

namespace dvrpsvr2 {
constexpr std::uint32_t magic = 0x32525644;
constexpr std::uint32_t leaseMs = 250;
enum class Profile : std::uint8_t { Off, Gun, Power, Crossbow };
enum class Status : long { Stopped, Starting, Ready, LoaderMissing, CapiMissing,
    ExportMissing, InitFailed, DriverInactive, CommandFailed, ResetFailed, BadProtocol };

inline std::uint64_t pack(std::uint32_t now, Profile profile, unsigned hand) {
    return (std::uint64_t(now) << 32) | (std::uint64_t(hand) << 8) | unsigned(profile);
}
struct Desired { Profile profile{Profile::Off}; unsigned hand{}; };
inline Desired decode(std::uint64_t word, std::uint32_t now, bool focused) {
    const auto profile = unsigned(word & 0xff);
    const auto hand = unsigned((word >> 8) & 0xff);
    if (!focused || hand != 0 || (word & 0xffff0000ULL) != 0
        || profile > unsigned(Profile::Crossbow)
        || std::uint32_t(now - std::uint32_t(word >> 32)) >= leaseMs)
        return {};
    return {Profile(profile), hand};
}
inline bool operator==(Desired a, Desired b) { return a.profile == b.profile && a.hand == b.hand; }
inline bool operator!=(Desired a, Desired b) { return !(a == b); }
}
