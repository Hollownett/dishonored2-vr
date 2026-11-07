// v1080 real prop shapes (src/prop_hull.hpp): hull planes and signed distance, the own frame, the slab pieces and their sections, the database key
// against the launcher's (launcher/PropShapes.cs, the book's real Havok hull), and the pinch seat on a thin handle (finger_sphere.hpp natural_seat).
#include "../src/finger_sphere.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
using d2vr::Vec3;
namespace r=d2vr::rig;
static bool near(double a,double b,double t=1e-6) {return std::abs(a-b)<=t;}
int main() {
    // a 20 x 10 x 4 cm box, turned and moved in its body frame: 6 planes, exact distances, its own frame back
    const Vec3 c{.1,-.2,.05};const double s=std::sqrt(.5);const Vec3 ex{s,s,0},ey{-s,s,0},ez{0,0,1};
    std::vector<Vec3> box;for(int i=-1;i<=1;i+=2) for(int j=-1;j<=1;j+=2) for(int k=-1;k<=1;k+=2) box.push_back(c+ex*(.1*i)+ey*(.05*j)+ez*(.02*k));
    const auto h=d2vr::build_hull(box,.01);assert(h && h->solid && h->planes.size()==6);
    assert(near(h->half.x,.11) && near(h->half.y,.06) && near(h->half.z,.03));       // radius included
    assert(near(h->distance(h->to_own(c)),-.03));                                    // the centre: the thinnest half deep
    assert(near(h->distance(h->to_own(c+ez*.05)),.02));                             // 3 cm off the face (2 cm out of the radius)
    assert(near(std::abs(d2vr::dot(h->axes[0],ex)),1,1e-9));
    assert(near(h->support({0,0,1}),.03));
    const auto sec=h->section(0);assert(sec && near(sec->hy,.06) && near(sec->hz,.03));
    // a sphere (one vertex) and a capsule (two): exact
    const auto ball=d2vr::build_hull({{.1,.2,.3}},.05);assert(ball && !ball->solid && near(ball->distance(ball->to_own({.1,.2,.4})),.05));
    const auto cap=d2vr::build_hull({{0,0,0},{.2,0,0}},.03);assert(cap && !cap->solid && near(cap->half.x,.13) && near(cap->distance(cap->to_own({.1,.05,0})),.02));
    // the key the launcher wrote for book_01's Havok hull (artifacts/prop-meshes/keys.txt: 0x07e0c36ef067ea03)
    const std::vector<Vec3> book{{0.14094177,0.11311249,0.045188155},{0.14094177,-0.11311249,0.045188155},{0.14094177,0.11311249,0.010000002},
        {-0.14094183,0.11311249,0.045188155},{0.14094177,-0.11311249,0.010000002},{-0.14094183,0.11311249,0.010000002},
        {-0.14094183,-0.11311249,0.045188155},{-0.14094183,-0.11311249,0.010000002}};
    assert(d2vr::hull_key(book)==0x07e0c36ef067ea03ull);
    // a spoon-like cloud: an 11 cm handle 1.2 x 0.4 cm, a 6 x 4 x 1.5 cm bowl; pieces along it, the handle's section thin
    std::vector<Vec3> spoon;
    for(double x=-.1;x<=.01;x+=.005) for(int j=-1;j<=1;j+=2) for(int k=-1;k<=1;k+=2) spoon.push_back({x,.006*j,.002*k});
    for(double a=0;a<6.283;a+=.3) for(int k=-1;k<=1;k+=2) spoon.push_back({.04+.03*std::cos(a),.02*std::sin(a),.0075*k});
    const auto ps=d2vr::build_prop_shape(spoon,0,true);assert(ps && ps->pieces.size()>=3 && ps->pieces.size()<=d2vr::prop_max_pieces);
    const double handle_x=ps->whole.to_own({-.06,0,0}).x;const auto hs=ps->section(handle_x);
    assert(hs && std::min(hs->hy,hs->hz)<=r::seat_pinch_thin && std::max(hs->hy,hs->hz)<=r::seat_pinch_wide);
    const auto bs=ps->section(ps->whole.to_own({.04,0,0}).x);assert(bs && std::max(bs->hy,bs->hz)>r::seat_pinch_wide);
    // the natural seat pinches it by the handle (the centre line at the pinch point), and lays it in the palm by the bowl
    d2vr::WorldTrackedPose body{};body.basis={Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}};
    const Vec3 n{0,0,1},fw{0,1,0},skin{-.06,0,-.004};const d2vr::NativeGrabBounds b{{-1,-1,-1},{1,1,1}};
    bool pinch=false;const auto seat=r::natural_seat(body,b,skin,fw,n,&*ps,&pinch);assert(seat && pinch);
    const Vec3 grip=skin+fw*r::seat_pinch_reach+n*r::seat_pinch_lift,on=seat->position+seat->basis[0]*-.06; // the handle point that was taken
    assert(d2vr::length(on-grip)<.01);
    pinch=true;const auto bowl=r::natural_seat(body,b,{.04,0,-.0075},fw,n,&*ps,&pinch);assert(bowl && !pinch);
    // the pieces as contact shapes: valid, and the fingers meet the handle's surface (2 mm thick half) above its centre line
    std::array<r::HeldShape,d2vr::prop_max_pieces> got{};const unsigned count=r::prop_grip_shapes(body,*ps,got.data(),static_cast<unsigned>(got.size()));
    assert(count==ps->pieces.size());double d=1e9;for(unsigned i=0;i<count;++i) {r::require_shape(got[i]);d=std::min(d,r::shape_distance(got[i],{-.06,0,.012}));}
    assert(near(d,.01,1e-3));
    std::puts("prop_hull_check: ok");
    return 0;
}
