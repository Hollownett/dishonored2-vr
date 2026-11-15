#pragma once
#include "Protocol.hpp"
#include <string_view>
#include <string>
namespace dvrpsvr2 {
inline bool Headset(std::string_view runtime,std::string_view system) {
    return runtime.find("SteamVR")!=runtime.npos &&
      (system.find("PlayStation VR2")!=system.npos || system.find("PSVR2")!=system.npos ||
       system.find("PS VR2")!=system.npos || system.find("playstation_vr2")!=system.npos);
}
inline bool PowerClass(std::string_view name) {
    for(auto power:{"Bendtime","Blink","DarkVision","DevouringSwarm","Doppelganger","Grapple","Mesmerize","PhantasmalLink","Possession","ShadowWalk","Windblast"}) {
        // Only a concrete power component, never an arbitrary unknown item.
        if(name.starts_with(".?AVarkCpntPower") && name.substr(16)==std::string(power)+"@@") return true;
    }
    return false;
}
inline bool WeaponFire(Profile item, float pull, bool held) {
    if(item!=Profile::Gun && item!=Profile::Crossbow) return true;
    return pull >= (held ? 0.70f : 0.80f);
}
inline Profile Choose(Profile item,bool play,bool focused,bool tracked,bool blocked,float pull) {
    if(!play || !focused || !tracked || blocked) return Profile::Off;
    if(item==Profile::Power && pull<0.12f) return Profile::Off;
    return item;
}
}
