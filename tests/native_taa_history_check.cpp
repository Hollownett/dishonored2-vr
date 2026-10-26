#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../src/native_taa_history.hpp"
#include "../src/post_tonemap_pass.hpp"
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
static unsigned fallback_calls{};
static unsigned long long __fastcall fallback_pass(void* context) {
    auto bytes=static_cast<unsigned char*>(context);++fallback_calls;
    require((bytes[view_flags_b]&4)!=0,"Fallback established the temporal skip flag");
    bytes[view_flags_b]|=0x20; // unrelated engine flags must survive cleanup
    return 73;
}
static unsigned long long __fastcall fallback_throw(void* context) {
    fallback_pass(context);throw std::runtime_error("injected native exception");
}
static unsigned long long __fastcall fallback_new_frame(void* context) {
    fallback_pass(context);
    auto stamp=reinterpret_cast<std::uint32_t*>(static_cast<unsigned char*>(context)+d2vr::alternate_taa::stamp_offset);
    ++*stamp;return 74;
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

static void post_tonemap_tracks_admitted_logical_eye() {
    using d2vr::PostTonemapPass;
    static d2vr::alternate_taa::ReceiptBook book;
    static std::uint32_t stamp{};
    book.reset();PostTonemapPass::eye_views[0]=100;PostTonemapPass::eye_views[1]=200;
    require(PostTonemapPass::logical_eye(100)==0 && PostTonemapPass::logical_eye(200)==1,"Paired post-tonemap identities changed");
    PostTonemapPass::eye_resolver=+[](std::uintptr_t view,int native) {
        if(view!=100)return native;
        const auto receipt=book.resolve(view,stamp);
        return receipt?receipt->key.eye:-1;
    };
    for(std::uint32_t frame=10;frame<18;++frame) {
        stamp=frame;d2vr::alternate_taa::Receipt receipt;
        receipt.key={100,300,{400,500,0},1,2,frame,frame,1200,1300,static_cast<int>(frame&1u),true};
        require(book.publish(receipt),"Could not publish alternate post-tonemap receipt");
        require(PostTonemapPass::logical_eye(100)==-1,"Unadmitted reused view was treated as native eye zero");
        require(book.admit(100,frame,frame,1,2).has_value(),"Could not admit alternate post-tonemap receipt");
        require(PostTonemapPass::logical_eye(100)==static_cast<int>(frame&1u),"Post-tonemap lost the logical eye on the reused native view");
    }
    book.reset();require(PostTonemapPass::logical_eye(100)==-1,"Invalidated alternate view must not run an upscaler for the wrong eye");
    require(PostTonemapPass::logical_eye(200)==1,"Alternate resolver changed an unrelated paired view");
    PostTonemapPass::eye_resolver=nullptr;PostTonemapPass::eye_views[0]=0;PostTonemapPass::eye_views[1]=0;
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

        // Optional actual-pass metadata: raw stamp zero is present, and reads
        // must reject torn stamp/projection snapshots without changing legacy planning.
        alignas(16) std::array<unsigned char,0xc000> metadata_view{};
        const auto metadata_at=reinterpret_cast<std::uintptr_t>(metadata_view.data());
        const std::array<float,4> resident_jitter{.1f,.2f,-.1f,-.2f};
        const std::array<float,16> resident_projection{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        std::memcpy(metadata_view.data()+d2vr::alternate_taa::jitter_offset,resident_jitter.data(),16);
        std::memcpy(metadata_view.data()+d2vr::alternate_taa::current_matrix_offsets[0],resident_projection.data(),64);
        const auto metadata=read_pass_metadata(read_memory,metadata_at);
        require(metadata && metadata->counter==0 && metadata->jitter_uv==resident_jitter &&
                metadata->projection==resident_projection,"Actual zero-stamp pass metadata retained resident projection/jitter");
        unsigned stamp_reads{};
        const auto torn=[&](std::uintptr_t address,void* out,std::size_t size) {
            const bool result=read_memory(address,out,size);
            if(address==metadata_at+d2vr::alternate_taa::stamp_offset && ++stamp_reads==1) {
                const std::uint32_t changed=1;std::memcpy(metadata_view.data()+d2vr::alternate_taa::stamp_offset,&changed,4);
            }
            return result;
        };
        require(!read_pass_metadata(torn,metadata_at),"Changed native stamp rejects optional metadata");
        const auto unreadable_projection=[&](std::uintptr_t address,void* out,std::size_t size) {
            return address!=metadata_at+d2vr::alternate_taa::current_matrix_offsets[0] && read_memory(address,out,size);
        };
        require(!read_pass_metadata(unreadable_projection,metadata_at),"Unreadable projection rejects only metadata");

        // Plan, apply and restore against synthetic job/view/table memory.
        static FakeImage s0,s1,t0,t1;
        make_image(s0,26,0x5000);make_image(s1,26,0x6000);make_image(t0,26,0x7000);make_image(t1,26,0x8000);
        alignas(16) std::array<unsigned char,0xc000> view{};
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
        // v613: FSR/DLSS own the TAA dispatch, so the right eye is marked from its first pass on the untouched engine pair.
        in.upscaler_owns_taa=true;
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::planned && plan.eye==1 && plan.twins_pending &&
                !plan.write && plan.replaced==plan.engine && plan.engine[0]==handle(s0) && plan.engine[1]==handle(s1),"Upscaler right eye planned without twins");
        in.upscaler_owns_taa=false;
        require(snapshot(read_memory,reinterpret_cast<std::uintptr_t>(s0.bytes.data()),in.originals[0]) &&
                snapshot(read_memory,reinterpret_cast<std::uintptr_t>(s1.bytes.data()),in.originals[1]) &&
                snapshot(read_memory,reinterpret_cast<std::uintptr_t>(t0.bytes.data()),in.twins[0]) &&
                snapshot(read_memory,reinterpret_cast<std::uintptr_t>(t1.bytes.data()),in.twins[1]),"Snapshots");
        require(in.twins[0].same_layout(in.originals[0]),"Twin layout matches");
        in.twins_ready=true;
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::planned && plan.eye==1 && plan.write,"Right eye planned");
        require(plan.replaced[0]==handle(t0) && plan.replaced[1]==handle(t1),"First right pass keeps engine roles");
        require(!plan.twins_pending,"Ready twins are used");
        in.upscaler_owns_taa=true;
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::planned && !plan.twins_pending &&
                plan.replaced[0]!=handle(s0),"Upscaler right eye keeps ready twins");
        in.upscaler_owns_taa=false;alternation[1].reset();
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::planned && plan.replaced[0]==handle(t0) && plan.replaced[1]==handle(t1),"Right pass after reset keeps engine roles");
        unsigned optional_reads=0;
        const auto count_optional=[&](std::uintptr_t address,void* out,std::size_t size) {
            if(address==view_at+d2vr::alternate_taa::stamp_offset ||
               address==view_at+d2vr::alternate_taa::current_matrix_offsets[0]) ++optional_reads;
            return read_memory(address,out,size);
        };
        PassPlan disabled_plan;
        require(plan_pass(count_optional,in,ctx_at,alternation,disabled_plan,scratch)==PassResult::planned &&
                !disabled_plan.metadata && optional_reads==0 && disabled_plan.replaced==plan.replaced,
                "Disabled optional capture makes no metadata reads and preserves legacy planning");
        auto metadata_input=in;metadata_input.capture_metadata=true;PassPlan optional_plan;
        const auto fail_optional_stamp=[&](std::uintptr_t address,void* out,std::size_t size) {
            return address!=view_at+d2vr::alternate_taa::stamp_offset && read_memory(address,out,size);
        };
        require(plan_pass(fail_optional_stamp,metadata_input,ctx_at,alternation,optional_plan,scratch)==PassResult::planned &&
                !optional_plan.metadata && optional_plan.replaced==plan.replaced && optional_plan.eye==plan.eye,
                "Missing optional metadata never changes existing history/FSR pass planning");
        require(plan_pass(read_memory,metadata_input,ctx_at,alternation,optional_plan,scratch)==PassResult::planned &&
                optional_plan.metadata && optional_plan.metadata->counter==0 && optional_plan.replaced==plan.replaced,
                "Optional raw-zero metadata preserves native planning");
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

        // One native pointer can name either logical eye only through a Begin-admitted receipt.
        in.single_view=view_at;in.eye_views={view_at,view_at};
        std::memcpy(view.data()+d2vr::alternate_taa::stamp_offset,&counter,4);
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::fallback,"Singleton without receipt fails closed");
        d2vr::alternate_taa::Receipt receipt{};
        receipt.key.view=view_at;receipt.key.native_counter=counter;receipt.key.eye=1;
        receipt.current.jitter={.1f,.2f};receipt.previous.jitter={-.2f,-.1f};in.receipt=receipt;
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::fallback,"Setup-only receipt rejected");
        in.receipt->admitted=true;
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::planned && plan.eye==1 &&
                plan.jitter==std::optional<std::array<float,4>>{{.1f,.2f,-.2f,-.1f}},"Receipt supplies own-eye identity and frozen jitter");
        ++in.receipt->key.native_counter;
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::fallback,"Stale singleton receipt rejected");
        in.single_view=0;in.receipt.reset();plan={};

        require(without_temporal(fallback_pass,view.data(),view_at)==73 && fallback_calls==1 && view[view_flags_b]==0x20,
                "Fallback calls original once and preserves unrelated flags");
        view[view_flags_b]=4;
        require(without_temporal(fallback_pass,view.data(),view_at)==73 && fallback_calls==2 && view[view_flags_b]==0x24,
                "Existing native skip flag retained");
        view[view_flags_b]=0;
        bool threw=false;try {without_temporal(fallback_throw,view.data(),view_at);} catch(const std::runtime_error&) {threw=true;}
        require(threw && fallback_calls==3 && view[view_flags_b]==0x20,"Exception restores only owned skip bit");
        view[view_flags_b]=0;
        require(without_temporal(fallback_new_frame,view.data(),view_at)==74 && fallback_calls==4 && view[view_flags_b]==0x24,
                "Fallback never restores flags into a newer native frame");
        view[view_flags_b]=0;

        in.eye_views={view_at,0x1234};
        alternation[0].reset();
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::planned && plan.eye==0 && !plan.write,"Left eye uses shared pair");
        view[view_flags_b]=4;
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::idle,"Skipped TAA is idle");
        view[view_flags_b]=0;in.eye_views={0x1234,view_at};
        std::uint32_t wider=2400;std::memcpy(s0.bytes.data()+image_desc+8,&wider,4);
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::rebuild,"Resized original forces rebuild");
        in.upscaler_owns_taa=true;
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::planned && plan.twins_pending && !plan.write,"Upscaler right eye survives a resized original");
        in.upscaler_owns_taa=false;
        std::uint32_t bad=0;std::memcpy(ctx.data()+ctx_param_count,&bad,4);
        require(plan_pass(read_memory,in,ctx_at,alternation,plan,scratch)==PassResult::fallback,"Empty table falls back");
        post_tonemap_tracks_admitted_logical_eye();
        std::cout<<"native TAA history checks passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
