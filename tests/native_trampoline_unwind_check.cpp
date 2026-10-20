#include "../src/native_trampoline_unwind.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

// Two hook owners share one MinHook-like page. Each registers a tight table (base = trampoline, unwind block above it); every trampoline must resolve to its own entry
// with ImageBase == its trampoline. A table based below the page (old layout) shadows later tables on Wine.
namespace {
struct Block {std::array<RUNTIME_FUNCTION,2> functions{};std::array<unsigned char,8> unwind{1,5,2,0,5,0x34,1,0};std::array<unsigned char,4> relay{1,0,0,0};};
Block* add_table(std::uintptr_t trampoline) {
    auto* block=static_cast<Block*>(d2vr::alloc_unwind_above(trampoline));assert(block);
    const auto address=reinterpret_cast<std::uintptr_t>(block);assert(address>trampoline && (address&0xffff)==0 && address-trampoline<UINT32_MAX-4096);
    *block=Block{};const auto rva=address-trampoline;
    block->functions={RUNTIME_FUNCTION{0,19,static_cast<DWORD>(rva+offsetof(Block,unwind))},RUNTIME_FUNCTION{19,33,static_cast<DWORD>(rva+offsetof(Block,relay))}};
    for(DWORD64 probe=trampoline;probe<trampoline+33;++probe) {DWORD64 image{};assert(!RtlLookupFunctionEntry(probe,&image,nullptr));}
    assert(RtlAddFunctionTable(block->functions.data(),2,trampoline));return block;
}
void check_lookup(std::uintptr_t trampoline,Block* block) {
    for(unsigned offset=0;offset<33;++offset) {
        DWORD64 image{};const auto* function=RtlLookupFunctionEntry(trampoline+offset,&image,nullptr);
        assert(function==&block->functions[offset<19?0:1] && image==trampoline);
    }
}
}
int main() {
    auto* page=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(page);
    const auto first=reinterpret_cast<std::uintptr_t>(page);
    // Occupy the first candidate slot above the page so the upward search must step past it.
    const auto occupied=VirtualAlloc(reinterpret_cast<void*>((first&~std::uintptr_t{0xffff})+0x10000),4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    std::array<std::uintptr_t,4> trampolines{first+0x40,first+0x80,first+0xc0,first+0x100};std::array<Block*,4> blocks{};
    for(unsigned index=0;index<trampolines.size();++index) {
        blocks[index]=add_table(trampolines[index]);
        for(unsigned earlier=0;earlier<=index;++earlier)check_lookup(trampolines[earlier],blocks[earlier]);
    }
    for(unsigned left=0;left<blocks.size();++left)for(unsigned right=left+1;right<blocks.size();++right)assert(blocks[left]!=blocks[right]);
    for(unsigned index=0;index<blocks.size();++index) {
        assert(RtlDeleteFunctionTable(blocks[index]->functions.data()));
    }
    assert(occupied);
    std::cout<<"PASS: tight trampoline unwind tables coexist in one page\n";return 0;
}
