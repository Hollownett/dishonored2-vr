#define NOMINMAX
#include "../src/grab_beam.hpp"
#include <cassert>
#include <iostream>
#include <limits>

using namespace d2vr;
static void close(Vec3 a,Vec3 b,double tolerance=1e-6) {assert(length(a-b)<tolerance);}
// to_game_pose consumes a GameCameraPose; the beam API carries the same heading
// frame as a WorldTrackedPose, so convert one to the other for the round trip.
// Anchor positions stay exactly float-representable so scale cannot amplify an
// unrelated rounding error past the 1e-6 tolerance.
static GameCameraPose native(const WorldTrackedPose& frame) {
    GameCameraPose pose;
    pose.position={static_cast<float>(frame.position.x),static_cast<float>(frame.position.y),
                   static_cast<float>(frame.position.z)};
    for(std::size_t i=0;i<3;++i) {
        pose.basis[i*3]=static_cast<float>(frame.basis[i].x);
        pose.basis[i*3+1]=static_cast<float>(frame.basis[i].y);
        pose.basis[i*3+2]=static_cast<float>(frame.basis[i].z);
    }
    return pose;
}
static WorldTrackedPose heading(double yaw) {
    return {{10,20,30},{Vec3{std::cos(yaw),std::sin(yaw),0},Vec3{-std::sin(yaw),std::cos(yaw),0},Vec3{0,0,1}}};
}
int main() {
    // 1. xr -> world -> xr round trips over random yaw frames, scales and points.
    unsigned seed=12345;
    auto next=[&] {seed=seed*1664525u+1013904223u;return (seed>>8)/static_cast<double>(1<<24);};
    for(int i=0;i<200;++i) {
        const double yaw=next()*6.283185307179586,scale=.1+next()*99;
        const TrackedPose origin{{next(),1.7-next()*.4,next()},{0,std::sin(yaw*.5),0,std::cos(yaw*.5)}};
        const auto frame=heading(yaw*.7);
        const TrackedPose xr{{next()*2-1,next()*2,next()*2-1},{}};
        const auto world=to_game_pose(xr,origin,native(frame),scale).position;
        const auto back=world_to_xr(world,origin,frame,scale);
        assert(back);
        close(*back,xr.position);
    }
    // The controller path additionally undoes camera_relative_controller.
    const auto frame=heading(.4);
    const TrackedPose origin{{.5,1.6,.25},{0,std::sin(.3),0,std::cos(.3)}};
    const Vec3 current_head{.5,1.75,.25},calibrated_head{.45,1.6,.2};
    const TrackedPose hand{{.3,1.2,-.4},{}};
    const auto grabbed=to_game_pose(camera_relative_controller(hand,current_head,calibrated_head),origin,native(frame),2).position;
    const auto recovered=world_to_xr_controller(grabbed,origin,frame,2,current_head,calibrated_head);
    assert(recovered);
    close(*recovered,hand.position);
    const auto plain=world_to_xr(grabbed,origin,frame,2);
    assert(plain && length(*plain-*recovered)>1e-3); // the two paths must differ
    assert(!world_to_xr(grabbed,origin,frame,0) && !world_to_xr(grabbed,origin,frame,1001));
    assert(!world_to_xr({std::numeric_limits<double>::quiet_NaN(),0,0},origin,frame,1));
    auto tilted=frame;tilted.basis[0]={std::sin(.4),std::cos(.4),0};
    assert(!world_to_xr(grabbed,origin,tilted,1));
    tilted=frame;tilted.basis[2]={0,.2,1};
    assert(!world_to_xr(grabbed,origin,tilted,1));
    const TrackedPose rolled{{origin.position},{std::sqrt(.5),0,0,std::sqrt(.5)}};
    assert(!world_to_xr(grabbed,rolled,frame,1));
    assert(!world_to_xr(grabbed,{origin.position,{0,0,0,0}},frame,1));
    auto headless=origin;headless.position={std::numeric_limits<double>::infinity(),0,0};
    assert(!world_to_xr(grabbed,headless,frame,1));

    // 2. Quad orientation: +Y along the beam, +X across it, +Z towards the eye.
    const Vec3 from{-.2,1.5,-.5},to{.3,1.1,-.9},eye{0,1.6,0};
    const auto quad=beam_quad(from,to,eye);
    assert(quad);
    close(quad->centre,(from+to)*.5);
    const Vec3 along=to-from,span_y=unit(along);
    assert(std::abs(quad->length-length(along))<1e-6);
    assert(quad->width==.012f);
    const auto x=rotated(quad->orientation,{1,0,0}),y=rotated(quad->orientation,{0,1,0}),z=rotated(quad->orientation,{0,0,1});
    close(y,span_y);
    assert(std::abs(length(x)-1)<1e-9 && std::abs(dot(x,y))<1e-9);
    assert(std::abs(dot(x,unit(eye-quad->centre)))<1e-9);
    close(z,cross(x,y));
    assert(dot(cross(x,y),z)>.999); // right handed local frame
    assert(dot(z,eye-quad->centre)>0 && dot(z,eye-quad->centre)>1e-3); // faces the viewer
    assert(std::abs(dot(z,span_y))<1e-9);
    const auto unit_q=normalized(quad->orientation);
    assert(std::abs(unit_q.x*unit_q.x+unit_q.y*unit_q.y+unit_q.z*unit_q.z+unit_q.w*unit_q.w-1)<1e-9);
    // Different eyes change only where the ribbon faces, never its length.
    for(const auto& other:{Vec3{1,1.4,-2},Vec3{-2,1.9,.5},Vec3{0,3,-.7}}) {
        const auto view=beam_quad(from,to,other);
        assert(view && dot(rotated(view->orientation,{0,0,1}),other-view->centre)>0);
        assert(std::abs(view->length-quad->length)<1e-6);
        assert(std::abs(dot(rotated(view->orientation,{1,0,0}),unit(other-view->centre)))<1e-9);
    }
    assert(!beam_quad(from,to,from) && !beam_quad(from,to,quad->centre)); // eye on the beam axis
    assert(!beam_quad(from,from+span_y*.019,eye));
    assert(beam_quad(from,from+span_y*.021,eye));
    assert(!beam_quad(from,to,eye,0) && !beam_quad(from,to,eye,1));
    assert(!beam_quad(from,to,{std::numeric_limits<double>::infinity(),0,0}));
    assert(!beam_quad({0,0,0},{0,0,0},{1,0,0}));

    // 3. Beam image: transparent outer columns, opaque core, faded ends, stable.
    const auto beam=beam_image();
    assert(beam.size()==16*128*4);
    assert(beam==beam_image());
    for(int row=0;row<128;++row) {
        assert(beam[(row*16)*4+3]==0 && beam[(row*16+15)*4+3]==0);
        assert(beam[(row*16)*4]==0 && beam[(row*16)*4+2]==0);
    }
    const auto centre_alpha=beam[(63*16+7)*4+3],edge_alpha=beam[(63*16+1)*4+3];
    assert(centre_alpha>=200 && beam[(63*16+8)*4+3]>=200);
    assert(edge_alpha>0 && edge_alpha<centre_alpha/4);
    assert(beam[(63*16+7)*4]>=230 && beam[(63*16+7)*4+1]>=235 && beam[(63*16+7)*4+2]==255);
    assert(beam[(63*16+1)*4]<beam[(63*16+7)*4]); // edges read bluer than the core
    assert(beam[(0*16+7)*4+3]<centre_alpha/8 && beam[(127*16+7)*4+3]<centre_alpha/8);
    assert(beam[(31*16+7)*4+3]!=beam[(35*16+7)*4+3]); // bead pattern varies along the length
    assert(beam[0]==0 && beam.back()==0); // the last texel is an outer column

    // 4. Target glow: soft ring over a dim halo, empty border, stable.
    const auto glow=target_glow_image();
    assert(glow.size()==32*32*4);
    assert(glow==target_glow_image());
    assert(glow[(0*32)*4+3]==0 && glow[(31*32+31)*4+3]==0 && glow[(0*32)*4+2]==0);
    const auto ring=glow[(15*32+25)*4+3],halo=glow[(15*32+15)*4+3];
    assert(ring==255 && halo>0 && halo<ring && ring>glow[(15*32+1)*4+3]);
    assert(glow[(15*32+25)*4]>=240 && glow[(15*32+25)*4+2]==255 && glow[(15*32+15)*4+2]==255);
    std::cout<<"PASS: world_to_xr inverts to_game_pose and camera-relative controllers, beam quads face the eye, "
               "and the beam/target images have transparent edges over opaque cores\n";
}
