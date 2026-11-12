#pragma once
// v999 render-pass attribution: one hook on the engine's deferred job-item executor.
//
// Chosen interception point: 0x2c5d50.  Instruction evidence (Steam build sha256 b671f824...6180):
//   0x2c5d59  mov rcx,qword ptr [rcx+0x108]      ; the job's deferred context
//   0x2c5d6c  test dword ptr [rbx+0xf4],0x100    ; cached-list flag
//   0x2c5d80  call qword ptr [rbx+0xd8]          ; the ARK_RENDERPASS_* callback from 0x34fe00
//   0x2c5da1  mov rcx,qword ptr [rbx+0x108]
//   0x2c5dad  call 0x2bba20                      ; FinishCommandList (vtable slot 114 = +0x390), stored at [rbx+0x118]
// Its only caller is 0x2c85b6 in the job scheduler loop.  The second callback path, 0x2c5f60
// (caller 0x2c739d), is entered only for items the scheduler marked with flag 0x400; 0x2c77b0 sets
// that flag when the item's +0x108 is the immediate context (0x2c77c9..0x2c77fc), so that path cannot
// create a command list and needs no list attribution.
//
// The hook reads the callback pointer at item+0xd8 and the deferred context at item+0x108, maps the
// callback RVA through render_pass_table.hpp, and marks the thread for the duration of the callback
// (which includes the FinishCommandList).  TaaDispatchHook's existing census-free FinishCommandList
// observer calls finished(), which records list -> pass.  ExecuteCommandList looks the pass up again.
// One MinHook per target address; no source file hooked 0x2c5d50 before v999 (grep of src/ for
// "2c5d50"/"0x2c5" finds only the 0x327270 comment in native_taa_history.hpp).
//
// v1009: the same hook also carries the per-light shadow-map-info link across the views at
// TLFLIGHTCULL (pass 26).  See the LightLink helpers below and "Why the right eye lost dynamic
// shadows (phase 9)" in docs/ENGINE-PERF-PLAN.md: PREPARE writes light+0x1a8, GATHER_LIGHTS clears
// it for every view, and TLFLIGHTCULL publishes the per-light records from it.  The hook captures
// the first view's links and writes them around the second view's TLFLIGHTCULL callback, restoring
// the old values before the executor returns.  No new hook address and no atlas write.
// v1011: the link alone was not enough.  The per-view job builder 0x351780 zeroes the shared
// entry+0x18 record count before each view, so the second view's TLFLIGHTCULL still saw count 0
// and published nothing (see "Phase 10").  The capture now carries entry+0x18 and the restore
// writes it with the link; both are put back in __finally.
#include <windows.h>
#include <d3d11.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <sstream>
#include "command_list_pass_map.hpp"
#include "render_pass_table.hpp"
#include "shadow_share.hpp"
#include "visibility_stamp.hpp"

