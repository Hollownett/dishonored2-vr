#pragma once
#include "native_hand_reference.hpp"
#include <ostream>
#include <string>

namespace d2vr::rig {
using AuthoredPoseHash=std::array<char,64>;
using AuthoredJointNames=std::array<std::array<char,96>,69>;
struct AuthoredBankProvenance {
    // game, source report, candidate, left mesh, right mesh, review report SHA256.
    std::array<AuthoredPoseHash,6> hashes{};
    // Obtain from a separately SHA256-verified manifest/archive, not this file.
    std::uint64_t payload_checksum{};
};
struct AuthoredRigIdentity {
    AuthoredJointNames names{};ArmHierarchy parents{};FingerChains chains{};
    FingerPalmHelpers helpers{69,69};std::array<std::array<float,12>,69> inverse{};
    std::uint32_t accessory_leaf{69};
};
struct AuthoredHandResource {
    std::uint32_t side{};AuthoredRigIdentity rig{};std::array<unsigned char,69> roles{};
    std::array<Transform,69> open{},closed{};FingerBinding binding{};
};
struct AuthoredHandBank {
    AuthoredBankProvenance provenance{};std::array<AuthoredHandResource,2> hands{};
    // Research metadata; this resource does not establish visual acceptance.
    bool metadata_acceptance=false;
};
inline void validate_authored_provenance(const AuthoredBankProvenance& value) {
    constexpr char game[]="b671f8249422862bf557d00aab2f4bb671ed4515cc10ad8a8a9decad84516180";
    if(std::memcmp(value.hashes[0].data(),game,64))throw std::invalid_argument("Authored bank executable differs");
    for(const auto& hash:value.hashes) {
        bool nonzero=false;
        for(char c:hash){if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))throw std::invalid_argument("Invalid authored provenance hash");nonzero|=c!='0';}
        if(!nonzero)throw std::invalid_argument("Missing authored provenance hash");
    }
}
inline AuthoredHandResource authored_hand_resource(AuthoredHandResource value,const AuthoredRigIdentity& expected,unsigned side) {
    if(side>1 || value.side!=side || value.rig.names!=expected.names || value.rig.parents!=expected.parents ||
       value.rig.chains!=expected.chains || value.rig.helpers!=expected.helpers || value.rig.inverse!=expected.inverse ||
       value.rig.accessory_leaf!=expected.accessory_leaf)throw std::invalid_argument("Authored exact rig/side identity differs");
    for(unsigned joint=0;joint<69;++joint) {
        const auto& name=value.rig.names[joint];auto end=std::find(name.begin(),name.end(),'\0');
        if(end==name.begin() || end==name.end() || !std::all_of(end,name.end(),[](char c){return c==0;}))throw std::invalid_argument("Invalid authored joint name");
        for(auto at=name.begin();at!=end;++at)if(!((*at>='a'&&*at<='z')||(*at>='0'&&*at<='9')||*at=='_'))throw std::invalid_argument("Invalid authored joint alphabet");
        for(unsigned other=0;other<joint;++other)if(name==value.rig.names[other])throw std::invalid_argument("Duplicate authored joint name");
        (void)bind_transform(value.rig.inverse[joint]);
    }
    const auto leaf=value.rig.accessory_leaf;
    if(leaf>=69 || std::strcmp(value.rig.names[leaf].data(),side?"right_thumb_end_jnt":"left_index_end_jnt"))throw std::invalid_argument("Authored accessory leaf differs");
    for(auto parent:value.rig.parents)if(parent==static_cast<int>(leaf))throw std::invalid_argument("Authored accessory joint has children");
    constexpr std::array<const char*,5> digits{"thumb","index","medium","ring","pinky"};
    for(unsigned digit=0;digit<5;++digit)for(unsigned segment=0;segment<3;++segment) {
        const auto joint=value.rig.chains[digit][segment];
        const auto name=std::string(side?"right_":"left_")+digits[digit]+"_0"+std::to_string(segment+(digit?2:1))+"_jnt";
        if(joint>=69 || name!=value.rig.names[joint].data())throw std::invalid_argument("Authored named chain differs");
    }
    auto binding=native_finger_binding(value.open,value.closed,value.rig.parents,value.rig.chains,value.rig.helpers);
    if(!binding.mask[leaf] || binding.bone[leaf])throw std::invalid_argument("Accessory is not an excluded finger leaf");
    Transform identity{};identity.f[0]=identity.f[5]=identity.f[10]=1;
    for(unsigned joint=0;joint<69;++joint) {
        const auto role=static_cast<unsigned char>((binding.mask[joint]?1:0)|(binding.bone[joint]?2:0)|(joint==leaf?4:0));
        if(value.roles[joint]!=role)throw std::invalid_argument("Authored joint role differs");
        if(!binding.mask[joint] && value.open[joint].f!=value.closed[joint].f)throw std::invalid_argument("Authored pose changes outside hand");
        if(binding.mask[joint]) {
            const auto parent=value.rig.parents[joint];
            const auto a=rigid_retarget(value.open[joint],value.open[parent],identity);
            const auto b=rigid_retarget(value.closed[joint],value.closed[parent],identity);
            const bool helper=std::string(value.rig.names[joint].data()).find("helper")!=std::string::npos;
            if(length(position(a)-position(b))>(helper?.025:.0001))throw std::invalid_argument("Authored segment/helper offset differs");
        }
    }
    binding.mask[leaf]=false;binding.digit[leaf]=5;value.binding=binding;return value;
}
// Fixed little-endian Windows resource. FNV detects accidental payload damage;
// externally verified SHA256 provenance is required, never inferred from FNV.
constexpr std::size_t authored_hand_payload_size=2*(4+69*96+138+60+8+4+69+69*48+2*69*64);
inline std::uint64_t authored_payload_checksum(const std::array<unsigned char,authored_hand_payload_size>& bytes) {
    std::uint64_t hash=14695981039346656037ull;for(auto byte:bytes){hash^=byte;hash*=1099511628211ull;}return hash;
}
inline AuthoredHandBank read_authored_hand_bank(std::istream& stream,const AuthoredBankProvenance& expected,
    const std::array<AuthoredRigIdentity,2>& rigs) {
    static_assert(sizeof(Transform)==64 && sizeof(ArmHierarchy)==138 && sizeof(FingerChains)==60 && sizeof(FingerPalmHelpers)==8);
    const auto read=[&](void* out,std::size_t size){if(!stream.read(static_cast<char*>(out),static_cast<std::streamsize>(size)))throw std::invalid_argument("Truncated authored bank");};
    std::array<char,8> magic{};std::uint32_t flags{};AuthoredHandBank result{};std::uint64_t checksum{};
    read(magic.data(),8);read(&flags,4);read(result.provenance.hashes.data(),6*64);read(&checksum,8);
    if(std::memcmp(magic.data(),"D2VRAH01",8)||flags!=3)throw std::invalid_argument("Invalid authored bank header/acceptance flags");
    validate_authored_provenance(expected);validate_authored_provenance(result.provenance);
    result.provenance.payload_checksum=checksum;
    if(result.provenance.hashes!=expected.hashes || !expected.payload_checksum || checksum!=expected.payload_checksum)throw std::invalid_argument("Authored provenance differs");
    std::array<unsigned char,authored_hand_payload_size> bytes{};read(bytes.data(),bytes.size());
    if(authored_payload_checksum(bytes)!=checksum || stream.peek()!=std::char_traits<char>::eof())throw std::invalid_argument("Authored payload checksum/trailing data differs");
    std::size_t at=0;const auto take=[&](void* out,std::size_t size){if(size>bytes.size()-at)throw std::invalid_argument("Authored payload exceeds bounds");std::memcpy(out,bytes.data()+at,size);at+=size;};
    for(unsigned side=0;side<2;++side) {
        auto& hand=result.hands[side];auto& rig=hand.rig;
        take(&hand.side,4);take(rig.names.data(),69*96);take(rig.parents.data(),138);take(rig.chains.data(),60);take(rig.helpers.data(),8);
        take(&rig.accessory_leaf,4);take(hand.roles.data(),69);take(rig.inverse.data(),69*48);take(hand.open.data(),69*64);take(hand.closed.data(),69*64);
        hand=authored_hand_resource(hand,rigs[side],side);
    }
    return result;
}
struct AuthoredHandEligibility {
    bool opt_in{},exact_visible_primitives_validated{},native_idle_action_verified{};
    unsigned freshly_verified_empty_sides{};
};
inline std::array<Transform,69> authored_hand_pose(const AuthoredHandResource& hand,unsigned side,
    const std::array<Transform,69>& current,const AuthoredRigIdentity& current_rig,float grip,float index,
    AuthoredHandEligibility eligibility) {
    if(side>1 || hand.side!=side || current_rig.names!=hand.rig.names || current_rig.parents!=hand.rig.parents ||
       current_rig.inverse!=hand.rig.inverse || current_rig.chains!=hand.rig.chains || current_rig.helpers!=hand.rig.helpers ||
       current_rig.accessory_leaf!=hand.rig.accessory_leaf)throw std::invalid_argument("Authored runtime rig identity differs");
    if(!std::isfinite(grip)||!std::isfinite(index)||grip<0||grip>1||index<0||index>1)throw std::invalid_argument("Invalid authored controls");
    if(!eligibility.opt_in || !eligibility.exact_visible_primitives_validated || !eligibility.native_idle_action_verified ||
       !(eligibility.freshly_verified_empty_sides&(1u<<side)))return current;
    return curl_fingers(current,hand.rig.parents,hand.binding,std::array<float,5>{std::max(grip,.5f*index),index,grip,grip,grip});
}
} // namespace d2vr::rig
