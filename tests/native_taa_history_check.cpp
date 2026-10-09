#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_taa_history.hpp"
#include <cstring>
#include <iostream>
#include <map>
#include <string>

using namespace d2vr::taa_history;
void require(bool passed,const char* message) {if(!passed) throw std::runtime_error(message);}
static bool read_memory(std::uintptr_t address,void* out,std::size_t size) {
    if(!address) return false;
    std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;
}

// Synthetic engine memory with the verified offsets.
struct FakeImage {alignas(16) std::array<unsigned char,0x148> bytes{};};
static void make_image(FakeImage& image,std::uint32_t format,std::uintptr_t resource) {
    const auto at=reinterpret_cast<std::uintptr_t>(image.bytes.data());
    const std::uint64_t back=at;std::memcpy(image.bytes.data()+image_handle,&back,8);
    std::array<std::uint32_t,desc_words> desc{};desc[0]=0;desc[1]=format;desc[2]=2496;desc[3]=2688;
    std::memcpy(image.bytes.data()+image_desc,desc.data(),sizeof desc);
    std::memcpy(image.bytes.data()+image_resource,&resource,8);
}
static std::uintptr_t handle(FakeImage& image) {return reinterpret_cast<std::uintptr_t>(image.bytes.data())+image_handle;}

// Engine model: role parity p reads entry[p&1] and writes entry[p&1^1]. Swapping roles
// (read_first=false) covers the unverified opposite assignment.
struct Engine {
    std::map<std::uint64_t,std::string> content;
    std::string pass(const std::array<std::uint64_t,2>& entry,std::uint32_t parity,const std::string& written,bool read_first) {
        const auto read=entry[(parity&1)^(read_first?0u:1u)],write=entry[(parity&1)^(read_first?1u:0u)];
        const auto previous=content[read];content[write]=written;return previous;
    }
};
// Two eyes per frame. per_view_counter selects whether the engine counter advances per frame or per pass.
static void simulate(bool per_view_counter,bool read_first,bool fixed) {
    Engine engine;std::array<Alternation,2> alternation;
    const std::array<std::uint64_t,2> shared{100,101},twin{200,201};
    std::uint32_t counter=7;
    for(int frame=0;frame<12;++frame) for(int eye=0;eye<2;++eye) {
        if(per_view_counter || eye==0) ++counter;
        auto pair=fixed && eye==1?twin:shared;
        const auto entry=fixed?assignment(pair,alternation[static_cast<std::size_t>(eye)].flip(counter)):pair;
        const auto label=std::to_string(eye)+":"+std::to_string(frame);
        const auto previous=engine.pass(entry,counter,label,read_first);
        if(fixed) alternation[static_cast<std::size_t>(eye)].advance();
        if(frame>1) {
            const auto expected=std::to_string(eye)+":"+std::to_string(frame-1);
            if(fixed) require(previous==expected,"An eye did not reproject its own previous history");
            else if(!per_view_counter && eye==0) require(previous=="1:"+std::to_string(frame-1),"Model does not reproduce the shared-history defect");
        }
    }
}

