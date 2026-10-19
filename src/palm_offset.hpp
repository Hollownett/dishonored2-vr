#pragma once
// Grip-to-palm calibration (G5, C6, C12). The OpenXR grip origin is mapped onto a skeletal mid-palm point with no per-controller
// correction; research puts the resulting error at about 3-5 cm plus 10-20 degrees. A per-side, per-interaction-profile offset
// (in the grip frame: x forward, y left, z up) moves the game's grip pose onto the palm before the hand clamp, the reach clamp,
// the arm IK and the drawn weapon see it. Aim, throws and selection keep the raw controller (C9).
#include "tracked_pose.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <array>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace d2vr {
struct PalmOffset {Vec3 translation{};double pitch_deg{},yaw_deg{};};
inline constexpr double palm_offset_max_translation=.08,palm_offset_max_tilt_deg=30;
inline constexpr double palm_tilt_blend_seconds=.15;
inline constexpr const char* palm_default_profile="/interaction_profiles/oculus/touch_controller";

inline PalmOffset default_palm_offset(unsigned side) {return {{0,side?-.03:.03,0},15,0};}
inline bool palm_offset_valid(const PalmOffset& o) {
    const auto axis=[](double v) {return std::isfinite(v) && std::abs(v)<=palm_offset_max_translation;};
    const auto tilt=[](double v) {return std::isfinite(v) && std::abs(v)<=palm_offset_max_tilt_deg;};
    return axis(o.translation.x) && axis(o.translation.y) && axis(o.translation.z) && tilt(o.pitch_deg) && tilt(o.yaw_deg);
}
inline PalmOffset clamp_palm_offset(PalmOffset o) {
    const auto limit=[](double v,double bound) {return std::isfinite(v)?std::clamp(v,-bound,bound):0.;};
    o.translation={limit(o.translation.x,palm_offset_max_translation),limit(o.translation.y,palm_offset_max_translation),limit(o.translation.z,palm_offset_max_translation)};
    o.pitch_deg=limit(o.pitch_deg,palm_offset_max_tilt_deg);o.yaw_deg=limit(o.yaw_deg,palm_offset_max_tilt_deg);
    return o;
}
// The position moves by R*translation. A tilt rotates the basis about basis[1] by +pitch (forward tips toward -up), then about the
// resulting up axis by yaw (forward turns toward left). tilt_weight (0..1) scales both angles, so equipping a weapon can ease the tilt out.
inline WorldTrackedPose apply_palm_offset(const WorldTrackedPose& grip,const PalmOffset& offset,double tilt_weight) {
    constexpr double pi=3.14159265358979323846;
    auto out=grip;
    out.position=grip.position+grip.basis[0]*offset.translation.x+grip.basis[1]*offset.translation.y+grip.basis[2]*offset.translation.z;
    const double weight=std::isfinite(tilt_weight)?std::clamp(tilt_weight,0.,1.):0.;
    if(weight>0 && (offset.pitch_deg!=0 || offset.yaw_deg!=0)) {
        const double p=offset.pitch_deg*weight*pi/180,y=offset.yaw_deg*weight*pi/180;
        const double cp=std::cos(p),sp=std::sin(p),cy=std::cos(y),sy=std::sin(y);
        const Vec3 f=grip.basis[0]*cp-grip.basis[2]*sp,u=grip.basis[2]*cp+grip.basis[0]*sp,l=grip.basis[1];
        out.basis={f*cy+l*sy,l*cy-f*sy,u};
    }
    return out;
}
inline WorldTrackedPose apply_palm_offset(const WorldTrackedPose& grip,const PalmOffset& offset,bool tilt) {return apply_palm_offset(grip,offset,tilt?1.:0.);}

