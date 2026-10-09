// Private offline trial: exercise the production parent-local screw blend.
// This fixture is deliberately incompatible with native/authored reference archives.
#include "../src/finger_pose.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
namespace r=d2vr::rig;
template<class T> T take(std::istream& file) {
    T value{};if(!file.read(reinterpret_cast<char*>(&value),sizeof(value))) throw std::runtime_error("Truncated authored sweep fixture");return value;
}
template<class T> void put(std::ostream& file,const T& value) {
    if(!file.write(reinterpret_cast<const char*>(&value),sizeof(value))) throw std::runtime_error("Cannot save authored sweep");
}
int main(int argc,char** argv) {
    try {
        if(argc!=3) throw std::runtime_error("Usage: authored_hand_sweep.exe <test-only fixture> <test-only output>");
        std::ifstream source(argv[1],std::ios::binary);std::ofstream output(argv[2],std::ios::binary);
        const auto magic=take<std::array<char,8>>(source);
        if(std::memcmp(magic.data(),"D2VRTS01",8)) throw std::runtime_error("Not a private sweep fixture");
        const std::array<char,8> out_magic{'D','2','V','R','S','O','0','2'};put(output,out_magic);
        constexpr std::array<float,7> grips{0,.1f,.25f,.5f,.75f,.9f,1};put(output,std::uint32_t{112});
        for(unsigned side=0;side<2;++side) {
            const auto parents=take<r::ArmHierarchy>(source);const auto chains=take<r::FingerChains>(source);
            const auto helpers=take<r::FingerPalmHelpers>(source);const auto accessory_end=take<std::uint32_t>(source);
            const auto open=take<std::array<r::Transform,69>>(source),closed=take<std::array<r::Transform,69>>(source);
            if(accessory_end>=69) throw std::runtime_error("Invalid accessory-only leaf");
            for(auto parent:parents) if(parent==static_cast<int>(accessory_end)) throw std::runtime_error("Accessory-only joint is not a leaf");
            for(unsigned mode=0;mode<4;++mode) {
                auto binding=r::native_finger_binding(open,closed,parents,chains,helpers);
                if(mode&1) {binding.mask[accessory_end]=false;binding.digit[accessory_end]=5;}
                for(auto grip:grips) for(unsigned index=0;index<2;++index) {
                    const auto thumb=mode>=2?std::max(grip,.5f*index):grip;
                    const auto pose=r::curl_fingers(open,parents,binding,std::array<float,5>{thumb,static_cast<float>(index),grip,grip,grip});
                    put(output,std::uint32_t{side});put(output,std::uint32_t{mode});put(output,grip);put(output,std::uint32_t{index});put(output,pose);
                }
            }
        }
        if(source.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Unexpected sweep fixture trailing data");
        std::cout<<"PASS: 112 offline authored poses through actual curl_fingers screw interpolation; test-only output, no activation\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
