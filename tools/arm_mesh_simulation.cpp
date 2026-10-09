// Offline replay on extracted arm geometry. Uses the same pose_arm as the mod.
#include "../src/arm_palette.hpp"
#include "../src/hand_bind.hpp"
#include "../src/finger_pose.hpp"
#include "../src/native_hand_reference.hpp"
#include <fstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <numeric>

namespace r=d2vr::rig;
using d2vr::Vec3;
using Inverse=std::array<float,12>;
struct Vertex {std::array<float,3> position;std::array<unsigned char,8> weights,joints;};
static_assert(sizeof(Vertex)==28);
struct Surface {std::vector<Vertex> vertices;std::vector<std::uint16_t> indices;};
template<class T> T take(std::istream& stream) {
    T value{};if(!stream.read(reinterpret_cast<char*>(&value),sizeof(value))) throw std::runtime_error("Truncated mesh fixture");return value;
}
Vec3 skin(const Vertex& vertex,const std::array<r::Transform,69>& joints,const std::array<Inverse,69>& inverse) {
    Vec3 result{};const auto& p=vertex.position;
    for(unsigned i=0;i<8;++i) if(vertex.weights[i]) {
        const auto joint=vertex.joints[i];if(joint>=69) throw std::runtime_error("Mesh joint exceeds the native palette");
        const auto& bind=inverse[joint];const auto& t=joints[joint];
        const Vec3 local{bind[0]*p[0]+bind[1]*p[1]+bind[2]*p[2]+bind[3],
            bind[4]*p[0]+bind[5]*p[1]+bind[6]*p[2]+bind[7],bind[8]*p[0]+bind[9]*p[1]+bind[10]*p[2]+bind[11]};
        result=result+(r::position(t)+r::column(t,0)*local.x+r::column(t,1)*local.y+r::column(t,2)*local.z)*(vertex.weights[i]/255.);
    }
    if(!d2vr::finite(result)) throw std::runtime_error("Non-finite skinned vertex");return result;
}
int main(int argc,char** argv) {
    try {
        if(argc!=2 && argc!=3) throw std::runtime_error("Usage: arm_mesh_simulation.exe <private mesh-fixture.bin> [private native-hand-poses.bin]");
        std::optional<std::array<r::NativeHandReference,2>> hand_references;
        if(argc==3) {std::ifstream hand_file(argv[2],std::ios::binary);hand_references=r::read_native_hand_references(hand_file);}
        std::ifstream stream(argv[1],std::ios::binary|std::ios::ate);
        const auto size=stream.tellg();if(!stream || size<1024 || size>32*1024*1024) throw std::runtime_error("Invalid bounded mesh fixture");
        stream.seekg(0);const auto magic=take<std::array<char,8>>(stream);
        if(std::memcmp(magic.data(),"D2VRMS01",8) || take<std::uint32_t>(stream)!=2) throw std::runtime_error("Unknown mesh fixture");
        std::cout<<std::setprecision(9)<<"{\"offline_only\":true,\"game_memory_writes\":false,\"model_metric_scale_verified\":false,\"headset_deformation_verified\":false,\"arms\":[";
        for(unsigned side=0;side<2;++side) {
            const auto parents=take<r::ArmHierarchy>(stream);r::Upload native{};
            native.joints=take<std::array<r::Transform,69>>(stream);
            const auto inverse=take<std::array<Inverse,69>>(stream);
            if(!r::valid_hierarchy(parents) || parents[3]!=2 || parents[4]!=3) throw std::runtime_error("Native arm hierarchy differs");
            for(const auto& t:native.joints) if(!r::valid(t)) throw std::runtime_error("Invalid source joint");
            for(const auto& bind:inverse) for(auto v:bind) if(!std::isfinite(v) || std::abs(v)>1e7f) throw std::runtime_error("Invalid inverse bind");
            const auto surface_count=take<std::uint32_t>(stream);if(!surface_count || surface_count>64) throw std::runtime_error("Invalid mesh surface count");
            std::vector<Surface> surfaces(surface_count);
            for(auto& surface:surfaces) {
                const auto n=take<std::uint32_t>(stream);if(!n || n>500000) throw std::runtime_error("Excessive vertices");surface.vertices.resize(n);
                for(auto& vertex:surface.vertices) {
                    vertex=take<Vertex>(stream);
                    if(std::accumulate(vertex.weights.begin(),vertex.weights.end(),0)!=255) throw std::runtime_error("Non-normalised skin weights");
                    for(auto p:vertex.position) if(!std::isfinite(p) || std::abs(p)>1e6f) throw std::runtime_error("Invalid mesh vertex");
                }
                const auto m=take<std::uint32_t>(stream);if(!m || m>3000000 || m%3) throw std::runtime_error("Invalid triangles");surface.indices.resize(m);
                for(auto& index:surface.indices) {index=take<std::uint16_t>(stream);if(index>=n) throw std::runtime_error("Triangle exceeds its vertex stream");}
            }
            // Names/indices are checked by analyse_arm_mesh.py when exporting
            // this private fixture; these are MCPs, not metacarpal roots.
            const auto bind_wrist=r::bind_transform(inverse[4]);
            const std::array<unsigned,4> mcp{16,27,38,49};std::array<r::Transform,4> knuckles{};
            for(unsigned i=0;i<4;++i) {
                if(parents[mcp[i]]!=static_cast<int>(mcp[i]-1) || parents[mcp[i]-1]!=4)
                    throw std::runtime_error("Offline named knuckle hierarchy differs");
                knuckles[i]=r::bind_transform(inverse[mcp[i]]);
            }
            const auto bind_grip=r::hand_bind_grip(bind_wrist,knuckles);
            const auto animated_grip=r::rigid_retarget(bind_grip,bind_wrist,native.joints[4]);
            std::array<r::Transform,69> bind{};
            for(unsigned joint=0;joint<69;++joint) bind[joint]=r::bind_transform(inverse[joint]);
            const r::FingerChains finger_chains{{{5,6,7},{16,17,18},{27,28,29},{38,39,40},{49,50,51}}};
            auto finger_calibration=r::finger_binding(bind,parents,finger_chains);
            if(hand_references) {
                const auto& reference=(*hand_references)[side];
                if(reference.inverse!=inverse || reference.fingers.parents!=parents || reference.chains!=finger_chains)
                    throw std::runtime_error("Captured hand references do not match this private mesh rig");
                finger_calibration=reference.fingers;
            }
            const auto neutral_wrist=r::rigid_retarget(bind_wrist,bind_grip,animated_grip);
            if(d2vr::length(r::position(neutral_wrist)-r::position(native.joints[4]))>1e-5)
                throw std::runtime_error("Bind palm offset does not recover the native wrist");
            if(side) std::cout<<',';
            std::cout<<"{\"side\":\""<<(side?"right":"left")<<"\",\"bind_wrist_to_palm\":"
                <<d2vr::length(r::position(bind_grip)-r::position(bind_wrist))
                <<",\"physical_grip_alignment_verified\":false,\"native_finger_references_loaded\":"<<(hand_references?"true":"false")<<",\"cases\":[";
            const std::array<Vec3,7> offsets{{{0,0,0},{.15,0,0},{-.15,0,0},{0,.15,0},{0,-.15,0},{0,0,.15},{0,0,-.15}}};
            const r::ForearmHelpers helpers{64,65,66,67};
            for(unsigned scenario=0;scenario<31;++scenario) {
                const auto variation=scenario>=27?0:scenario%9;const bool grip_retarget=scenario>=9 && scenario<27;
                const float curl=scenario>=27?static_cast<float>(scenario-27)/3.f:0;
                auto target=grip_retarget?animated_grip:native.joints[4];
                if(variation<offsets.size()) {
                    target.f[12]+=static_cast<float>(offsets[variation].x);target.f[13]+=static_cast<float>(offsets[variation].y);target.f[14]+=static_cast<float>(offsets[variation].z);
                } else {
                    const double angle=variation==7?.785398163:-.785398163;
                    target=r::turn_root(target,{0,0,std::sin(angle/2),std::cos(angle/2)},r::position(target));
                }
                if(grip_retarget) target=r::rigid_retarget(bind_wrist,bind_grip,target);
                const bool helper_roll=scenario>=18 && scenario<27;
                auto posed=r::pose_arm(native,parents,{2,3,4},target,r::position(native.joints[3])-r::position(native.joints[2]),helper_roll?&helpers:nullptr);
                if(scenario>=27) posed.joints=r::curl_fingers(posed.joints,parents,finger_calibration,curl);
                double max_movement=0,max_stretch=0;std::uint64_t vertices=0,edges=0,stretched=0;
                std::vector<double> ratios;unsigned worst_surface{},worst_a{},worst_b{};double worst_length{};
                std::vector<double> surface_movements(surfaces.size());
                for(unsigned surface_index=0;surface_index<surfaces.size();++surface_index) {
                    const auto& surface=surfaces[surface_index];
                    std::vector<Vec3> before,after;before.reserve(surface.vertices.size());after.reserve(surface.vertices.size());
                    for(const auto& vertex:surface.vertices) {
                        before.push_back(skin(vertex,native.joints,inverse));after.push_back(skin(vertex,posed.joints,inverse));
                        max_movement=std::max(max_movement,d2vr::length(after.back()-before.back()));++vertices;
                        surface_movements[surface_index]=std::max(surface_movements[surface_index],d2vr::length(after.back()-before.back()));
                    }
                    for(std::size_t triangle=0;triangle<surface.indices.size();triangle+=3) for(unsigned edge=0;edge<3;++edge) {
                        const auto a=surface.indices[triangle+edge],b=surface.indices[triangle+(edge+1)%3];
                        const auto length=d2vr::length(before[a]-before[b]);if(length<1e-4) continue;
                        const auto ratio=d2vr::length(after[a]-after[b])/length;ratios.push_back(ratio);
                        if(ratio>max_stretch) {max_stretch=ratio;worst_surface=surface_index;worst_a=a;worst_b=b;worst_length=length;}
                        ++edges;if(ratio>8) ++stretched;
                    }
                }
                if(!variation && !curl && !(scenario>=27 && hand_references) && max_movement>1e-4)
                    throw std::runtime_error("Neutral wrist/grip target changes the native skinned mesh");
                if(scenario) std::cout<<',';
                std::sort(ratios.begin(),ratios.end());
                if(ratios.empty()) throw std::runtime_error("Mesh fixture contains no bounded edges");
                std::cout<<"{\"case\":"<<scenario<<",\"bind_grip_retarget\":"<<(grip_retarget?"true":"false")
                    <<",\"forearm_helper_roll\":"<<(helper_roll?"true":"false")<<",\"finger_curl\":"<<curl
                    <<",\"finger_pose_applied\":"<<(scenario>=27 && hand_references?"true":"false")<<",\"vertices\":"<<vertices<<",\"max_vertex_movement\":"<<max_movement
                    <<",\"max_triangle_edge_ratio\":"<<max_stretch<<",\"edges_checked\":"<<edges<<",\"edges_above_8x\":"<<stretched
                    <<",\"reach_clamped\":"<<(posed.reach_clamped?"true":"false")
                    <<",\"edge_ratio_p95\":"<<ratios[static_cast<std::size_t>((ratios.size()-1)*.95)]
                    <<",\"edge_ratio_p99\":"<<ratios[static_cast<std::size_t>((ratios.size()-1)*.99)]
                    <<",\"surface_max_vertex_movement\":[";
                for(unsigned surface=0;surface<surface_movements.size();++surface) {if(surface) std::cout<<',';std::cout<<surface_movements[surface];}
                std::cout<<"],\"worst_edge\":{\"surface\":"<<worst_surface<<",\"length_metres\":"<<worst_length<<",\"vertices\":[";
                for(unsigned endpoint=0;endpoint<2;++endpoint) {
                    if(endpoint) std::cout<<',';
                    const auto index=endpoint?worst_b:worst_a;const auto& vertex=surfaces[worst_surface].vertices[index];
                    std::cout<<"{\"index\":"<<index<<",\"weights\":[";bool first=true;
                    for(unsigned weight=0;weight<8;++weight) if(vertex.weights[weight]) {
                        if(!first) std::cout<<',';first=false;
                        std::cout<<"["<<static_cast<unsigned>(vertex.joints[weight])<<','<<static_cast<unsigned>(vertex.weights[weight])<<']';
                    }
                    std::cout<<"]}";
                }
                std::cout<<"]}}";
            }
            std::cout<<"]}";
        }
        if(stream.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Unexpected trailing fixture data");
        std::cout<<"]}\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
