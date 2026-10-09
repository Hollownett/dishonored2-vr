#include "../src/gameplay_aim.hpp"
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    const GameplayAim native{{1,2,3},{1,0,0,0,1,0,0,0,1}};
    const GameplayAim planned{{1,2.5f,3},{0,1,0,-1,0,0,0,0,1}};
    GameplayAim memory=native;
    unsigned reads=0,writes=0;int fail_read=0,fail_write=0;bool corrupt=false;
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {
        assert(at==0x10000 && size==48);++reads;
        if(static_cast<int>(reads)==fail_read) return false;
        std::memcpy(out,&memory,size);return true;
    };
    auto write=[&](std::uintptr_t at,const void* in,std::size_t size) {
        assert(at==0x10000 && size==48);++writes;
        if(static_cast<int>(writes)==fail_write) {
            // Model a partially failed write; the transaction must restore it.
            std::memcpy(&memory,in,12);return false;
        }
        std::memcpy(&memory,in,size);
        if(corrupt && writes==1) memory.axis[0]=.125f;
        return true;
    };
    auto reset=[&] {memory=native;reads=writes=0;fail_read=fail_write=0;corrupt=false;};
    assert(write_gameplay_aim(read,write,0x10000,native,planned)==AimWriteResult::applied);
    assert(std::memcmp(&memory,&planned,48)==0 && writes==1);
    reset();memory.origin[0]=10;
    assert(write_gameplay_aim(read,write,0x10000,native,planned)==AimWriteResult::changed_before_write && writes==0);
    reset();fail_read=1;
    assert(write_gameplay_aim(read,write,0x10000,native,planned)==AimWriteResult::changed_before_write && writes==0);
    reset();fail_write=1;
    assert(write_gameplay_aim(read,write,0x10000,native,planned)==AimWriteResult::write_failed);
    assert(std::memcmp(&memory,&native,48)==0 && writes==2);
    reset();fail_read=2;
    assert(write_gameplay_aim(read,write,0x10000,native,planned)==AimWriteResult::write_failed);
    assert(std::memcmp(&memory,&native,48)==0);
    reset();corrupt=true;
    assert(write_gameplay_aim(read,write,0x10000,native,planned)==AimWriteResult::write_failed);
    assert(std::memcmp(&memory,&native,48)==0);
    reset();corrupt=true;fail_write=2;
    assert(write_gameplay_aim(read,write,0x10000,native,planned)==AimWriteResult::rollback_failed);
    assert(std::memcmp(&memory,&native,48)!=0);
    std::cout<<"PASS: full-pose publication, changed source rejection, partial-write/readback failures, verified rollback and rollback failure\n";
}
