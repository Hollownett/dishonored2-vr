#pragma once
#include "finger_ik.hpp"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

// One record of what the finger solver was given and what it answered (artifacts/finger-snapshots.jsonl, one JSON object per line), so
// tools/hand_pose_lab.cpp --snapshot FILE[:index] can pose and render exactly that input offline. Pure data and text: no game memory.
namespace d2vr::rig {
struct SnapShape {Vec3 position{};std::array<Vec3,3> basis{};Vec3 half{};double radius{};std::string kind;double radius_end{-1};double rim{-1};}; // radius_end >= 0: a round cone, rim >= 0: a flat-ended cylinder (HeldShape)
// What the native hold read from the game when the record was written: the raw bounds and axes of the first frame and of now, whether the
// drawn model had caught up with the published pose, what the cylinder fit made of them, where the palm touched, and the seat.
struct SnapHeld {
    bool present{};std::uint64_t entity{};
    bool have_first{};std::array<float,8> first_box{};std::array<float,3> first_origin{};std::array<float,9> first_axes{};
    bool have_now{};std::array<float,8> box{};std::array<float,3> origin{};std::array<float,9> axes{};
    double gate_distance{-1};bool gate_ok{};          // |model origin - last published origin| < 2 cm: the drawn model has taken the published pose
    bool cylinder_ok{};std::string cylinder_error;unsigned cylinder_axis{};double cylinder_half{},cylinder_radius{};
    bool have_pre{};std::array<float,8> pre_box{};std::array<float,9> pre_axes{};std::array<float,3> pre_origin{};double pre_age{}; // the prop as the hand targeted it before the pickup (ms old): what the touch was measured on
    RadiusProfile profile{};                          // the prop's measured cross-section (radius_profile.hpp); invalid: none
    bool have_extents{};Vec3 extents{};               // the prop's local mesh box (half extents along its own axes), solved from the world AABB
    unsigned touch_mode{};double touch_rotation{-1}; // the seat the touch chose (seat_held mode) and how far it turns the prop from its pre-pickup axes (rad; -1: not measured)
    bool touched{};double touch_along{held_grip_fraction},touch_roll{};double touch_gap{-1};std::string touch_error,touch_source; // source: the bounds the touch was measured on ("probe": seen by the finger probes before the pickup; "first frame")
    // The controller grip as the hand pressed (HeldTouch measured the touch against it, with the prop's pre-pickup bounds): position, axes, how old
    // the sample was and its squeeze; and what each hand's probes/aim last saw (entity, ms old; -1: nothing): why a touch had no pre-pickup bounds.
    bool have_press{};Vec3 press_position{};std::array<Vec3,3> press_basis{};double press_age{},press_squeeze{};
    std::array<std::uint64_t,2> seen_entity{};std::array<double,2> seen_age{-1,-1};
    // Why a touch had, or had not, the prop as it was before the pickup: the RTTI class of the held entity and of each hand's seen one, the seen props' box
    // centres and local half extents (0: unsolved), and how one was matched to the held prop: "id", "same", "similar" or "none" (empty: no pickup resolved).
    // The grip fit of the hold (finger_ik.hpp GripFitter) once it has finished: the knuckle shift and the slide along the hole axis it chose, the fingers' summed
    // float on the constant's seat and on the chosen one (m), and the seats it tried.
    bool have_fit{};double fit_shift{},fit_slide{},fit_plain{},fit_fitted{};unsigned fit_tried{};
    std::string held_class,match;std::array<std::string,2> seen_class;std::array<Vec3,2> seen_centre{},seen_half{};
    Vec3 centre{},seat_origin{},published{};std::array<Vec3,3> seat_axes{};bool seated{};
    std::uint64_t cylinder_set_frames{},gate_fail_frames{};
};
struct FingerSnapshot {
    std::uint64_t tick{};unsigned side{};std::string reason;std::string source;
    WorldTrackedPose palm{},grip{};double dt{};float squeeze{},index{},reach{1};bool holding{};
    std::vector<SnapShape> shapes;
    FingerCurls contact{};std::array<std::array<float,3>,5> turns{};double cost_us{};
    SnapHeld held{};
};

namespace snapshot_detail {
inline std::string num(double v) {char b[40];std::snprintf(b,sizeof b,"%.9g",std::isfinite(v)?v:0.);return b;}
inline std::string vec(Vec3 v) {return "["+num(v.x)+","+num(v.y)+","+num(v.z)+"]";}
inline std::string basis(const std::array<Vec3,3>& b) {return "["+vec(b[0])+","+vec(b[1])+","+vec(b[2])+"]";}
template<std::size_t N> std::string list(const std::array<float,N>& a) {std::string o="[";for(std::size_t i=0;i<N;++i) o+=(i?",":"")+num(a[i]);return o+"]";}
inline std::string quote(const std::string& s) {
    std::string o="\"";
    for(unsigned char c:s) {if(c=='"'||c=='\\') {o+='\\';o+=static_cast<char>(c);} else if(c<32) o+='?';else o+=static_cast<char>(c);}
    return o+"\"";
}
inline std::string pose(const WorldTrackedPose& p) {return "{\"p\":"+vec(p.position)+",\"basis\":"+basis(p.basis)+"}";}
}

inline std::string snapshot_json(const FingerSnapshot& s) {
    using namespace snapshot_detail;
    std::ostringstream o;
    o<<"{\"tick\":"<<s.tick<<",\"side\":"<<s.side<<",\"reason\":"<<quote(s.reason)<<",\"source\":"<<quote(s.source)<<",\"palm\":"<<pose(s.palm)<<",\"grip\":"<<pose(s.grip)
     <<",\"dt\":"<<num(s.dt)<<",\"squeeze\":"<<num(s.squeeze)<<",\"index\":"<<num(s.index)<<",\"reach\":"<<num(s.reach)<<",\"holding\":"<<(s.holding?"true":"false")<<",\"shapes\":[";
    for(std::size_t i=0;i<s.shapes.size();++i) {const auto& h=s.shapes[i];
        o<<(i?",":"")<<"{\"p\":"<<vec(h.position)<<",\"basis\":"<<basis(h.basis)<<",\"half\":"<<vec(h.half)<<",\"radius\":"<<num(h.radius)<<(h.radius_end>=0?",\"radius_end\":"+num(h.radius_end):std::string())<<(h.rim>=0?",\"rim\":"+num(h.rim):std::string())<<",\"kind\":"<<quote(h.kind)<<"}";}
    o<<"],\"contact\":{\"pose\":[";
    for(unsigned f=0;f<5;++f) o<<(f?",":"")<<list(s.contact.pose[f]);
    o<<"],\"finger\":"<<list(s.contact.finger)<<",\"touching\":[";
    for(unsigned f=0;f<5;++f) o<<(f?",":"")<<(s.contact.touching[f]?"true":"false");
    o<<"],\"resting\":"<<num(s.contact.resting)<<"},\"turns\":[";
    for(unsigned f=0;f<5;++f) o<<(f?",":"")<<list(s.turns[f]);
    o<<"],\"cost_us\":"<<num(s.cost_us);
    const auto& h=s.held;
    o<<",\"held\":{\"present\":"<<(h.present?"true":"false");
    if(h.present) {
        o<<",\"entity\":"<<h.entity<<",\"first\":"<<(h.have_first?"{\"box\":"+list(h.first_box)+",\"origin\":"+list(h.first_origin)+",\"axes\":"+list(h.first_axes)+"}":std::string("null"))
         <<",\"now\":"<<(h.have_now?"{\"box\":"+list(h.box)+",\"origin\":"+list(h.origin)+",\"axes\":"+list(h.axes)+"}":std::string("null"))
         <<",\"gate\":{\"distance\":"<<num(h.gate_distance)<<",\"ok\":"<<(h.gate_ok?"true":"false")<<",\"cylinder_frames\":"<<h.cylinder_set_frames<<",\"fail_frames\":"<<h.gate_fail_frames<<"}"
         <<",\"cylinder\":{\"ok\":"<<(h.cylinder_ok?"true":"false")<<",\"error\":"<<quote(h.cylinder_error)<<",\"axis\":"<<h.cylinder_axis<<",\"half\":"<<num(h.cylinder_half)<<",\"radius\":"<<num(h.cylinder_radius)<<",\"extents\":"<<(h.have_extents?vec(h.extents):std::string("null"))<<",\"pre\":"<<(h.have_pre?"{\"box\":"+list(h.pre_box)+",\"origin\":"+list(h.pre_origin)+",\"axes\":"+list(h.pre_axes)+",\"age\":"+num(h.pre_age)+"}":std::string("null"))<<",\"profile\":"<<(h.profile.half>0?"{\"half\":"+num(h.profile.half)+",\"axis\":"+std::to_string(h.profile.axis)+",\"plus\":"+list(h.profile.plus)+",\"minus\":"+list(h.profile.minus)+"}":std::string("null"))<<"}"
         <<",\"touch\":{\"touched\":"<<(h.touched?"true":"false")<<",\"along\":"<<num(h.touch_along)<<",\"roll\":"<<num(h.touch_roll)<<",\"mode\":"<<h.touch_mode<<",\"rotation\":"<<num(h.touch_rotation)<<",\"gap\":"<<num(h.touch_gap)<<",\"error\":"<<quote(h.touch_error)<<",\"source\":"<<quote(h.touch_source)
         <<",\"press\":"<<(h.have_press?"{\"p\":"+vec(h.press_position)+",\"basis\":"+basis(h.press_basis)+",\"age\":"+num(h.press_age)+",\"squeeze\":"+num(h.press_squeeze)+"}":std::string("null"))
         <<",\"seen\":[["<<h.seen_entity[0]<<","<<num(h.seen_age[0])<<"],["<<h.seen_entity[1]<<","<<num(h.seen_age[1])<<"]]"
         <<",\"class\":"<<quote(h.held_class)<<",\"match\":"<<quote(h.match)<<",\"info\":[";
        for(unsigned k=0;k<2;++k) o<<(k?",":"")<<"{\"class\":"<<quote(h.seen_class[k])<<",\"centre\":"<<vec(h.seen_centre[k])<<",\"half\":"<<vec(h.seen_half[k])<<"}";
        o<<"]}"
         
         <<",\"fit\":"<<(h.have_fit?"{\"shift\":"+num(h.fit_shift)+",\"slide\":"+num(h.fit_slide)+",\"plain\":"+num(h.fit_plain)+",\"fitted\":"+num(h.fit_fitted)+",\"tried\":"+std::to_string(h.fit_tried)+"}":std::string("null"))
         <<",\"centre\":"<<vec(h.centre)<<",\"published\":"<<vec(h.published)<<",\"seated\":"<<(h.seated?"true":"false")<<",\"seat_origin\":"<<vec(h.seat_origin)<<",\"seat_axes\":"<<basis(h.seat_axes);
    }
    o<<"}}";
    return o.str();
}

// One hand's finger geometry as the solver was given it (finger_ik.hpp hand_geometry, hinge_axes): the palm normal, per finger the pivot, bones,
// hinges, existing bend and lowest turn (palm frame), the hinge axes in the palette's space, the bind grip, and the bind transforms of the wrist and
// the thumb's three joints (16 floats, a column-major 4x4 as the palette holds them). artifacts/finger-geometry.jsonl in game, tools/hand_pose_lab --write-geometry
// for the lab's own: the two diff line against line, which says whether the game's hand and the lab's are the same hand.
inline std::string geometry_json(unsigned side,const HandGeometry& g,const std::array<std::array<Vec3,3>,5>& axes,const Transform& grip,const std::array<Transform,4>& wrist_and_thumb) {
    using namespace snapshot_detail;
    std::ostringstream o;
    o<<"{\"side\":"<<side<<",\"palm_in\":"<<vec(g.palm_in)<<",\"grip\":"<<list(grip.f)<<",\"bind\":[";
    for(unsigned i=0;i<4;++i) o<<(i?",":"")<<list(wrist_and_thumb[i].f);
    o<<"],\"fingers\":[";
    for(unsigned f=0;f<5;++f) {
        const auto& s=g.finger[f];
        o<<(f?",":"")<<"{\"pivot\":"<<vec(s.pivot)<<",\"bone\":"<<basis(s.bone)<<",\"hinge\":"<<basis(s.hinge)<<",\"axes\":"<<basis(axes[f])
         <<",\"existing\":"<<vec({s.existing[0],s.existing[1],s.existing[2]})<<",\"lowest\":"<<vec({s.lowest[0],s.lowest[1],s.lowest[2]})<<"}";
    }
    o<<"]}";
    return o.str();
}

// A small JSON reader (numbers, strings, booleans, null, arrays, objects): enough for the lines above.
struct Json {
    enum Type {null,boolean,number,string,array,object} type{null};
    bool b{};double n{};std::string s;std::vector<Json> a;std::map<std::string,Json> o;
    const Json* get(const std::string& key) const {if(type!=object) return nullptr;const auto it=o.find(key);return it==o.end()?nullptr:&it->second;}
    double num_or(double fallback) const {return type==number?n:fallback;}
};
namespace snapshot_detail {
struct Reader {
    const std::string& t;std::size_t i{};
    void ws() {while(i<t.size() && (t[i]==' '||t[i]=='\n'||t[i]=='\r'||t[i]=='\t')) ++i;}
    [[noreturn]] void fail(const char* what) {throw std::invalid_argument(std::string("Invalid snapshot JSON: ")+what);}
    Json value() {
        ws();if(i>=t.size()) fail("end");
        Json j;const char c=t[i];
        if(c=='{') {
            j.type=Json::object;++i;ws();
            if(i<t.size() && t[i]=='}') {++i;return j;}
            for(;;) {ws();const auto k=value();if(k.type!=Json::string) fail("key");ws();if(i>=t.size() || t[i]!=':') fail("colon");++i;j.o[k.s]=value();ws();
                if(i<t.size() && t[i]==',') {++i;continue;}
                if(i<t.size() && t[i]=='}') {++i;return j;}
                fail("object");}
        }
        if(c=='[') {
            j.type=Json::array;++i;ws();
            if(i<t.size() && t[i]==']') {++i;return j;}
            for(;;) {j.a.push_back(value());ws();
                if(i<t.size() && t[i]==',') {++i;continue;}
                if(i<t.size() && t[i]==']') {++i;return j;}
                fail("array");}
        }
        if(c=='"') {
            j.type=Json::string;++i;
            while(i<t.size() && t[i]!='"') {if(t[i]=='\\' && i+1<t.size()) ++i;j.s+=t[i++];}
            if(i>=t.size()) fail("string");++i;return j;
        }
        if(t.compare(i,4,"true")==0) {j.type=Json::boolean;j.b=true;i+=4;return j;}
        if(t.compare(i,5,"false")==0) {j.type=Json::boolean;i+=5;return j;}
        if(t.compare(i,4,"null")==0) {i+=4;return j;}
        char* end=nullptr;j.n=std::strtod(t.c_str()+i,&end);
        if(end==t.c_str()+i) fail("number");
        j.type=Json::number;i=static_cast<std::size_t>(end-t.c_str());return j;
    }
};
inline Vec3 vec3(const Json* j) {
    if(!j || j->type!=Json::array || j->a.size()!=3) throw std::invalid_argument("Invalid snapshot vector");
    return {j->a[0].n,j->a[1].n,j->a[2].n};
}
inline std::array<Vec3,3> basis3(const Json* j) {
    if(!j || j->type!=Json::array || j->a.size()!=3) throw std::invalid_argument("Invalid snapshot basis");
    return {vec3(&j->a[0]),vec3(&j->a[1]),vec3(&j->a[2])};
}
inline WorldTrackedPose pose3(const Json* j) {
    if(!j) throw std::invalid_argument("Invalid snapshot pose");
    return {vec3(j->get("p")),basis3(j->get("basis"))};
}
template<std::size_t N> std::array<float,N> floats(const Json* j) {
    if(!j || j->type!=Json::array || j->a.size()!=N) throw std::invalid_argument("Invalid snapshot list");
    std::array<float,N> out{};for(std::size_t i=0;i<N;++i) out[i]=static_cast<float>(j->a[i].n);return out;
}
}
inline Json parse_json(const std::string& text) {snapshot_detail::Reader r{text};auto j=r.value();return j;}

// The solver inputs and outputs of one line. The held block is read back as far as the lab needs it.
inline FingerSnapshot parse_snapshot(const std::string& line) {
    using namespace snapshot_detail;
    const auto j=parse_json(line);
    if(j.type!=Json::object) throw std::invalid_argument("Invalid snapshot record");
    FingerSnapshot s{};
    s.tick=static_cast<std::uint64_t>(j.get("tick")?j.get("tick")->n:0);s.side=static_cast<unsigned>(j.get("side")?j.get("side")->n:0)&1u;
    if(const auto* r=j.get("reason")) s.reason=r->s;
    if(const auto* r=j.get("source")) s.source=r->s;
    s.palm=pose3(j.get("palm"));s.grip=pose3(j.get("grip"));
    s.dt=j.get("dt")?j.get("dt")->n:0;s.squeeze=static_cast<float>(j.get("squeeze")?j.get("squeeze")->n:0);s.index=static_cast<float>(j.get("index")?j.get("index")->n:0);
    s.reach=static_cast<float>(j.get("reach")?j.get("reach")->n:1);s.holding=j.get("holding") && j.get("holding")->b;
    if(const auto* sh=j.get("shapes")) if(sh->type==Json::array) for(const auto& e:sh->a) {
        SnapShape x{};x.position=vec3(e.get("p"));x.basis=basis3(e.get("basis"));x.half=vec3(e.get("half"));x.radius=e.get("radius")?e.get("radius")->n:0;x.radius_end=e.get("radius_end")?e.get("radius_end")->n:-1;x.rim=e.get("rim")?e.get("rim")->n:-1;
        if(const auto* k=e.get("kind")) x.kind=k->s;
        s.shapes.push_back(x);
    }
    if(const auto* c=j.get("contact")) {
        if(const auto* p=c->get("pose")) if(p->type==Json::array && p->a.size()==5) for(unsigned f=0;f<5;++f) s.contact.pose[f]=floats<3>(&p->a[f]);
        if(const auto* p=c->get("finger")) s.contact.finger=floats<5>(p);
        if(const auto* p=c->get("touching")) if(p->type==Json::array && p->a.size()==5) for(unsigned f=0;f<5;++f) s.contact.touching[f]=p->a[f].b;
        s.contact.resting=static_cast<float>(c->get("resting")?c->get("resting")->n:0);
    }
    if(const auto* t=j.get("turns")) if(t->type==Json::array && t->a.size()==5) for(unsigned f=0;f<5;++f) s.turns[f]=floats<3>(&t->a[f]);
    s.cost_us=j.get("cost_us")?j.get("cost_us")->n:0;
    if(const auto* h=j.get("held")) if(h->get("present") && h->get("present")->b) {
        auto& x=s.held;x.present=true;x.entity=static_cast<std::uint64_t>(h->get("entity")?h->get("entity")->n:0);
        if(const auto* f=h->get("first")) if(f->type==Json::object) {x.have_first=true;x.first_box=floats<8>(f->get("box"));x.first_origin=floats<3>(f->get("origin"));x.first_axes=floats<9>(f->get("axes"));}
        if(const auto* f=h->get("now")) if(f->type==Json::object) {x.have_now=true;x.box=floats<8>(f->get("box"));x.origin=floats<3>(f->get("origin"));x.axes=floats<9>(f->get("axes"));}
        if(const auto* g=h->get("gate")) {x.gate_distance=g->get("distance")?g->get("distance")->n:-1;x.gate_ok=g->get("ok") && g->get("ok")->b;
            x.cylinder_set_frames=static_cast<std::uint64_t>(g->get("cylinder_frames")?g->get("cylinder_frames")->n:0);x.gate_fail_frames=static_cast<std::uint64_t>(g->get("fail_frames")?g->get("fail_frames")->n:0);}
        if(const auto* c=h->get("cylinder")) {x.cylinder_ok=c->get("ok") && c->get("ok")->b;if(const auto* e=c->get("error")) x.cylinder_error=e->s;
            x.cylinder_axis=static_cast<unsigned>(c->get("axis")?c->get("axis")->n:0);x.cylinder_half=c->get("half")?c->get("half")->n:0;x.cylinder_radius=c->get("radius")?c->get("radius")->n:0;
            if(const auto* ex=c->get("extents")) if(ex->type==Json::array) {x.have_extents=true;x.extents=vec3(ex);}
            if(const auto* pr=c->get("pre")) if(pr->type==Json::object) {
                x.have_pre=true;x.pre_box=floats<8>(pr->get("box"));x.pre_origin=floats<3>(pr->get("origin"));x.pre_axes=floats<9>(pr->get("axes"));x.pre_age=pr->get("age")?pr->get("age")->n:0;
            }
            if(const auto* pr=c->get("profile")) if(pr->type==Json::object) {
                x.profile.half=pr->get("half")?pr->get("half")->n:0;x.profile.axis=static_cast<unsigned>(pr->get("axis")?pr->get("axis")->n:0);
                if(pr->get("plus")) x.profile.plus=floats<profile_stations>(pr->get("plus"));
                if(pr->get("minus")) x.profile.minus=floats<profile_stations>(pr->get("minus"));
            }}
        if(const auto* t=h->get("touch")) {x.touched=t->get("touched") && t->get("touched")->b;x.touch_along=t->get("along")?t->get("along")->n:held_grip_fraction;x.touch_roll=t->get("roll")?t->get("roll")->n:0;
            x.touch_gap=t->get("gap")?t->get("gap")->n:-1;x.touch_mode=static_cast<unsigned>(t->get("mode")?t->get("mode")->n:0);x.touch_rotation=t->get("rotation")?t->get("rotation")->n:-1;if(const auto* e=t->get("error")) x.touch_error=e->s;if(const auto* e=t->get("source")) x.touch_source=e->s;
            if(const auto* p=t->get("press")) if(p->type==Json::object) {x.have_press=true;x.press_position=vec3(p->get("p"));x.press_basis=basis3(p->get("basis"));x.press_age=p->get("age")?p->get("age")->n:0;x.press_squeeze=p->get("squeeze")?p->get("squeeze")->n:0;}
            if(const auto* sn=t->get("seen")) if(sn->type==Json::array && sn->a.size()==2) for(unsigned i=0;i<2;++i) if(sn->a[i].type==Json::array && sn->a[i].a.size()==2) {
                x.seen_entity[i]=static_cast<std::uint64_t>(sn->a[i].a[0].n);x.seen_age[i]=sn->a[i].a[1].n;}
            if(const auto* e=t->get("class")) x.held_class=e->s;
            if(const auto* e=t->get("match")) x.match=e->s;
            if(const auto* in=t->get("info")) if(in->type==Json::array && in->a.size()==2) for(unsigned i=0;i<2;++i) {
                if(const auto* e=in->a[i].get("class")) x.seen_class[i]=e->s;
                if(in->a[i].get("centre")) x.seen_centre[i]=vec3(in->a[i].get("centre"));
                if(in->a[i].get("half")) x.seen_half[i]=vec3(in->a[i].get("half"));
            }}
        if(const auto* ft=h->get("fit")) if(ft->type==Json::object) {
            x.have_fit=true;x.fit_shift=ft->get("shift")?ft->get("shift")->n:0;x.fit_slide=ft->get("slide")?ft->get("slide")->n:0;
            x.fit_plain=ft->get("plain")?ft->get("plain")->n:0;x.fit_fitted=ft->get("fitted")?ft->get("fitted")->n:0;x.fit_tried=static_cast<unsigned>(ft->get("tried")?ft->get("tried")->n:0);
        }
        if(h->get("centre")) x.centre=vec3(h->get("centre"));
        if(h->get("published")) x.published=vec3(h->get("published"));
        x.seated=h->get("seated") && h->get("seated")->b;
        if(h->get("seat_origin")) x.seat_origin=vec3(h->get("seat_origin"));
        if(h->get("seat_axes")) x.seat_axes=basis3(h->get("seat_axes"));
    }
    return s;
}
// The record on `index` of a JSONL text (negative: from the end; the last line by default).
inline std::string snapshot_line(const std::string& text,int index=-1) {
    std::vector<std::string> lines;std::istringstream in(text);std::string line;
    while(std::getline(in,line)) {if(!line.empty() && line.back()=='\r') line.pop_back();if(!line.empty()) lines.push_back(line);}
    if(lines.empty()) throw std::invalid_argument("No snapshots");
    const int n=static_cast<int>(lines.size()),at=index<0?n+index:index;
    if(at<0 || at>=n) throw std::invalid_argument("Snapshot index out of range");
    return lines[static_cast<std::size_t>(at)];
}
} // namespace d2vr::rig
