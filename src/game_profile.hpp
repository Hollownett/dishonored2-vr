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
// The note-hand art ids the player's arms can be (note_hand_mesh.hpp identities; launcher/NoteHandMesh.cs writes note_hand_billie_{left,right}.bin
// into this game's home): Billie (2) only. Her void right arm (player_rightarm_void_billie.md6) has no art: bind() refuses it, the note stays a quad.
inline constexpr unsigned note_hand_first_character = 2, note_hand_character_end = 3;
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
// The player's ranged weapon is Billie's Voltaic Gun (arkCpntWeaponGaussGun), mounted on her left forearm: the hand-fired copy kind
// "crossbow" of NativeRangedFire / NativeWeaponHands is that gun, and the "pistol" kind has no player weapon (tools/doto/reviews/combat-port.md
// sections 0-2). Its RTTI vtables (component, declaration) are this build's own values: offsets.json keeps them unchanged.
inline constexpr bool ranged_player_weapon_is_voltaic = true;
// A native weapon is drawn from whichever arm joint the game binds it to (the Voltaic Gun's tags move it between arm joints;
// user, DOTO headset v145734: the gun moved with the hand and the hand stuck in a fist whenever its tag was not the helper2 one).
inline constexpr bool bind_any_arm_joint = true;
inline constexpr std::uintptr_t voltaic_comp_vtable = 0x1c45768, voltaic_decl_vtable = 0x1c45618;
// v145741 (live, lab9: the inventory's GaussGun record 0x40138 -> its component's +0x10 owner): the Voltaic Gun's entity is a plain
// idEntity (vtable 0x1d0dd88), not the weapon entity class the crossbow uses; owner +0xb4 is the player, id +0xb0 matches.
inline constexpr std::uintptr_t voltaic_entity_vtable = 0x1d0dd88;
// v145744: idEntity spawn id and its flags byte (bit 0 hidden, bit 1 not presented), for new code that must not depend on a port rule
inline constexpr std::uintptr_t entity_id_offset = 0xb0, entity_flags_offset = 0xd8;
inline constexpr std::uintptr_t entity_bind_offset = 0x368;   // v145747: the entity's arkCpntReferenceFrame (D2 +0x358; combat-port.md 8.2)
// v145745: the Gauss charge (static RE, release 0x8d0bc0 / fire 0x3c1d70 / rebuild 0x68cf80): overcharged byte (charged = voltaic_configure_from),
// the component flags byte and its charged-mode bit (the charge upgrade owned for the ammo in use)
inline constexpr std::uintptr_t voltaic_shot_overcharged = 0xe1, voltaic_charge_flags = 0xc8; inline constexpr unsigned voltaic_charge_bit = 4;
inline constexpr bool displaced_sword_stows = true;   // v145747: a wheel item that takes the sword's arm puts the sword away at once
// The second gun the other forearm shows in dual wield: the gun's own entity def, spawned by name (no owner: its components never start).
inline constexpr const char* voltaic_copy_def = "models/weapons/dlc01/player/billie/gaussgun01/ply_billie_gaussgun01.def";
// The Gauss fire (combat-port.md section 2): before its create it copies byte comp+0xe0 into the create/sound configure byte comp+0xb0;
// the ammo attribute index at comp+0x178 marks the projectile (info +0x18d |= 4) when the projectile entity's decl (+0x2f8) is the weapon
// decl's +0x310; info +0x1a0 = decl +0x308 when the shot's projectile def is decl +0x300. Speed is the constant 1.
inline constexpr std::uintptr_t voltaic_configure = 0xb0, voltaic_configure_from = 0xe0, voltaic_mark_attribute = 0x178,
    voltaic_entity_decl = 0x2f8, voltaic_mark_decl = 0x310, voltaic_info_def = 0x300, voltaic_info_value = 0x308, voltaic_info_field = 0x1a0;
#else
inline constexpr const char* name = "Dishonored 2";
inline constexpr const char* exe = "Dishonored2.exe";
inline constexpr std::uint64_t render_pipeline_depth = 1;   // Setup N is consumed by Begin N+1
inline constexpr bool optional_native_consumers = false;    // every verified consumer is required
inline constexpr bool billie_avatar = false;
inline constexpr unsigned note_hand_first_character = 0, note_hand_character_end = 2;   // Corvo (0), Emily (1)
inline constexpr const char* sword_copy_def = "models/campaign/dunwall/midnightassets/sword_player_midnight.def";
inline constexpr bool grenade_spawn_follows_velocity = true;
// D2 0x86cbf0: mov rax,rsp (3); push rsi (1); sub rsp,0x60 (4) = 8 bytes; UWOP_ALLOC_SMALL 0x60 at 8, UWOP_PUSH_NONVOL rsi at 4.
inline constexpr double player_blade_length = 0.65;   // Corvo/Emily's sword (its bbox)
inline constexpr bool sword_copy_spawns_hidden = false;
inline constexpr std::size_t slash_perform_steal = 8;
inline constexpr std::array<unsigned char,8> slash_perform_unwind{1,8,2,0,8,0xb2,4,0x60};
inline constexpr bool ranged_player_weapon_is_voltaic = false;   // the pistol and the crossbow (arkCpntWeaponGun / arkCpntWeaponCrossbow)
inline constexpr bool bind_any_arm_joint = false;   // D2: hand attachment 59 (and a forearm helper) only, as since v1319
inline constexpr std::uintptr_t voltaic_comp_vtable = 0, voltaic_decl_vtable = 0;
inline constexpr std::uintptr_t voltaic_entity_vtable = 0;
inline constexpr std::uintptr_t entity_id_offset = 0xa8, entity_flags_offset = 0xd0;
inline constexpr std::uintptr_t entity_bind_offset = 0x358;
inline constexpr std::uintptr_t voltaic_shot_overcharged = 0, voltaic_charge_flags = 0; inline constexpr unsigned voltaic_charge_bit = 0;
inline constexpr bool displaced_sword_stows = false;
inline constexpr const char* voltaic_copy_def = nullptr;
inline constexpr std::uintptr_t voltaic_configure = 0, voltaic_configure_from = 0, voltaic_mark_attribute = 0,
    voltaic_entity_decl = 0, voltaic_mark_decl = 0, voltaic_info_def = 0, voltaic_info_value = 0, voltaic_info_field = 0;
#endif
} // namespace d2vr::game_traits
