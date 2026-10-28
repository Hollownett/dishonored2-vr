#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(disable:4706) // Existing GameXr diagnostic frame gates assign in conditions.
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "../src/game_xr.hpp"
#include "../src/simulated_xr.hpp"
#include "../src/full_body_driver.hpp"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
using Microsoft::WRL::ComPtr;
using namespace d2vr;
namespace sim=d2vr::simxr;
// Offline fixture captures only: these are simulated OpenXR eyes with native
// model assets and a striped test source, never game/headset visual evidence.
void save_eye(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source,const std::filesystem::path& path) {
    assert(source);D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);
    const auto format=desc.Format;
    assert(format==DXGI_FORMAT_R8G8B8A8_UNORM || format==DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
        format==DXGI_FORMAT_B8G8R8A8_UNORM || format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB);
    desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
    ComPtr<ID3D11Texture2D> staging;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));
    context->CopyResource(staging.Get(),source);D3D11_MAPPED_SUBRESOURCE map{};
    assert(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
    std::ofstream out(path,std::ios::binary);assert(out);
    out<<"P6\n# Offline native-model fixture; striped test UI; no headset/game evidence\n"<<desc.Width<<' '<<desc.Height<<"\n255\n";
    const bool bgra=format==DXGI_FORMAT_B8G8R8A8_UNORM || format==DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    for(unsigned y=0;y<desc.Height;++y)for(unsigned x=0;x<desc.Width;++x) {
        const auto* p=static_cast<const unsigned char*>(map.pData)+y*map.RowPitch+x*4;
        const char rgb[]{static_cast<char>(p[bgra?2:0]),static_cast<char>(p[1]),static_cast<char>(p[bgra?0:2])};out.write(rgb,3);
    }
    context->Unmap(staging.Get(),0);assert(out.good());
}
std::vector<unsigned char> eye_pixels(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source) {
    D3D11_TEXTURE2D_DESC desc{};source->GetDesc(&desc);desc.Usage=D3D11_USAGE_STAGING;
    desc.BindFlags=desc.MiscFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));
    context->CopyResource(staging.Get(),source);D3D11_MAPPED_SUBRESOURCE map{};
    assert(SUCCEEDED(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&map)));
    std::vector<unsigned char> result(static_cast<std::size_t>(desc.Width)*desc.Height*4);
    for(unsigned y=0;y<desc.Height;++y)std::memcpy(result.data()+static_cast<std::size_t>(y)*desc.Width*4,
        static_cast<const unsigned char*>(map.pData)+y*map.RowPitch,desc.Width*4);
    context->Unmap(staging.Get(),0);return result;
}
rig::ArmBinding binding_for(unsigned side) {
    const auto skeleton=fullbody::skeleton(side?fullbody::data::kRightArm:fullbody::data::kLeftArm);
    const std::string prefix=side?"right":"left";
    rig::ArmBinding binding{};
    binding.chain={static_cast<unsigned>(skeleton.find(prefix+"_arm_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_elbow_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_wrist_jnt"))};
    binding.helpers={static_cast<unsigned>(skeleton.find(prefix+"_lower_armhelper1_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_lower_armhelper2_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_lower_armhelper3_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_wrist_rotation_jnt"))};
    std::array<rig::Transform,69> palette{};
    for(unsigned joint=0;joint<69;++joint) {binding.parents[joint]=static_cast<std::int16_t>(skeleton.parent[joint]);palette[joint]=fullbody::to_transform(skeleton.bind[joint]);}
    const std::array<std::string,5> digits{"thumb","index","medium","ring","pinky"};
    for(unsigned finger=0;finger<5;++finger)for(unsigned segment=0;segment<3;++segment)
        binding.finger_chains[finger][segment]=static_cast<unsigned>(skeleton.find(prefix+"_"+digits[finger]+"_0"+std::to_string(segment+(finger?2:1))+"_jnt"));
    binding.palm={static_cast<unsigned>(skeleton.find(prefix+"_helper_underwrist_jnt")),static_cast<unsigned>(skeleton.find(prefix+"_helper_upperwrist_jnt"))};
    binding.bind_wrist=palette[binding.chain.wrist];std::array<rig::Transform,4> knuckles{};
    for(unsigned finger=1;finger<5;++finger)knuckles[finger-1]=palette[binding.finger_chains[finger][0]];
    binding.bind_grip=rig::hand_bind_grip(binding.bind_wrist,knuckles);
    binding.fingers=rig::finger_binding(palette,binding.parents,binding.finger_chains);
    return binding;
}
int main(int argc,char** argv) {
    const auto capture_folder=argc>2?std::filesystem::path(argv[2]):std::filesystem::path{};
    if(!capture_folder.empty())std::filesystem::create_directories(capture_folder);
    const UINT pixels=capture_folder.empty()?64u:640u;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    sim::configure(device.Get());
    // Native bind geometry puts the wrist along +grip Y and fingers along -Y.
    // XR identity therefore describes fingers pointing down. For a physical
    // forward swipe, rotate right grip +90 deg about XR X: wrist toward the
    // wearer (+Z), index toward the page (-Z), matching gameplay's grip mapping.
    sim::control.grips[1].orientation={static_cast<float>(std::sqrt(.5)),0,0,static_cast<float>(std::sqrt(.5))};
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=pixels;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> left,right,native_ui;
    assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&left)) && SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&right)));
    assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&native_ui)));
    const std::array<unsigned char,4> background{20,25,32,255};
    std::vector<unsigned char> source_pixels(static_cast<std::size_t>(pixels)*pixels*4);
    for(std::size_t i=0;i<source_pixels.size();++i)source_pixels[i]=background[i%4];
    context->UpdateSubresource(left.Get(),0,nullptr,source_pixels.data(),pixels*4,0);
    context->UpdateSubresource(right.Get(),0,nullptr,source_pixels.data(),pixels*4,0);
    for(unsigned y=0;y<pixels;++y)for(unsigned x=0;x<pixels;++x) {
        const bool line=y%32<5 && x>pixels/10 && x<pixels*9/10;
        const std::array<unsigned char,4> color=line?std::array<unsigned char,4>{90,76,58,255}:std::array<unsigned char,4>{224,211,181,255};
        for(unsigned c=0;c<4;++c)source_pixels[(static_cast<std::size_t>(y)*pixels+x)*4+c]=color[c];
    }
    context->UpdateSubresource(native_ui.Get(),0,nullptr,source_pixels.data(),pixels*4,0);
    GameXr xr(device.Get(),pixels,pixels,DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,nullptr,true,true,true,true,1,{}, {},true,&sim::dispatch);
    bool allow_native_receipt=false;
    bool synthetic_depth_witness=false;
    std::array<rig::FingerIkState,2> fixture_fingers{};
    UINT64 previous_frame_tick=GetTickCount64(),minimum_frame_advance=~UINT64{0};
    const auto frame=[&] {
        // GetTickCount64 can repeat after Sleep(10). Physical swipe velocity
        // intentionally waits for a positive clock delta before consuming it.
        UINT64 tick{};do {Sleep(1);tick=GetTickCount64();}while(tick<=previous_frame_tick);
        minimum_frame_advance=std::min(minimum_frame_advance,tick-previous_frame_tick);
        ++xr.alternate_frame;
        // The fixture supplies an explicit native-draw witness. It does not
        // exercise the production original-model solver or graphics substitution.
        for(unsigned side=0;side<2;++side)xr.grip_locations[side].pose=sim::control.grips[side];
        if(allow_native_receipt)if(const auto target=xr.note_target_panel(xr.alternate_frame)) {
            std::array<note_hand::Geometry,2> geometry;Vec3 tip{};
            for(unsigned side=0;side<2;++side) {
                const auto* poser=xr.note_poser(side);const auto* mesh=xr.note_mesh(side);assert(poser && mesh);
                const auto& p=sim::control.grips[side];const Quaternion q{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w};
                WorldTrackedPose grip{{p.position.x,p.position.y,p.position.z},
                    {rotated(q,{0,0,-1}),rotated(q,{-1,0,0}),rotated(q,{0,1,0})}};
                const auto palm=apply_palm_offset(grip,xr.palm_offset(side),true);
                const auto posed=poser->pose(side,palm,*target,fixture_fingers[side],.01,
                    side?sim::control.input.right_grip:sim::control.input.left_grip,TrackedPose{{0,1.65,0},{}});
                geometry[side]=note_hand::skin_xr(*mesh,mesh->inverse,posed.joints,poser->model_scale());
                if(side)tip=posed.index_tip;
            }
            if(synthetic_depth_witness)for(auto& hand:geometry) {
                hand.vertices={{-1,1,-.8},{1,1,-.8},{0,2.3,-.8}};hand.indices={0,1,2};
            }
            xr.accept_native_note(*target,std::move(geometry),tip);
        }
        if(xr.tracking_views()) {const auto& views=*xr.tracking_views();xr.record_rendered_eye(0,views[0].fov);xr.record_rendered_eye(1,views[1].fov);}
        xr.present(left.Get(),right.Get(),1.6f,1.6f,false,false,false); // paused native UI throughout
        previous_frame_tick=GetTickCount64();
    };
    const auto capture=[&](const char* name) {
        if(capture_folder.empty())return;
        for(unsigned eye=0;eye<2;++eye)save_eye(device.Get(),context.Get(),sim::summary.images[eye].Get(),
            capture_folder/(std::string("offline-native-fixture-")+name+(eye?"-right.ppm":"-left.ppm")));
    };
    frame();frame();xr.world_ui(native_ui.Get(),2);frame();
    assert(xr.last_world_ui_layer && xr.world_ui_in_hand()); // native controller attachment without hand assets
    const auto before=sim::summary.overlays.back().pose.position;
    sim::control.grips[0].position.x+=.13f;frame();
    assert(std::abs(sim::summary.overlays.back().pose.position.x-before.x-.13f)<1e-5);
    sim::control.grips[0].position.x-=.13f;frame(); // captures below use the simulator's original controller pose
    xr.enable_note_tablet(true);sim::control.input.turn_y=.7f;frame();
    assert(xr.note_scroll_axis()==.7f && !xr.native_menu_input.active); // existing native analog scroll
    sim::control.input.turn_y=0;
    const auto folder=argc>1?std::filesystem::path(argv[1]):std::filesystem::temp_directory_path()/"d2vr-native-note-hand-export"/"output";
    std::array<std::shared_ptr<const note_hand::Mesh>,2> models;
    for(unsigned side=0;side<2;++side) {
        const auto path=folder/(side?"note_hand_corvo_right.bin":"note_hand_corvo_left.bin");
        std::ifstream in(path,std::ios::binary);
        if(!in) {std::cerr<<"Missing actual textured native hand fixture: "<<path<<"\n";return 2;}
        models[side]=std::make_shared<const note_hand::Mesh>(note_hand::read(in));
        bool textured_hand=false;
        for(const auto& surface:models[side]->surfaces)if(surface.hand) {
            assert(surface.uv.size()==surface.vertices.size() && !surface.diffuse.rgba.empty());textured_hand=true;
        }
        assert(textured_hand);
    }
    std::array<rig::ArmBinding,2> bindings{binding_for(0),binding_for(1)};
    for(unsigned side=0;side<2;++side)bindings[side].inverse=models[side]->inverse;
    const auto snapshot=std::make_shared<const rig::NativeArmBindingSnapshot>(rig::NativeArmBindingSnapshot{{},bindings,{}});
    xr.note_models(snapshot,models,{1.,1.});xr.note_generation(7);frame();frame();
    assert(xr.note_model_owner()==snapshot && !xr.note_composition_ready());
    assert(!xr.note_drawn_hand(0) && !xr.note_drawn_hand(1) && !xr.note_index_tip());
    assert(xr.last_world_ui_layer); // loading assets alone cannot invent a native draw receipt
    std::vector<unsigned char> transparent_ui(source_pixels.size(),0);
    context->UpdateSubresource(native_ui.Get(),0,nullptr,transparent_ui.data(),pixels*4,0);frame();
    const auto without_witness=eye_pixels(device.Get(),context.Get(),sim::summary.images[0].Get());
    allow_native_receipt=true;synthetic_depth_witness=true;frame();
    assert(xr.note_composition_ready());
    assert(eye_pixels(device.Get(),context.Get(),sim::summary.images[0].Get())==without_witness);
    // A large witness triangle may occlude page pixels, but contributes no hand color.
    synthetic_depth_witness=false;
    context->UpdateSubresource(native_ui.Get(),0,nullptr,source_pixels.data(),pixels*4,0);
    allow_native_receipt=true;frame();frame();
    assert(xr.note_model_owner()==snapshot && xr.note_composition_ready());
    assert(xr.note_drawn_hand(0) && xr.note_drawn_hand(1) && xr.note_index_tip());
    assert(!xr.note_drawn_hand(0)->vertices.empty() && !xr.note_drawn_hand(1)->vertices.empty());
    assert(!xr.last_world_ui_layer && !xr.native_menu_input.active); // original native page composed into stereo eyes
    const auto draws=xr.note_eye_draws;frame();assert(xr.note_eye_draws==draws+2);
    capture("default-anchor");
    auto panel=xr.note_displayed_panel();assert(panel);
    const auto held_page=[&] { // the page rides the tracked left palm, rotation included
        const auto& p=sim::control.grips[0];const Quaternion q{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w};
        const WorldTrackedPose grip{{p.position.x,p.position.y,p.position.z},{rotated(q,{0,0,-1}),rotated(q,{-1,0,0}),rotated(q,{0,1,0})}};
        const auto size=xr.note_dimensions();return xr.note_poser(0)->page(apply_palm_offset(grip,xr.palm_offset(0),true),size[0],size[1]);
    };
    const auto same_pose=[](const TrackedPose& a,const TrackedPose& b) {
        if(length(a.position-b.position)>1e-5)return false;
        for(const Vec3 axis:{Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1}})if(length(rotated(a.orientation,axis)-rotated(b.orientation,axis))>1e-5)return false;
        return true;
    };
    assert(same_pose(panel->pose,held_page()));
    { // a wrist roll turns the held page with it
        const auto resting=sim::control.grips[0];const float h=static_cast<float>(std::sin(.35)),c=static_cast<float>(std::cos(.35));
        const auto& o=resting.orientation;sim::control.grips[0].orientation={c*o.x+h*o.w,c*o.y+h*o.z,c*o.z-h*o.y,c*o.w-h*o.x}; // q*(x-axis turn)
        frame();const auto rolled=xr.note_displayed_panel();assert(rolled && same_pose(rolled->pose,held_page()));
        assert(length(rotated(rolled->pose.orientation,{0,1,0})-rotated(panel->pose.orientation,{0,1,0}))>.3);
        sim::control.grips[0]=resting;frame();
    }
    std::size_t hand_vertex{},vertex_offset{};bool found_hand_vertex=false;
    for(const auto& surface:models[1]->surfaces) {
        for(std::size_t i=0;i<surface.vertices.size() && !found_hand_vertex;++i) {
            const auto& v=surface.vertices[i];bool on_hand=true;
            for(unsigned w=0;w<8;++w)if(v.weights[w] && !rig::in_subtree(bindings[1].parents,v.joints[w],bindings[1].chain.wrist))on_hand=false;
            if(on_hand) {hand_vertex=vertex_offset+i;found_hand_vertex=true;}
        }
        vertex_offset+=surface.vertices.size();
    }
    assert(found_hand_vertex);const auto old_vertex=xr.note_drawn_hand(1)->vertices[hand_vertex];
    sim::control.grips[1].position.x+=.04f;frame();
    assert(length(xr.note_drawn_hand(1)->vertices[hand_vertex]-old_vertex-Vec3{.04,0,0})<1e-5);
    assert(length(xr.note_displayed_panel()->pose.position-panel->pose.position)<1e-7);
    const auto move_tip=[&](Vec3 target) {
        const auto tip=xr.note_index_tip();assert(tip);
        const auto delta=target-*tip;auto& p=sim::control.grips[1].position;
        p.x+=static_cast<float>(delta.x);p.y+=static_cast<float>(delta.y);p.z+=static_cast<float>(delta.z);frame();
    };
    const auto q=panel->pose.orientation;const auto normal=rotated(q,{0,0,1}),up=rotated(q,{0,1,0});
    move_tip(panel->pose.position+normal*.1);assert(xr.note_scroll_axis()==0); // physical lift arms contact
    move_tip(panel->pose.position);assert(xr.note_scroll_axis()==0); // first contact does not jump
    assert(std::abs(dot(*xr.note_index_tip()-panel->pose.position,normal))<1e-5);
    assert(sim::control.input.right_trigger==0);
    capture("physical-index-contact");
    move_tip(panel->pose.position+up*.0035);assert(xr.note_scroll_axis()<0); // zero-trigger physical swipe
    const auto before_missing_receipt=xr.note_eye_draws;
    allow_native_receipt=false;frame();
    assert(!xr.note_composition_ready() && !xr.note_drawn_hand(0) && !xr.note_drawn_hand(1) && !xr.note_index_tip());
    assert(xr.note_scroll_axis()==0 && xr.note_eye_draws==before_missing_receipt && xr.last_world_ui_layer);
    allow_native_receipt=true;frame();frame();
    assert(xr.note_composition_ready() && xr.note_scroll_axis()==0);
    move_tip(panel->pose.position+normal*.1);move_tip(panel->pose.position);
    move_tip(panel->pose.position+up*.0035);assert(xr.note_scroll_axis()<0);
    const auto before_focus_draws=xr.note_eye_draws;
    sim::push_state(XR_SESSION_STATE_VISIBLE);frame();
    assert(!xr.note_drawn_hand(0) && !xr.note_drawn_hand(1) && !xr.note_index_tip());
    assert(!xr.note_composition_ready() && xr.note_scroll_axis()==0 && xr.note_eye_draws==before_focus_draws);
    assert(xr.note_model_owner()==snapshot); // focus loss must retain the owned native models/page identity
    auto& unfocused_grip=sim::control.grips[1].position;
    unfocused_grip.x+=static_cast<float>(up.x*.004);unfocused_grip.y+=static_cast<float>(up.y*.004);unfocused_grip.z+=static_cast<float>(up.z*.004);
    sim::push_state(XR_SESSION_STATE_FOCUSED);frame();frame();
    assert(xr.note_model_owner()==snapshot && xr.note_composition_ready());
    assert(xr.note_drawn_hand(0) && xr.note_drawn_hand(1) && xr.note_displayed_panel()->generation==panel->generation);
    assert(xr.note_eye_draws>=before_focus_draws+2 && xr.note_scroll_axis()==0); // no ShowNote/model refresh or opening jump
    move_tip(panel->pose.position+up*.011);assert(xr.note_scroll_axis()==0); // hand remaining on page cannot resume held touch
    move_tip(panel->pose.position+normal*.1);assert(xr.note_scroll_axis()==0);
    move_tip(panel->pose.position);assert(xr.note_scroll_axis()==0);
    move_tip(panel->pose.position+up*.0035);assert(xr.note_scroll_axis()<0); // lift and fresh contact restore physical swipe
    move_tip(panel->pose.position+normal*.08);assert(xr.note_scroll_axis()==0);
    if(!capture_folder.empty()) {
        // A separate physical reading pose for inspection: the same original
        // controller anchor, raised into view, with actual right index contact.
        const auto resting_left=sim::control.grips[0];const auto resting_right=sim::control.grips[1];
        sim::control.grips[0].position={-.12f,1.45f,-.5f};frame();
        const auto raised=xr.note_displayed_panel();assert(raised);
        assert(same_pose(raised->pose,held_page()));
        const auto raised_right=rotated(raised->pose.orientation,{1,0,0}),raised_up=rotated(raised->pose.orientation,{0,1,0});
        move_tip(raised->pose.position+raised_right*.16+raised_up*.04);capture("raised-reading-contact");
        sim::control.grips[0]=resting_left;sim::control.grips[1]=resting_right;frame();
    }
    sim::control.input.b=true;sim::control.input.left_grip=.8f;sim::control.input.right_grip=.7f;frame();
    assert(xr.controller_input.b && xr.controller_input.left_grip==.8f && xr.controller_input.right_grip==.7f); // native close/navigation controls survive
    sim::control.input.b=false;sim::control.input.left_grip=sim::control.input.right_grip=0;
    xr.note_models({}, {}, {1.,1.});
    assert(!xr.note_model_owner() && !xr.note_displayed_panel() && !xr.note_index_tip());
    assert(!xr.note_drawn_hand(0) && !xr.note_drawn_hand(1) && xr.note_scroll_axis()==0);
    const auto revoked_draws=xr.note_eye_draws;frame();
    assert(xr.last_world_ui_layer && xr.world_ui_in_hand() && xr.note_eye_draws==revoked_draws); // native controller page survives asset revocation
    xr.note_models(snapshot,models,{1.,1.});frame();frame();assert(xr.note_drawn_hand(0) && xr.note_drawn_hand(1));
    xr.world_ui(nullptr,0);const auto closed_draws=xr.note_eye_draws;frame();
    assert(!xr.note_drawn_hand(0) && !xr.note_drawn_hand(1) && !xr.note_index_tip());
    assert(!xr.note_composition_ready() && xr.note_scroll_axis()==0 && xr.note_eye_draws==closed_draws);
    assert(!sim::summary.protocol_errors);
    std::cout<<"PASS: explicit fixture native-draw witnesses gate paused note composition/touch; original controller anchor, moving contact geometry, physical zero-trigger swipe, native controls and close cleanup; minimum frame clock advance "<<minimum_frame_advance<<" ms; no production solver or headset proof\n";
}
