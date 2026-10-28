#include "../src/note_frame_history.hpp"
#include "../src/world_ui_completion.hpp"
#include <cassert>
#include <iostream>

int main() {
    d2vr::NoteFrameHistory<unsigned> frames;
    d2vr::WorldUiCompletion executed;
    auto context=reinterpret_cast<void*>(0x1000),list=reinterpret_cast<void*>(0x2000);
    frames.publish(3923,std::make_shared<unsigned>(1));
    executed.recorded(context,1,0,1);executed.recorded(context,2,0,1);
    assert(!executed.ready(1,0,1));
    executed.finished(context,list);assert(!executed.ready(1,0,1));
    executed.executed(list);
    frames.publish(3924,std::make_shared<unsigned>(2)); // newer preparation before presentation
    assert(!frames.completed(3924,false));
    assert(*frames.completed(3924,true)==1 && executed.ready(1,0,1) && executed.ready(2,0,1));
    assert(*frames.completed(3925,true)==2 && !executed.ready(1,0,2));
    executed.immediate(1,0,2);assert(!executed.ready(2,0,2)); // mixed serials
    executed.immediate(2,0,2);assert(executed.ready(1,0,2) && executed.ready(2,0,2));
    executed.immediate(1,0,1);assert(!executed.ready(1,0,2)); // late older execution revokes the newer pair
    assert(!frames.completed(0,true) && !frames.completed(1,true) && !frames.completed(3923,true));
    frames.publish(3926,std::make_shared<unsigned>(4));assert(!frames.completed(3924,true)); // bounded overwrite
    assert(*frames.completed(3925,true)==2);
    frames.clear();assert(!frames.completed(3925,true));
    std::cout<<"PASS: exact native recording/display mapping, inflight retention and executed same-serial hands\n";
}
