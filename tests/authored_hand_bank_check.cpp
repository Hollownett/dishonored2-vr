#include "../src/authored_hand_bank.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <cassert>
namespace r=d2vr::rig;
int main(int argc,char** argv) {
    assert(argc==2);std::ifstream file(argv[1],std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(file)),{});
    constexpr std::size_t header=404,side_bytes=r::authored_hand_payload_size/2;
    assert(bytes.size()==header+r::authored_hand_payload_size);
    r::AuthoredBankProvenance provenance{};std::memcpy(provenance.hashes.data(),bytes.data()+12,384);
    std::memcpy(&provenance.payload_checksum,bytes.data()+396,8);
    std::array<r::AuthoredRigIdentity,2> identities{};
    // Trusted private fixture supplies independently pinned expectations for
    // mutation tests; production must obtain these from the verified live rig.
    for(unsigned side=0;side<2;++side) {
        std::size_t at=header+side*side_bytes+4;auto& rig=identities[side];
        const auto take=[&](void* out,std::size_t n){std::memcpy(out,bytes.data()+at,n);at+=n;};
        take(rig.names.data(),69*96);take(rig.parents.data(),138);take(rig.chains.data(),60);take(rig.helpers.data(),8);take(&rig.accessory_leaf,4);at+=69;take(rig.inverse.data(),69*48);
    }
    const auto load=[&](const std::string& input){std::istringstream stream(input);return r::read_authored_hand_bank(stream,provenance,identities);};
    const auto rejected=[&](const std::string& input){try{(void)load(input);}catch(const std::invalid_argument&){return true;}return false;};
    const auto reseal=[&](std::string input){std::array<unsigned char,r::authored_hand_payload_size> payload{};std::memcpy(payload.data(),input.data()+header,payload.size());const auto checksum=r::authored_payload_checksum(payload);std::memcpy(input.data()+396,&checksum,8);return input;};
    const auto bank=load(bytes);assert(!bank.metadata_acceptance);
    // Direct factory checks also cover structurally corrupted resources after
    // a malicious or mistaken exporter has recomputed its internal checksum.
    const auto invalid_resource=[&](r::AuthoredHandResource value){try{(void)r::authored_hand_resource(value,identities[0],0);}catch(const std::invalid_argument&){return true;}return false;};
    auto broken=bank.hands[0];broken.roles[0]^=1;assert(invalid_resource(broken));
    broken=bank.hands[0];broken.closed[0].f[0]=4;assert(invalid_resource(broken));
    broken=bank.hands[0];broken.closed[broken.rig.chains[1][1]].f[12]+=1;assert(invalid_resource(broken));
    auto bad=bytes;bad[0]='X';assert(rejected(bad));bad=bytes;bad[8]=7;assert(rejected(bad));
    bad=bytes;bad[12+64]='0';assert(rejected(bad));assert(rejected(bytes.substr(0,bytes.size()-1)));assert(rejected(bytes+"x"));
    bad=bytes;bad.back()^=1;assert(rejected(bad));
    bad=bytes;bad[header]=1;assert(rejected(reseal(bad)));
    bad=bytes;bad[header+4]='x';assert(rejected(reseal(bad)));
    bad=bytes;bad[header+4+69*96]=69;assert(rejected(reseal(bad)));
    bad=bytes;bad[header+4+69*96+138+60+8+4]^=1;assert(rejected(reseal(bad)));
    // Corrupt a closed root rotation while preserving payload checksum.
    bad=bytes;const float invalid=4;std::memcpy(bad.data()+header+side_bytes-69*64,&invalid,4);assert(rejected(reseal(bad)));
    auto wrong=identities;wrong[0].inverse[0][0]+=1;std::istringstream mismatch(bytes);bool denied=false;
    try{(void)r::read_authored_hand_bank(mismatch,provenance,wrong);}catch(const std::invalid_argument&){denied=true;}assert(denied);
    for(unsigned side=0;side<2;++side) {
        const auto& hand=bank.hands[side];const r::AuthoredHandEligibility enabled{true,true,true,3};
        const auto trigger_only=r::authored_hand_pose(hand,side,hand.open,identities[side],0,1,enabled);
        const auto explicit_envelope=r::curl_fingers(hand.open,hand.rig.parents,hand.binding,std::array<float,5>{.5f,1,0,0,0});
        for(unsigned j=0;j<69;++j)assert(trigger_only[j].f==explicit_envelope[j].f);
        for(float grip:{0.f,.1f,.25f,.5f,.75f,.9f,1.f})for(float index:{0.f,1.f}) {
            const auto pose=r::authored_hand_pose(hand,side,hand.open,identities[side],grip,index,enabled);
            assert(pose[hand.rig.accessory_leaf].f==hand.open[hand.rig.accessory_leaf].f);
            for(const auto& joint:pose)assert(r::valid(joint));
            if(grip==0 && index==0)for(unsigned j=0;j<69;++j)assert(pose[j].f==hand.open[j].f);
        }
        for(const auto gate:{r::AuthoredHandEligibility{false,true,true,3},{true,false,true,3},{true,true,false,3},{true,true,true,0}}) {
            const auto pose=r::authored_hand_pose(hand,side,hand.open,identities[side],1,1,gate);
            for(unsigned j=0;j<69;++j)assert(pose[j].f==hand.open[j].f);
        }
        denied=false;try{(void)r::authored_hand_pose(hand,1-side,hand.open,identities[side],1,1,enabled);}catch(const std::invalid_argument&){denied=true;}assert(denied);
    }
    std::cout<<"PASS: private authored bank load, corruption/identity/geometry rejection, zero controls, accessory leaf and independent runtime gates; no activation or visual acceptance\n";
}
