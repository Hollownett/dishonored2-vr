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
    return failures?1:0;
}
