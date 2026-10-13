#pragma once
// Typed adapter for the native command system (idCmdSystemLocal-like singleton, pinned Steam build
// 17576867, image base 0x140000000). Evidence, command list and unverified items: docs/native-console.md.
// NOTHING here is installed or called by default. Call buffer_command()/execute_now() ONLY from the
// game thread (the thread that runs the per-frame pump 0x140045460, e.g. the Stage0 scope); the
// command system has no lock around its text buffer.
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <tuple>
#include <type_traits>

namespace d2vr {

// Parsed command line passed to every command handler (built by 0x132630(args, text, 1)).
struct NativeCmdArgs {
    std::int32_t argc{};                // 0x000 includes the command name (argv[0])
    std::int32_t pad04{};
    const char* argv[128]{};            // 0x008 argv[0] = command name; max 0x80 entries
    char text[0x1000]{};                // 0x408 tokenised copy the argv pointers point into
};
static_assert(sizeof(NativeCmdArgs)==0x1408 && offsetof(NativeCmdArgs,argv)==0x08 && offsetof(NativeCmdArgs,text)==0x408);

class NativeConsole {
public:
    using Read=bool (*)(std::uintptr_t,void*,std::size_t);
    // All methods take the singleton as RCX (MS x64). Text must stay valid for the call only.
    using TextFn=void (__fastcall*)(void* system,const char* text);     // slots 0x40, 0x48, 0x70
    using BufferFn=void (__fastcall*)(void* system);                    // slot 0x50
    using ArgsFn=void (__fastcall*)(void* system,const NativeCmdArgs*); // slot 0x68

    // *(base+0x228bae0) = pointer to the static singleton (file value 0x14228bb00, i.e. object RVA 0x228bb00).
    inline static constexpr std::uintptr_t system_pointer_rva=0x228bae0,system_object_rva=0x228bb00,vtable_rva=0x1aef458;
    // vtable slot byte offsets (qword each) and their pinned targets
    inline static constexpr std::uintptr_t slot_register_builtin=0x08,slot_add_command=0x10,slot_remove_command=0x18,
        slot_execute_text=0x40,slot_append_text=0x48,slot_execute_buffer=0x50,slot_set_wait=0x58,
        slot_command_list=0x60,slot_execute_args=0x68,slot_insert_text=0x70;
    inline static constexpr std::uintptr_t add_command_rva=0xfeba0,execute_text_rva=0xffba0,append_text_rva=0xfec70,
        execute_buffer_rva=0xff8f0,set_wait_rva=0x12da30,command_list_rva=0x8832b0,execute_args_rva=0xffbe0,
        insert_text_rva=0xffff0,args_ctor_rva=0x132630,frame_pump_rva=0x45460;
    // command handlers (void(NativeCmdArgs*), no locking, game thread)
    inline static constexpr std::uintptr_t setviewpos_rva=0x886d10,getviewpos_rva=0x8861c0,loadgame_rva=0x658d00,
        god_rva=0x718f00,cheat_gate_rva=0x11d080; // cheat_gate = "mov al,1; ret"
    inline static constexpr std::uintptr_t game_pointer_rva=0x39a5290; // same game object as the other adapters
    inline static constexpr std::size_t max_line=480; // keeps well below the 0x1000 token buffer / 128 args
    // cvars toggled by `god` (names verified as strings; object RVAs 0x398d500 / 0x398d580 are inferred from
    // the god handler, confirm by reading the name pointer at +0x30 before writing +0x28)
    inline static constexpr const char* god_player_cvar="ark_god_player";
    inline static constexpr const char* god_npc_cvar="ark_god_npc";

