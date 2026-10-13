#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <ostream>
#include <set>

namespace d2vr {
// Logs CBPerViewGlobals (0x320 bytes) as bound at CS slot 1 right after the
// native TAA pass was recorded (slot 2: the TAA pass's PerInstanceCB, whose viewport and
// texture sizes convert motion vectors to pixels and place the history fetch). The CopyResource joins the caller's deferred
// command list, so a staging copy is only meaningful once the game executed
// that list: drain waits three frames and maps without blocking (DO_NOT_WAIT)
// and only ever maps staging buffers, never a game resource.
class TaaConstantsCapture {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    static constexpr unsigned ring_size=8,field_count=9,instance_field_count=6;
public:
    static constexpr unsigned block_bytes=0x320;
    static constexpr const char* header="frame,eye,field,cb,ctx_type,f00,f01,f02,f03,f04,f05,f06,f07,f08,f09,f10,f11,f12,f13,f14,f15";
    std::atomic<std::uint64_t> recorded{0},misses{0},drops{0},written_rows{0};
private:
    // Byte offsets inside the per-view block. Matrices use all 16 floats; the
    // shorter fields use only their leading floats and the rest read as zero.
    struct Field {const char* name;unsigned offset;unsigned count;};
    static constexpr Field fields[field_count]{
        {"viewprojection",0x020,16},{"view",0x060,16},{"projection",0x0b0,16},
        {"previous_viewprojection",0x0f0,16},{"previous_view",0x130,16},{"previous_projection",0x170,16},
        {"jittervectors",0x1d0,4},{"globalviewinfos",0x2a0,4},{"framenumber",0x318,1}};
    // PerInstanceCB of the native TAA shader (offsets from its source in base/shaderCache/ShaderCache_PC.ucd).
    static constexpr Field instance_fields[instance_field_count]{
        {"positiontoviewtexture",0x00,4},{"viewport",0x10,4},{"taatexsize",0x20,4},{"taaditherandviewportsize",0x30,4},
        {"exposureindex_prevresolutionscale",0xa0,4},{"whitelevel_maxluminance_taaamount_evbias",0xb0,4}};
    const Field* fields_=fields;unsigned fields_count_=field_count;UINT slot_=1,min_bytes_=block_bytes;
    struct Slot {
        Ptr<ID3D11Buffer> staging;
        std::atomic<unsigned> state{0}; // 0 free, 1 copying, 2 recorded
        unsigned long long frame{};
        int eye{};
        std::uintptr_t cb{};
        D3D11_DEVICE_CONTEXT_TYPE ctx_type{D3D11_DEVICE_CONTEXT_IMMEDIATE};
    };
    struct Entry {unsigned long long frame{};int eye{};std::uintptr_t cb{};D3D11_DEVICE_CONTEXT_TYPE ctx_type{};};
    const unsigned max_frames_;
    mutable std::mutex mutex_;
    std::array<Slot,ring_size> ring_;
    Ptr<ID3D11Device> staging_device_;
    UINT staging_bytes_{};
    std::set<unsigned long long> started_,completed_;
    std::atomic<bool> complete_{false},header_written_{false};
    // Called with mutex_ held. The ring is only rebuilt while every slot is
    // free, so a resized or foreign-device buffer can never invalidate a copy.
    bool ensure_ring(ID3D11Buffer* buffer,UINT bytes) noexcept {
        Ptr<ID3D11Device> device;buffer->GetDevice(device.GetAddressOf());
        if(!device) return false;
        if(ring_[0].staging && staging_bytes_==bytes && staging_device_.Get()==device.Get()) return true;
        if(ring_[0].staging) {
            for(const auto& slot:ring_) if(slot.state.load(std::memory_order_relaxed)) return false;
            for(auto& slot:ring_) slot.staging.Reset();
            staging_device_.Reset();
        }
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=bytes;desc.Usage=D3D11_USAGE_STAGING;
        desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        for(auto& slot:ring_) if(FAILED(device->CreateBuffer(&desc,nullptr,slot.staging.GetAddressOf()))) {
            for(auto& created:ring_) created.staging.Reset();
            return false;
        }
        staging_device_=device;staging_bytes_=bytes;return true;
    }
    // Called with mutex_ held once a slot is free again. A frame counts as
    // captured when neither eye still has a recorded slot for it.
    void settle(unsigned long long frame) {
        for(const auto& slot:ring_) if(slot.state.load(std::memory_order_relaxed)==2 && slot.frame==frame) return;
        if(completed_.insert(frame).second && completed_.size()>=max_frames_) complete_.store(true,std::memory_order_release);
    }
    void write_rows(const Entry& entry,const void* data,std::ostream& csv) const {
        const auto* bytes=static_cast<const unsigned char*>(data);
        const auto flags=csv.flags();const auto precision=csv.precision();
        csv<<std::setprecision(9);
        for(unsigned f=0;f<fields_count_;++f) {
            const auto& field=fields_[f];
            csv<<entry.frame<<','<<entry.eye<<','<<field.name<<",0x"<<std::hex
               <<static_cast<unsigned long long>(entry.cb)<<std::dec<<','<<static_cast<int>(entry.ctx_type);
            for(unsigned i=0;i<16;++i) {
                float value{};
                if(i<field.count) std::memcpy(&value,bytes+field.offset+i*sizeof(float),sizeof value);
                csv<<','<<value;
            }
            csv<<'\n';
        }
        csv.flags(flags);csv.precision(precision);
    }
public:
    explicit TaaConstantsCapture(unsigned max_frames=64,UINT slot=1):max_frames_(max_frames?max_frames:1) {
        if(slot==2) {fields_=instance_fields;fields_count_=instance_field_count;slot_=2;min_bytes_=0xc0;}
    }

