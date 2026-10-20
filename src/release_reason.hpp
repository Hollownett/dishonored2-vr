#pragma once
// Why a hold ended. Lives in its own header so the physical grab motor and the ragdoll safety envelope share it without an include cycle.
namespace d2vr {
enum class ReleaseReason : unsigned {none,open_fist,lag,lifecycle,runaway,fault,state_changed,refused};
}
