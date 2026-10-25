#include "../src/bink_video.hpp"
#include <cstdio>
static int failures=0;
static void expect(bool ok,const char* what) {if(!ok) {std::printf("FAIL %s\n",what);++failures;}}
int main() {
    using d2vr::BinkTracker;
    BinkTracker t;
    expect(!t.playing(0),"nothing open");
    t.open(0x1000,100);
    expect(t.playing(100) && t.playing(600) && !t.playing(601),"open counts for 500 ms");
    t.frame(0x1000,700);expect(t.playing(1200) && !t.playing(1201),"a decoded frame keeps it playing");
    t.frame(0x2000,900);expect(!t.playing(1300),"a frame of an unknown handle is ignored");
    t.close(0x1000);expect(!t.playing(800) && t.open_count()==0,"close ends it at once");
    t.open(0,10);expect(t.open_count()==0,"null handle (failed open) is ignored");
    BinkTracker m;m.open(1,0);m.open(2,5);m.close(1);expect(m.playing(10) && m.open_count()==1,"two videos: closing one keeps the other");
    BinkTracker leak;for(std::uintptr_t h=1;h<=20;++h) leak.open(h*16,h);expect(leak.open_count()==8 && leak.playing(20),"leaked handles are recycled");
    expect(!t.playing(5) || true,"time going backwards does not crash");
    std::printf(failures?"bink_video_check FAILED\n":"bink_video_check ok\n");
    return failures?1:0;
}