    // Called on the job thread that owns this deferred context. The copy is
    // part of that context's command list, which is why it is issued here and
    // never on another thread's context.
    void record(ID3D11DeviceContext* context,int eye,unsigned long long frame) noexcept {
        if(!context || complete_.load(std::memory_order_acquire) || (eye!=0 && eye!=1)) return;
        Ptr<ID3D11Buffer> cb;context->CSGetConstantBuffers(slot_,1,cb.GetAddressOf());
        if(!cb) {++misses;return;}
        D3D11_BUFFER_DESC desc{};cb->GetDesc(&desc);
        if(desc.ByteWidth<min_bytes_ || !(desc.BindFlags&D3D11_BIND_CONSTANT_BUFFER)) {++misses;return;}
        const Entry entry{frame,eye,reinterpret_cast<std::uintptr_t>(cb.Get()),context->GetType()};
        unsigned index=ring_size;
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            if(complete_.load(std::memory_order_acquire)) return;
            if(!ensure_ring(cb.Get(),desc.ByteWidth)) {++misses;return;}
            if(started_.find(frame)==started_.end()) {
                // Once the requested frames are underway no further frame starts.
                if(started_.size()>=max_frames_) return;
                started_.insert(frame);
            }
            for(unsigned i=0;i<ring_size;++i) if(!ring_[i].state.load(std::memory_order_relaxed)) {
                index=i;ring_[i].state.store(1,std::memory_order_relaxed);
                ring_[i].frame=entry.frame;ring_[i].eye=entry.eye;ring_[i].cb=entry.cb;ring_[i].ctx_type=entry.ctx_type;
                break;
            }
            if(index==ring_size) {++drops;return;}
        } catch(...) {++drops;return;}
        context->CopyResource(ring_[index].staging.Get(),cb.Get());
        ring_[index].state.store(2,std::memory_order_release);
        ++recorded;
    }

    // Called on the Present thread with the immediate context. Rows are only
    // written once the deferred list has had three frames to run; a slot that
    // is still in flight is retried on a later call.
    void drain(ID3D11DeviceContext* immediate,unsigned long long current_frame,std::ostream& csv) {
        if(!immediate) return;
        if(!header_written_.exchange(true,std::memory_order_relaxed)) csv<<header<<'\n';
        for(unsigned i=0;i<ring_size;++i) {
            Entry entry;ID3D11Buffer* staging=nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const auto& slot=ring_[i];
                if(slot.state.load(std::memory_order_relaxed)!=2 || !slot.staging) continue;
                if(slot.frame>current_frame || current_frame-slot.frame<3) continue;
                entry={slot.frame,slot.eye,slot.cb,slot.ctx_type};staging=slot.staging.Get();
            }
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const auto result=immediate->Map(staging,0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&mapped);
            if(result==DXGI_ERROR_WAS_STILL_DRAWING) continue;
            if(FAILED(result) || !mapped.pData) {
                std::lock_guard<std::mutex> lock(mutex_);
                ++drops;ring_[i].state.store(0,std::memory_order_release);settle(entry.frame);
                continue;
            }
            write_rows(entry,mapped.pData,csv);immediate->Unmap(staging,0);
            std::lock_guard<std::mutex> lock(mutex_);
            written_rows+=fields_count_;ring_[i].state.store(0,std::memory_order_release);settle(entry.frame);
        }
    }
    bool complete() const {return complete_.load(std::memory_order_acquire);}
};
} // namespace d2vr
