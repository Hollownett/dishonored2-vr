#include "../src/aa_first_start.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace d2vr;namespace fs=std::filesystem;
        { // decision_table: 2 aa_chosen x 2 file_existed x 4 vendors x 2 dlss_offered = 32 combinations against the five rules
            int cases=0;
            for(const bool chosen:{false,true}) for(const bool existed:{false,true}) for(const std::uint32_t vendor:{0u,0x10deu,0x1002u,1u}) for(const bool dlss:{false,true}) {
                FirstStartAa expected=chosen?FirstStartAa::keep:vendor==0?FirstStartAa::wait:dlss?FirstStartAa::dlaa:!existed?FirstStartAa::fsr3_native:FirstStartAa::keep;
                require(first_start_aa(chosen,existed,vendor,dlss)==expected,"first-start decision differs from the rules");++cases;
            }
            require(cases==32,"decision table did not cover 32 combinations");
        }
        require(first_start_aa(false,true,0x1002,false)==FirstStartAa::keep,"A pre-v609 non-RTX file lost its saved mode");
        require(first_start_aa(false,false,0x10de,true)==FirstStartAa::dlaa && first_start_aa(false,true,0x10de,true)==FirstStartAa::dlaa,"RTX did not get DLAA");
        require(first_start_aa(false,false,0x1002,false)==FirstStartAa::fsr3_native,"A fresh non-RTX install did not get FSR 3 native");
        require(first_start_aa(false,true,1,false)==FirstStartAa::keep,"A vendor failure with a saved file did not keep the mode");
        require(first_start_aa(false,true,0,true)==FirstStartAa::wait,"An unknown vendor did not wait");
        {
            const auto dir=fs::temp_directory_path()/"d2vr_aa_first_start_check";fs::remove_all(dir);fs::create_directories(dir);
            const auto put=[&](const char* name,const char* text) {std::ofstream(dir/name,std::ios::binary)<<text;};
            put("aa-mode","3\n");put("fsr","0.4 1 1\n");put("fsr4","");put("force-taa","");put("fsr4.off","");put("hud-scale","1.2\n");
            const auto removed=remove_legacy_aa_knobs(dir);
            require(removed.size()==4,"Did not remove exactly four legacy knobs");
            const char* names[]={"aa-mode","force-taa","fsr","fsr4"};const char* values[]={"3","","0.4 1 1",""}; // sorted by name
            for(int i=0;i<4;++i) require(removed[i].name==names[i] && removed[i].value==values[i],"Legacy knob name or value wrong");
            for(const char* name:names) require(!fs::exists(dir/name),"A legacy knob remained");
            require(fs::exists(dir/"fsr4.off") && fs::exists(dir/"hud-scale"),"A non-legacy knob was removed");
            require(remove_legacy_aa_knobs(dir).empty(),"A second pass removed something");
            put("aa-mode","  7 \r\nsecond line\n");require(remove_legacy_aa_knobs(dir).at(0).value=="7","Value was not the trimmed first line");
            put("fsr",std::string(200,'x').c_str());require(remove_legacy_aa_knobs(dir).at(0).value.size()==64,"Value was not capped at 64 characters");
            fs::remove_all(dir);
            require(remove_legacy_aa_knobs(dir).empty(),"A missing directory returned entries");
        }
        std::cout<<"PASS aa_first_start decision table, legacy knob removal\n";return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
