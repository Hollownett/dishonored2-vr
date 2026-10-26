#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/native_cvars.hpp"
#include <iostream>
void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
int main() {
    try {
        using namespace d2vr;
        struct Memory {
            bool operator()(std::uintptr_t address,void* destination,std::size_t bytes) const {
                std::memcpy(destination,reinterpret_cast<const void*>(address),bytes);return true;
            }
        } read;
        // rvas reach ~0x39a4220, so reserve the whole span and commit only the
        // pages each synthetic cvar object lands in.
        const auto region=VirtualAlloc(nullptr,0x3a00000,MEM_RESERVE,PAGE_NOACCESS);
        require(region!=nullptr,"Cannot reserve the synthetic cvar span");
        const auto base=reinterpret_cast<std::uintptr_t>(region);
        const std::array<LONG,NativeCvars::vr_defaults.size()> originals{{0,0,1,1,1,1,0,0,0,0}};
        const auto object=[&](std::size_t index) {
            const auto address=base+NativeCvars::vr_defaults[index].rva;
            require(VirtualAlloc(reinterpret_cast<void*>(address&~static_cast<std::uintptr_t>(0xfff)),0x1000,MEM_COMMIT,PAGE_READWRITE)!=nullptr,
                "Cannot commit a synthetic cvar page");
            return reinterpret_cast<unsigned char*>(address);
        };
        const auto place=[&](std::size_t index) {
            auto* memory=object(index);
            *reinterpret_cast<const char**>(memory+0x30)=NativeCvars::vr_defaults[index].name;
            *reinterpret_cast<LONG*>(memory+0x28)=originals[index];
            *reinterpret_cast<float*>(memory+0x2c)=static_cast<float>(originals[index]);
        };
        const auto cache=[&](std::size_t index,LONG& integer,float& scalar) {
            const auto* memory=reinterpret_cast<const unsigned char*>(base+NativeCvars::vr_defaults[index].rva);
            std::memcpy(&integer,memory+0x28,4);std::memcpy(&scalar,memory+0x2c,4);
        };
        const auto originals_restored=[&](const char* message) {
            for(std::size_t i=0;i<NativeCvars::vr_defaults.size();++i) {
                LONG integer{};float scalar{};cache(i,integer,scalar);
                require(integer==originals[i] && scalar==static_cast<float>(originals[i]),message);
            }
        };
        for(std::size_t i=0;i<NativeCvars::vr_defaults.size();++i) place(i);
        NativeCvars cvars;
        // r_skipTemporalAA already holds its override, so apply() writes nine.
        require(cvars.apply(base,read,true)==10u,"apply did not write ten overrides");
        for(std::size_t i=0;i<NativeCvars::vr_defaults.size();++i) {
            LONG integer{};float scalar{};cache(i,integer,scalar);
            require(integer==NativeCvars::vr_defaults[i].value && scalar==static_cast<float>(NativeCvars::vr_defaults[i].value),
                "apply left a cvar cache wrong");
        }
        require(cvars.set(NativeCvars::skip_temporal_aa,1),"set on an already-equal cvar did nothing");
        {LONG integer{};float scalar{};cache(NativeCvars::skip_temporal_aa,integer,scalar);
         require(integer==1 && scalar==1.f,"set did not move both temporal AA caches");}
        // A second live edit must CAS from the value the first one left behind.
        require(cvars.set(0,5),"set refused an active override");
        {LONG integer{};float scalar{};cache(0,integer,scalar);require(integer==5 && scalar==5.f,"second set did not move both caches");}
        require(!cvars.set(NativeCvars::vr_defaults.size(),1),"set accepted an out-of-range index");
        cvars.restore();originals_restored("restore did not put the original values back after a set");
        require(!cvars.set(NativeCvars::skip_temporal_aa,1),"set wrote a cvar that apply had restored");
        for(std::size_t i=0;i<NativeCvars::vr_defaults.size();++i) place(i);
        NativeCvars otherwise;
        require(otherwise.apply(base,read,false)==8u,"apply without temporal AA did not write eight overrides");
        for(std::size_t i=NativeCvars::first_temporal;i<NativeCvars::vr_defaults.size();++i) {
            LONG integer{};float scalar{};cache(i,integer,scalar);
            require(integer==originals[i] && !otherwise.set(i,1),"apply without temporal AA touched a temporal cvar");
        }
        otherwise.restore();originals_restored("restore without temporal AA changed a cache");
        for(std::size_t i=0;i<NativeCvars::vr_defaults.size();++i) place(i);
        *reinterpret_cast<const char**>(object(3)+0x30)="not_a_cvar";
        NativeCvars filtered;
        require(filtered.apply(base,read,true)==9u,"apply counted a cvar whose name differs");
        {LONG integer{};float scalar{};cache(3,integer,scalar);
         require(integer==originals[3] && scalar==static_cast<float>(originals[3]),"apply wrote a cvar whose name differs");}
        filtered.restore();
        for(std::size_t i=0;i<NativeCvars::vr_defaults.size();++i) place(i);
        // Initial equal integer with a different scalar is a real pair change.
        *reinterpret_cast<float*>(object(10)+0x2c)=.25f;
        NativeCvars paired;
        require(paired.apply(base,read,true)==11u,"apply ignored an equal-integer scalar mismatch");
        require(paired.matches(10,0),"apply did not claim the complete desired pair");
        require(paired.set(3,1),"rs_enable reduced-mode set failed");
        // rs_enable resets to its saved native baseline; reapply is bounded to
        // exactly that pair, even when the requested mode changes at the reset.
        require(paired.set(3,0),"rs_enable full-mode set failed");
        place(3);require(paired.set(3,0)&&paired.matches(3,0),"saved-pair reset could not be reapplied");
        require(paired.set(3,1),"reduced mode after reset failed");
        // The game rewrites rs_enable itself (live: 3); a set takes that pair over instead of refusing forever.
        *reinterpret_cast<LONG*>(object(3)+0x28)=0;
        require(paired.set(3,1)&&paired.matches(3,1),"game-rewritten mixed pair was not taken over");
        *reinterpret_cast<LONG*>(object(3)+0x28)=7;*reinterpret_cast<float*>(object(3)+0x2c)=7.f;
        require(paired.set(3,0)&&paired.matches(3,0),"game-rewritten pair was not taken over");
        paired.restore();
        {LONG integer{};float scalar{};cache(3,integer,scalar);require(integer==originals[3]&&scalar==static_cast<float>(originals[3]),"restore did not return the original after a takeover");}
        {LONG integer{};float scalar{};cache(10,integer,scalar);require(integer==0&&scalar==.25f,"restore lost original scalar mismatch");}
        // Restore must preserve a mixed pair as a unit, including a half that
        // still happens to equal our value.
        for(std::size_t i=0;i<NativeCvars::vr_defaults.size();++i) place(i);
        NativeCvars mixed_restore;require(mixed_restore.apply(base,read,true)==10u,"mixed restore setup failed");
        *reinterpret_cast<float*>(object(3)+0x2c)=9.f;
        mixed_restore.restore();
        {LONG integer{};float scalar{};cache(3,integer,scalar);require(integer==0&&scalar==9.f,"restore altered a mixed foreign pair");}
        for(std::size_t i=0;i<NativeCvars::vr_defaults.size();++i) place(i);
        *reinterpret_cast<LONG*>(object(3)+0x28)=0;*reinterpret_cast<float*>(object(3)+0x2c)=0.f;
        NativeCvars reduced_reset;reduced_reset.apply(base,read,true);
        require(reduced_reset.set(3,1),"reduced reset setup failed");
        *reinterpret_cast<LONG*>(object(3)+0x28)=0;*reinterpret_cast<float*>(object(3)+0x2c)=0.f;
        require(reduced_reset.set(3,1)&&reduced_reset.matches(3,1),"reduced-mode baseline reset failed to reapply");
        reduced_reset.restore();
        {LONG integer{};float scalar{};cache(3,integer,scalar);require(integer==0&&scalar==0.f,"reduced reset restore lost saved zero baseline");}
        for(std::size_t i=0;i<NativeCvars::vr_defaults.size();++i) place(i);
        NativeCvars native_depth;require(native_depth.apply(base,read,true,true)==9u,"native depth ownership was not excluded from raw cache writes");
        {LONG integer{};float scalar{};cache(NativeCvars::player_depth_hack,integer,scalar);
         require(integer==originals[NativeCvars::player_depth_hack] && scalar==1.f && !native_depth.set(NativeCvars::player_depth_hack,0),"raw cvars retained native depth ownership");}
        native_depth.restore();originals_restored("raw restore altered native depth ownership");
        VirtualFree(region,0,MEM_RELEASE);
        std::cout<<"PASS: atomic cvar pairs, saved-baseline reapply, mode switches, mixed/foreign preservation, scalar mismatch restore and name filtering\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
