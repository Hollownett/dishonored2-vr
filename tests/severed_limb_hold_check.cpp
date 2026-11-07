// v1087 severed-limb hold lab (user, v1086 headset: "when I grab a leg I still have some kind of sphere and hold it at a distance ... it must work
// exactly like bottles and glasses, the same for arms and heads; run a lab on how it should be attached to the hand").
// A severed leg (thigh, calf, foot), arm (upper arm, forearm, hand) and head lie on the floor. Their bodies are the REAL Havok hulls of a guard's
// ragdoll from the user's v1086 run (artifacts/prop-hulls.jsonl, read live by prop_hull; bone order 4-7, 11-13; each body's origin is its joint).
// Every step is the game's own code: native_severed_limb_body reads the limb from fake engine memory (the body whose surface is nearest the palm),
// decide_grip_alone decides the press (the game's Use target is the same limb, kind loot), rig::prop_seat seats it, PhysicalGrab binds it and its
// sticking law (corpse_motor, a prop) holds it while the rest of the limb hangs on the held body. The player stands, runs at 5 m/s, then snap-turns
// 45 degrees. v1086 for comparison: the press went to the game's Use, and had it been grabbed it was held where it was taken (no seat).
// v1088 (user, v1087 headset: "not fixed ... make it hold like bottles, the hand in full contact with the limb"): the press is decided as the game
// decides it now (native_limb: never the game's carry; v1087 had no palm pick while the limb was the Use target, so Use took it), the pick reaches
// limb_reach_m, and the seat puts the part's real surface on the palm skin (rig::surface_onto_skin). Presses 20-40 cm short are taken too.
#include "../src/native_rigid_body.hpp"
#include "../src/finger_sphere.hpp"
#include "../src/grip_decision.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>
using namespace d2vr;
// The live hulls (user v1086 run, artifacts/prop-hulls.jsonl lines 28-37: one guard ragdoll in bone order), own frame, metres, 0.1 mm.
static const float hull_head[][3]={{0.1069f,-0.0525f,0.0000f},{0.1093f,0.0809f,-0.0060f},{0.1226f,0.0172f,0.0014f},{0.1239f,0.0024f,-0.0003f},{-0.0478f,0.0774f,-0.0583f},{-0.0471f,0.0748f,0.0587f},{-0.0367f,0.1218f,-0.0419f},{-0.0364f,0.1193f,-0.0446f},{-0.0363f,0.1200f,0.0442f},{-0.0351f,0.1169f,0.0492f},{-0.0327f,0.0078f,-0.0503f},{-0.0324f,0.0024f,0.0479f},{-0.0322f,0.0056f,0.0502f},{-0.0240f,0.1434f,0.0090f},{-0.0210f,0.1435f,0.0139f},{-0.0055f,0.1421f,-0.0298f},{-0.0005f,0.1484f,-0.0177f},{0.0107f,0.1206f,0.0584f},{0.0130f,0.0735f,0.0725f},{0.0130f,0.1209f,-0.0578f},{0.0131f,0.1209f,-0.0579f},{0.0141f,0.1181f,0.0599f},{0.0145f,0.1479f,0.0187f},{0.0177f,0.0856f,-0.0738f},{0.0202f,-0.0500f,-0.0096f},{0.0203f,0.0835f,-0.0741f},{0.0210f,0.1413f,0.0307f},{0.0263f,0.0589f,0.0731f},{0.0305f,-0.0532f,-0.0247f},{0.0328f,-0.0506f,0.0294f},{0.0332f,0.0001f,0.0645f},{0.0343f,-0.0077f,-0.0624f},{0.0360f,-0.0046f,-0.0633f},{0.0384f,-0.0048f,0.0625f},{0.0440f,0.1409f,-0.0134f},{0.0465f,-0.0536f,0.0311f},{0.0574f,0.1351f,-0.0179f},{0.0607f,-0.0517f,-0.0363f},{0.0607f,-0.0519f,0.0362f},{0.0622f,0.1176f,-0.0449f},{0.0731f,0.1098f,0.0466f},{0.0776f,0.1033f,0.0472f},{0.0807f,0.1186f,-0.0013f},{0.0808f,0.0802f,-0.0579f},{0.0828f,0.0934f,-0.0507f},{0.0843f,0.0590f,0.0577f},{0.0880f,-0.0558f,-0.0269f},{0.0881f,-0.0647f,0.0000f},{0.0886f,-0.0554f,0.0269f},{0.0910f,-0.0538f,-0.0269f},{0.0910f,-0.0538f,0.0269f},{0.0918f,-0.0018f,0.0488f},{0.0946f,-0.0097f,0.0446f},{0.0953f,-0.0016f,-0.0478f},{0.0974f,0.0049f,-0.0478f},{0.1054f,0.0883f,-0.0111f},{-0.0481f,0.0770f,0.0578f},{-0.0573f,0.0104f,-0.0002f},{-0.0575f,0.1227f,0.0000f},{-0.0761f,0.0800f,0.0000f}};
static const float hull_upperarm[][3]={{0.0414f,-0.0271f,0.0402f},{0.0430f,-0.0562f,-0.0004f},{0.0511f,0.2221f,0.0003f},{0.0582f,-0.0271f,-0.0004f},{-0.0397f,-0.0271f,-0.0409f},{-0.0350f,0.2221f,-0.0354f},{-0.0350f,0.2221f,0.0360f},{-0.0345f,0.2525f,0.0003f},{-0.0268f,-0.0619f,-0.0280f},{-0.0268f,-0.0619f,0.0273f},{-0.0261f,0.2473f,-0.0264f},{-0.0261f,0.2473f,0.0270f},{-0.0081f,0.2508f,-0.0321f},{-0.0081f,0.2508f,0.0327f},{0.0004f,0.2624f,0.0003f},{0.0006f,0.2221f,-0.0502f},{0.0006f,0.2221f,0.0508f},{0.0006f,0.2472f,-0.0376f},{0.0006f,0.2472f,0.0382f},{0.0008f,-0.0730f,-0.0004f},{0.0008f,-0.0562f,-0.0425f},{0.0008f,-0.0562f,0.0418f},{0.0008f,-0.0271f,-0.0577f},{0.0008f,-0.0271f,0.0570f},{0.0274f,0.2472f,-0.0265f},{0.0274f,0.2472f,0.0271f},{0.0285f,-0.0619f,-0.0280f},{0.0285f,-0.0619f,0.0273f},{0.0298f,0.2508f,-0.0164f},{0.0298f,0.2508f,0.0170f},{0.0364f,0.2221f,-0.0354f},{0.0364f,0.2221f,0.0360f},{0.0384f,0.2473f,0.0003f},{0.0414f,-0.0271f,-0.0409f},{-0.0397f,-0.0271f,0.0402f},{-0.0413f,-0.0562f,-0.0004f},{-0.0498f,0.2221f,0.0003f},{-0.0565f,-0.0271f,-0.0004f},{-0.0565f,-0.0271f,-0.0004f},{-0.0565f,-0.0271f,-0.0004f}};
static const float hull_forearm[][3]={{0.0363f,-0.0001f,0.0342f},{0.0376f,-0.0246f,-0.0001f},{0.0429f,0.2371f,-0.0001f},{0.0505f,-0.0001f,-0.0001f},{-0.0324f,0.0000f,-0.0345f},{-0.0319f,0.2586f,-0.0001f},{-0.0302f,0.2372f,0.0301f},{-0.0301f,0.2372f,-0.0304f},{-0.0215f,-0.0295f,-0.0235f},{-0.0215f,-0.0295f,0.0233f},{-0.0210f,0.2630f,-0.0212f},{-0.0210f,0.2630f,0.0209f},{0.0001f,0.2586f,0.0319f},{0.0001f,0.2372f,0.0426f},{0.0001f,0.2586f,-0.0322f},{0.0001f,0.2714f,-0.0001f},{0.0001f,0.2372f,-0.0429f},{0.0019f,0.0000f,-0.0487f},{0.0019f,0.0000f,0.0485f},{0.0019f,-0.0389f,-0.0001f},{0.0019f,-0.0246f,-0.0357f},{0.0019f,-0.0246f,0.0355f},{0.0212f,0.2630f,-0.0212f},{0.0212f,0.2630f,0.0209f},{0.0253f,-0.0295f,-0.0235f},{0.0253f,-0.0295f,0.0233f},{0.0304f,0.2371f,-0.0304f},{0.0304f,0.2371f,0.0301f},{0.0321f,0.2586f,-0.0001f},{0.0362f,-0.0001f,-0.0345f},{-0.0324f,0.0000f,0.0342f},{-0.0337f,-0.0246f,-0.0001f},{-0.0427f,0.2372f,-0.0001f},{-0.0466f,0.0000f,-0.0001f},{-0.0466f,0.0000f,-0.0001f},{-0.0466f,0.0000f,-0.0001f}};
static const float hull_hand[][3]={{0.0353f,0.1177f,0.0131f},{0.0360f,0.1064f,-0.0505f},{0.0422f,0.0081f,0.0002f},{0.0505f,0.1201f,-0.0267f},{-0.0332f,0.1155f,0.0135f},{-0.0312f,0.0095f,0.0309f},{-0.0303f,-0.0171f,-0.0006f},{-0.0293f,-0.0179f,0.0008f},{-0.0285f,-0.0096f,-0.0232f},{-0.0259f,0.2075f,-0.0570f},{-0.0232f,-0.0071f,-0.0346f},{-0.0227f,-0.0138f,0.0222f},{-0.0184f,0.2075f,-0.0671f},{-0.0150f,0.1995f,-0.0720f},{-0.0149f,0.2143f,-0.0499f},{-0.0119f,0.2115f,-0.0438f},{-0.0081f,-0.0097f,-0.0395f},{-0.0061f,0.2096f,-0.0404f},{-0.0016f,0.2170f,-0.0592f},{-0.0005f,-0.0181f,0.0291f},{-0.0005f,0.0075f,0.0426f},{-0.0005f,-0.0261f,0.0004f},{-0.0003f,-0.0134f,-0.0356f},{0.0010f,0.1150f,0.0263f},{0.0015f,0.0940f,-0.0608f},{0.0216f,-0.0138f,0.0222f},{0.0255f,-0.0092f,-0.0267f},{0.0281f,0.2061f,-0.0634f},{0.0284f,-0.0180f,0.0005f},{0.0285f,0.2070f,-0.0628f},{0.0294f,0.2101f,-0.0493f},{0.0296f,0.2104f,-0.0501f},{0.0302f,0.0095f,0.0309f},{0.0322f,0.1262f,-0.0567f},{0.0329f,0.2074f,-0.0556f},{0.0336f,0.1231f,0.0130f},{-0.0341f,0.1074f,-0.0505f},{-0.0432f,0.0080f,0.0002f},{-0.0453f,0.1266f,-0.0345f},{-0.0477f,0.1063f,-0.0229f}};
static const float hull_thigh[][3]={{0.0997f,0.0857f,0.0566f},{0.1014f,0.0463f,0.0009f},{0.1016f,0.0886f,-0.0470f},{0.1238f,0.0831f,0.0004f},{-0.0777f,0.0763f,0.0551f},{-0.0745f,0.4172f,0.0450f},{-0.0741f,0.4181f,-0.0446f},{-0.0738f,0.4466f,0.0000f},{-0.0738f,0.4148f,0.0466f},{-0.0736f,0.4151f,-0.0464f},{-0.0693f,0.0679f,-0.0485f},{-0.0679f,0.0667f,-0.0488f},{-0.0675f,0.0666f,0.0580f},{-0.0643f,0.0287f,-0.0125f},{-0.0566f,0.4550f,-0.0305f},{-0.0566f,0.4550f,0.0305f},{-0.0511f,0.4576f,-0.0309f},{-0.0511f,0.4576f,0.0309f},{-0.0508f,0.0343f,-0.0352f},{-0.0496f,0.0349f,0.0449f},{-0.0230f,0.0321f,0.0484f},{-0.0105f,0.4764f,0.0000f},{-0.0094f,0.4533f,-0.0480f},{-0.0094f,0.4533f,0.0480f},{-0.0065f,0.4266f,-0.0637f},{-0.0065f,0.4266f,0.0637f},{-0.0056f,0.4259f,-0.0640f},{-0.0055f,0.4258f,0.0640f},{0.0205f,0.0788f,-0.0716f},{0.0205f,0.0785f,0.0806f},{0.0244f,0.0753f,0.0797f},{0.0244f,0.0755f,-0.0706f},{0.0255f,0.0406f,0.0618f},{0.0257f,0.0333f,-0.0490f},{0.0280f,0.0329f,-0.0480f},{0.0284f,0.0193f,-0.0001f},{0.0303f,0.4654f,-0.0306f},{0.0304f,0.4654f,0.0306f},{0.0373f,0.4636f,0.0300f},{0.0373f,0.4635f,-0.0300f},{0.0551f,0.4588f,0.0000f},{0.0597f,0.4308f,0.0444f},{0.0599f,0.4306f,-0.0442f},{0.0633f,0.0387f,0.0481f},{0.0782f,0.4325f,0.0000f},{0.0793f,0.4303f,-0.0019f},{0.0802f,0.0446f,0.0453f},{0.0803f,0.0442f,-0.0359f},{0.0904f,0.0403f,-0.0127f},{0.0957f,0.0809f,-0.0491f},{0.0961f,0.0808f,0.0581f},{-0.0777f,0.0321f,0.0004f},{-0.0892f,0.4222f,0.0000f},{-0.0927f,0.4109f,0.0089f},{-0.1051f,0.0650f,0.0009f},{-0.1051f,0.0650f,0.0009f}};
static const float hull_calf[][3]={{0.0582f,0.1646f,-0.0020f},{0.0586f,0.1599f,-0.0013f},{0.0635f,0.0061f,-0.0041f},{0.0652f,0.0126f,0.0034f},{-0.0511f,-0.0152f,-0.0038f},{-0.0466f,0.0097f,0.0648f},{-0.0464f,0.0107f,0.0655f},{-0.0461f,0.0083f,-0.0697f},{-0.0459f,0.0095f,-0.0704f},{-0.0347f,0.3020f,0.0517f},{-0.0345f,-0.0205f,0.0456f},{-0.0343f,-0.0206f,-0.0517f},{-0.0331f,-0.0183f,0.0553f},{-0.0331f,-0.0185f,-0.0605f},{-0.0320f,0.4122f,0.0007f},{-0.0248f,0.4019f,0.0488f},{-0.0220f,0.4156f,-0.0223f},{-0.0206f,0.4162f,-0.0227f},{-0.0202f,0.4199f,0.0358f},{-0.0190f,0.4179f,0.0436f},{0.0000f,-0.0316f,-0.0051f},{0.0000f,-0.0154f,-0.0737f},{0.0000f,-0.0153f,0.0686f},{0.0000f,0.0064f,0.0797f},{0.0000f,0.0108f,-0.0872f},{0.0000f,0.1435f,0.0882f},{0.0000f,0.4125f,-0.0308f},{0.0000f,0.4155f,0.0539f},{0.0000f,0.4274f,0.0014f},{0.0190f,0.4179f,0.0436f},{0.0202f,0.4199f,0.0358f},{0.0206f,0.4162f,-0.0227f},{0.0220f,0.4156f,-0.0223f},{0.0248f,0.4019f,0.0488f},{0.0320f,0.4122f,0.0007f},{0.0331f,-0.0185f,-0.0605f},{0.0331f,-0.0183f,0.0553f},{0.0343f,-0.0206f,-0.0517f},{0.0345f,-0.0205f,0.0456f},{0.0347f,0.3020f,0.0517f},{0.0459f,0.0095f,-0.0704f},{0.0461f,0.0083f,-0.0697f},{0.0464f,0.0107f,0.0655f},{0.0466f,0.0097f,0.0648f},{0.0511f,-0.0152f,-0.0038f},{-0.0582f,0.1646f,-0.0020f},{-0.0586f,0.1599f,-0.0013f},{-0.0635f,0.0061f,-0.0041f},{-0.0652f,0.0126f,0.0034f},{-0.0652f,0.0126f,0.0034f},{-0.0652f,0.0126f,0.0034f},{-0.0652f,0.0126f,0.0034f}};
static const float hull_foot[][3]={{0.0940f,0.2554f,-0.0469f},{0.0956f,0.2608f,-0.0466f},{0.0981f,0.2727f,-0.0024f},{0.0985f,0.2718f,-0.0041f},{-0.1098f,0.0655f,0.0337f},{-0.0973f,0.0336f,0.0365f},{-0.0965f,0.0340f,-0.0362f},{-0.0894f,0.0890f,0.0391f},{-0.0888f,0.0288f,-0.0384f},{-0.0821f,0.0230f,0.0402f},{-0.0426f,0.0966f,0.0509f},{-0.0314f,-0.0498f,-0.0004f},{-0.0210f,-0.0354f,0.0331f},{-0.0199f,-0.0341f,-0.0357f},{0.0169f,0.0166f,0.0408f},{0.0257f,0.0286f,0.0264f},{0.0263f,0.0291f,-0.0381f},{0.0446f,0.2433f,0.0582f},{0.0508f,0.1925f,0.0655f},{0.0597f,0.2261f,0.0642f},{0.0756f,0.2790f,-0.0404f},{0.0815f,0.2858f,-0.0037f},{-0.1103f,0.0648f,-0.0297f},{-0.1108f,0.0200f,0.0001f},{-0.1120f,0.0219f,-0.0010f},{-0.1221f,0.0513f,-0.0005f},{-0.1221f,0.0513f,-0.0005f},{-0.1221f,0.0513f,-0.0005f}};
namespace {
struct Part {const char* name;const float (*v)[3];unsigned n;double kg;};
std::map<std::uintptr_t,unsigned char> bytes;
template<class T> void put(std::uintptr_t at,const T& value) {const auto* p=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i) bytes[at+i]=p[i];}
bool read(std::uintptr_t at,void* out,std::size_t size) {auto* p=static_cast<unsigned char*>(out);for(std::size_t i=0;i<size;++i) {const auto it=bytes.find(at+i);if(it==bytes.end()) return false;p[i]=it->second;}return true;}
constexpr std::uintptr_t kBase=0x140000000,kGame=0x1000000,kWorld=0x5000000,kPool=0x6000000;
WorldTrackedPose pose_of(Vec3 p,Vec3 x,Vec3 y) {WorldTrackedPose o{};o.position=p;o.basis={unit(x),unit(y),cross(unit(x),unit(y))};return o;}
Vec3 local(const WorldTrackedPose& f,Vec3 world_point) {const Vec3 d=world_point-f.position;return {dot(d,f.basis[0]),dot(d,f.basis[1]),dot(d,f.basis[2])};}
Vec3 spin(Vec3 v,Vec3 k,double a) {return v*std::cos(a)+cross(k,v)*std::sin(a)+k*(dot(k,v)*(1-std::cos(a)));}
// A severed limb at registry index `slot`: entity, skinned model, ragdoll system with one body per part (a chain along `along` from `root`).
struct Limb {std::uintptr_t entity{};std::vector<WorldTrackedPose> bodies;std::vector<const Part*> parts;};
Limb make_limb(unsigned slot,const std::vector<const Part*>& parts,Vec3 root,Vec3 along,Vec3 up,const std::vector<double>& joints) {
    static bool rtti=false;const std::uintptr_t vtable=0x9010000,locator=0x9020000;const std::uint32_t rva=0x700000;
    if(!rtti) {rtti=true;put(vtable-8,locator);put(locator+12,rva);const char name[24]=".?AVarkSeveredLimb@@";for(unsigned i=0;i<sizeof(name);++i) put(kBase+rva+16+i,name[i]);}
    const std::uintptr_t at=0x10000000+slot*0x100000,entity=at,model=at+0x1000,list=at+0x2000,system=at+0x3000,handles=at+0x4000,data=at+0x5000;
    Limb limb{entity,{},parts};
    put(kGame+0x3130b0+slot*8,entity);put(entity,vtable);put(entity+0xa8,std::uint32_t{(slot<<16)|1u});put(entity+0xf0,model);put(model,kBase+0x1b0d628);
    put(entity+0x268,rig::List{list,1,1<<8});put(list,system);put(system,kBase+0x1c54008);put(system+0x18,rig::Identity{9,10});put(system+0x100,handles);
    put(handles+0x28,data);put(handles+0x30,static_cast<std::uint32_t>(parts.size()));
    Vec3 joint=root;
    for(unsigned k=0;k<parts.size();++k) {
        const unsigned index=slot*8+k;const std::uint32_t h=0x3000000u+index;const auto rec=kPool+index*0xb0;put(data+k*4,h);
        const std::uintptr_t shape=at+0x10000+k*0x1000;   // the live hull: convex (dispatch 2), its vertices at +0x40 + 0x30
        put(shape,kBase+0x1ec1ab8);put(shape+0x20,std::uint32_t{1u<<16});put(shape+0x24,static_cast<unsigned char>(2));put(shape+0x28,.01f);
        put(shape+0x40,parts[k]->n|(0x30u<<16));
        for(unsigned i=0;i<parts[k]->n;++i) put(shape+0x70+i*16,std::array<float,4>{parts[k]->v[i][0],parts[k]->v[i][1],parts[k]->v[i][2],0.f});
        // a body's own +y runs along the bone from its joint (its origin); own +z up
        const auto p=pose_of(joint,cross(along,up),along);limb.bodies.push_back(p);
        rig::Transform t{};for(unsigned r=0;r<3;++r) {t.f[r*4]=static_cast<float>(p.basis[r].x);t.f[r*4+1]=static_cast<float>(p.basis[r].y);t.f[r*4+2]=static_cast<float>(p.basis[r].z);}
        t.f[12]=static_cast<float>(p.position.x);t.f[13]=static_cast<float>(p.position.y);t.f[14]=static_cast<float>(p.position.z);t.f[15]=1;
        put(rec,t);put(rec+0x44,std::uint32_t{130});put(rec+0x40,std::uint32_t{1});put(rec+0x70,h);put(rec+0x74,h);put(rec+0x60,shape);
        if(k<joints.size()) joint=joint+along*joints[k];
    }
    return limb;
}
struct Take {const char* name;const Limb* limb;Vec3 palm,normal,forward;};
struct Result {int body{};double press_mm{},gap_mm{},out_mm{},slide_mm{},turn_deg{};bool natural{};unsigned settle{};double hold_mm{},run_mm{};unsigned turn_back{};bool grab{},v1086_use{};};
Result take(const Take& t) {
    Result r{};
    const auto body=native_severed_limb_body(read,kBase,t.limb->entity,-1,t.palm);
    assert(body && body->hull && body->bounds);r.body=body->limb;
    // The press: the game's Use target is this limb (kind loot, its whole skinned box round the palm: native cost 0); the grab's pick is the same
    // entity, scored from the hand sphere out to limb_reach_m (native_grab.hpp focus_body's limb pass)
    GripAloneInput in{};PalmTarget use{};use.id=t.limb->entity;use.kind=PalmKind::loot;GrabTuning tuning;use.priority=tuning.priority(use.kind);
    use.minimum=t.palm-Vec3{.5,.5,.5};use.maximum=t.palm+Vec3{.5,.5,.5};in.native=use;
    PalmProbe probe{};probe.palm=t.palm;probe.forward=t.forward;probe.direction=t.normal;probe.valid=true;in.probe=hand_sphere_probe(t.palm);
    const auto target=palm_body_target(*body,tuning);const auto cost=palm_cost(in.probe,target,tuning.hover_radius_m,limb_reach_m);
    assert(cost);
    in.native_fresh=true;in.since_ms=1000;in.now_ms=1200;in.native_carry=true;
    r.v1086_use=decide_grip_alone(in,tuning,tuning.hover_radius_m)==GripAloneChoice::use;   // v1087: no palm pick was scored while the limb was the Use target
    in.physical=FocusScore{target.id,PalmKind::prop,cost->cost,0.,cost->distance,t.limb->entity};in.native_limb=true;
    r.grab=decide_grip_alone(in,tuning,tuning.hover_radius_m)==GripAloneChoice::grab;   // v1088
    // The seat (the game's prop seat, a limb part's limit), then the hold: the hand frame is the palm's (forward, side, normal)
    auto seat=rig::prop_seat(*body,t.palm,t.normal,t.forward,true,limb_seat_move_m);r.natural=seat.natural;
    seat.pose=rig::surface_onto_skin(*body,seat.pose,t.palm,t.normal);   // v1088: its real surface on the palm skin (as native_grab does for a limb)
    const auto stats=rig::seat_stats(*body,seat.pose,t.palm,t.normal);   // the same numbers the game logs (severed-limb-hold.csv "seat")
    r.press_mm=stats.reach_mm;r.gap_mm=stats.gap_mm;r.slide_mm=stats.slide_mm;r.turn_deg=stats.turn_deg;
    // The v1062 seat-out then moves the thing along the palm normal until the drawn hand lies on it (finger_ik.hpp held_standoff: a pull of at most
    // seat_max_pull, a push of at most seat_max_push); here along the palm skin point only (the hand lab runs the real one on the real hand).
    r.out_mm=r.gap_mm-std::clamp(r.gap_mm,-rig::seat_max_push*1000,rig::seat_max_pull*1000);
    // Dynamics at 60 Hz: the hand presses (bind at the seat), the limb comes to the hand, the player stands 1 s, runs 5 m/s for 2 s, snap-turns 45 deg.
    // The rest of the limb hangs on the held body: its weight and the held body's own pull down every step (a stiff chain, the worst case).
    double chain=0,held=t.limb->parts[static_cast<std::size_t>(body->limb)]->kg;for(const auto* p:t.limb->parts) chain+=p->kg;
    const double sag=9.81*chain/held;
    auto hand=pose_of(t.palm,t.forward,cross(t.normal,t.forward));
    PhysicalGrab g;g.sample(hand,0.f,true,1,1000);g.sample(hand,0.f,true,1,1010);g.sample(hand,1.f,true,1,1020);
    assert(g.bind(seat.pose,body->contact(t.palm),limb_reach_m));
    auto now=body->pose;Vec3 v{},w{};std::uint64_t tick=1020;
    for(int i=0;i<300;++i) {
        const double s=i/60.;Vec3 carrier{};
        if(s>=1 && s<3) {carrier={5,0,0};hand.position=hand.position+carrier*(1./60);}
        if(i==180) {   // the snap turn: the hand round the player's pivot (0.92 m behind it), 45 deg about up, in one step
            const Vec3 up{0,0,1},pivot=hand.position+Vec3{-.92,0,0};hand.position=pivot+spin(hand.position-pivot,up,.785398);for(auto& b:hand.basis) b=spin(b,up,.785398);
        }
        tick+=16+(i%3==0?1:0);g.sample(hand,1.f,true,1,tick);
        CorpseMotorInput m;m.body_velocity=v;m.body_spin=w;m.velocity_known=true;m.corpse=false;m.object_kg=held;m.inertia_kgm2=.05;m.dt=1./60;m.tuning=&tuning;m.carrier=carrier;m.chain=true;
        const auto out=g.corpse_motor(now,true,tick+10,tick,m);
        if(!out || out->motor.release) {std::printf("%s: released at step %d (reason %d, error %.3f m)\n",t.name,i,out?static_cast<int>(out->reason):-1,length(g.target().position-now.position));std::fflush(stdout);assert(false);}
        // the body and the goal of the same instant (as held_prop_follow_check), then the step: the motor's velocity, less the hanging weight's pull
        const auto goal=g.target();const double gap=length(goal.position-now.position),rot=length(grab_rotation_error(now,goal))*57.29578;
        if(i<60 && !r.settle && gap<.02 && rot<5) r.settle=static_cast<unsigned>(i);
        if(i==59) r.hold_mm=gap*1000;
        if(s>=1.5 && s<3) r.run_mm=std::max(r.run_mm,gap*1000);
        if(i>180 && !r.turn_back && gap<.02) r.turn_back=static_cast<unsigned>(i-180);
        v=out->motor.linear-Vec3{0,0,sag/60};w=out->motor.angular;
        now.position=now.position+v*(1./60);const double a=length(w)/60;if(a>1e-12) for(auto& b:now.basis) b=spin(b,unit(w),a);
    }
    return r;
}
}
int main() {
    const Part thigh{"thigh",hull_thigh,sizeof(hull_thigh)/sizeof(hull_thigh[0]),9.},calf{"calf",hull_calf,sizeof(hull_calf)/sizeof(hull_calf[0]),4.},
        foot{"foot",hull_foot,sizeof(hull_foot)/sizeof(hull_foot[0]),1.5},upper{"upper arm",hull_upperarm,sizeof(hull_upperarm)/sizeof(hull_upperarm[0]),2.5},
        fore{"forearm",hull_forearm,sizeof(hull_forearm)/sizeof(hull_forearm[0]),1.5},hand{"hand",hull_hand,sizeof(hull_hand)/sizeof(hull_hand[0]),.6},
        head{"head",hull_head,sizeof(hull_head)/sizeof(hull_head[0]),5.};
    put(kBase+0x39a5290,kGame);put(kGame,kBase+0x1c940a8);put(kGame+0x60410,kWorld);put(kWorld+0x28,kPool);put(kWorld+0x30,std::uint32_t{0x10000});
    // On the floor, each along +x, up +z: a leg from the hip (knee at 0.46 m, ankle 0.42 m on), an arm from the shoulder, a head.
    const Limb leg=make_limb(5,{&thigh,&calf,&foot},{0,0,.12},{1,0,0},{0,0,1},{.46,.42});
    const Limb arm=make_limb(6,{&upper,&fore,&hand},{0,1,.07},{1,0,0},{0,0,1},{.30,.27});
    const Limb skull=make_limb(7,{&head},{0,2,.10},{1,0,0},{0,0,1},{});
    const Vec3 down{0,0,-1},across{0,1,0},along{1,0,0},angled=unit(Vec3{.5,.866,0});
    // The palm faces down (normal -z), its skin 2-3 cm above the part or reaching 10 cm short of it; fingers across the limb, at 30 deg, or along it.
    const std::vector<Take> takes{
        {"leg: calf, 20 cm reach",&leg,{.80,0,.42},down,across},{"leg: calf, 40 cm reach",&leg,{.80,0,.62},down,across},
        {"arm: forearm, 30 cm reach",&arm,{.45,1,.45},down,across},{"head: 25 cm reach",&skull,{.04,2,.45},down,across},
        {"leg: calf by the ankle, across",&leg,{.80,0,.27},down,across},{"leg: calf by the ankle, 30 deg",&leg,{.80,0,.27},down,angled},
        {"leg: calf mid, fingers along",&leg,{.66,0,.27},down,along},{"leg: calf, 10 cm reach",&leg,{.80,0,.37},down,across},
        {"leg: thigh mid, across",&leg,{.25,0,.28},down,across},{"leg: foot, across",&leg,{1.00,0,.24},down,across},
        {"arm: forearm by the wrist, across",&arm,{.52,1,.15},down,across},{"arm: forearm, 30 deg",&arm,{.45,1,.15},down,angled},
        {"arm: upper arm, across",&arm,{.17,1,.15},down,across},{"arm: hand, across",&arm,{.66,1,.15},down,across},
        {"head: from above",&skull,{.04,2,.31},down,across},{"head: from above, 30 deg",&skull,{.04,2,.31},down,angled}};
    std::printf("%-34s %-9s %8s %7s %7s %8s %8s %5s %7s %7s %7s %6s\n","take (palm skin to the part's real surface)","body","press mm","seat mm","out mm","slide mm","turn deg",
        "seat","settle","hold mm","run mm","turn");
    bool ok=true;
    for(const auto& t:takes) {
        const auto r=take(t);
        std::printf("%-34s %-9s %8.1f %7.1f %7.1f %8.1f %8.1f %5s %4u fr %7.1f %7.1f %3u fr\n",t.name,t.limb->parts[static_cast<std::size_t>(r.body)]->name,r.press_mm,r.gap_mm,r.out_mm,
            r.slide_mm,r.turn_deg,r.natural?"palm":"ball",r.settle,r.hold_mm,r.run_mm,r.turn_back);
        // The press is the grab's (v1087 gave it to the game's Use: no palm pick); after the seat its real surface lies on the palm skin within 2 mm; it is in the
        // hand (2 cm, 5 deg) within 6 frames of the press, stays within 2 cm while running at 5 m/s with the rest of the limb hanging on it, and is back
        // within 2 cm 6 frames after a snap turn.
        ok=ok && r.v1086_use && r.grab && std::abs(r.gap_mm)<=2 && std::abs(r.out_mm)<=2 && r.settle>0 && r.settle<=6 && r.run_mm<20 && r.turn_back>0 && r.turn_back<=6;
    }
    std::puts("columns: press = the palm skin to the part at the press (v1086 held it there: no seat; v1087 used the game's carry); seat = after rig::prop_seat; out = after the seat-out's\n"
              "pull (<= 30 mm along the palm normal); slide = the grasped spot moved along the bone; settle = frames to within 2 cm / 5 deg of the hold;\n"
              "hold = the standing error with the rest of the limb hanging on the held body; run = the worst gap at 5 m/s; turn = frames back after a 45 deg snap turn");
    std::fflush(stdout);
    assert(ok);
    std::puts("PASS: severed leg, arm and head - the grab takes the press, the part is seated on the palm and held in the hand (running, snap turn)");
}