    inline static constexpr std::array<unsigned char,32> add_command_code{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0x59,0x08,0x4d,0x8b};
    inline static constexpr std::array<unsigned char,32> execute_text_code{0x40,0x53,0xb8,0x30,0x14,0x00,0x00,0xe8,0x14,0xdc,0x0a,0x01,0x48,0x2b,0xe0,0x48,0x8b,0xd9,0x48,0x8d,0x4c,0x24,0x20,0x41,0xb0,0x01,0xe8,0x71,0x2a,0x03,0x00,0x48};
    inline static constexpr std::array<unsigned char,9> append_text_code{0x48,0x83,0xc1,0x18,0xe9,0xb7,0x18,0x04,0x00};
    inline static constexpr std::array<unsigned char,32> execute_buffer_code{0x40,0x53,0x41,0x55,0xb8,0x48,0x14,0x00,0x00,0xe8,0xc2,0xde,0x0a,0x01,0x48,0x2b,0xe0,0x4c,0x8b,0xe9,0x48,0x83,0xc1,0x18,0xe8,0xe3,0x46,0x04,0x00,0x85,0xc0,0x0f};
    inline static constexpr std::array<unsigned char,4> set_wait_code{0x89,0x51,0x10,0xc3};
    inline static constexpr std::array<unsigned char,32> execute_args_code{0x40,0x57,0x48,0x83,0xec,0x20,0x8b,0x02,0x48,0x8b,0xfa,0x85,0xc0,0x0f,0x84,0x8f,0x00,0x00,0x00,0x7e,0x0d,0x48,0x8b,0x42,0x08,0x80,0x38,0x5f,0x0f,0x84,0x80,0x00};
    inline static constexpr std::array<unsigned char,32> insert_text_code{0x48,0x8b,0xc4,0x57,0x48,0x81,0xec,0xd0,0x00,0x00,0x00,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x8b,0xda,0x48};
    inline static constexpr std::array<unsigned char,32> setviewpos_code{0x40,0x53,0x41,0x57,0x48,0x81,0xec,0x88,0x00,0x00,0x00,0x48,0x8b,0xd9,0x48,0x8b,0x0d,0x6b,0xe5,0x11,0x03,0xe8,0x06,0x1b,0x02,0x00,0x4c,0x8b,0xf8,0x48,0x85,0xc0};
    inline static constexpr std::array<unsigned char,32> getviewpos_code{0x48,0x8b,0xc4,0x55,0x48,0x8d,0x68,0xa1,0x48,0x81,0xec,0xf0,0x00,0x00,0x00,0x48,0xc7,0x45,0xcf,0xfe,0xff,0xff,0xff,0x0f,0x29,0x70,0xe8,0x0f,0x29,0x78,0xd8,0x48};
    inline static constexpr std::array<unsigned char,32> loadgame_code{0x48,0x8b,0xc4,0x41,0x56,0x48,0x81,0xec,0x40,0x01,0x00,0x00,0x48,0xc7,0x44,0x24,0x20,0xfe,0xff,0xff,0xff,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70};
    inline static constexpr std::array<unsigned char,32> god_code{0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x8b,0x01,0x48,0x8b,0xd9,0x83,0xf8,0x02,0x7c,0x7f,0x48,0x8d,0x3d,0x2d,0x99,0x39,0x01,0x83,0xf8,0x01,0x7e,0x06};
    inline static constexpr std::array<unsigned char,3> cheat_gate_code{0xb0,0x01,0xc3};

