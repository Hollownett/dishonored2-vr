#include "../src/world_ui_completion.hpp"
#include <cstdio>
#include <stdexcept>
#include <thread>

static void check(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
int main() {
    try {
        d2vr::WorldUiCompletion completion;
        int ctxA{},ctxB{},listA{},listB{},listC{};
        check(!completion.ready(0,0,0),"zero scope never ready");
        check(completion.recorded(&ctxA,0,0,1),"record left wheel");
        check(completion.recorded(&ctxA,0,0,1),"duplicate record harmless");
        check(completion.recorded(&ctxA,1,1,2),"record right notes");
        check(!completion.ready(0,0,1),"recording does not publish");
        check(completion.finished(&ctxA,&listA),"finish first list");
        check(!completion.ready(0,0,1),"FinishCommandList does not publish");
        check(completion.recorded(&ctxA,0,0,3) && completion.finished(&ctxA,&listB),"same context owns multiple pending lists");
        check(completion.executed(&listB) && completion.ready(0,0,3),"newer list completes first");
        check(completion.executed(&listA) && completion.ready(0,0,1) && completion.ready(1,1,2),"execution order publishes actual older pixels and every movie");
        check(!completion.ready(0,0,3),"later execution of old list invalidates newer image publication");
        check(!completion.executed(&listA),"completed list association removed");
        check(completion.recorded(&ctxB,2,0,4) && completion.finished(&ctxB,&listC),"abandoned list");
        check(completion.recorded(&ctxA,2,1,5) && completion.finished(&ctxA,&listC),"recycled native list identity");
        check(completion.executed(&listC) && completion.ready(2,1,5) && !completion.ready(2,0,4),"recycled identity cannot publish abandoned owner");
        check(completion.immediate(2,0,6) && completion.ready(2,0,6),"explicit immediate draw completion");
        std::array<int,32> lists{};
        for(unsigned i=0;i<lists.size();++i) check(completion.recorded(&ctxA,0,1,100+i) && completion.finished(&ctxA,&lists[i]),"more than eight split lists retained");
        for(unsigned i=0;i<lists.size();++i) check(completion.executed(&lists[i]) && completion.ready(0,1,100+i),"all queued lists complete");
        // Independent workers publish through one synchronized tracker.
        std::array<int,2> contexts{},workerLists{};
        std::thread left([&]() {completion.recorded(&contexts[0],1,0,200);completion.finished(&contexts[0],&workerLists[0]);completion.executed(&workerLists[0]);});
        std::thread right([&]() {completion.recorded(&contexts[1],1,1,201);completion.finished(&contexts[1],&workerLists[1]);completion.executed(&workerLists[1]);});
        left.join();right.join();check(completion.ready(1,0,200) && completion.ready(1,1,201),"worker ownership remains separate");
        check(!completion.recorded(nullptr,0,0,7) && !completion.recorded(&ctxA,3,0,7) && !completion.immediate(0,2,7),"reject invalid ownership");
        check(completion.recorded(&ctxA,0,0,8) && completion.finished(&ctxA,&listA),"pending cleanup fixture");
        completion.clear();check(!completion.ready(0,0,1) && !completion.ready(1,0,200) && !completion.executed(&listA),"owner cleanup removes pending and published scopes");
        check(completion.finished_lists==39 && completion.executed_lists==37 && completion.immediate_scopes==1 && completion.rejected==3,"completion counters");
        std::puts("PASS: world UI submission ordering, split lists, immediate completion, recycled identities, worker isolation and cleanup");return 0;
    } catch(const std::exception& e) {std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
