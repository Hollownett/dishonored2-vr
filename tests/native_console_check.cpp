#include "../src/native_console.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
using namespace d2vr;
#define CHECK(x) do{if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using C=NativeConsole;
std::map<std::uintptr_t,std::vector<unsigned char>> memory;
bool read_mem(std::uintptr_t at,void* out,std::size_t size) {
    for(const auto& [start,bytes]:memory) if(at>=start && at+size<=start+bytes.size()) {std::memcpy(out,bytes.data()+(at-start),size);return true;}
    return false;
}
void put(std::uintptr_t at,const void* p,std::size_t n) {const auto* b=static_cast<const unsigned char*>(p);memory[at]=std::vector<unsigned char>(b,b+n);}
void put64(std::uintptr_t at,std::uint64_t v) {put(at,&v,8);}
template<class A> void put_code(std::uintptr_t at,const A& a) {put(at,a.data(),a.size());}
std::string last_text;void* last_system{};unsigned appended{};
void __fastcall fake_append(void* sys,const char* text) {last_system=sys;last_text=text;++appended;}
void __fastcall fake_exec(void* sys,const char* text) {last_system=sys;last_text=text;++appended;}

// Fake image: base chosen so base+append_text_rva / execute_text_rva are real, callable fakes is not possible
// for both at once, so call tests use a second base per function.
void build(std::uintptr_t base,std::uintptr_t object) {
    memory.clear();
    put_code(base+C::add_command_rva,C::add_command_code);put_code(base+C::execute_text_rva,C::execute_text_code);
    put_code(base+C::append_text_rva,C::append_text_code);put_code(base+C::execute_buffer_rva,C::execute_buffer_code);
    put_code(base+C::set_wait_rva,C::set_wait_code);put_code(base+C::execute_args_rva,C::execute_args_code);
    put_code(base+C::insert_text_rva,C::insert_text_code);put_code(base+C::setviewpos_rva,C::setviewpos_code);
    put_code(base+C::getviewpos_rva,C::getviewpos_code);put_code(base+C::loadgame_rva,C::loadgame_code);
    put_code(base+C::god_rva,C::god_code);put_code(base+C::cheat_gate_rva,C::cheat_gate_code);
    const auto vt=base+C::vtable_rva;
    put64(vt+C::slot_add_command,base+C::add_command_rva);put64(vt+C::slot_execute_text,base+C::execute_text_rva);
    put64(vt+C::slot_append_text,base+C::append_text_rva);put64(vt+C::slot_execute_buffer,base+C::execute_buffer_rva);
    put64(vt+C::slot_set_wait,base+C::set_wait_rva);put64(vt+C::slot_command_list,base+C::command_list_rva);
    put64(vt+C::slot_execute_args,base+C::execute_args_rva);put64(vt+C::slot_insert_text,base+C::insert_text_rva);
    put64(base+C::system_pointer_rva,object);put64(object,vt);
}
int main() {
    static_assert(C::slot_add_command==0x10 && C::slot_execute_text==0x40 && C::slot_append_text==0x48 &&
                  C::slot_execute_buffer==0x50 && C::slot_execute_args==0x68 && C::slot_insert_text==0x70);
    CHECK(C::valid_line("setviewpos 1 2 3") && C::valid_line("LoadGame 2"));
    CHECK(!C::valid_line(nullptr) && !C::valid_line("") && !C::valid_line("a\nb") && !C::valid_line("a\rb") &&
          !C::valid_line("tab\t") && !C::valid_line(std::string(C::max_line+1,'a').c_str()) &&
          C::valid_line(std::string(C::max_line,'a').c_str()));
    char buf[96];
    CHECK(C::format_setviewpos(buf,sizeof buf,1.5,-2,3.25,90) && std::string(buf)=="setviewpos 1.5000 -2.0000 3.2500 90.00 0.00");
    CHECK(!C::format_setviewpos(buf,sizeof buf,NAN,0,0) && !C::format_setviewpos(buf,8,1,2,3) && !C::format_setviewpos(nullptr,8,1,2,3));

    const std::uintptr_t base=0x7ff600000000ull,object=base+C::system_object_rva;
    build(base,object);
    CHECK(C::verify(base,read_mem) && !C::verify(0,read_mem) && !C::verify(base,nullptr));
    CHECK(C::system(base,read_mem)==object);
    // corrupt one code byte
    {auto saved=memory[base+C::setviewpos_rva];memory[base+C::setviewpos_rva][5]^=1;CHECK(!C::verify(base,read_mem));memory[base+C::setviewpos_rva]=saved;}
    // corrupt a vtable slot
    {put64(base+C::vtable_rva+C::slot_append_text,1);CHECK(!C::verify(base,read_mem));put64(base+C::vtable_rva+C::slot_append_text,base+C::append_text_rva);}
    CHECK(C::verify(base,read_mem));
    // wrong vtable on the object -> system() refuses
    put64(object,base+0x1234);CHECK(!C::system(base,read_mem));put64(object,base+C::vtable_rva);
    put64(base+C::system_pointer_rva,0);CHECK(!C::system(base,read_mem));put64(base+C::system_pointer_rva,object);
    CHECK(!C::buffer_command(base,read_mem,"bad\ntext") && appended==0);
    // call path: make the slot target a real fake (base2 chosen so base2+rva == &fake)
    {
        const std::uintptr_t base2=reinterpret_cast<std::uintptr_t>(&fake_append)-C::append_text_rva,obj2=0x2000;
        build(base2,obj2);
        CHECK(C::buffer_command(base2,read_mem,"setviewpos 1 2 3") && appended==1 &&
              last_text=="setviewpos 1 2 3\n" && reinterpret_cast<std::uintptr_t>(last_system)==obj2);
        CHECK(!C::buffer_command(base2,read_mem,"") && appended==1);
        const std::uintptr_t base3=reinterpret_cast<std::uintptr_t>(&fake_exec)-C::execute_text_rva;
        build(base3,obj2);
        CHECK(C::execute_now(base3,read_mem,"god player") && last_text=="god player" && appended==2);
    }
    std::puts("native_console_check ok");
    return 0;
}
