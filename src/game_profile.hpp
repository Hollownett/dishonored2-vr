#pragma once
// One mod, several games. The Dishonored 2 build compiles src/ as is. Another game is a port target of tools/doto
// (tools/doto/targets/<name>.json): the generator writes a translated copy of src/ whose forced include
// src/game_target.hpp defines D2VR_GAME_<NAME> before anything else. Shared code asks these traits instead of testing
// for a game, so a new target adds one profile block here (and its reviewed rules in its targets folder).
// Per-game DATA (an avatar's mesh pins, resource names) stays next to the code that uses it, under the game's define.
#include <array>
#include <cstddef>
#include <cstdint>

namespace d2vr::game_traits {
#if defined(D2VR_GAME_DOTO)
inline constexpr const char* name = "Dishonored: Death of the Outsider";
inline constexpr const char* exe = "Dishonored_DO.exe";
// Present intervals between a view's Setup and the Present that shows its image. Death of the Outsider runs a view's
// Setup and its Begin inside one Present interval (live trace 2026-11-22: setup N / begin N on one thread).
inline constexpr std::uint64_t render_pipeline_depth = 0;
// An install-time consumer whose engine code changed in this build is skipped (the game keeps its own behaviour for it)
// instead of refusing the whole session: Block / Slash-rate / Choke-scope / Choke-offer, the door owner.
inline constexpr bool optional_native_consumers = true;
// The player avatar: Billie Lurk (six textured body surfaces) instead of Corvo/Emily (one hidden proxy surface).
inline constexpr bool billie_avatar = true;
// The standalone world model the mod spawns for a dropped/thrown/second-hand blade (no physics, no audio): DOTO has no
// dunwall/midnightassets; its bank mission's cinematic knife is Billie's blade as a plain entity (resource index 2026-11-22).
inline constexpr const char* sword_copy_def = "models/campaign/dlc01/bank/midnight_props/knife_midnight.def";
// The grenade Throw frame keeps the launch point 0x20 bytes past the velocity the mod's setter hook receives (D2). DOTO's Throw keeps
// it 0x10 bytes before (native_grenade_throw.hpp): the hand-throw rewrite stays off until that frame is confirmed.
inline constexpr bool grenade_spawn_follows_velocity = false;
// The slash perform entry's stolen prologue (native_slash_scene_rate.hpp): DOTO 0x8d7ed0 = mov rax,rsp (3); push rsi (1);
// sub rsp,0x90 (7) = 11 bytes, so MinHook copies 11. Its unwind info: prolog 11, 3 slots: UWOP_ALLOC_LARGE 0x90/8 at 11,
// UWOP_PUSH_NONVOL rsi at 4.
// The player blade, grip to tip along its length axis: ply_billie_knife01.md6 bounds x -0.209..0.62 (resource decl, 2026-11-22).
inline constexpr double player_blade_length = 0.62;
// The blade copy (sword_copy_def) is declared hidden (flags { hide = true }): a spawned copy must be shown explicitly.
inline constexpr bool sword_copy_spawns_hidden = true;
inline constexpr std::size_t slash_perform_steal = 11;
inline constexpr std::array<unsigned char,10> slash_perform_unwind{1,11,3,0, 11,0x01, 0x12,0x00, 4,0x60};
#else
inline constexpr const char* name = "Dishonored 2";
inline constexpr const char* exe = "Dishonored2.exe";
inline constexpr std::uint64_t render_pipeline_depth = 1;   // Setup N is consumed by Begin N+1
inline constexpr bool optional_native_consumers = false;    // every verified consumer is required
inline constexpr bool billie_avatar = false;
inline constexpr const char* sword_copy_def = "models/campaign/dunwall/midnightassets/sword_player_midnight.def";
inline constexpr bool grenade_spawn_follows_velocity = true;
// D2 0x86cbf0: mov rax,rsp (3); push rsi (1); sub rsp,0x60 (4) = 8 bytes; UWOP_ALLOC_SMALL 0x60 at 8, UWOP_PUSH_NONVOL rsi at 4.
inline constexpr double player_blade_length = 0.65;   // Corvo/Emily's sword (its bbox)
inline constexpr bool sword_copy_spawns_hidden = false;
inline constexpr std::size_t slash_perform_steal = 8;
inline constexpr std::array<unsigned char,8> slash_perform_unwind{1,8,2,0,8,0xb2,4,0x60};
#endif
} // namespace d2vr::game_traits
