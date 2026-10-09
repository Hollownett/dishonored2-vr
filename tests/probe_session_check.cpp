#include "../src/probe_session.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <thread>

int main() {
    d2vr::ProbeSession session;
    session.begin(30,500);
    assert(session.running(500) && session.running(30499) && !session.running(30500));
    session.begin(0,100);
    assert(session.running(100) && session.running(100+24ull*60*60*1000));
    std::thread stop([&]{session.request_stop();});stop.join();
    assert(session.stop_requested() && !session.running(101));
    session.begin(10,std::numeric_limits<std::uint64_t>::max()-5000);
    assert(!session.stop_requested() && session.running(0) && !session.running(4999));
    session.begin(300,1000);
    assert(session.running(300999) && !session.running(301000));
    session.request_stop();
    assert(!session.running(2000));
    for(int invalid:{-1,1,9,301}) {
        bool rejected=false;
        try {session.begin(invalid,0);} catch(const std::invalid_argument&) {rejected=true;}
        assert(rejected);
    }
    std::cout << "PASS: timed expiry, continuous lifetime, cross-thread stop, restart and tick wrap\n";
}
