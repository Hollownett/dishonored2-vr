#include "../src/gun_equip_name.hpp"
#include <cstdio>
static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}
int main() {
    using namespace d2vr;
    expect(is_gun_equip_name("Gun"),"Gun");
    expect(is_gun_equip_name("gun_emily"),"decl name");
    expect(is_gun_equip_name("GunEquip") && is_gun_equip_name("Pistol_Draw") && is_gun_equip_name("pistol"),"swap spellings");
    expect(is_gun_equip_name("Crossbow") && is_gun_equip_name("CrossBow_Emily") && is_gun_equip_name("crossbow"),"crossbow spellings");
    expect(!is_gun_equip_name("Empty") && !is_gun_equip_name("") && !is_gun_equip_name("Heart") && !is_gun_equip_name("Grenade") && !is_gun_equip_name("Mine"),"non guns");
    expect(!is_gun_equip_name("Gu") && !is_gun_equip_name("weapon_mine_spring"),"short or unrelated");
    expect(is_gadget_equip_name("Grenade") && is_gadget_equip_name("StickyGrenade") && is_gadget_equip_name("StunMine") && is_gadget_equip_name("SpringRazor"),"gadgets");
    expect(!is_gadget_equip_name("Gun") && !is_gadget_equip_name("Crossbow") && !is_gadget_equip_name("Blink") && !is_gadget_equip_name("Empty"),"not gadgets");
    expect(is_barrel_equip_name("Gun") && is_barrel_equip_name("Crossbow"),"v612 barrel equip routing");
    expect(!is_barrel_equip_name("GunEquip") && !is_barrel_equip_name("Power_Pistol") && !is_barrel_equip_name("Empty"),"broad decl names never reroute a power");
    expect(is_power_equip_name("Blink") && is_power_equip_name("FarReach") && is_power_equip_name("DarkVision"),"powers draw the player's fingers");
    expect(!is_power_equip_name("Gun") && !is_power_equip_name("Crossbow") && !is_power_equip_name("Grenade") && !is_power_equip_name("StunMine") && !is_power_equip_name("Heart") && !is_power_equip_name("Empty") && !is_power_equip_name(""),"held models keep the authored grip");
    AimEquipTracker tracker;
    expect(tracker.update(1,10,"Gun") && tracker.revision()==1,"first weapon");
    expect(!tracker.update(1,10,"Gun") && tracker.revision()==1,"same equip keeps filter");
    expect(tracker.update(2,20,"Crossbow") && tracker.revision()==2,"gun swap resets");
    expect(tracker.update(3,0,"Blink") && tracker.revision()==3,"power swap resets");
    expect(tracker.update(3,0,"FarReach") && tracker.revision()==4,"same slot power swap resets");
    expect(tracker.update(0,0,"") && tracker.revision()==5,"holster resets");
    return failures?1:0;
}
