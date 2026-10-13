#include "../src/game_art.hpp"
#include <cstdio>

static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}

int main() {
    namespace fs=std::filesystem;
    const auto dir=fs::current_path()/"build"/"tmpcheck"/"game_art_fixture";fs::create_directories(dir); // the system temp folder may be blocked
    {   // 2x1 art: opaque red | half-transparent blue
        std::ofstream out(dir/"test.rgba",std::ios::binary);const std::uint32_t wh[2]{2,1};out.write(reinterpret_cast<const char*>(wh),8);
        const unsigned char px[8]{255,0,0,255, 0,0,255,128};out.write(reinterpret_cast<const char*>(px),8);
    }
    {std::ofstream bad(dir/"bad.rgba",std::ios::binary);const std::uint32_t wh[2]{4,4};bad.write(reinterpret_cast<const char*>(wh),8);} // truncated
    d2vr::GameArtCache cache(dir);
    const auto art=cache.get("test");
    expect(art && art->width==2 && art->height==1,"loads the raw art");
    expect(!cache.get("bad") && !cache.get("missing"),"truncated or missing art is a miss");
    expect(cache.get("test")==art,"cached");
    std::vector<std::uint32_t> surface(20*10,0x000000);
    d2vr::blit_art(surface.data(),20,10,*art,0,0,20,10); // fit: 20x10 rectangle, 2:1 art fills it
    const auto at=[&](int x,int y) {return surface[static_cast<std::size_t>(y)*20+x];};
    expect(at(1,5)==0xff0000,"opaque texel replaces the surface");
    const auto b=at(18,5);
    expect((b>>16&255)==0 && (b&255)>=120 && (b&255)<=136,"half alpha blends over black");
    std::vector<std::uint32_t> tall(10*20,0x101010);
    d2vr::blit_art(tall.data(),10,20,*art,0,0,10,20); // fit keeps 2:1 centred: rows 7..12 only
    expect(tall[0]==0x101010 && tall[static_cast<std::size_t>(10)*10+1]!=0x101010,"aspect kept and centred");
    std::error_code ec;fs::remove_all(dir,ec); // an antivirus scan may still hold the file
    if(!failures) std::printf("PASS: game art cache loads raw RGBA once, rejects truncated/missing, blends with alpha and keeps aspect\n");
    return failures?1:0;
}
