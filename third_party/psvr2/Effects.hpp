#pragma once
#include <cstdint>
#include "pad_trigger_effect.h"
#include "Protocol.hpp"
enum class VRControllerType : uint8_t { Left=0, Right=1, Both=2 };
namespace dvrpsvr2 {
static_assert(sizeof(ScePadTriggerEffectCommand)==56);
inline ScePadTriggerEffectCommand makeCommand(Profile p) {
    ScePadTriggerEffectCommand c{};
    c.mode=SCE_PAD_TRIGGER_EFFECT_MODE_OFF;
    if(p==Profile::Gun || p==Profile::Crossbow) {
        c.mode=SCE_PAD_TRIGGER_EFFECT_MODE_WEAPON;
        // Pistol: later, firm wall approaching the 0.80 input gate; physical alignment needs headset calibration.
        c.commandData.weaponParam.startPosition=(p==Profile::Gun ? 5 : 2);
        c.commandData.weaponParam.endPosition=(p==Profile::Gun ? 7 : 3);
        c.commandData.weaponParam.strength=(p==Profile::Crossbow ? 4 : 5);
    } else if(p==Profile::Power) {
        c.mode=SCE_PAD_TRIGGER_EFFECT_MODE_VIBRATION;
        c.commandData.vibrationParam.position=2;
        c.commandData.vibrationParam.amplitude=1;
        c.commandData.vibrationParam.frequency=60;
    }
    return c;
}
}
