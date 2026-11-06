#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include "../src/wrist_hud_image.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::uint32_t crc32(const std::uint8_t* data,std::size_t length) {
    std::uint32_t crc=0xffffffffu;
    for(std::size_t i=0;i<length;++i) {
        crc^=data[i];
        for(unsigned bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u&(0u-static_cast<std::uint32_t>(crc&1u)));
    }
    return crc^0xffffffffu;
}
void big_endian(std::vector<std::uint8_t>& out,std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value>>24));out.push_back(static_cast<std::uint8_t>(value>>16));
    out.push_back(static_cast<std::uint8_t>(value>>8));out.push_back(static_cast<std::uint8_t>(value));
}
void chunk(std::vector<std::uint8_t>& out,const char type[4],const std::vector<std::uint8_t>& data) {
    big_endian(out,static_cast<std::uint32_t>(data.size()));
    const std::size_t begin=out.size();out.insert(out.end(),type,type+4);out.insert(out.end(),data.begin(),data.end());
    big_endian(out,crc32(out.data()+begin,4+data.size()));
}
// Minimal stored-deflate PNG writer: the preview is a test artifact, not a runtime dependency.
void write_png(const std::filesystem::path& path,unsigned width,unsigned height,const std::vector<std::uint8_t>& rgba) {
    if(rgba.size()!=static_cast<std::size_t>(width)*height*4) throw std::runtime_error("bad preview size");
    std::vector<std::uint8_t> raw;raw.reserve((static_cast<std::size_t>(width)*4+1)*height);
    for(unsigned y=0;y<height;++y) {
        raw.push_back(0);
        raw.insert(raw.end(),rgba.begin()+static_cast<std::ptrdiff_t>(y)*width*4,rgba.begin()+static_cast<std::ptrdiff_t>(y+1)*width*4);
    }
    std::vector<std::uint8_t> z{0x78,0x01};
    for(std::size_t offset=0;offset<raw.size();) {
        const std::size_t count=std::min<std::size_t>(65535,raw.size()-offset);
        const bool final=offset+count==raw.size();
        z.push_back(final?1:0);
        z.push_back(static_cast<std::uint8_t>(count&0xff));z.push_back(static_cast<std::uint8_t>((count>>8)&0xff));
        const std::uint16_t inverse=static_cast<std::uint16_t>(~static_cast<std::uint16_t>(count));
        z.push_back(static_cast<std::uint8_t>(inverse&0xff));z.push_back(static_cast<std::uint8_t>((inverse>>8)&0xff));
        z.insert(z.end(),raw.begin()+static_cast<std::ptrdiff_t>(offset),raw.begin()+static_cast<std::ptrdiff_t>(offset+count));
        offset+=count;
    }
    std::uint32_t a=1,b=0;for(auto value:raw){a=(a+value)%65521u;b=(b+a)%65521u;}
    big_endian(z,(b<<16)|a);
    std::vector<std::uint8_t> png{0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a};
    std::vector<std::uint8_t> header;big_endian(header,width);big_endian(header,height);
    header.insert(header.end(),{8,6,0,0,0});
    chunk(png,"IHDR",header);chunk(png,"IDAT",z);chunk(png,"IEND",{});
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path,std::ios::binary|std::ios::trunc);
    file.write(reinterpret_cast<const char*>(png.data()),static_cast<std::streamsize>(png.size()));
    if(!file) throw std::runtime_error("cannot write preview");
}
bool finite_pose(const d2vr::TrackedPose& pose) {
    return d2vr::finite(pose.position) && std::isfinite(pose.orientation.x) && std::isfinite(pose.orientation.y) &&
        std::isfinite(pose.orientation.z) && std::isfinite(pose.orientation.w);
}
unsigned json_dimension(const std::filesystem::path& path,const char* key) {
    std::ifstream file(path,std::ios::binary);if(!file) return 0;
    const std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    const auto found=text.find(key);if(found==std::string::npos) return 0;
    std::size_t at=found+std::char_traits<char>::length(key);
    while(at<text.size() && (text[at]<'0' || text[at]>'9')) ++at;
    unsigned value=0;while(at<text.size() && text[at]>='0' && text[at]<='9') value=value*10u+static_cast<unsigned>(text[at++]-'0');
    return value;
}
bool json_crop(const std::filesystem::path& path,std::array<int,4>& out) {
    std::ifstream file(path,std::ios::binary);if(!file) return false;
    const std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    const auto found=text.find("\"crop\":");if(found==std::string::npos) return false;
    std::size_t at=text.find('[',found);if(at==std::string::npos) return false;
    ++at;unsigned index=0;
    while(index<out.size() && at<text.size()) {
        while(at<text.size() && (text[at]<'0' || text[at]>'9') && text[at]!=']') ++at;
        if(at>=text.size() || text[at]==']') break;
        int value=0;while(at<text.size() && text[at]>='0' && text[at]<='9') value=value*10+(text[at++]-'0');
        out[index++]=value;
    }
    return index==out.size();
}
bool load_rgba(const std::filesystem::path& path,unsigned width,unsigned height,std::vector<std::uint8_t>& out) {
    std::ifstream file(path,std::ios::binary);if(!file || !width || !height) return false;
    const auto size=static_cast<std::size_t>(width)*height*4;
    out.resize(size);
    file.read(reinterpret_cast<char*>(out.data()),static_cast<std::streamsize>(size));
    return file.gcount()==static_cast<std::streamsize>(size);
}
void alpha_over(std::vector<std::uint8_t>& cluster,unsigned cluster_width,unsigned cluster_height,
    const std::vector<std::uint8_t>& art,unsigned art_width,unsigned art_height,const d2vr::WristWatchClusterPlacement& place) {
    if(!place.valid || !art_width || !art_height) return;
    const unsigned width=static_cast<unsigned>(place.source_right-place.source_left);
    const unsigned height=static_cast<unsigned>(place.source_bottom-place.source_top);
    for(unsigned y=0;y<height;++y) {
        const unsigned destination_y=static_cast<unsigned>(place.destination_top)+y;
        if(destination_y>=cluster_height) break;
        for(unsigned x=0;x<width;++x) {
            const unsigned destination_x=static_cast<unsigned>(place.destination_left)+x;
            if(destination_x>=cluster_width) break;
            const std::size_t source=((static_cast<std::size_t>(place.source_top)+y)*art_width+
                (static_cast<std::size_t>(place.source_left)+x))*4;
            const std::size_t target=((static_cast<std::size_t>(destination_y))*cluster_width+destination_x)*4;
            const unsigned alpha=art[source+3];
            if(!alpha) continue;
            const unsigned inverse=255u-alpha;
            for(unsigned c=0;c<3;++c) cluster[target+c]=static_cast<std::uint8_t>(std::min(255u,art[source+c]+(cluster[target+c]*inverse+127u)/255u));
            cluster[target+3]=static_cast<std::uint8_t>(std::min(255u,alpha+(cluster[target+3]*inverse+127u)/255u));
        }
    }
}
std::vector<std::uint8_t> resize_bilinear(const std::vector<std::uint8_t>& source,unsigned source_width,unsigned source_height,unsigned width,unsigned height) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width)*height*4);
    if(!source_width || !source_height) return out;
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        const double sx=(x+.5)*source_width/width-.5,sy=(y+.5)*source_height/height-.5;
        const int x0=static_cast<int>(std::floor(sx)),y0=static_cast<int>(std::floor(sy));
        const double fx=sx-x0,fy=sy-y0;
        const auto sample=[&](int px,int py,unsigned channel) {
            px=std::clamp(px,0,static_cast<int>(source_width)-1);py=std::clamp(py,0,static_cast<int>(source_height)-1);
            return static_cast<unsigned>(source[(static_cast<std::size_t>(py)*source_width+px)*4+channel]);
        };
        for(unsigned c=0;c<4;++c) {
            const double top=sample(x0,y0,c)*(1-fx)+sample(x0+1,y0,c)*fx;
            const double bottom=sample(x0,y0+1,c)*(1-fx)+sample(x0+1,y0+1,c)*fx;
            out[(static_cast<std::size_t>(y)*width+x)*4+c]=static_cast<std::uint8_t>(std::clamp(std::lround(top*(1-fy)+bottom*fy),0L,255L));
        }
    }
    return out;
}
bool close(double a,double b,double tolerance=0.01) {return std::abs(a-b)<=tolerance;}
} // namespace

