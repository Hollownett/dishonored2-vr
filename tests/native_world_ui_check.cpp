#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_world_ui.hpp"
#include "../src/world_ui_pose.hpp"
#include <unordered_map>
#include <cassert>
#include <iostream>
int main() {
    using namespace d2vr;
    constexpr std::uintptr_t base=0x7ff700000000,root=0x100000,callback=0x200000,batches=0x300000,
        wrapper=0x400000,file=0x500000,player=0x600000,iggy=0x700000,name=0x800000;
    constexpr auto manager=root+0x18;
    std::unordered_map<std::uintptr_t,unsigned char> memory;
    auto put=[&](std::uintptr_t at,const auto& value) {const auto* p=reinterpret_cast<const unsigned char*>(&value);for(unsigned i=0;i<sizeof(value);++i) memory[at+i]=p[i];};
    auto path=[&](const char* text) {for(unsigned i=0;i<256;++i) memory[name+i]=0;for(std::size_t i=0;i<std::strlen(text);++i) memory[name+i]=static_cast<unsigned char>(text[i]);};
    auto read=[&](std::uintptr_t at,void* out,std::size_t size) {for(std::size_t i=0;i<size;++i) {const auto p=memory.find(at+i);if(p==memory.end())return false;static_cast<unsigned char*>(out)[i]=p->second;}return true;};
    put(base+0x2274270,root);put(manager+0x828,callback);put(callback,base+0x1abe800);put(callback+8,manager);put(callback+0x10,base+0x32d40);
    put(manager+8,rig::List{batches,1,256});put(batches+0x10,rig::List{wrapper,1,256});
    put(wrapper,file);put(file+0x1618,player);put(file+0x1608,iggy);put(file+0x1658,name);
    put(wrapper+0xb08,std::array<float,2>{1920,1080});put(wrapper+0x878,rig::List{wrapper+0x900,2,512});
    for(const auto& entry:std::array<std::pair<const char*,WorldUiKind>,3>{{{"ui/quick_access_wheel.iggy",WorldUiKind::wheel},{"ui/notes.iggy",WorldUiKind::notes},{"ui/journal.iggy",WorldUiKind::journal}}}) {
        path(entry.first);const auto owner=native_world_ui_owner(read,base,iggy);
        assert(owner && owner->kind==entry.second && owner->menu.file==file);
        assert(!native_world_ui_owner(read,base,player)); // Input player is not the render Iggy object.
    }
    path("ui/choice_emily.iggy");{const auto o=native_world_ui_owner(read,base,iggy);assert(o && o->kind==WorldUiKind::journal);} // dialogue choices
    // v1144: the wheel's frame removal (Unframe) runs only for WorldUiKind::wheel, and that kind is the quick access wheel alone.
    for(const char* other:{"ui/notes.iggy","ui/journal.iggy","ui/choice_corvo.iggy","ui/choice_emily.iggy","ui/narrative.iggy","ui/corvo_blackmarket.iggy",
                           "ui/emily_blackmarket.iggy","ui/hud.iggy","ui/lib_wheel.iggy","ui/pausemenu.iggy"}) assert(world_ui_kind(other)!=WorldUiKind::wheel);
    assert(world_ui_kind("ui/quick_access_wheel.iggy")==WorldUiKind::wheel);
    path("ui/narrative.iggy");{const auto o=native_world_ui_owner(read,base,iggy);assert(narrative_world_ui?(o && o->kind==WorldUiKind::journal):!o);} // v724: the choices' real movie
    path("ui/choice_emily.iggy");
    // v722: a newer batch above the one that holds the choice movie (a scripted scene) does not hide it
    put(manager+8,rig::List{batches,2,512});put(batches+32+0x10,rig::List{0x380000,0,256});
    {const auto o=native_world_ui_owner(read,base,iggy);assert(o && o->kind==WorldUiKind::journal);}
    put(manager+8,rig::List{batches,1,256});
    assert(world_ui_logic(512)==WorldUiKind::journal && world_ui_logic(2)==WorldUiKind::none);
    assert(native_note_reader_logic(16) && native_note_reader_logic(128|16));
    assert(world_ui_logic(128|16)==WorldUiKind::notes); // Actual journal -> Read combines both native bits.
    for(auto logic:{2u,4u,128u,256u,512u,1024u,18u,48u,146u})assert(!native_note_reader_logic(logic));
    assert(world_ui_logic(1024)==WorldUiKind::journal && world_ui_kind("ui/emily_blackmarket.iggy")==WorldUiKind::journal &&
           world_ui_kind("ui/lib_blackmarket.iggy")==WorldUiKind::none); // black market panel
    path("ui/hud.iggy");assert(!native_world_ui_owner(read,base,iggy));
    path("mod/ui/notes.iggy");assert(!native_world_ui_owner(read,base,iggy));
    path("ui/notes.iggy");put(callback+8,manager+8);assert(!native_world_ui_owner(read,base,iggy));put(callback+8,manager);
    for(unsigned i=0;i<256;++i)memory[name+i]='a';assert(!native_world_ui_owner(read,base,iggy));
    path("ui/notes.iggy");
    auto recycled=[&](std::uintptr_t at,void* out,std::size_t size) {const bool ok=read(at,out,size);if(at==name)put(file+0x1618,player+8);return ok;};
    assert(!native_world_ui_owner(recycled,base,iggy));
    assert(!NativeWorldUi::current);
    const NativeWorldUiDraw selected{WorldUiKind::wheel,file,iggy,0x900000,0xa00000};
    assert(native_world_ui_target_allowed(&selected,base,base+0x2dd632));
    assert(!native_world_ui_target_allowed(&selected,base,base+0x2dd62d));
    assert(!native_world_ui_target_allowed(&selected,base,base+0x31a5f0));
    assert(!native_world_ui_target_allowed(nullptr,base,base+0x2dd632));
    const NativeWorldUiDraw unknown{};
    assert(!native_world_ui_target_allowed(&unknown,base,base+0x2dd632));
    assert(!world_ui_quad_allowed(true,true,-1) && world_ui_quad_allowed(true,true,0) && world_ui_quad_allowed(true,false,-1) &&
           world_ui_quad_allowed(false,true,-1) && world_ui_quad_allowed(false,false,-1));
    // v1038: the wheel movie is captured off-screen under any logic (its opening/closing frames outside 256 were drawn flat); the others
    // only under their own logic, narrative only in the dialogue choices.
    assert(world_ui_capture_allowed(WorldUiKind::wheel,256,false) && world_ui_capture_allowed(WorldUiKind::wheel,2,false) &&
           world_ui_capture_allowed(WorldUiKind::wheel,4,false));
    assert(world_ui_capture_allowed(WorldUiKind::notes,16,false) && !world_ui_capture_allowed(WorldUiKind::notes,2,false));
    assert(world_ui_capture_allowed(WorldUiKind::journal,128,false) && !world_ui_capture_allowed(WorldUiKind::journal,2,false));
    assert(world_ui_capture_allowed(WorldUiKind::journal,512,true) && !world_ui_capture_allowed(WorldUiKind::journal,2,true) &&
           !world_ui_capture_allowed(WorldUiKind::journal,128,true));
    assert(!world_ui_capture_allowed(WorldUiKind::none,2,false) && !world_ui_capture_allowed(WorldUiKind::none,256,false));
    std::cout<<"PASS: exact native wheel/notes/journal movie ownership, render/input distinction and recycled owner rejection\n";
}
