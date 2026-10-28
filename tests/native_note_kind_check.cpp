#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_note_kind.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <unordered_map>
#include <vector>

int main(int argc,char** argv) {
    using namespace d2vr;
    assert(argc==2); // pinned Dishonored2.exe; ABI bytes are verified, never synthesized
    std::ifstream stream(argv[1],std::ios::binary);
    const std::vector<char> image{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
    assert(image.size()>sizeof(IMAGE_DOS_HEADER));
    IMAGE_DOS_HEADER dos{};std::memcpy(&dos,image.data(),sizeof(dos));
    assert(dos.e_magic==IMAGE_DOS_SIGNATURE && dos.e_lfanew>0);
    const auto nt_offset=static_cast<std::size_t>(dos.e_lfanew);
    assert(nt_offset+sizeof(IMAGE_NT_HEADERS64)<=image.size());
    IMAGE_NT_HEADERS64 nt{};std::memcpy(&nt,image.data()+nt_offset,sizeof(nt));
    assert(nt.Signature==IMAGE_NT_SIGNATURE);
    const auto section_offset=nt_offset+offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
    assert(section_offset+nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)<=image.size());
    constexpr std::uintptr_t base=0x140000000,root=0x100000,callback=0x200000,batches=0x300000,
        wrapper=0x400000,file=0x500000,player=0x600000,iggy=0x700000,name=0x800000,
        method=0x900000,receiver=0xa00000,block=0xb00000,root_path=0xc00000;
    constexpr auto manager=root+0x18;
    std::unordered_map<std::uintptr_t,unsigned char> memory;
    const auto put=[&](std::uintptr_t at,const auto& value) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(&value);
        for(std::size_t i=0;i<sizeof(value);++i)memory[at+i]=bytes[i];
    };
    const auto read=[&](std::uintptr_t at,void* out,std::size_t size) {
        if(memory.find(at)==memory.end() && at>=base)for(unsigned s=0;s<nt.FileHeader.NumberOfSections;++s) {
            IMAGE_SECTION_HEADER section{};
            std::memcpy(&section,image.data()+section_offset+s*sizeof(section),sizeof(section));
            const auto rva=at-base;
            if(rva>=section.VirtualAddress && rva-section.VirtualAddress+size<=section.SizeOfRawData) {
                const auto offset=section.PointerToRawData+rva-section.VirtualAddress;
                if(offset+size>image.size())return false;
                std::memcpy(out,image.data()+offset,size);return true;
            }
        }
        for(std::size_t i=0;i<size;++i) {
            const auto p=memory.find(at+i);if(p==memory.end())return false;
            static_cast<unsigned char*>(out)[i]=p->second;
        }
        return true;
    };
    put(base+0x2274270,root);put(manager+0x828,callback);put(callback,base+0x1abe800);
    put(callback+8,manager);put(callback+0x10,base+0x32d40);
    put(manager+8,rig::List{batches,1,256});put(batches+0x10,rig::List{wrapper,1,256});
    put(wrapper,file);put(file+0x1618,player);put(file+0x1608,iggy);put(file+0x1658,name);
    put(wrapper+0xb08,std::array<float,2>{1920,1080});put(wrapper+0x878,rig::List{wrapper+0x900,2,512});
    put(name,"ui/notes.iggy");put(method,iggy);put(method+8,std::uintptr_t{0xd00000});
    put(receiver+8,iggy);put(receiver+0x18,receiver+0x28);put(receiver+0x20,std::uint32_t{0x05000001});
    put(file+0x1780,root_path);put(root_path,std::array<unsigned char,0x28>{});put(receiver+0x28,std::array<unsigned char,0x28>{});
    put(block+0x10,std::uint32_t{1});put(block+0x14,std::uint32_t{0});
    auto reader=read;NativeNoteKind kind;assert(kind.configure(reader,base));
    const auto event=[&](bool before,std::uintptr_t caller=0x790755,std::uint32_t result=1,std::uint64_t epoch=1) {
        kind.observe(reader,base,before,caller,method,receiver,block,result,epoch);
    };
    const auto current=[&](std::uint64_t epoch=1) {return kind.current(reader,base,file,iggy,epoch);};
    assert(!current());event(false);assert(!current()); // AFTER alone cannot authorize
    event(true);assert(!current());event(false);assert(current());
    const auto first=current()->generation;
    const auto any=kind.current(reader,base,1);
    assert(any && any->owner.file==file && any->iggy==iggy && any->generation==first);
    assert(!current(2));assert(!kind.current(reader,base,file,player,1));
    assert(!kind.current(reader,base+8,file,iggy,1));
    event(true);assert(!current());event(false);assert(current()->generation>first);
    event(true,0x790574);assert(!current()); // map revokes even if its binding is invalid
    put(block+0x10,std::uint32_t{5});put(block+0x14,std::uint32_t{5});
    event(true,0x790574);event(false,0x790574);assert(!current());
    put(block+0x10,std::uint32_t{1});put(block+0x14,std::uint32_t{0});
    event(true);event(false,0x790755,0);assert(!current());
    event(true);kind.clear();event(false);assert(!current());
    event(true);event(false,0x790755,1,2);assert(!current(2));
    event(true);put(file+0x1618,player+8);event(false);assert(!current());put(file+0x1618,player);
    event(true);event(false);assert(current());put(callback+8,manager+8);assert(!current());put(callback+8,manager);
    event(true);event(false);assert(current());put(file+0x1618,player+8);assert(!current());put(file+0x1618,player);
    assert(!kind.current(reader,base,1));
    event(true);event(false);put(file+0x1618,player+8);assert(!kind.current(reader,base,1));put(file+0x1618,player);
    put(receiver+0x28,std::array<unsigned char,0x28>{1});event(true);event(false);assert(!current());
    put(receiver+0x28,std::array<unsigned char,0x28>{});
    event(true,0x790754);event(false,0x790754);assert(!current());
    event(true);std::thread other([&]{event(false);});other.join();assert(!current());
    auto changed=[&](std::uintptr_t at,void* out,std::size_t size) {const bool ok=read(at,out,size);if(at==base+0x790380 && size)static_cast<unsigned char*>(out)[0]^=1;return ok;};
    assert(!kind.configure(changed,base));event(true);event(false);assert(!current());
    std::cout<<"PASS: pinned note/map call ABI, root/movie ownership, paired calls, generation/epoch, map/failure/reuse/thread rejection\n";
}
