#pragma once
#include <cstdint>

namespace dvrpsvr2 {
constexpr std::uint32_t magic = 0x32525644;
constexpr std::uint32_t leaseMs = 250;
// Protocol version 2: one profile per controller (left and right), so either hand can hold a gun, the crossbow or a power.
constexpr long protocolVersion = 2;
enum class Profile : std::uint8_t { Off, Gun, Power, Crossbow };
enum class Status : long { Stopped, Starting, Ready, LoaderMissing, CapiMissing,
    ExportMissing, InitFailed, DriverInactive, CommandFailed, ResetFailed, BadProtocol };

// Word: bits 32-63 the publish tick (ms, the lease), bits 8-15 the right controller's profile, bits 0-7 the left's.
inline std::uint64_t pack(std::uint32_t now, Profile left, Profile right) {
    return (std::uint64_t(now) << 32) | (std::uint64_t(unsigned(right)) << 8) | unsigned(left);
}
struct Desired { Profile left{Profile::Off}, right{Profile::Off}; };
inline bool valid(unsigned p) { return p <= unsigned(Profile::Crossbow); }
// A stale lease, an unfocused game or a malformed word turns both triggers off.
inline Desired decode(std::uint64_t word, std::uint32_t now, bool focused) {
    const auto left = unsigned(word & 0xff), right = unsigned((word >> 8) & 0xff);
    if (!focused || (word & 0xffff0000ULL) != 0 || !valid(left) || !valid(right)
        || std::uint32_t(now - std::uint32_t(word >> 32)) >= leaseMs)
        return {};
    return {Profile(left), Profile(right)};
}
inline bool operator==(Desired a, Desired b) { return a.left == b.left && a.right == b.right; }
inline bool operator!=(Desired a, Desired b) { return !(a == b); }
}
