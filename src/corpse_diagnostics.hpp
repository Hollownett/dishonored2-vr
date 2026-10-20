#pragma once
// F4: corpse grab diagnostics. Observation only: nothing here touches the engine.
// note_* run on the game/dispatcher thread, json() on the status thread, so every counter is a relaxed atomic.
// Bounded CSV trace: BoundedCsvTraceWriter<Sample,Format> is generic (plan B reuses it for the hand body); CorpseTraceWriter is
// its corpse instantiation. The producer only copies a sample into a ring of 1024; a worker thread formats and writes.
#include "grab_mass.hpp"
#include "ragdoll_safety.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace d2vr {
struct CorpseMotorSample {
    std::uint64_t tick{};unsigned side{};int bone{-1};
    double lag_m{},body_speed{},peak_all17{},mass_kg{},cap_fraction{},implied_force_n{};
    bool capped{},tow_written{},tow_saturated{};MassSource mass_source{};
};

class CorpseDiagnostics {
public:
    static constexpr std::size_t csv_max_rows=20000;
    static constexpr unsigned sides=2;
    static constexpr unsigned reason_count=8;   // ReleaseReason values, none..refused
    void note_bind(unsigned side,int bone,MassSource source) noexcept {
        bump(binds_[idx(side)]);
        bump((bone==2||bone==3)?bones_collar_:bones_limb_);
        mass_source_.store(static_cast<unsigned>(source),std::memory_order_relaxed);
    }
    void note_release(unsigned side,ReleaseReason reason) noexcept {
        const auto r=static_cast<unsigned>(reason);
        if(r==0||r>=reason_count) return;       // none is not a release
        bump(releases_[r][idx(side)]);
        if(reason==ReleaseReason::runaway) bump(runaway_);
    }
    void note_motor(const CorpseMotorSample& s) noexcept {
        const auto i=idx(s.side);
        bump(motor_samples_[i]);
        if(s.capped) bump(capped_writes_[i]);
        if(s.tow_written) bump(tow_writes_);
        if(s.tow_saturated) bump(tow_saturated_);
        raise(lag_max_[i],s.lag_m);raise(body_peak_[i],s.body_speed);raise(all17_peak_[i],s.peak_all17);
        mass_source_.store(static_cast<unsigned>(s.mass_source),std::memory_order_relaxed);
    }
    void note_mass_rejected() noexcept {bump(mass_rejected_);}
    void note_mass(const BodyMass& m) noexcept {if(m.rejected) note_mass_rejected();}   // call at each BodyMass use site
    void note_post(bool allowed) noexcept {bump(allowed?posts_:posts_suppressed_);}
    void note_wake_attempt() noexcept {bump(wake_attempts_);}
    void note_wake_refused() noexcept {bump(wake_refused_);}
    void note_near_player(unsigned side) noexcept {bump(near_player_[idx(side)]);}
    void note_anchor_gap(unsigned side,double metres) noexcept {raise(anchor_gap_[idx(side)],metres);}
    void set_fault_latch(bool on) noexcept {fault_latch_.store(on,std::memory_order_relaxed);}
    void set_tuning(bool from_file,bool tow) noexcept {tuning_file_.store(from_file,std::memory_order_relaxed);tow_enabled_.store(tow,std::memory_order_relaxed);}
    std::string json() const {
        try {
            std::string o="{\"holds\":[";
            for(unsigned s=0;s<sides;++s) o+=(s?",":"")+std::to_string(holds(s));
            o+="],\"binds\":"+pair(binds_)+",\"releases\":{";
            static const char* const keys[]={nullptr,"open_fist","lag","lifecycle","runaway","fault","state_changed","refused"};
            for(unsigned r=1;r<reason_count;++r) o+=std::string(r>1?",":"")+"\""+keys[r]+"\":"+pair(releases_[r]);
            o+="},\"motor_samples\":"+pair(motor_samples_)+",\"capped_writes\":"+pair(capped_writes_)+",\"cap_fraction_mean\":[";
            for(unsigned s=0;s<sides;++s) {
                const auto n=motor_samples_[s].load(std::memory_order_relaxed);
                o+=std::string(s?",":"")+num(n?static_cast<double>(capped_writes_[s].load(std::memory_order_relaxed))/static_cast<double>(n):0.0);
            }
            o+="],\"mass_source\":\""+std::string(mass_source_.load(std::memory_order_relaxed)==static_cast<unsigned>(MassSource::measured)?"measured":"class")+
                "\",\"mass_verified\":false,\"mass_read_rejected\":"+n(mass_rejected_)+
                ",\"lag_m_max\":"+dpair(lag_max_)+",\"peak_body_speed\":"+dpair(body_peak_)+",\"peak_bone_speed_all17\":"+dpair(all17_peak_)+
                ",\"runaway\":"+n(runaway_)+",\"tow_writes\":"+n(tow_writes_)+",\"tow_disabled_saturated\":"+n(tow_saturated_)+
                ",\"event_posts_b9\":"+n(posts_)+",\"event_posts_suppressed\":"+n(posts_suppressed_)+
                ",\"fault_latch\":"+(fault_latch_.load(std::memory_order_relaxed)?"true":"false")+
                ",\"wake_attempts\":"+n(wake_attempts_)+",\"wake_refused\":"+n(wake_refused_)+
                ",\"body_near_player\":"+pair(near_player_)+
                ",\"bones\":{\"limb\":"+n(bones_limb_)+",\"collar\":"+n(bones_collar_)+"}"+
                ",\"drawn_anchor_gap_m\":"+dpair(anchor_gap_)+
                ",\"tuning\":\""+(tuning_file_.load(std::memory_order_relaxed)?"file":"default")+"\""+
                ",\"tow_enabled\":"+(tow_enabled_.load(std::memory_order_relaxed)?"true":"false")+"}";
            return o;
        } catch(...) {return "null";}
    }
    static std::string csv_header() {
        return "tick,side,bone,lag_m,body_speed,peak_all17,mass_kg,cap_fraction,implied_force_n,capped,tow_written,tow_saturated,mass_source";
    }
    static std::string csv_row(const CorpseMotorSample& s) {
        try {
            return std::to_string(s.tick)+","+std::to_string(s.side)+","+std::to_string(s.bone)+","+num(s.lag_m)+","+num(s.body_speed)+","+num(s.peak_all17)+","+
                num(s.mass_kg)+","+num(s.cap_fraction)+","+num(s.implied_force_n)+","+(s.capped?"1":"0")+","+(s.tow_written?"1":"0")+","+(s.tow_saturated?"1":"0")+","+
                (s.mass_source==MassSource::measured?"measured":"class");
        } catch(...) {return {};}
    }
private:
    using Count=std::atomic<std::uint64_t>;
    static unsigned idx(unsigned side) noexcept {return side<sides?side:sides-1;}
    static void bump(Count& c) noexcept {c.fetch_add(1,std::memory_order_relaxed);}
    static void raise(std::atomic<double>& a,double v) noexcept {
        if(!std::isfinite(v)) return;
        for(double c=a.load(std::memory_order_relaxed);v>c && !a.compare_exchange_weak(c,v,std::memory_order_relaxed););
    }
    static std::string num(double v) {
        if(!std::isfinite(v)) return "null";
        char b[32];std::snprintf(b,sizeof b,"%.6g",v);return b;
    }
    static std::string n(const Count& a) {return std::to_string(a.load(std::memory_order_relaxed));}
    static std::string pair(const std::array<Count,sides>& a) {return "["+n(a[0])+","+n(a[1])+"]";}
    static std::string dpair(const std::array<std::atomic<double>,sides>& a) {return "["+num(a[0].load(std::memory_order_relaxed))+","+num(a[1].load(std::memory_order_relaxed))+"]";}
    std::uint64_t holds(unsigned s) const noexcept {
        std::uint64_t rel=0;
        for(unsigned r=1;r<reason_count;++r) rel+=releases_[r][s].load(std::memory_order_relaxed);
        const auto b=binds_[s].load(std::memory_order_relaxed);
        return b>rel?b-rel:0;
    }
    std::array<Count,sides> binds_{},motor_samples_{},capped_writes_{},near_player_{};
    std::array<std::array<Count,sides>,reason_count> releases_{};
    std::array<std::atomic<double>,sides> lag_max_{},body_peak_{},all17_peak_{},anchor_gap_{};
    Count bones_limb_{},bones_collar_{},mass_rejected_{},runaway_{},tow_writes_{},tow_saturated_{},posts_{},posts_suppressed_{},wake_attempts_{},wake_refused_{};
    std::atomic<unsigned> mass_source_{static_cast<unsigned>(MassSource::cls)};
    std::atomic<bool> fault_latch_{false},tuning_file_{false},tow_enabled_{false};
};

