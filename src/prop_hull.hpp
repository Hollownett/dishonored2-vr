#pragma once
// A prop's REAL shape for the hand's grip (user 2026-11-07: "reverse engineer the game and apply proper grip points to all props ... make sure that
// everything is wrapped as it needs to be wrapped"). The render bounds (model+0x1840) are a world box: turned props made it loose, and a box is not a
// spoon, a bottle or a skull. Every physical prop and ragdoll bone is a Havok hknp body instead, and its collision shape is the game's own description
// of the thing's surface:
//   body record +0x60          the shape (body shape setter 0xca6910 adds/removes its reference there)
//   shape +0x22 u8 type, +0x24 u8 dispatch: 2 = convex (hknpConvexPolytopeShape, hknpBoxShape, hknpCapsuleShape, hknpSphereShape, debris)
//   shape +0x28 float          the convex radius: the surface lies this far outside the vertices' hull
//   shape +0x40 u16 count, +0x42 u16 offset   the vertices, 16-byte xyzw, at shape+0x40+offset, in the body's own frame
// (box factory 0xca15b0 writes type 5, dispatch 2, 8 vertices at offset 0x70; sphere ctor 0xca6550 one vertex; src/native_world_contact_surface.hpp
// reads the same fields live for spheres and capsules.) Composite shapes (compound, mesh, scaled; dispatch != 2) are not read: the caller keeps the box
// of the render bounds, and the miss is counted with its vtable so a headset log names it.
// The game's hulls are coarse (tools/prop_mesh_export read them from the .hkx files: a book, a plate, a box are 8-corner boxes, a screwdriver 7
// vertices, a spoon's hull is its bowl only - the handle has no collision - and a bottle is a two-hull compound), so the hull is the KEY, and the surface
// is the prop's render mesh: the launcher writes prop-shapes.bin (launcher/PropShapes.cs) with every prop model's decimated render-mesh vertices under
// the FNV-1a hash of its hull's vertices (0.1 mm), one entry per model and scale. A body whose hull has an entry gets that mesh as a PropShape: the
// whole mesh's frame (the seat, the flat/bar/pinch choice) and up to six convex slabs along its long axis (the finger contact: a spoon's handle and
// bowl, a screwdriver's handle and shaft). No entry: the hull itself, when it covers the render box (native_rigid_body checks); else the render box.
#include "arm_ik.hpp"
#include "game_rig.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>
namespace d2vr {
inline constexpr unsigned hull_vertex_cap=64; // a hull is built from at most this many vertices (more: the extreme ones along 64 directions)
struct HullSection {double y{},z{},hy{},hz{};};   // a cross-section across the own x axis: its centre (own y, z) and half spans, radius included
struct ConvexHull {
    std::vector<Vec3> vertices;                 // in the hull's own frame
    std::vector<std::array<double,4>> planes;   // unit outward normal (own frame) and offset: n.p - w is the plane distance
    double radius{};                            // the convex radius
    Vec3 centre{};std::array<Vec3,3> axes{{{1,0,0},{0,1,0},{0,0,1}}}; // the own frame in the body's: body point = centre + sum axes[k]*own[k]
    Vec3 half{};                                // own half extents, radius included: x the longest side, z the thinnest
    Vec3 a{},b{};bool solid{};                  // not solid (a point or a segment: a sphere, a capsule): the surface is the capsule a-b
    Vec3 to_own(Vec3 body) const {const Vec3 d=body-centre;return {dot(d,axes[0]),dot(d,axes[1]),dot(d,axes[2])};}
    // Signed distance of an own-frame point to the surface (negative inside). Exact inside and over the faces; near an edge outside it is the largest
    // plane distance, a little short of the true one (zero at the surface itself, where the fingers stop).
    double distance(Vec3 q) const {
        if(!solid) {const Vec3 ab=b-a;const double l2=dot(ab,ab),t=l2>1e-12?std::clamp(dot(q-a,ab)/l2,0.,1.):0.;return length(q-(a+ab*t))-radius;}
        double m=-1e300;for(const auto& p:planes) m=std::max(m,p[0]*q.x+p[1]*q.y+p[2]*q.z-p[3]);
        return m-radius;
    }
    // How far the surface reaches along an own-frame unit direction (from the own origin).
    double support(Vec3 d) const {
        if(!solid) return std::max(dot(a,d),dot(b,d))+radius;
        double s=-1e300;for(const auto& v:vertices) s=std::max(s,dot(v,d));return s+radius;
    }
    // The cross-section across the own x axis at own x: exact for the hull (every vertex pair that straddles the plane), grown by the radius.
    std::optional<HullSection> section(double x) const {
        if(!solid) {const double x0=std::min(a.x,b.x),x1=std::max(a.x,b.x),out=std::max({x0-x,x-x1,0.});
            if(out>=radius) return {};const double r=std::sqrt(radius*radius-out*out);return HullSection{0,0,r,r};}
        double y0=1e300,y1=-1e300,z0=1e300,z1=-1e300;
        const auto take=[&](Vec3 p) {y0=std::min(y0,p.y);y1=std::max(y1,p.y);z0=std::min(z0,p.z);z1=std::max(z1,p.z);};
        for(std::size_t i=0;i<vertices.size();++i) {
            const Vec3 p=vertices[i];if(std::abs(p.x-x)<1e-9) take(p);
            for(std::size_t j=i+1;j<vertices.size();++j) {const Vec3 q=vertices[j];
                if((p.x-x)*(q.x-x)<0) take(p+(q-p)*((x-p.x)/(q.x-p.x)));}
        }
        if(!(y1>=y0)) return {};
        return HullSection{(y0+y1)*.5,(z0+z1)*.5,(y1-y0)*.5+radius,(z1-z0)*.5+radius};
    }
};
// The symmetric 3x3 eigenvectors (Jacobi): the columns of V.
inline void hull_eigen(double A[3][3],double V[3][3]) {
    for(int i=0;i<3;++i) for(int j=0;j<3;++j) V[i][j]=i==j;
    for(int sweep=0;sweep<32;++sweep) {
        const double off=A[0][1]*A[0][1]+A[0][2]*A[0][2]+A[1][2]*A[1][2];if(off<1e-30) break;
        for(int p=0;p<2;++p) for(int q=p+1;q<3;++q) {
            if(std::abs(A[p][q])<1e-300) continue;
            const double th=(A[q][q]-A[p][p])/(2*A[p][q]),t=(th>=0?1.:-1.)/(std::abs(th)+std::sqrt(th*th+1)),c=1/std::sqrt(t*t+1),s=t*c;
            for(int k=0;k<3;++k) {const double akp=A[k][p],akq=A[k][q];A[k][p]=c*akp-s*akq;A[k][q]=s*akp+c*akq;}
            for(int k=0;k<3;++k) {const double apk=A[p][k],aqk=A[q][k];A[p][k]=c*apk-s*aqk;A[q][k]=s*apk+c*aqk;}
            for(int k=0;k<3;++k) {const double vkp=V[k][p],vkq=V[k][q];V[k][p]=c*vkp-s*vkq;V[k][q]=s*vkp+c*vkq;}
        }
    }
}
// The hull of body-frame vertices grown by `radius`. Its own frame: the tighter box (by volume) of the vertices' principal axes and the body's own
// axes (props are modelled square to their axes: an uneven mesh tilted the book's principal axes 6 degrees), sorted longest first
// (ponytail: two candidate frames, not the minimum box).
inline std::optional<ConvexHull> build_hull(std::vector<Vec3> v,double radius) {
    if(v.empty() || v.size()>65536 || !std::isfinite(radius) || radius<0 || radius>2) return {};
    for(const auto& p:v) if(!finite(p) || length(p)>20) return {};
    if(v.size()>hull_vertex_cap) { // the extreme vertex along each of 64 spread directions (all hull vertices of a small hull, the outline of a big one)
        std::vector<Vec3> kept;
        for(unsigned i=0;i<hull_vertex_cap;++i) {
            const double z=1-(i+.5)*2/hull_vertex_cap,r=std::sqrt(std::max(0.,1-z*z)),phi=i*2.399963229728653;const Vec3 d{r*std::cos(phi),r*std::sin(phi),z};
            const Vec3 best=*std::max_element(v.begin(),v.end(),[&](Vec3 p,Vec3 q) {return dot(p,d)<dot(q,d);});
            if(std::none_of(kept.begin(),kept.end(),[&](Vec3 k) {return length(k-best)<1e-7;})) kept.push_back(best);
        }
        v=kept;
    }
    ConvexHull h{};h.radius=radius;
    Vec3 mean{};for(const auto& p:v) mean=mean+p;mean=mean*(1./v.size());
    double C[3][3]{},E[3][3];
    for(const auto& p:v) {const Vec3 d=p-mean;const double e[3]{d.x,d.y,d.z};for(int i=0;i<3;++i) for(int j=0;j<3;++j) C[i][j]+=e[i]*e[j];}
    hull_eigen(C,E);
    std::array<Vec3,3> ax{Vec3{E[0][0],E[1][0],E[2][0]},Vec3{E[0][1],E[1][1],E[2][1]},Vec3{E[0][2],E[1][2],E[2][2]}};
    double lo[3],hi[3];
    const auto fit=[&](std::array<Vec3,3>& a) {double volume=1;
        for(int k=0;k<3;++k) {a[k]=unit(a[k]);lo[k]=1e300;hi[k]=-1e300;for(const auto& p:v) {const double s=dot(p-mean,a[k]);lo[k]=std::min(lo[k],s);hi[k]=std::max(hi[k],s);}volume*=hi[k]-lo[k]+1e-6;}
        return volume;};
    {std::array<Vec3,3> body{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};if(fit(body)<=.999*fit(ax)) ax=body;fit(ax);}
    int order[3]{0,1,2};std::sort(order,order+3,[&](int i,int j) {return hi[i]-lo[i]>hi[j]-lo[j];});
    h.axes={ax[order[0]],ax[order[1]],cross(ax[order[0]],ax[order[1]])};
    h.centre=mean;for(int k=0;k<3;++k) h.centre=h.centre+ax[order[k]]*((lo[order[k]]+hi[order[k]])*.5);
    for(const auto& p:v) h.vertices.push_back(h.to_own(p));
    const double span[3]{hi[order[0]]-lo[order[0]],hi[order[1]]-lo[order[1]],hi[order[2]]-lo[order[2]]};
    h.half={span[0]*.5+radius,span[1]*.5+radius,span[2]*.5+radius};
    const double scale=std::max(span[0],1e-3),flat=1e-5+1e-4*scale;
    if(span[1]<flat) { // a point or a segment (a sphere, a capsule): exact as a capsule
        h.a={-span[0]*.5,0,0};h.b={span[0]*.5,0,0};h.solid=false;
        return h.radius>0 || span[0]>0?std::optional<ConvexHull>(h):std::nullopt;
    }
    std::vector<Vec3> w=h.vertices;
    if(span[2]<flat) {const std::size_t n=w.size();for(std::size_t i=0;i<n;++i) {w[i].z=-flat;w.push_back({w[i].x,w[i].y,flat});}} // a flat polygon: a thin slab
    const double eps=1e-6+1e-5*scale;
    for(std::size_t i=0;i<w.size();++i) for(std::size_t j=i+1;j<w.size();++j) for(std::size_t k=j+1;k<w.size();++k) {
        Vec3 n=cross(w[j]-w[i],w[k]-w[i]);const double l=length(n);if(l<1e-12) continue;n=n*(1/l);double d=dot(n,w[i]);
        bool pos=false,neg=false;for(const auto& p:w) {const double s=dot(n,p)-d;if(s>eps) pos=true;else if(s<-eps) neg=true;if(pos && neg) break;}
        if(pos && neg) continue;
        if(pos) {n=n*-1.;d=-d;}
        if(std::any_of(h.planes.begin(),h.planes.end(),[&](const std::array<double,4>& p) {return p[0]*n.x+p[1]*n.y+p[2]*n.z>1-1e-7 && std::abs(p[3]-d)<eps;})) continue;
        h.planes.push_back({n.x,n.y,n.z,d});
    }
    h.vertices=w;h.solid=h.planes.size()>=4;
    if(!h.solid) return {};
    return h;
}
// A prop's shape for the grip: the whole (its frame, support and box) and the convex pieces the fingers meet; `section` is the real cross-section
// across the whole's long axis at own x (the slab there, in the whole's frame). `mesh`: from the render mesh (else the Havok hull alone).
struct PropShape {
    ConvexHull whole;std::vector<ConvexHull> pieces;std::vector<std::array<double,6>> slabs;bool mesh{}; // slabs: x0,x1,y0,y1,z0,z1 in the whole's frame
    Vec3 box_centre{}; // the centre of its box in the BODY's axes (the model's: a Use hold's seat places that box, dxgi_probe.cpp)
    // At own x of the whole: the slab there - its centre line (whole frame) and its piece's own half width and half thickness (a curved spoon
    // handle's slab spans 2 cm in the whole frame, but the handle is 3 mm thick).
    std::optional<HullSection> section(double x) const {
        for(std::size_t i=0;i<slabs.size();++i) {const auto& s=slabs[i];
            if(x>=s[0] && x<=s[1]) return HullSection{(s[2]+s[3])*.5,(s[4]+s[5])*.5,pieces[i].half.y,pieces[i].half.z};}
        return whole.section(x);
    }
};
inline constexpr unsigned prop_max_pieces=6;inline constexpr double prop_slab_length=.05,prop_slab_overlap=.003;
// Body-frame vertices (a render mesh, or a hull's) -> the whole hull and its slabs: a long thing (its long side 1.5x the others or more) is cut into
// slabs of about prop_slab_length (2..prop_max_pieces) across its long axis, each the hull of its vertices (3 mm overlap so they join); else one piece.
inline std::optional<PropShape> build_prop_shape(const std::vector<Vec3>& v,double radius,bool mesh) {
    auto whole=build_hull(v,radius);if(!whole) return {};
    PropShape s{};s.whole=*whole;s.mesh=mesh;
    {Vec3 lo=v.front(),hi=v.front();for(const auto& p:v) {lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
     s.box_centre=(lo+hi)*.5;}
    const double L=whole->half.x-radius;
    const unsigned k=whole->solid && whole->half.x>=1.5*whole->half.y?std::clamp(static_cast<unsigned>(std::ceil(2*L/prop_slab_length)),2u,prop_max_pieces):1u;
    if(k>1) for(unsigned i=0;i<k;++i) {
        const double x0=-L+2*L*i/k-prop_slab_overlap,x1=-L+2*L*(i+1)/k+prop_slab_overlap;std::vector<Vec3> in;std::array<double,6> box{x0,x1,1e300,-1e300,1e300,-1e300};
        for(const auto& p:v) {const Vec3 q=whole->to_own(p);if(q.x<x0 || q.x>x1) continue;in.push_back(p);
            box[2]=std::min(box[2],q.y);box[3]=std::max(box[3],q.y);box[4]=std::min(box[4],q.z);box[5]=std::max(box[5],q.z);}
        if(in.size()<4) continue;
        if(auto piece=build_hull(in,radius);piece && piece->solid) {s.pieces.push_back(std::move(*piece));s.slabs.push_back(box);} // (a flat or empty slab: none)
    }
    if(s.pieces.empty()) {s.pieces.push_back(s.whole);s.slabs.clear();}
    return s;
}
// The key of a hull: FNV-1a 64 over its vertex count (u32) and its vertices' x, y, z as int32 0.1 mm, in their stored order (launcher/PropShapes.cs).
inline std::uint64_t hull_key(const std::vector<Vec3>& v) {
    std::uint64_t h=1469598103934665603ull;
    const auto mix=[&](std::uint32_t x) {for(unsigned b=0;b<4;++b) {h^=(x>>(8*b))&0xffu;h*=1099511628211ull;}};
    mix(static_cast<std::uint32_t>(v.size()));
    for(const auto& p:v) for(const double c:{p.x,p.y,p.z}) mix(static_cast<std::uint32_t>(static_cast<std::int32_t>(std::lround(c*10000.0))));
    return h;
}
// prop-shapes.bin: "D2PS", u32 version 1, u32 count, then per entry u64 key, u32 n, n x float3 (body frame, metres). Loaded once at install.
inline std::unordered_map<std::uint64_t,std::vector<Vec3>>& prop_shape_db() {static std::unordered_map<std::uint64_t,std::vector<Vec3>> db;return db;}
inline std::size_t load_prop_shape_db(const std::filesystem::path& file) {
    auto& db=prop_shape_db();db.clear();
    std::ifstream in(file,std::ios::binary);char magic[4]{};std::uint32_t version=0,count=0;
    if(!in.read(magic,4) || std::string_view(magic,4)!="D2PS" || !in.read(reinterpret_cast<char*>(&version),4) || version!=1 || !in.read(reinterpret_cast<char*>(&count),4) || count>1000000) return 0;
    for(std::uint32_t i=0;i<count;++i) {
        std::uint64_t key=0;std::uint32_t n=0;if(!in.read(reinterpret_cast<char*>(&key),8) || !in.read(reinterpret_cast<char*>(&n),4) || n>4096) break;
        std::vector<float> f(static_cast<std::size_t>(n)*3);if(!in.read(reinterpret_cast<char*>(f.data()),static_cast<std::streamsize>(f.size()*4))) break;
        auto& v=db[key];v.clear();for(std::uint32_t j=0;j<n;++j) v.push_back({f[j*3],f[j*3+1],f[j*3+2]});
    }
    return db.size();
}
// Live counters (status file): shapes built (from a render mesh / the hull alone), composite shapes kept on the render box (with the last one's vtable
// RVA and type), unreadable shapes.
struct PropHullStats {std::atomic<std::uint64_t> built{},meshes{},composite{},bad{};std::atomic<std::uint64_t> last_vtable{};std::atomic<std::uint32_t> last_type{};};
inline PropHullStats prop_hull_stats{};
inline std::filesystem::path prop_hull_dump; // when set: each new hull appended as one JSON line (the lab replays them: tools/hand_pose_lab --hulls)
// The body's hull from its Havok shape, built on first sight and cached for the session per shape (Havok shares one shape between the instances of a
// model, so this is a per-model cache). Never freed: a held shape keeps pointing at it. nullptr: no convex shape (the render box stands).
template<class Read> const PropShape* prop_hull(Read& read,std::uintptr_t base,std::uintptr_t record) noexcept {
    try {
        struct Entry {std::uintptr_t vtable{};std::uint32_t head{},rel{};float radius{};std::array<float,4> first{};const PropShape* hull{};};
        static std::mutex mutex;static std::unordered_map<std::uintptr_t,Entry> cache;static std::deque<PropShape> store;
        const auto shape=rig::value<std::uintptr_t>(read,record+0x60);if(!shape || !rig::pointer(*shape)) {++prop_hull_stats.bad;return nullptr;}
        const auto vtable=rig::value<std::uintptr_t>(read,*shape);const auto head=rig::value<std::uint32_t>(read,*shape+0x20),rel=rig::value<std::uint32_t>(read,*shape+0x40);
        const auto dispatch=rig::value<unsigned char>(read,*shape+0x24);const auto r=rig::value<float>(read,*shape+0x28);
        if(!vtable || !head || !rel || !dispatch || !r) {++prop_hull_stats.bad;return nullptr;}
        if(*dispatch!=2) {++prop_hull_stats.composite;prop_hull_stats.last_vtable=*vtable-base;prop_hull_stats.last_type=(*head>>16)&0xff;return nullptr;}
        const unsigned count=*rel&0xffff,offset=*rel>>16;
        if(!count || count>256 || offset<4 || offset>0x400) {++prop_hull_stats.bad;return nullptr;}
        const auto first=rig::value<std::array<float,4>>(read,*shape+0x40+offset);if(!first) {++prop_hull_stats.bad;return nullptr;}
        std::lock_guard<std::mutex> lock(mutex);
        auto& e=cache[*shape];
        if(e.vtable==*vtable && e.head==*head && e.rel==*rel && e.radius==*r && e.first==*first) return e.hull;
        e={*vtable,*head,*rel,*r,*first,nullptr};
        std::vector<Vec3> v;v.reserve(count);
        for(unsigned i=0;i<count;++i) {const auto p=rig::value<std::array<float,4>>(read,*shape+0x40+offset+i*16);if(!p) {++prop_hull_stats.bad;return nullptr;}v.push_back({(*p)[0],(*p)[1],(*p)[2]});}
        const auto found=prop_shape_db().find(hull_key(v));
        auto hull=found!=prop_shape_db().end()?build_prop_shape(found->second,0,true):build_prop_shape(v,*r,false);if(!hull) {++prop_hull_stats.bad;return nullptr;}
        store.push_back(std::move(*hull));e.hull=&store.back();++prop_hull_stats.built;prop_hull_stats.meshes+=e.hull->mesh;
        if(!prop_hull_dump.empty()) {
            std::ofstream out(prop_hull_dump,std::ios::app);char line[160];
            std::snprintf(line,sizeof line,"{\"vtable\":\"0x%llx\",\"type\":%u,\"mesh\":%u,\"radius\":%.5f,\"vertices\":[",static_cast<unsigned long long>(*vtable-base),(*head>>16)&0xffu,e.hull->mesh?1u:0u,*r);out<<line;
            for(unsigned i=0;i<count;++i) {std::snprintf(line,sizeof line,"%s[%.5f,%.5f,%.5f]",i?",":"",v[i].x,v[i].y,v[i].z);out<<line;}
            out<<"]}\n";
        }
        return e.hull;
    } catch(...) {++prop_hull_stats.bad;return nullptr;}
}
}