namespace d2vr {
class RenderPassMap {
public:
    static constexpr std::uintptr_t execute_rva=0x2c5d50;
private:
    using ExecuteFn=bool (__fastcall*)(void*);
    using PassFn=bool (__fastcall*)(void*);
    static constexpr std::array<unsigned char,16> prologue{
        0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x89,0x08,0x01,0x00,0x00};
    inline static ExecuteFn original_{};
    inline static void* target_{};
    inline static RenderPassMap* instance_{};
    inline static thread_local int current_pass_=-1;
    inline static thread_local void* current_context_=nullptr;
    inline static thread_local void* current_view_=nullptr;   // item+0x70: the view the pass renders (an eye view for a view pass)
    // v1199: a serial per pass job (one job = one command list: the executor finishes the list it recorded). A per-view constant write and a
    // draw recorded in the same job on the same deferred context are in the same list, so the draw reads exactly what that write put in.
    inline static thread_local std::uint64_t current_job_=0;
    inline static std::atomic<std::uint64_t> job_serial_{};
    inline static std::atomic<shadow_share::Runtime*> shadow_share_{};
    // v1009: the per-light shadow-map-info link that GATHER_LIGHTS clears and PREPARE sets.
    //   GATHER_LIGHTS 0x31c074: [light+0x1a8] = 0 (rdi)
    //   PREPARE      0x3328ac: [[entry]+0x1a8] = entry   ([entry] is the light node)
    //   TLFLIGHTCULL 0x2f75ca: r13 = [light+0x1a8]; 0x2f7710.. copies entry->[+0x10] records
    // The light list is at item+0x40: [list] = data (light pointers), [list+8] & 0xffffff = count.
    // v1011: `records` is entry+0x18's low 24 bits, the count TLFLIGHTCULL copies (0x2f7646).
    struct LightLink {void* light{};std::uintptr_t info{};std::uint32_t records{};};
    struct SavedLightLink {void* light{};std::uintptr_t previous{};void* entry{};std::uint32_t previous_records{};};
    inline static thread_local std::array<LightLink,shadow_share::light_capture_capacity> light_links_{};
    inline static thread_local unsigned light_link_count_{};
    inline static thread_local std::array<SavedLightLink,shadow_share::light_capture_capacity> saved_light_links_{};
    inline static thread_local unsigned saved_light_link_count_{};
    static unsigned collect_light_links(void* item) noexcept {
        light_link_count_=0;
        __try {
            char* list=*reinterpret_cast<char**>(static_cast<char*>(item)+0x40);
            if(!list) return 0;
            unsigned count=*reinterpret_cast<std::uint32_t*>(list+8)&0xffffffu;
            char* data=*reinterpret_cast<char**>(list);
            if(!data) return 0;
            if(count>shadow_share::light_capture_capacity) count=shadow_share::light_capture_capacity;
            for(unsigned i=0;i<count;++i) {
                void* light=*reinterpret_cast<void**>(data+static_cast<std::size_t>(i)*8);
                if(!light) continue;
                char* info=*reinterpret_cast<char**>(static_cast<char*>(light)+0x1a8);
                std::uint32_t records=0;
                if(info) records=*reinterpret_cast<std::uint32_t*>(info+0x18)&0xffffffu;
                light_links_[light_link_count_++]={light,reinterpret_cast<std::uintptr_t>(info),records};
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        return light_link_count_;
    }
    static bool write_light_info(void* light,std::uintptr_t info) noexcept {
        __try {
            *reinterpret_cast<std::uintptr_t*>(static_cast<char*>(light)+0x1a8)=info;
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    static std::uint32_t read_light_records(void* entry) noexcept {
        __try {
            return *reinterpret_cast<std::uint32_t*>(static_cast<char*>(entry)+0x18)&0xffffffu;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }
    // The per-view job builder 0x351dec..0x351e4d clears the low 24 bits of entry+0x18 and
    // keeps the top byte (0x351e3b/0x351e4d).  Write the captured count the same way.
    static bool write_light_records(void* entry,std::uint32_t records) noexcept {
        __try {
            auto* field=reinterpret_cast<std::uint32_t*>(static_cast<char*>(entry)+0x18);
            *field=(*field&0xff000000u)|(records&0xffffffu);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    static void restore_saved_light_links() noexcept {
        for(unsigned i=0;i<saved_light_link_count_;++i) {
            write_light_info(saved_light_links_[i].light,saved_light_links_[i].previous);
            if(saved_light_links_[i].entry)
                write_light_records(saved_light_links_[i].entry,saved_light_links_[i].previous_records);
        }
        saved_light_link_count_=0;
    }
    // The substituted pass callback: same signature as the ARK_RENDERPASS_* callback, and it
    // returns false like every shadow callback.  The executor still finishes a command list,
    // now empty, so the scheduler sees an ordinary job that recorded no work.
    static bool __fastcall skip_shadow_pass(void*) noexcept {return false;}
    // v1006: the per-view shadow setup resource.  0x2b08c0(item, hash) reads the job item's
    // +0x58 table (count at +0x60, 16-byte {u32 hash, u32 pad, u64 value} entries) and returns
    // entry+8; 0x311e5c / 0x31e0c2 / 0x309602 look up hash 0x3719f109 ("shadowMapsInfos") and
    // 0x310720 binds the returned value.  Redirecting that one value for the second view's
    // reading job is the whole runtime mutation; it is restored before the executor returns.
    struct ShadowMapsParam {
        void** slot{};
        std::uintptr_t value{};
        bool found{};   // the entry exists; value may legitimately be null (unproduced resource)
    };
    static bool __fastcall read_shadow_maps_infos(void* item, ShadowMapsParam& param) noexcept {
        __try {
            const auto count=*reinterpret_cast<std::uint32_t*>(static_cast<char*>(item)+0x60)&0xffffffu;
            if(count>0x1000u) return true; // implausible table: treat as absent, never scan far
            char* table=*reinterpret_cast<char**>(static_cast<char*>(item)+0x58);
            if(!table) return true;
            for(unsigned i=0;i<count;++i) {
                char* entry=table+static_cast<std::size_t>(i)*16;
                if(*reinterpret_cast<std::uint32_t*>(entry)==0x3719f109u) {
                    param.slot=reinterpret_cast<void**>(entry+8);
                    param.value=reinterpret_cast<std::uintptr_t>(*param.slot);
                    param.found=true;
                    return true;
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }
    static bool __fastcall write_shadow_maps_infos(void** slot, std::uintptr_t value) noexcept {
        __try {
            *slot=reinterpret_cast<void*>(value);
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    // v1011 probe (build/shadow-share-probe): one bounded read of a TLFLIGHTCULL job's inputs
    // and its cpu_shadowMapsInfos output.  `entry+0x18` is the record count the pass reads and
    // `entry+0x620` is the base published index it writes (0x2f78cf).  The CPU array is
    // [[cpu_shadowMapsInfos value]] + 8 (0x2f7381..0x2f738b, 0x2f7717).
    struct ProbeSnapshot {
        shadow_share::ProbeLight lights[shadow_share::probe_light_capacity]{};
        unsigned light_count{};
        std::uintptr_t light_list{};
        std::uintptr_t light_list_data{};
        std::uintptr_t shadow_maps_infos{};
        std::uintptr_t record_array{};
        std::uint32_t record_words[32]{}; // first four 0x90-byte records, first 0x20 bytes each
        unsigned record_word_count{};
    };
    static void gather_probe_snapshot(void* item, ProbeSnapshot& snapshot) noexcept {
        __try {
            char* list=*reinterpret_cast<char**>(static_cast<char*>(item)+0x40);
            if(list) {
                snapshot.light_list=reinterpret_cast<std::uintptr_t>(list);
                unsigned count=*reinterpret_cast<std::uint32_t*>(list+8)&0xffffffu;
                char* data=*reinterpret_cast<char**>(list);
                snapshot.light_list_data=reinterpret_cast<std::uintptr_t>(data);
                if(data) {
                    if(count>shadow_share::probe_light_capacity) count=shadow_share::probe_light_capacity;
                    for(unsigned i=0;i<count;++i) {
                        void* light=*reinterpret_cast<void**>(data+static_cast<std::size_t>(i)*8);
                        if(!light) continue;
                        char* info=*reinterpret_cast<char**>(static_cast<char*>(light)+0x1a8);
                        shadow_share::ProbeLight entry{};
                        entry.light=reinterpret_cast<std::uintptr_t>(light);
                        entry.info=reinterpret_cast<std::uintptr_t>(info);
                        if(info) {
                            entry.records=*reinterpret_cast<std::uint32_t*>(info+0x18)&0xffffffu;
                            entry.published=*reinterpret_cast<std::int32_t*>(info+0x620);
                        }
                        snapshot.lights[snapshot.light_count++]=entry;
                    }
                }
            }
            ShadowMapsParam param{};
            if(read_shadow_maps_infos(item,param) && param.found) snapshot.shadow_maps_infos=param.value;
            // Named object parameter cpu_shadowMapsInfos (0x3b921636): 16-byte table entries
            // hold a descriptor pointer; descriptor+8 is the hash and +0x10 the value.
            const unsigned objects=*reinterpret_cast<std::uint32_t*>(static_cast<char*>(item)+0xe8)&0xffffffu;
            if(objects<=0x1000u) {
                char* table=*reinterpret_cast<char**>(static_cast<char*>(item)+0xe0);
                for(unsigned i=0;table && i<objects;++i) {
                    char* descriptor=*reinterpret_cast<char**>(table+static_cast<std::size_t>(i)*16);
                    if(!descriptor || *reinterpret_cast<std::uint32_t*>(descriptor+8)!=0x3b921636u) continue;
                    char* value=*reinterpret_cast<char**>(descriptor+0x10);
                    if(value) {
                        char* object=*reinterpret_cast<char**>(value);
                        if(object) snapshot.record_array=*reinterpret_cast<std::uintptr_t*>(object+8);
                    }
                    break;
                }
            }
            if(snapshot.record_array) {
                snapshot.record_word_count=32;
                for(unsigned i=0;i<snapshot.record_word_count;++i)
                    snapshot.record_words[i]=*reinterpret_cast<std::uint32_t*>(snapshot.record_array+static_cast<std::size_t>(i)*4);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {snapshot.record_word_count=0;}
    }
    static unsigned probe_records_published(const ProbeSnapshot& snapshot) noexcept {
        unsigned published=0;
        for(unsigned i=0;i<snapshot.light_count;++i) {
            const auto& light=snapshot.lights[i];
            if(!light.info || light.published<0) continue;
            const unsigned end=static_cast<unsigned>(light.published)+light.records;
            if(end>published) published=end;
        }
        return published;
    }
    std::uintptr_t base_{};
    CommandListPassMap lists_{};
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint64_t> pass_jobs_{},mapped_finishes_{},unmatched_finishes_{},failures_{};
    int pass_for_callback(void* callback) const noexcept {
        const auto address=reinterpret_cast<std::uintptr_t>(callback);
        if(!address || address<base_) return -1;
        return render_pass_for_callback_rva(address-base_);
    }
    static bool __fastcall execute_hook(void* item) {
        auto* self=instance_;
        if(!self || !original_) return false;
        void* callback{};
        void* deferred{};
        void* view{};
        __try {
            callback=*reinterpret_cast<void**>(static_cast<char*>(item)+0xd8);
            // item+0x108 is the engine's context wrapper, not the D3D11 context: 0x2bba20 finishes the list on its first field
            // (mov rcx,[rcx]; call [rax+0x390]), and that field is what the FinishCommandList observer sees.
            if(void* wrapper=*reinterpret_cast<void**>(static_cast<char*>(item)+0x108)) deferred=*reinterpret_cast<void**>(wrapper);
            // item+0x70 is the view this pass job renders (0x333550/0x32d4b0/0x330020/0x330c10/0x32c7f0 all read it first): one of
            // the eye views for a view pass, a light/other view for a shadow or light pass. Exposed for the weapon placement's eye.
            view=*reinterpret_cast<void**>(static_cast<char*>(item)+0x70);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            ++self->failures_;
            return original_(item);
        }
        const int pass=self->pass_for_callback(callback);
        // v1009/v1011: TLFLIGHTCULL copies each light's shadow records from light+0x1a8 and
        // entry+0x18.  The second view's GATHER_LIGHTS has already cleared the links and its
        // PREPARE is suppressed in setup mode, so the first view's links AND record counts are
        // written around the second view's job and the old values are put back afterwards (the
        // only engine memory this path writes).  The count carry is what v1009 missed: the
        // per-view job builder 0x351dec..0x351e4d zeroes entry+0x18 for the second view, so the
        // restored link alone still published zero records and skipped the GPU upload.
        if(pass==shadow_share::pass_tlflightcull) {
            if(auto* share=shadow_share_.load(std::memory_order_acquire)) {
                const auto light_action=share->light_action(pass,reinterpret_cast<std::uintptr_t>(view));
                std::uint64_t probe_frame=0;
                int probe_view=-1;
                const bool probe=share->probe_enabled() &&
                    share->probe_begin(reinterpret_cast<std::uintptr_t>(view),probe_frame,probe_view);
                if(light_action!=shadow_share::LightAction::none || probe) {
                    if(light_action==shadow_share::LightAction::capture) {
                        const unsigned count=collect_light_links(item);
                        for(unsigned i=0;i<count;++i)
                            share->capture_light(reinterpret_cast<std::uintptr_t>(light_links_[i].light),
                                light_links_[i].info,light_links_[i].records);
                    } else if(light_action==shadow_share::LightAction::restore) {
                        const unsigned count=collect_light_links(item);
                        saved_light_link_count_=0;
                        for(unsigned i=0;i<count;++i) {
                            std::uintptr_t captured=0;
                            std::uint32_t captured_records=0;
                            if(!share->find_light(reinterpret_cast<std::uintptr_t>(light_links_[i].light),
                                                 captured,captured_records)) {
                                share->note_light_miss();
                                continue;
                            }
                            const std::uintptr_t previous=light_links_[i].info;
                            void* entry=reinterpret_cast<void*>(captured);
                            const std::uint32_t previous_records=entry?read_light_records(entry):0;
                            bool changed=false,link_written=false,records_written=false;
                            if(previous!=captured && write_light_info(light_links_[i].light,captured)) {
                                changed=true;
                                link_written=true;
                            }
                            if(entry && previous_records!=captured_records &&
                               write_light_records(entry,captured_records)) {
                                changed=true;
                                records_written=true;
                            }
                            if(changed) {
                                if(saved_light_link_count_<shadow_share::light_capture_capacity)
                                    saved_light_links_[saved_light_link_count_++]=
                                        {light_links_[i].light,previous,entry,previous_records};
                                if(link_written) share->note_light_restored();
                                if(records_written) share->note_light_record_restored();
                            }
                        }
                    }
                    const int previous_pass=current_pass_;
                    void* previous_context=current_context_;
                    void* previous_view=current_view_;const std::uint64_t previous_job=current_job_;
                    if(deferred) {
                        ++self->pass_jobs_;
                        current_pass_=pass;
                        current_context_=deferred;
                        current_view_=view;current_job_=job_serial_.fetch_add(1,std::memory_order_relaxed)+1;VisibilityStamp::next_job();
                    }
                    bool result=false;
                    __try {
                        result=original_(item);
                        if(probe) {
                            ProbeSnapshot after{};
                            gather_probe_snapshot(item,after);
                            share->probe_write(probe_view,probe_frame,after.light_list,after.light_list_data,
                                after.shadow_maps_infos,after.lights,after.light_count,probe_records_published(after),
                                after.record_words,after.record_word_count);
                        }
                    }
                    __finally {
                        restore_saved_light_links();
                        current_pass_=previous_pass;
                        current_context_=previous_context;
                        current_view_=previous_view;current_job_=previous_job;
                    }
                    return result;
                }
            }
        }
        // Fast path: only the five shadow writers and the five shadow-reading passes need the
        // state machine.  Every other pass pays one pass-id range check and no mutex.
        if(pass>=0 && (shadow_share::mutating_shadow_pass(pass) || shadow_share::shadow_reader_pass(pass))) {
        if(auto* share=shadow_share_.load(std::memory_order_acquire)) {
            bool cached=false;
            __try {cached=(*reinterpret_cast<std::uint32_t*>(static_cast<char*>(item)+0xf4)&0x100u)!=0;}
            __except(EXCEPTION_EXECUTE_HANDLER) {cached=true;}
            ShadowMapsParam infos{};
            bool infos_read=false;
            if(pass>=0 && shadow_share::shadow_reader_pass(pass)) {
                infos_read=read_shadow_maps_infos(item,infos);
            }
            const auto action=share->on_job(pass,reinterpret_cast<std::uintptr_t>(view),cached,
                infos_read && infos.found,infos.value);
            if(action.suppress) {
                auto* slot=reinterpret_cast<PassFn*>(static_cast<char*>(item)+0xd8);
                const PassFn previous=*slot;
                *slot=&skip_shadow_pass;
                bool result=false;
                __try {result=original_(item);}
                __finally {*slot=previous;}
                return result;
            }
            // v1006 setup mode: make this second-view job read the first view's setup resource.
            // The job item table entry is per job and only this thread consumes it; restore it
            // even if the callback throws.
            if(action.redirect && infos.slot && infos.found && infos.value!=action.redirect_value &&
               write_shadow_maps_infos(infos.slot,action.redirect_value)) {
                share->note_redirect_applied();
                const std::uintptr_t previous=infos.value;
                const int previous_pass=current_pass_;
                void* previous_context=current_context_;
                void* previous_view=current_view_;const std::uint64_t previous_job=current_job_;
                // This job records real work, so keep the pass attribution the executor would
                // otherwise get (the redirected job still needs its gpu_passes eye1 row, and the
                // weapon draw placement needs the pass view to name the eye of its draws).
                if(deferred) {
                    ++self->pass_jobs_;
                    current_pass_=pass;
                    current_context_=deferred;
                    current_view_=view;current_job_=job_serial_.fetch_add(1,std::memory_order_relaxed)+1;VisibilityStamp::next_job();
                }
                bool result=false;
                __try {result=original_(item);}
                __finally {
                    write_shadow_maps_infos(infos.slot,previous);
                    current_pass_=previous_pass;
                    current_context_=previous_context;
                    current_view_=previous_view;current_job_=previous_job;
                }
                return result;
            }
        }
        }
        if(pass<0 || !deferred) {
            ++self->unmatched_finishes_;
            return original_(item);
        }
        ++self->pass_jobs_;
        const int previous_pass=current_pass_;
        void* previous_context=current_context_;
        void* previous_view=current_view_;const std::uint64_t previous_job=current_job_;
        current_pass_=pass;
        current_context_=deferred;
        current_view_=view;current_job_=job_serial_.fetch_add(1,std::memory_order_relaxed)+1;VisibilityStamp::next_job();
        bool result=false;
        __try {result=original_(item);}
        __finally {
            current_pass_=previous_pass;
            current_context_=previous_context;
            current_view_=previous_view;current_job_=previous_job;
        }
        return result;
    }
public:
    // The pass this thread is recording, and the view the pass renders (0 outside a pass). Read by the draw hooks through
    // ObjectMotion::set_pass_source (src/dxgi_probe.cpp): a view pass's view is one of the eye views, which names the eye exactly.
    static int current_pass() noexcept {return current_pass_;}
    // v1259: a secondary pass (engine secondary-view render, the Timepiece lens) borrows an EYE's view object, so its jobs carry that eye's
    // view at item+0x70. While one runs (dxgi_probe lens_pass sets secondary_view around it) its jobs name no view: no consumer of the pass
    // view (draw placement, object motion, rig mask, cross-eye history, visibility stamp) may take the lens camera for that eye's render.
    // The shadow share keeps the job's real view (execute_hook's local) and isolates the pass itself (shadow_share Model::on_secondary_begin).
    inline static std::atomic<std::uintptr_t> secondary_view{};
    static bool current_secondary() noexcept {const auto v=reinterpret_cast<std::uintptr_t>(current_view_);return v && v==secondary_view.load(std::memory_order_relaxed);}
    static std::uintptr_t current_view() noexcept {return current_secondary()?0:reinterpret_cast<std::uintptr_t>(current_view_);}
    static std::uint64_t current_job() noexcept {return current_job_;} // v1199: 0 outside a pass job
    // Called from the existing FinishCommandList observer, on the deferred job thread, before the
    // thread-local pass is cleared.
    void finished(ID3D11DeviceContext* context,ID3D11CommandList* list) noexcept {
        if(current_pass_<0 || !context || !list || context!=current_context_) {
            ++unmatched_finishes_;
            return;
        }
        if(lists_.record(list,current_pass_)) ++mapped_finishes_;
    }
    int pass_for_list(ID3D11CommandList* list) const noexcept {return lists_.find(list);}
    void set_shadow_share(shadow_share::Runtime* share) noexcept {shadow_share_.store(share,std::memory_order_release);}
    bool install(std::uintptr_t base) noexcept {
        if(target_) {
            const bool enabled=MH_EnableHook(target_)==MH_OK;
            enabled_.store(enabled,std::memory_order_release);
            if(enabled) instance_=this;
            return enabled;
        }
        if(!base) return false;
        void* target=reinterpret_cast<void*>(base+execute_rva);
        std::array<unsigned char,16> code{};
        SIZE_T read{};
        if(!ReadProcessMemory(GetCurrentProcess(),target,code.data(),code.size(),&read) ||
           read!=code.size() || code!=prologue) {
            ++failures_;
            return false;
        }
        base_=base;
        instance_=this;
        if(MH_CreateHook(target,reinterpret_cast<void*>(&execute_hook),reinterpret_cast<void**>(&original_))!=MH_OK) {
            original_=nullptr;
            instance_=nullptr;
            ++failures_;
            return false;
        }
        if(MH_EnableHook(target)!=MH_OK) {
            MH_RemoveHook(target);
            original_=nullptr;
            instance_=nullptr;
            ++failures_;
            return false;
        }
        target_=target;
        enabled_.store(true,std::memory_order_release);
        return true;
    }
    // Stops attribution for the session.  The trampoline and target stay allocated so a later session
    // can re-enable the same one MinHook target without creating a second hook.
    bool disable() noexcept {
        enabled_.store(false,std::memory_order_release);
        current_pass_=-1;
        current_context_=nullptr;
        current_view_=nullptr;current_job_=0;
        lists_.clear();
        if(!target_) return true;
        const auto status=MH_DisableHook(target_);
        return status==MH_OK || status==MH_ERROR_DISABLED;
    }
    bool installed() const noexcept {return enabled_.load(std::memory_order_acquire) && target_!=nullptr;}
    void clear() noexcept {
        lists_.clear();
        pass_jobs_=0;mapped_finishes_=0;unmatched_finishes_=0;failures_=0;
    }
    std::string json() const {
        const auto list=lists_.stats();
        std::ostringstream out;
        out<<"{\"installed\":"<<(installed()?"true":"false")
           <<",\"execute_rva\":"<<execute_rva
           <<",\"pass_jobs\":"<<pass_jobs_.load(std::memory_order_relaxed)
           <<",\"mapped_finishes\":"<<mapped_finishes_.load(std::memory_order_relaxed)
           <<",\"unmatched_finishes\":"<<unmatched_finishes_.load(std::memory_order_relaxed)
           <<",\"failures\":"<<failures_.load(std::memory_order_relaxed)
           <<",\"list_recorded\":"<<list.recorded
           <<",\"list_updated\":"<<list.updated
           <<",\"list_evicted\":"<<list.evicted
           <<",\"list_rejected\":"<<list.rejected<<'}';
        return out.str();
    }
};
} // namespace d2vr
