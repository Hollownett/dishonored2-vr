#include "../src/latest_file_writer.hpp"
#include <cassert>
#include <iostream>
#include <sstream>
using namespace d2vr;
static std::string slurp(const std::filesystem::path& p){std::ifstream f(p);std::ostringstream o;o<<f.rdbuf();return o.str();}
int main(){
    const auto dir=std::filesystem::temp_directory_path()/"d2vr_writer_check";std::filesystem::create_directories(dir);
    const auto a=dir/"a.json",b=dir/"b.csv",bad=dir/"no_such_dir"/"x";std::filesystem::remove(a);std::filesystem::remove(b);
    LatestFileWriter w;std::atomic<bool> failed{};
    for(int i=0;i<1000;++i) w.post(a,"v"+std::to_string(i)); // latest wins; no unbounded queue
    w.post(b,"1\n",true);w.post(b,"2\n",true);w.post(bad,"x",false,&failed);
    w.stop(); // drains, then joins
    assert(slurp(a)=="v999");
    assert(slurp(b)=="1\n2\n");
    assert(failed.load());
    w.post(a,"late");w.stop();
    assert(slurp(a)=="v999"); // closed: nothing is written or restarted after stop
    w.open();w.post(a,"again");w.stop();
    assert(slurp(a)=="again");
    std::filesystem::remove_all(dir);std::cout<<"latest_file_writer_check OK\n";
}