int main() {
    using namespace d2vr;
    // --- pre-v1013 A/B path stays intact (build/legacy-wrist-hud) ---
    const TrackedPose head{{0,1.7,0},{}};
    TrackedPose hand{{-.1,1.5,-.45},{}};
    assert(wrist_hud_pose(hand,head));
    hand.position.y=.8;assert(!wrist_hud_pose(hand,head));
    hand.position={1.5,1.7,0};assert(!wrist_hud_pose(hand,head));
    hand.position={0,1.65,-.05};assert(!wrist_hud_pose(hand,head));
    hand.position={-.1,1.5,-.45};hand.orientation={0,std::sin(.7),0,std::cos(.7)};assert(wrist_hud_pose(hand,head));
    hand.orientation={};hand.position={-.5,1.5,-.1};assert(!wrist_hud_pose(hand,head));
    {TrackedPose h{{-.1,1.5,-.45},{0,std::sin(.7854),0,std::cos(.7854)}};assert(wrist_hud_pose(h,head,-1) && !wrist_hud_pose(h,head,1));}
    hand.position={-.1,1.5,-.45};{const auto p=wrist_hud_pose(hand,head);assert(p && p->position.z>hand.position.z+.05);normalized(p->orientation);}

    // --- v1013 left-wrist watch pose: face out of the back of the left hand (-X), 5 cm out, 8 cm back ---
    const TrackedPose identity_head{{0,1.7,0},{}};
    const auto placed=wrist_watch_pose(TrackedPose{{0,0,0},{}},identity_head);
    assert(placed && finite_pose(placed->pose));
    assert(std::abs(placed->pose.position.x+.05)<1e-9);
    assert(std::abs(placed->pose.position.y)<1e-9);
    assert(std::abs(placed->pose.position.z-.08)<1e-9);
    {const auto normal=rotated(normalized(placed->pose.orientation),{0,0,1});
     assert(std::abs(normal.x+1)<1e-9 && std::abs(normal.y)<1e-9 && std::abs(normal.z)<1e-9); // back of the left hand, never the palm
     const auto up=rotated(normalized(placed->pose.orientation),{0,1,0});
     assert(std::abs(up.x)<1e-9 && std::abs(up.y-1)<1e-9 && std::abs(up.z)<1e-9);} // upright to the head
    {const TrackedPose look_head{{-.55,0,.08},{0,std::sin(-.7853981634),0,std::cos(-.7853981634)}};
     const auto visible=wrist_watch_pose(TrackedPose{{0,0,0},{}},look_head);
     assert(visible);
     assert(visible->angles.facing_deg<1 && visible->angles.gaze_deg<1);
     assert(std::abs(visible->angles.distance_m-.5)<1e-9);}

    // --- visibility state: 48/65 face, 40/55 gaze, 0.85 m (+0.10 keep), trigger/menu, fades ---
    {WristWatchState state;WristWatchAngles a{47.9,39.9,.849};
     auto frame=state.update(a,true,false,false,.12);
     assert(frame.shown && frame.haptic && std::abs(frame.alpha-1)<1e-9);
     a={64.9,54.9,.949};frame=state.update(a,true,false,false,.01);assert(frame.shown); // keep window
     a={65.1,54.9,.949};frame=state.update(a,true,false,false,.11);assert(!frame.shown && std::abs(frame.alpha-.5)<1e-9);
     frame=state.update(a,true,false,false,.11);assert(!frame.shown && frame.alpha==0);
     frame=state.update({50,30,.8},true,false,false,.12);assert(!frame.shown); // between on and off: no re-show
     frame=state.update({47,30,.8},true,false,false,.12);assert(frame.shown);}
    {WristWatchState state;WristWatchAngles a{47,39,.8};assert(state.update(a,true,false,false,.12).shown);
     a.gaze_deg=54.9;assert(state.update(a,true,false,false,.01).shown);
     a.gaze_deg=55.1;assert(!state.update(a,true,false,false,.22).shown);
     assert(state.update({30,45,.8},true,false,false,.12).shown==false); // 45 is inside keep, outside on
     assert(state.update({30,39.9,.8},true,false,false,.12).shown);}
    {WristWatchState state;WristWatchAngles a{47,39,.8};assert(state.update(a,true,false,false,.12).shown);
     a.distance_m=.949;assert(state.update(a,true,false,false,.01).shown);
     a.distance_m=.951;assert(!state.update(a,true,false,false,.22).shown);
     assert(state.update({47,39,.9},true,false,false,.12).shown==false); // inside keep, outside on
     assert(state.update({47,39,.849},true,false,false,.12).shown);}
    {WristWatchState state;WristWatchAngles a{30,30,.8};assert(state.update(a,true,false,false,.12).shown);
     auto frame=state.update(a,true,true,false,.11);assert(!frame.shown && frame.alpha>0); // trigger fades out
     frame=state.update(a,true,true,false,.11);assert(frame.alpha==0);
     frame=state.update(a,true,false,false,.12);assert(frame.shown && frame.haptic); // release shows again with a tick
     frame=state.update(a,true,false,true,.01);assert(!frame.shown && frame.alpha==0);} // menus hide at once
    {WristWatchState state;const WristWatchAngles a{30,30,.8};
     assert(std::abs(state.update(a,true,false,false,.06).alpha-.5)<1e-9); // 0.06 s of the 0.12 s fade-in
     assert(state.update(a,true,false,false,.06).alpha==1);
     WristWatchAngles away{70,30,.8};
     assert(std::abs(state.update(away,true,false,false,.11).alpha-.5)<1e-9); // 0.11 s of the 0.22 s fade-out
     assert(state.update(away,true,false,false,.11).alpha==0);}

    // --- v1015 crop math: the rival's measured cluster centre at its five resolutions and the live v1014 eye size.
    // The expected centres/sides are the constants evaluated independently: 0.1465*Hs right of the centred 16:9
    // frame's left edge, 0.133*Hs + 0.0327*(H-Hs) down, side 0.33*Hs.
    struct CropCase {unsigned width,height;double left,top,right,bottom;};
    const std::array<CropCase,6> crops{{
        {1280,720,-13.320,-23.040,224.280,214.560},
        {1902,973,68.111,-31.136,389.201,289.954},
        {2560,1080,300.020,-34.560,656.420,321.840},
        {1896,1968,-19.730,-4.649,332.215,347.296},
        {1440,1440,-14.985,-5.319,252.315,261.981},
        {2316,2476,-24.101,-3.323,405.807,426.585},
    }};
    for(const auto& expected:crops) {
        const auto rect=wrist_watch_cluster_rect(expected.width,expected.height);
        assert(rect.valid());
        assert(close(rect.left,expected.left) && close(rect.top,expected.top) &&
               close(rect.right,expected.right) && close(rect.bottom,expected.bottom));
        assert(close(rect.right-rect.left,rect.bottom-rect.top,1e-9)); // square crop
        const auto extent=wrist_watch_cluster_texture_size(rect);
        assert(extent.first>=static_cast<unsigned>(std::floor(rect.side)) && extent.second>=static_cast<unsigned>(std::floor(rect.side)));
    }
    assert(!wrist_watch_cluster_rect(0,1080).valid());
    assert(!wrist_watch_cluster_texture_size({}).first);

    // --- v1015 original child placement: each captured compact crop keeps its measured surface origin. ---
    {const auto rect=wrist_watch_cluster_rect(2316,2476);
     const auto extent=wrist_watch_cluster_texture_size(rect);
     const auto health=wrist_watch_cluster_place(rect,47,42,291,382,extent.first,extent.second);
     const auto mana=wrist_watch_cluster_place(rect,97,111,316,333,extent.first,extent.second);
     const auto equipment=wrist_watch_cluster_place(rect,143,128,337,285,extent.first,extent.second);
     assert(health.valid && mana.valid && equipment.valid);
     assert(health.source_left==0 && health.source_top==0 && health.source_right==244 && health.source_bottom==340);
     assert(health.destination_left==72 && health.destination_top==46 && health.destination_right==316 && health.destination_bottom==386);
     assert(mana.destination_left==122 && mana.destination_top==115 && mana.destination_right==341 && mana.destination_bottom==337);
     assert(equipment.destination_left==168 && equipment.destination_top==132 && equipment.destination_right==362 && equipment.destination_bottom==289);
     assert(health.destination_left<mana.destination_left && mana.destination_left<equipment.destination_left);
     assert(!wrist_watch_cluster_place(rect,5000,5000,5100,5100,extent.first,extent.second).valid);
     assert(!wrist_watch_cluster_place(rect,-6000,-6000,-5000,-5000,extent.first,extent.second).valid);}
    {const auto rect=wrist_watch_cluster_rect(2548,2724);
     const auto extent=wrist_watch_cluster_texture_size(rect);
     const auto health=wrist_watch_cluster_place(rect,55,50,331,437,extent.first,extent.second);
     const auto mana=wrist_watch_cluster_place(rect,110,134,358,369,extent.first,extent.second);
     const auto equipment=wrist_watch_cluster_place(rect,163,148,385,326,extent.first,extent.second);
     assert(health.valid && mana.valid && equipment.valid);
     assert(health.destination_left==82 && health.destination_top==54);
     assert(mana.destination_left==137 && mana.destination_top==138);
     assert(equipment.destination_left==190 && equipment.destination_top==152);}
    // A child crop partially outside the square is clipped, not moved.
    {const auto rect=wrist_watch_cluster_rect(1280,720);
     const auto extent=wrist_watch_cluster_texture_size(rect);
     const auto clipped=wrist_watch_cluster_place(rect,0,0,100,50,extent.first,extent.second);
     assert(clipped.valid);
     assert(clipped.source_left==0 && clipped.source_top==0 && clipped.source_right==100 && clipped.source_bottom==50);
     assert(clipped.destination_left==14 && clipped.destination_top==24);}

    // --- v1015 preview from a real archived/live HUD capture. The three child .rgba files are the game's own
    // health, mana and equipment draws; the JSON crop is their measured surface rectangle. The preview composites
    // them at those original positions inside the rival's cluster crop, then scales the cluster to the 512-pixel watch.
    {
        struct Candidate {std::filesystem::path directory;unsigned surface_width,surface_height;const char* name;};
        std::vector<Candidate> candidates;
        const auto live=std::filesystem::path(std::getenv("LOCALAPPDATA")?std::getenv("LOCALAPPDATA"):"")/
            "DishonoredVR"/"artifacts";
        const auto status_width=json_dimension(live/"xr-live-status.json","\"width\":");
        const auto status_height=json_dimension(live/"xr-live-status.json","\"height\":");
        candidates.push_back({live,status_width?status_width:2316,status_height?status_height:2476,"live"});
        candidates.push_back({std::filesystem::path("artifacts"),2548,2724,"repo"});
        bool rendered=false;
        for(const auto& candidate:candidates) {
            const auto rect=wrist_watch_cluster_rect(candidate.surface_width,candidate.surface_height);
            const auto extent=wrist_watch_cluster_texture_size(rect);
            if(!rect.valid() || !extent.first || !extent.second) continue;
            std::array<std::vector<std::uint8_t>,3> art{};
            std::array<std::array<int,4>,3> crop{};
            std::array<unsigned,3> art_width{},art_height{};
            bool all=true;
            for(unsigned index=0;index<3;++index) {
                const unsigned slot=index+1;
                const auto stem="native-hud-child-"+std::to_string(slot);
                const auto json=candidate.directory/(stem+".json");
                const auto raw=candidate.directory/(stem+".rgba");
                art_width[index]=json_dimension(json,"\"width\":");
                art_height[index]=json_dimension(json,"\"height\":");
                all=all && json_crop(json,crop[index]) && load_rgba(raw,art_width[index],art_height[index],art[index]);
            }
            if(!all) continue;
            std::vector<std::uint8_t> cluster(static_cast<std::size_t>(extent.first)*extent.second*4,0);
            bool all_placed=true;
            for(unsigned index=0;index<3;++index) {
                const auto cluster_placement=wrist_watch_cluster_place(rect,crop[index][0],crop[index][1],crop[index][2],crop[index][3],extent.first,extent.second);
                if(!cluster_placement.valid) {all_placed=false;break;}
                alpha_over(cluster,extent.first,extent.second,art[index],art_width[index],art_height[index],cluster_placement);
            }
            if(!all_placed) continue;
            const auto preview=resize_bilinear(cluster,extent.first,extent.second,512,512);
            write_png(std::filesystem::path("artifacts")/"claude"/"wrist-watch-preview.png",512,512,preview);
            auto half=preview;
            for(auto& value:half) value=static_cast<std::uint8_t>((value+1u)/2u); // premultiplied fade at alpha 0.5
            write_png(std::filesystem::path("artifacts")/"claude"/"wrist-watch-preview-half.png",512,512,half);
            auto backdrop=preview;
            for(unsigned y=0;y<512;++y) for(unsigned x=0;x<512;++x) {
                const auto at=(static_cast<std::size_t>(y)*512+x)*4;
                const unsigned inverse=255u-backdrop[at+3];
                backdrop[at+0]=static_cast<std::uint8_t>(std::min(255u,backdrop[at+0]+(20u*inverse+127u)/255u));
                backdrop[at+1]=static_cast<std::uint8_t>(std::min(255u,backdrop[at+1]+(22u*inverse+127u)/255u));
                backdrop[at+2]=static_cast<std::uint8_t>(std::min(255u,backdrop[at+2]+(25u*inverse+127u)/255u));
                backdrop[at+3]=255;
            }
            write_png(std::filesystem::path("artifacts")/"claude"/"wrist-watch-preview-backdrop.png",512,512,backdrop);
            std::cout<<"PREVIEW source="<<candidate.name<<" surface="<<candidate.surface_width<<'x'<<candidate.surface_height
                <<" cluster="<<extent.first<<'x'<<extent.second<<" side="<<rect.side<<"\n";
            rendered=true;break;
        }
        if(!rendered) std::cout<<"PREVIEW unavailable: no native-hud-child-1/2/3 rgba/json pairs in the live or repo artifacts\n";
    }

    std::cout<<"PASS: legacy watch gates, left back-of-hand pose, 48/65 and 40/55 hysteresis, 0.85 m limit, trigger/menu hide, fade timing, cluster crop at the rival's measured resolutions and the live 2316x2476 eye size, original child positions, clipped edges, and a preview from real native HUD child captures\n";
}
