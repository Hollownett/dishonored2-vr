#include "../src/frame_timing_queue.hpp"
#include <cassert>
#include <condition_variable>
#include <iostream>
#include <sstream>
#include <thread>

int main() {
    static d2vr::FrameTimingQueue queue;
    d2vr::FrameTimingRecord record{};record.has_xr=true;record.xr[0]=record.present[0]=7;
    record.xr[1]=100;record.xr[2]=180;record.present[1]=90;record.present[2]=180;
    record.present[3]=210;record.present[4]=220;record.present[5]=30;record.present[6]=10;record.present[7]=130;
    assert(queue.submit(record));const auto copied=queue.pop();assert(copied && copied->has_xr);
    assert(copied->xr==record.xr && copied->present==record.present && !queue.pop());
    std::ostringstream csv;d2vr::write_frame_timing_row(csv,copied->present);
    assert(csv.str()=="7,90,180,210,220,30,10,130,0,0,0,0,0,0,0,0,0,0\n");
    // Emulate a worker stalled in its file sink after taking a record. Present
    // can fill its bounded queue and report loss without waiting on that sink.
    std::mutex gate;std::condition_variable changed;bool writing=false,release=false;
    assert(queue.submit(record));
    std::thread worker([&] {
        const auto taken=queue.pop();assert(taken && taken->xr==record.xr);
        std::unique_lock<std::mutex> lock(gate);writing=true;changed.notify_one();
        changed.wait(lock,[&]{return release;});
        std::ostringstream sink;d2vr::write_frame_timing_row(sink,taken->present);
    });
    {std::unique_lock<std::mutex> lock(gate);changed.wait(lock,[&]{return writing;});}
    for(unsigned i=0;i<queue.capacity;++i) {record.xr[0]=record.present[0]=100+i;assert(queue.submit(record));}
    assert(!queue.submit(record) && queue.dropped()==1);
    {std::lock_guard<std::mutex> lock(gate);release=true;}changed.notify_one();worker.join();
    for(unsigned i=0;i<queue.capacity;++i) {
        const auto taken=queue.pop();assert(taken && taken->has_xr && taken->xr[0]==100+i && taken->present[0]==taken->xr[0]);
    }
    assert(!queue.pop());queue.reset();assert(!queue.pop() && queue.dropped()==0);
    // Pairs remain together when no XR wait happened on a native Present.
    record.has_xr=false;assert(queue.submit(record));assert(!queue.pop()->has_xr);
    assert(queue.submit(record));queue.stop();assert(!queue.submit(record) && queue.dropped()==1);
    assert(queue.pop() && !queue.pop());queue.reset();assert(queue.submit(record));
    std::cout<<"PASS: matched XR/Present records, CSV values, blocked sink isolation, bounded drop, FIFO, wrap and reset\n";
}
