#include "../src/full_hand_query_transaction.hpp"
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line=%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
using namespace d2vr;
using Transaction=FullHandQueryTransaction;
using State=Transaction::State;
using Palette=Transaction::Palette;
constexpr double radius=.02;
const std::array<float,15> identity{0,0,0,1,0,0,0,1,0,0,0,1,1,1,1};
bool close_vec(Vec3 a,Vec3 b,double tolerance=1e-6){return length(a-b)<tolerance;}
NativeWorldQuerySource source(){NativeWorldQuerySource s{};s.base=0x140000000;s.game=0x2000000;s.player=0x2100000;s.camera=0x2200000;s.camera_controller=0x2300000;s.physics=0x2400000;s.delegate=0x2500000;s.controller=0x2600000;s.world=0x2700000;s.pool=0x2800000;s.body=s.pool+2*0xb0;s.handle=0x12000002;s.spawn=6;s.body_count=64;s.broadphase=1;s.body_flags=3;s.camera_identity={7,10};s.physics_identity={9,10};s.component_list_data=0x2900000;s.component_list_count=s.component_list_capacity=4;CHECK(s.valid());return s;}
WorldTrackedPose grip(){return {{0,0,0},{{{1,0,0},{0,1,0},{0,0,1}}}};}
void position(rig::Transform& t,Vec3 p){t.f[12]=static_cast<float>(p.x);t.f[13]=static_cast<float>(p.y);t.f[14]=static_cast<float>(p.z);}
Palette palette(){Palette p{};for(unsigned i=0;i<p.size();++i){p[i].f={1,0,0,0,0,1,0,0,0,0,1,0,.04f,.04f,static_cast<float>(i)*.001f,0};}return p;}
// Deliberately synthetic adapter: first24 joint translations encode the24 spheres.
// This tests transaction geometry identity, not native skeleton fitting or ownership.
std::optional<HandProxies> geometry_of(const Palette& p,const std::array<float,15>& frame=identity){const rig::ModelFrame model(frame);HandProxies h{};for(unsigned i=0;i<h.size();++i){if(!rig::valid(p[i]))return {};h[i]={model.world_point(rig::position(p[i])),radius*model.scale[0]};}return h;}
WorldTraceQueryResult empty(){WorldTraceQueryResult q{};q.complete=true;q.initial.emplace();q.regular.emplace();return q;}
void wall(WorldTraceQueryResult& q,Vec3 from,Vec3 n,double offset,double r,std::uint32_t body=0x12000003){const auto separation=dot(from,n)-offset-r;if(separation>0)return;auto& initial=q.initial->hits[q.initial->count++].contact;initial.point=from-n*(dot(from,n)-offset);initial.normal=n;initial.normal_valid=true;initial.signed_separation=static_cast<float>(separation);initial.source_body=source().handle;initial.body=body;initial.shape_key=7;auto& regular=q.regular->hits[q.regular->count++];regular.point=initial.point;regular.normal=n;regular.normal_valid=true;regular.fraction=0;regular.initial_overlap=true;regular.body_handle=body;regular.body=body&0xffffffu;regular.shape_key=7;}
struct Fixture {
 Palette raw=palette();WorldTrackedPose raw_grip=grip();NativeWorldQuerySource src=source();
 std::unique_ptr<Transaction::Working> work=std::make_unique<Transaction::Working>();
 unsigned casts{},solves{},geometries{},checks{},fail_check{},incomplete_at=999,cancel_cast=999;
 int selected=-1;bool cancelled{},raw_wall{},corner{},opposed{},new_final_target{},opposed_final{},solve_fail{},nonfinite{},radius_change{},geometry_fail_raw{},geometry_fail_solved{},bad_reached{};
 Vec3 articulation{};unsigned articulated=7;
 std::array<Vec3,48> observed_to{};
 auto run(){
  const auto raw_geometry=geometry_of(raw);CHECK(raw_geometry);
  auto current=[&](){++checks;return !cancelled && (!fail_check || checks!=fail_check);};
  auto cast=[&](Vec3 from,Vec3 to,double r){const unsigned call=casts++;CHECK(call<48);observed_to[call]=to;if(call==cancel_cast)cancelled=true;if(call==incomplete_at)return WorldTraceQueryResult{};auto q=empty();const bool final=call>=24;const unsigned proxy=call%24;if(selected>=0 && proxy!=static_cast<unsigned>(selected))return q;if(raw_wall)wall(q,from,{1,0,0},0,r);if(corner)wall(q,from,{0,1,0},0,r,0x12000004);if(opposed)wall(q,from,{-1,0,0},-.02,r,0x12000005);if(final && new_final_target)wall(q,from,{1,0,0},.006,r,0x12000006);if(final && opposed_final){wall(q,from,{1,0,0},to.x-r+.001,r,0x12000007);wall(q,from,{-1,0,0},-to.x-r+.001,r,0x12000008);}return q;};
  auto solve=[&](const WorldTrackedPose& target)->std::optional<Palette>{++solves;if(solve_fail || bad_reached)return {};auto out=raw;for(auto& joint:out)position(joint,rig::position(joint)+target.position-raw_grip.position);position(out[articulated],rig::position(out[articulated])+articulation);if(nonfinite)out[68].f[0]=std::numeric_limits<float>::quiet_NaN();return out;};
  auto geometry=[&](const Palette& p)->std::optional<HandProxies>{++geometries;if((geometries==1 && geometry_fail_raw)||(geometries==2 && geometry_fail_solved))return {};auto result=geometry_of(p);if(result && geometries==2 && radius_change)(*result)[7].radius+=.001;return result;};
  Transaction::run(raw_grip,raw,*raw_geometry,src,cast,solve,geometry,current,*work);CHECK(work->result.casts==casts);return work->result;
 }
 void active(unsigned proxy,Vec3 p={.01,.04,0}){position(raw[proxy],p);selected=static_cast<int>(proxy);raw_wall=true;}
};
void complete_and_digits(){Fixture clear;const auto unblocked=clear.run();CHECK(unblocked.ready() && unblocked.casts==48 && !unblocked.contact && clear.solves==1 && clear.geometries==2);CHECK(Transaction::same_geometry(unblocked.proxies,*geometry_of(unblocked.palette)));
 for(unsigned proxy:{0u,3u,7u,11u,15u,19u,23u}){Fixture f;f.active(proxy);const auto r=f.run();CHECK(r.ready() && r.contact && r.casts==48 && close_vec(r.shift,{.01,0,0}));CHECK(r.proxies[proxy].centre.x>radius-1e-6);for(unsigned i=0;i<24;++i)CHECK(close_vec(f.observed_to[24+i],r.proxies[i].centre));for(unsigned j=0;j<69;++j)CHECK(r.palette[j].f==f.work->solved.value()[j].f);}
 std::cout<<"PASS48 casts and each selected palm/digit index; exact returned palette/proxies queried\n";
}
void final_articulation(){Fixture f;f.active(7);f.articulation={.012,.004,0};const auto r=f.run();CHECK(r.ready() && close_vec(r.shift,{.01,0,0}));CHECK(close_vec(r.proxies[7].centre,{.032,.044,0}));CHECK(close_vec(f.observed_to[31],r.proxies[7].centre));CHECK(!close_vec(f.observed_to[31],rig::position(f.raw[7])+r.shift));
 Fixture inward;inward.active(7);inward.articulation={-.006,0,0};const auto rejected=inward.run();CHECK(!rejected.ready() && rejected.state==State::penetrating && rejected.casts==48);
 Fixture late;late.active(7);late.new_final_target=true;const auto new_target=late.run();CHECK(new_target.state==State::penetrating && !new_target.ready() && new_target.casts==48);
 Fixture mismatch;auto supplied=*geometry_of(mismatch.raw);supplied[7].centre.x+=.001;auto cast=[](Vec3,Vec3,double){CHECK(false);return empty();};auto solve=[](const WorldTrackedPose&)->std::optional<Palette>{CHECK(false);return {};};auto geometry=[](const Palette& p){return geometry_of(p);};auto current=[](){return true;};Transaction::run(mismatch.raw_grip,mismatch.raw,supplied,mismatch.src,cast,solve,geometry,current,*mismatch.work);CHECK(mismatch.work->result.state==State::geometry && mismatch.work->result.casts==0);
 std::cout<<"PASS final articulation geometry and late target/penetration rejection\n";
}
void corners_and_limits(){Fixture c;c.raw_wall=true;c.corner=true;for(auto& t:c.raw)position(t,{.01,.01,.03});auto r=c.run();CHECK(r.ready() && close_vec(r.shift,{.01,.01,0}));
 Fixture impossible;impossible.active(7);impossible.opposed=true;r=impossible.run();CHECK(r.state==State::correction_limit && r.casts==24 && impossible.solves==0);
 Fixture final_opposed;final_opposed.active(7);final_opposed.opposed_final=true;r=final_opposed.run();CHECK(r.state==State::final_query && r.casts==48 && !r.ready());
 Fixture deep;deep.active(7,{-.07,.04,0});r=deep.run();CHECK(r.state==State::correction_limit && r.casts==24 && deep.solves==0);
 Fixture accepted;accepted.active(7);accepted.articulation={-.00049,0,0};CHECK(accepted.run().ready());Fixture too_deep;too_deep.active(7);too_deep.articulation={-.00051,0,0};CHECK(too_deep.run().state==State::penetrating);
 std::cout<<"PASS corner/opposed constraints,8cm bound and0.5mm final slop boundary\n";
}
void failures_and_cancellation(){
 Fixture raw;raw.incomplete_at=6;auto r=raw.run();CHECK(r.state==State::raw_query && r.casts==7 && raw.solves==0);
 Fixture final;final.incomplete_at=43;r=final.run();CHECK(r.state==State::final_query && r.casts==44 && final.solves==1);
 for(unsigned stop:{1u,2u,9u,26u,27u,39u,51u}){Fixture f;f.fail_check=stop;r=f.run();CHECK(r.state==State::interrupted && !r.ready() && !r.contact);const unsigned expected=stop<=25?(stop<=2?0:stop-2):(stop==26||stop==27?24:stop==51?48:stop-3);CHECK(r.casts==expected);CHECK(length(r.shift)==0);for(const auto& p:r.proxies)CHECK(p.radius==0);}
 for(unsigned cast:{0u,23u,24u,47u}){Fixture f;f.cancel_cast=cast;r=f.run();CHECK(r.state==State::interrupted && !r.ready() && r.casts==cast+1);}
 Fixture solve;solve.solve_fail=true;CHECK(solve.run().state==State::arm_solve);Fixture reached;reached.bad_reached=true;CHECK(reached.run().state==State::arm_solve);
 Fixture gr;gr.geometry_fail_raw=true;r=gr.run();CHECK(r.state==State::geometry && r.casts==0);Fixture gs;gs.geometry_fail_solved=true;r=gs.run();CHECK(r.state==State::geometry && r.casts==24);Fixture radii;radii.radius_change=true;r=radii.run();CHECK(r.state==State::geometry && r.casts==24);Fixture nan;nan.nonfinite=true;r=nan.run();CHECK(r.state==State::geometry && r.casts==24);
 Fixture excessive;excessive.articulation={.086,0,0};r=excessive.run();CHECK(r.state==State::geometry && r.casts==24);
 Fixture reuse;CHECK(reuse.run().ready());reuse.casts=reuse.solves=reuse.geometries=reuse.checks=0;reuse.fail_check=1;r=reuse.run();CHECK(r.state==State::interrupted && !r.ready() && !r.contact && r.casts==0);
 std::cout<<"PASS incomplete queries, cancellation at each phase, reached/geometry/radius/nonfinite failures and Working reuse\n";
}
void world_rebase(){Fixture f;f.active(7);auto r=f.run();CHECK(r.ready());const std::array<float,15> from{4,-2,6,0,1,0,-1,0,0,0,0,1,1,1,1},to{-3,2,1,0,0,-1,0,1,0,1,0,0,1,1,1};const rig::ModelFrame old(from),next(to);for(auto& joint:r.palette){for(unsigned axis=0;axis<3;++axis){const auto v=old.model_direction(rig::column(joint,axis));joint.f[axis*4]=static_cast<float>(v.x);joint.f[axis*4+1]=static_cast<float>(v.y);joint.f[axis*4+2]=static_cast<float>(v.z);}position(joint,old.model_point(rig::position(joint)));}CHECK(Transaction::same_geometry(*geometry_of(r.palette,from),r.proxies));
 auto geometry=[](const Palette& p,const std::array<float,15>& frame){return geometry_of(p,frame);};const auto rebased=Transaction::rebase(r,from,to,geometry);CHECK(rebased && Transaction::same_geometry(*geometry_of(*rebased,to),r.proxies,.0001));for(unsigned i=0;i<69;++i){CHECK(close_vec(old.world_point(rig::position(r.palette[i])),next.world_point(rig::position((*rebased)[i]))));for(unsigned a=0;a<3;++a)CHECK(close_vec(old.world_direction(rig::column(r.palette[i],a)),next.world_direction(rig::column((*rebased)[i],a))));}
 auto scale=to;scale[12]=1.1f;CHECK(!Transaction::rebase(r,from,scale,geometry));auto invalid=to;invalid[3]=0;invalid[4]=0;invalid[5]=0;CHECK(!Transaction::rebase(r,from,invalid,geometry));auto changed=[](const Palette& p,const std::array<float,15>& frame){auto h=geometry_of(p,frame);if(h)(*h)[23].centre.x+=.001;return h;};CHECK(!Transaction::rebase(r,from,to,changed));auto absent=[](const Palette&,const std::array<float,15>&)->std::optional<HandProxies>{return {};};CHECK(!Transaction::rebase(r,from,to,absent));auto throws=[](const Palette&,const std::array<float,15>&)->std::optional<HandProxies>{throw std::runtime_error("synthetic failed geometry adapter");};CHECK(!Transaction::rebase(r,from,to,throws));r.state=State::interrupted;CHECK(!Transaction::rebase(r,from,to,geometry));
 std::cout<<"PASS world rebase preserves tested24 centres/radii and all69 joint world positions/directions; drift/scale/frame failures rejected\n";
}
}
int main(){complete_and_digits();final_articulation();corners_and_limits();failures_and_cancellation();world_rebase();std::cout<<"full_hand_query_transaction_check passed (synthetic geometry/world; no native scheduling, lifetime, clearance or headset proof)\n";}