    // Code bytes at every RVA used, plus vtable targets. Does not need the singleton to be constructed.
    static bool verify(std::uintptr_t base,Read read) {
        if(!base || !read) return false;
        const auto code=[&](std::uintptr_t rva,const auto& expect) {
            std::array<unsigned char,std::tuple_size<std::decay_t<decltype(expect)>>::value> got{};
            return read(base+rva,got.data(),got.size()) && got==expect;
        };
        const auto qword=[&](std::uintptr_t at) {std::uint64_t v{};return read(at,&v,8)?v:0;};
        const auto slot=[&](std::uintptr_t offset,std::uintptr_t rva) {return qword(base+vtable_rva+offset)==base+rva;};
        return code(add_command_rva,add_command_code) && code(execute_text_rva,execute_text_code) &&
            code(append_text_rva,append_text_code) && code(execute_buffer_rva,execute_buffer_code) &&
            code(set_wait_rva,set_wait_code) && code(execute_args_rva,execute_args_code) &&
            code(insert_text_rva,insert_text_code) && code(setviewpos_rva,setviewpos_code) &&
            code(getviewpos_rva,getviewpos_code) && code(loadgame_rva,loadgame_code) && code(god_rva,god_code) &&
            code(cheat_gate_rva,cheat_gate_code) &&
            slot(slot_add_command,add_command_rva) && slot(slot_execute_text,execute_text_rva) &&
            slot(slot_append_text,append_text_rva) && slot(slot_execute_buffer,execute_buffer_rva) &&
            slot(slot_set_wait,set_wait_rva) && slot(slot_command_list,command_list_rva) &&
            slot(slot_execute_args,execute_args_rva) && slot(slot_insert_text,insert_text_rva);
    }
    // The singleton, or 0 if not constructed / vtable mismatch.
    static std::uintptr_t system(std::uintptr_t base,Read read) {
        std::uintptr_t object{},vtable{};
        if(!base || !read || !read(base+system_pointer_rva,&object,8) || !object) return 0;
        if(!read(object,&vtable,8) || vtable!=base+vtable_rva) return 0;
        return object;
    }
    // One command line: printable ASCII, no CR/LF/NUL, non-empty, <= max_line. ';' is accepted but the
    // engine splits on it (and on newline) and ignores ';' inside double quotes.
    static bool valid_line(const char* text) {
        if(!text) return false;
        const auto n=std::strlen(text);
        if(!n || n>max_line) return false;
        for(std::size_t i=0;i<n;++i) if(static_cast<unsigned char>(text[i])<0x20 || static_cast<unsigned char>(text[i])>0x7e) return false;
        return true;
    }
    static std::uintptr_t slot_target(std::uintptr_t object,Read read,std::uintptr_t slot) {
        std::uintptr_t vtable{},fn{};
        return object && read(object,&vtable,8) && vtable && read(vtable+slot,&fn,8) ? fn : 0;
    }
    // Append "text\n" to the command buffer (slot 0x48, idStr append at this+0x18); the engine runs it at
    // the next frame pump (0x45460 -> slot 0x50) on the game thread. GAME THREAD ONLY (no lock, the buffer
    // is reallocated and consumed in place by execute_buffer).
    static bool buffer_command(std::uintptr_t base,Read read,const char* text) {
        if(!valid_line(text)) return false;
        const auto object=system(base,read);
        const auto fn=slot_target(object,read,slot_append_text);
        if(!fn || fn!=base+append_text_rva) return false;
        const std::string line=std::string(text)+"\n";
        reinterpret_cast<TextFn>(fn)(reinterpret_cast<void*>(object),line.c_str());
        return true;
    }
    // Execute immediately on the calling thread (slot 0x40: tokenise + dispatch). GAME THREAD ONLY.
    static bool execute_now(std::uintptr_t base,Read read,const char* text) {
        if(!valid_line(text)) return false;
        const auto object=system(base,read);
        const auto fn=slot_target(object,read,slot_execute_text);
        if(!fn || fn!=base+execute_text_rva) return false;
        reinterpret_cast<TextFn>(fn)(reinterpret_cast<void*>(object),text);
        return true;
    }
    // `setviewpos x y z [yaw [pitch]]` (argc >= 4 incl. name). z is the VIEW (eye) height; the handler
    // subtracts a runtime player view-height offset (UNVERIFIED value). yaw/pitch degrees, metres (UNVERIFIED).
    static bool format_setviewpos(char* out,std::size_t size,double x,double y,double z,double yaw=0,double pitch=0) {
        if(!out || !size || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(yaw) || !std::isfinite(pitch)) return false;
        const int n=std::snprintf(out,size,"setviewpos %.4f %.4f %.4f %.2f %.2f",x,y,z,yaw,pitch);
        return n>0 && static_cast<std::size_t>(n)<size;
    }
};

} // namespace d2vr