// Per hand: 1 while the hand is empty, 0 while armed, easing over 0.15 s so equipping does not pop.
class PalmTiltBlend {
    double weight_=1;bool seen_{};
public:
    // The first call sets the weight from the hand's state (a weapon out at session start or a reacquired hand never flashes the tilt).
    double update(bool armed,double dt) {
        if(!seen_) {seen_=true;weight_=armed?0.:1.;return weight_;}
        const double step=std::isfinite(dt)?std::clamp(dt,0.,.1)/palm_tilt_blend_seconds:0.;
        weight_=armed?std::max(0.,weight_-step):std::min(1.,weight_+step);
        return weight_;
    }
    double weight() const noexcept {return weight_;}
};

// "d2vr-palm-offsets 1\n", then "<side> <profile> tx ty tz pitch yaw\n". At most 16 entries and 4096 bytes. load() throws on a file
// that is unreadable, oversized or malformed and leaves the store as it was; a missing file is a first run, not an error.
// The hands that are NOT empty, as a bit per side: a drawn weapon, a carried body or a held object. The tilt applies to empty hands only. Carrying
// and holding come from a state the game publishes with a tick, so they count only while fresh; the weapon bits are used as published.
inline unsigned palm_occupied_bits(unsigned weapon_bits,unsigned carrying_bits,bool holding,unsigned hold_side,bool state_fresh) {
    unsigned bits=weapon_bits&3u;
    if(state_fresh) {bits|=carrying_bits&3u;if(holding) bits|=1u<<(hold_side&1u);}
    return bits;
}