int main() {
    try {
        require(name_hash("temporalAAHistory0")==0x3337e277u && name_hash("temporalAAHistory1")==0x0d35680eu,"History hashes");
        require(name_hash("hdrBuffer2")==0x8e999aedu && name_hash("distoVectors")==0x1bbe2d1au,"Other pass hashes");
        require(param_count(0xab000003u)==3,"Parameter count mask");

        std::array<ParamEntry,5> entries{{{0x8f93ab75u,0,1},{history_hashes[1],0,2},{history_hashes[0],0,3},{history_hashes[0],0,4},{1,0,5}}};
        auto slots=find_history(entries.data(),5);
        require(slots.complete() && slots.index[0]==2 && slots.index[1]==1,"First history matches");
        require(!find_history(entries.data(),2).complete(),"Missing history is incomplete");

        require(temporal_runs(0,0) && !temporal_runs(1,0) && !temporal_runs(0,4) && temporal_runs(2,0x3b),"TAA gate");
        require(eye_of(10,10,20)==0 && eye_of(20,10,20)==1 && eye_of(30,10,20)==-1 && eye_of(10,10,10)==-1 && eye_of(0,0,20)==-1,"Eye identity");
        require(handle_of(0x1098,0x1000) && !handle_of(0x1098,0x1001) && !handle_of(0x10,0),"Handle back-pointer");

        D3D11_TEXTURE2D_DESC a{};a.Width=2496;a.Height=2688;a.MipLevels=1;a.ArraySize=1;a.Format=DXGI_FORMAT_R11G11B10_FLOAT;
        a.SampleDesc.Count=1;a.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET|D3D11_BIND_UNORDERED_ACCESS;
        auto b=a;require(same_texture(a,b),"Equal texture descs");
        b.Width=2400;require(!same_texture(a,b),"Width change detected");
        b=a;b.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;require(!same_texture(a,b),"Format change detected");

        for(bool per_view:{false,true}) for(bool read_first:{false,true}) {simulate(per_view,read_first,true);simulate(per_view,read_first,false);}

        // Plan, apply and restore against synthetic job/view/table memory.
        static FakeImage s0,s1,t0,t1;
        make_image(s0,26,0x5000);make_image(s1,26,0x6000);make_image(t0,26,0x7000);make_image(t1,26,0x8000);
        alignas(16) std::array<unsigned char,0x3b00> view{};
        alignas(16) std::array<ParamEntry,3> table{{{0x8f93ab75u,0,1},{history_hashes[0],0,handle(s0)},{history_hashes[1],0,handle(s1)}}};
        alignas(16) std::array<unsigned char,0x120> ctx{};
        const auto view_at=reinterpret_cast<std::uintptr_t>(view.data()),table_at=reinterpret_cast<std::uintptr_t>(table.data());
        const std::uint32_t count=0x5a000003u;
        std::memcpy(ctx.data()+ctx_view,&view_at,8);std::memcpy(ctx.data()+ctx_params,&table_at,8);std::memcpy(ctx.data()+ctx_param_count,&count,4);
        std::uint32_t counter=11;const auto base=reinterpret_cast<std::uintptr_t>(&counter)-frame_counter_rva;
        const auto ctx_at=reinterpret_cast<std::uintptr_t>(ctx.data());
        PassInputs in{base,{0x1234,view_at},false,{},{}};
        std::array<Alternation,2> alternation;std::vector<ParamEntry> scratch;PassPlan plan;
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::rebuild,"Right eye waits for twins");
        require(snapshot(read_memory,reinterpret_cast<std::uintptr_t>(s0.bytes.data()),in.originals[0]) &&
                snapshot(read_memory,reinterpret_cast<std::uintptr_t>(s1.bytes.data()),in.originals[1]) &&
                snapshot(read_memory,reinterpret_cast<std::uintptr_t>(t0.bytes.data()),in.twins[0]) &&
                snapshot(read_memory,reinterpret_cast<std::uintptr_t>(t1.bytes.data()),in.twins[1]),"Snapshots");
        require(in.twins[0].same_layout(in.originals[0]),"Twin layout matches");
        in.twins_ready=true;
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::planned && plan.eye==1 && plan.write,"Right eye planned");
        require(plan.replaced[0]==handle(t0) && plan.replaced[1]==handle(t1),"First right pass keeps engine roles");
        apply_plan(plan);
        require(table[1].value==handle(t0) && table[2].value==handle(t1) && table[0].value==1,"Only history entries replaced");
        restore_plan(plan);
        require(table[1].value==handle(s0) && table[2].value==handle(s1),"Entries restored");
        alternation[1].advance();counter+=2; // engine counter moved twice: roles must swap for this eye
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::planned &&
                plan.replaced[0]==handle(t1) && plan.replaced[1]==handle(t0),"Own alternation overrides engine parity");
        apply_plan(plan);table[1].value=42;restore_plan(plan); // a foreign write is never undone
        require(table[1].value==42 && table[2].value==handle(s1),"Restore is compare-exchange");
        table[1].value=handle(s0);

        in.eye_views={view_at,0x1234};
        alternation[0].reset();
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::planned && plan.eye==0 && !plan.write,"Left eye uses shared pair");
        view[view_flags_b]=4;
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::idle,"Skipped TAA is idle");
        view[view_flags_b]=0;in.eye_views={0x1234,view_at};
        std::uint32_t wider=2400;std::memcpy(s0.bytes.data()+image_desc+8,&wider,4);
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::rebuild,"Resized original forces rebuild");
        std::uint32_t bad=0;std::memcpy(ctx.data()+ctx_param_count,&bad,4);
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::fallback,"Empty table falls back");
        std::cout<<"native TAA history checks passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