// Generic bounded CSV trace (single producer thread). Format provides static header() and row(const Sample&).
// A full ring drops the sample (counted); after csv_max_rows accepted rows nothing more is taken. Never throws into the caller.
template<class Sample,class Format> class BoundedCsvTraceWriter {
public:
    static constexpr std::size_t ring=1024;
    ~BoundedCsvTraceWriter() {stop();}
    bool start(const std::filesystem::path& path) noexcept {
        try {
            if(running_.load()) return true;
            out_.open(path,std::ios::trunc);if(!out_.is_open()) return false;
            out_<<Format::header()<<'\n';
            head_=0;tail_=0;pushed_=0;written_=0;dropped_=0;
            running_=true;worker_=std::thread([this] {run();});
            return true;
        } catch(...) {running_=false;try {out_.close();} catch(...) {}return false;}
    }
    void push(const Sample& s) noexcept {
        if(!running_.load(std::memory_order_acquire) || pushed_>=CorpseDiagnostics::csv_max_rows) return;
        const auto h=head_.load(std::memory_order_relaxed);
        if(h-tail_.load(std::memory_order_acquire)>=ring) {dropped_.fetch_add(1,std::memory_order_relaxed);return;}
        buf_[h%ring]=s;head_.store(h+1,std::memory_order_release);++pushed_;
    }
    void stop() noexcept {
        try {
            if(!running_.exchange(false)) return;
            if(worker_.joinable()) worker_.join();
            drain();out_.flush();out_.close();
        } catch(...) {}
    }
    std::uint64_t written() const noexcept {return written_.load();}
    std::uint64_t dropped() const noexcept {return dropped_.load();}
private:
    void drain() noexcept {
        try {
            std::string text;
            for(auto t=tail_.load(std::memory_order_relaxed);t<head_.load(std::memory_order_acquire);++t) {
                text+=Format::row(buf_[t%ring]);text+='\n';written_.fetch_add(1,std::memory_order_relaxed);tail_.store(t+1,std::memory_order_release);
            }
            if(!text.empty()) out_<<text;
        } catch(...) {}
    }
    void run() noexcept {
        auto flushed=std::chrono::steady_clock::now();
        while(running_.load()) {
            drain();
            const auto now=std::chrono::steady_clock::now();
            if(now-flushed>=std::chrono::seconds(1)) {try {out_.flush();} catch(...) {}flushed=now;}
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    std::array<Sample,ring> buf_{};std::atomic<std::uint64_t> head_{},tail_{};
    std::atomic<std::uint64_t> written_{},dropped_{};std::uint64_t pushed_{};
    std::atomic<bool> running_{};std::thread worker_;std::ofstream out_;
};
struct CorpseTraceFormat {
    static std::string header() {return CorpseDiagnostics::csv_header();}
    static std::string row(const CorpseMotorSample& s) {return CorpseDiagnostics::csv_row(s);}
};
using CorpseTraceWriter=BoundedCsvTraceWriter<CorpseMotorSample,CorpseTraceFormat>;
}