class PalmOffsetStore {
public:
    static constexpr std::size_t max_entries=16,max_bytes=4096,max_profile=128;
private:
    struct Entry {unsigned side{};std::string profile;PalmOffset value;};
    std::vector<Entry> entries_;
public:
    static bool valid_profile(const std::string& p) {return profile_ok(p);}
private:
    static bool profile_ok(const std::string& p) {
        static const std::string prefix="/interaction_profiles/";
        if(p.size()<=prefix.size() || p.size()>max_profile || p.compare(0,prefix.size(),prefix)!=0) return false;
        return std::all_of(p.begin(),p.end(),[](unsigned char c) {return c>32 && c<127;});
    }
    static bool number(std::istringstream& in,double& out) {
        std::string token;if(!(in>>token)) return false;
        char* end=nullptr;out=std::strtod(token.c_str(),&end);
        return end==token.c_str()+token.size() && std::isfinite(out);
    }
    Entry* find(unsigned side,const std::string& profile) {
        for(auto& e:entries_) if(e.side==side && e.profile==profile) return &e;
        return nullptr;
    }
    const Entry* find(unsigned side,const std::string& profile) const {
        for(const auto& e:entries_) if(e.side==side && e.profile==profile) return &e;
        return nullptr;
    }
public:
    void load(const std::filesystem::path& file) {
        std::error_code ec;
        const bool present=std::filesystem::exists(file,ec);
        if(ec) throw std::runtime_error("Cannot inspect palm offsets");
        if(!present) return;
        if(!std::filesystem::is_regular_file(file,ec)) throw std::runtime_error("Palm offsets path is not a file");
        const auto size=std::filesystem::file_size(file,ec);
        if(ec || size>max_bytes) throw std::runtime_error("Palm offsets file unreadable or too large");
        std::string text;
        {std::ifstream in(file,std::ios::binary);if(!in) throw std::runtime_error("Cannot open palm offsets");
         text.assign(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
         if(in.bad()) throw std::runtime_error("Cannot read palm offsets");}
        if(text.size()>max_bytes) throw std::runtime_error("Palm offsets file too large");
        std::istringstream lines(text);std::string line;
        if(!std::getline(lines,line) || line!="d2vr-palm-offsets 1") throw std::runtime_error("Bad palm offsets header");
        std::vector<Entry> parsed;
        while(std::getline(lines,line)) {
            if(line.empty()) continue;
            if(parsed.size()>=max_entries) throw std::runtime_error("Too many palm offsets");
            std::istringstream fields(line);Entry e{};std::string side_token;
            if(!(fields>>side_token>>e.profile) || (side_token!="0" && side_token!="1")) throw std::runtime_error("Bad palm offset entry");
            e.side=side_token=="1";
            double v[5]{};for(auto& x:v) if(!number(fields,x)) throw std::runtime_error("Bad palm offset number");
            std::string rest;if(fields>>rest) throw std::runtime_error("Trailing palm offset data");
            e.value={{v[0],v[1],v[2]},v[3],v[4]};
            if(!profile_ok(e.profile) || !palm_offset_valid(e.value)) throw std::runtime_error("Palm offset out of range");
            for(const auto& p:parsed) if(p.side==e.side && p.profile==e.profile) throw std::runtime_error("Duplicate palm offset");
            parsed.push_back(std::move(e));
        }
        entries_=std::move(parsed);
    }
    void save(const std::filesystem::path& file) const {
        std::ostringstream text;text.precision(17);text<<"d2vr-palm-offsets 1\n";
        for(const auto& e:entries_) text<<e.side<<' '<<e.profile<<' '<<e.value.translation.x<<' '<<e.value.translation.y<<' '<<e.value.translation.z<<' '<<e.value.pitch_deg<<' '<<e.value.yaw_deg<<'\n';
        const auto body=text.str();
        if(body.size()>max_bytes) throw std::runtime_error("Palm offsets would exceed the file limit"); // the loader would reject it
        auto temporary=file;temporary+=L".tmp";
        {std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
         if(!out) throw std::runtime_error("Cannot save palm offsets");
         out<<body;out.flush();if(!out) throw std::runtime_error("Cannot write palm offsets");}
        std::error_code ec;std::filesystem::rename(temporary,file,ec);
        if(ec) {std::error_code ignore;std::filesystem::remove(temporary,ignore);throw std::runtime_error("Cannot publish palm offsets; previous file retained");}
    }
    std::pair<PalmOffset,bool> get(unsigned side,const std::string& profile) const {
        side&=1u;
        if(const auto* e=find(side,profile)) return {e->value,true};
        return {default_palm_offset(side),false};
    }
    void set(unsigned side,const std::string& profile,const PalmOffset& value) {
        side&=1u;
        if(!profile_ok(profile)) throw std::invalid_argument("Invalid interaction profile");
        const auto clamped=clamp_palm_offset(value);
        if(auto* e=find(side,profile)) {e->value=clamped;return;}
        if(entries_.size()>=max_entries) entries_.erase(entries_.begin()); // the oldest entry makes room
        entries_.push_back({side,profile,clamped});
    }
};

// The store plus the interaction profile each hand is bound to. A fit confirmed before the runtime has named the profile is held pending
// and keyed under the real profile when it arrives (never stranded under the suggested fallback); a runtime that cannot be asked
// (profile_query_available=false) treats the suggested profile as authoritative. Not thread-safe: the owner locks.
class PalmCalibration {
    PalmOffsetStore store_;std::array<std::string,2> profile_{};std::array<std::optional<PalmOffset>,2> pending_{};
    std::string key(unsigned side) const {return profile_[side&1u].empty()?std::string(palm_default_profile):profile_[side&1u];}
public:
    bool profile_query_available=true;
    PalmOffsetStore& store() {return store_;}
    const PalmOffsetStore& store() const {return store_;}
    std::string profile(unsigned side) const {return key(side);}
    PalmOffset get(unsigned side,bool* saved=nullptr) const {
        side&=1u;
        if(pending_[side]) {if(saved) *saved=false;return *pending_[side];}
        const auto stored=store_.get(side,key(side));
        if(saved) *saved=stored.second;
        return stored.first;
    }
    // true: the store changed and should be written.
    bool confirm(unsigned side,const PalmOffset& value) {
        side&=1u;
        if(profile_[side].empty() && profile_query_available) {pending_[side]=clamp_palm_offset(value);return false;}
        pending_[side].reset();store_.set(side,key(side),value);return true;
    }
    // A profile reported by the runtime. true: a deferred fit was keyed under it and the store should be written.
    bool set_profile(unsigned side,const std::string& profile) {
        side&=1u;
        if(!PalmOffsetStore::valid_profile(profile)) return false;
        profile_[side]=profile;
        if(!pending_[side]) return false;
        store_.set(side,profile,*pending_[side]);pending_[side].reset();return true;
    }
};

// Writes snapshots of the store on a worker thread so the XR frame never waits on the disk. Only the newest pending snapshot is kept. The
// worker owns its state (shared), so destroying the saver never blocks on the disk or on a thread the loader may have killed.
class PalmSaver {
    struct State {
        std::mutex mutex;std::condition_variable wake,idle;std::optional<PalmOffsetStore> job;std::filesystem::path file;
        bool stop{},busy{},error{},started{};
    };
    std::shared_ptr<State> state_=std::make_shared<State>();
    static void run(std::shared_ptr<State> state) {
        std::unique_lock<std::mutex> lock(state->mutex);
        for(;;) {
            state->wake.wait(lock,[&]{return state->stop || state->job;});
            if(state->stop && !state->job) return;
            if(state->stop) return; // shutting down: drop the pending write rather than stall the exit
            auto job=std::move(*state->job);state->job.reset();state->busy=true;
            lock.unlock();
            bool failed=false;try {job.save(state->file);} catch(...) {failed=true;}
            lock.lock();state->busy=false;state->error=failed;state->idle.notify_all();
        }
    }
public:
    PalmSaver()=default;
    explicit PalmSaver(std::filesystem::path file) {state_->file=std::move(file);}
    void set_file(std::filesystem::path file) {std::lock_guard<std::mutex> lock(state_->mutex);state_->file=std::move(file);}
    PalmSaver(const PalmSaver&)=delete;PalmSaver& operator=(const PalmSaver&)=delete;
    ~PalmSaver() {{std::lock_guard<std::mutex> lock(state_->mutex);state_->stop=true;state_->idle.notify_all();}state_->wake.notify_all();}
    void submit(PalmOffsetStore snapshot) noexcept {
        try {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if(state_->file.empty() || state_->stop) return;
            state_->job=std::move(snapshot);
            if(!state_->started) {state_->started=true;std::thread(run,state_).detach();}
        } catch(...) {}
        state_->wake.notify_all();
    }
    void flush() { // for tests: wait until nothing is pending or being written
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->idle.wait(lock,[&]{return !state_->job && !state_->busy;});
    }
    bool error() const {std::lock_guard<std::mutex> lock(state_->mutex);return state_->error;}
};

// The hand-fit step: the user holds a controller naturally, nudges with the thumbsticks, confirms with the trigger.
class PalmFitStep {
    bool active_{};unsigned side_{};PalmOffset value_{};
public:
    static constexpr double move_rate=.02,turn_rate_deg=20;
    void begin(unsigned side,PalmOffset current) {side_=side&1u;value_=clamp_palm_offset(current);active_=true;}
    // Stick up = forward, stick left = hand left; right stick up = pitch up (the fingertips rise), right stick left = yaw left.
    void nudge(float move_x,float move_y,float turn_x,float turn_y,double dt) {
        if(!active_ || !std::isfinite(dt) || dt<=0 || !std::isfinite(move_x) || !std::isfinite(move_y) || !std::isfinite(turn_x) || !std::isfinite(turn_y)) return;
        dt=std::min(dt,.1);
        const auto unit=[](float v) {return static_cast<double>(std::clamp(v,-1.f,1.f));};
        value_.translation.x+=unit(move_y)*move_rate*dt;value_.translation.y+=-unit(move_x)*move_rate*dt;
        value_.pitch_deg+=-unit(turn_y)*turn_rate_deg*dt;value_.yaw_deg+=-unit(turn_x)*turn_rate_deg*dt;
        value_=clamp_palm_offset(value_);
    }
    PalmOffset value() const {return value_;}
    unsigned side() const noexcept {return side_;}
    std::optional<PalmOffset> confirm() {if(!active_) return std::nullopt;active_=false;return value_;}
    void cancel() {active_=false;}
    bool active() const noexcept {return active_;}
};
} // namespace d2vr
