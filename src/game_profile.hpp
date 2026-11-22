#pragma once
// One mod, several games. The Dishonored 2 build compiles src/ as is. Another game is a port target of tools/doto
// (tools/doto/targets/<name>.json): the generator writes a translated copy of src/ whose forced include
// src/game_target.hpp defines D2VR_GAME_<NAME> before anything else. Shared code asks these traits instead of testing
// for a game, so a new target adds one profile block here (and its reviewed rules in its targets folder).
// Per-game DATA (an avatar's mesh pins, resource names) stays next to the code that uses it, under the game's define.
#include <cstdint>

namespace d2vr::game {
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
#else
inline constexpr const char* name = "Dishonored 2";
inline constexpr const char* exe = "Dishonored2.exe";
inline constexpr std::uint64_t render_pipeline_depth = 1;   // Setup N is consumed by Begin N+1
inline constexpr bool optional_native_consumers = false;    // every verified consumer is required
inline constexpr bool billie_avatar = false;
#endif
} // namespace d2vr::game
