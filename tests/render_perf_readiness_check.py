import argparse
import pathlib
import subprocess
import tempfile


def extract(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 1
    cursor = brace + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor] + (";" if signature.startswith("struct ") else "")


def fixture(source):
    declarations = "\n".join(extract(source, "struct " + name + " {") for name in ("Pair", "Tag", "Span", "Mark", "Frame", "Hud", "Row", "Readiness"))
    signatures = ("template<class ValueType> HRESULT read(", "void emit(const Frame&", "bool inside(", "void release_frame(", "static void readiness_read(", "void cache_readiness(", "void report_readiness(", "void poll_locked(")
    methods = "\n".join(extract(source, signature) for signature in signatures)
    return PREAMBLE + declarations + MEMBERS + methods + TESTS


PREAMBLE = r"""
#define NOMINMAX
#include <windows.h>
#include <array>
#include <vector>
#include <memory>
#include <cstdint>
#include <cassert>
#include <iostream>
constexpr unsigned D3D11_ASYNC_GETDATA_DONOTFLUSH=1;
struct ID3D11Query {unsigned identity{};};
struct D3D11_QUERY_DATA_TIMESTAMP_DISJOINT {std::uint64_t Frequency{};int Disjoint{};};
struct ID3D11DeviceContext {
    std::array<HRESULT,9> results{};std::vector<unsigned> calls;bool disjoint{};
    HRESULT GetData(ID3D11Query* query,void* value,unsigned size,unsigned flags) {
        assert(flags==D3D11_ASYNC_GETDATA_DONOTFLUSH);calls.push_back(query->identity);
        if(results[query->identity]!=S_OK)return results[query->identity];
        if(size==sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT)) *static_cast<D3D11_QUERY_DATA_TIMESTAMP_DISJOINT*>(value)={1000,disjoint?1:0};
        else *static_cast<std::uint64_t*>(value)=query->identity==2?100:query->identity==3?200:150;
        return S_OK;
    }
};
template<class ValueType> struct Handle {ValueType* pointer{};ValueType* Get() const{return pointer;}};
class Probe {
public:
    template<class ValueType> using Ptr=Handle<ValueType>;
    enum class LossReason {query_read_failed,poll_owner};
    static constexpr unsigned frame_count=8,span_count=128,mark_count=32,hud_count=2048,row_count=4096;
"""
MEMBERS = r"""
    std::array<Frame,frame_count> frames_{};std::array<Hud,hud_count> hud_{};std::array<Row,row_count> rows_{};
    std::array<Readiness,frame_count> readiness_cache_{};unsigned hud_used_{},rows_used_{},row_begin_{};
    std::uint64_t dropped_{},queued_rows_lost_{},replays_{},failures_{},seen_present_=100,readiness_last_present_{};
    std::array<std::uint64_t,2> loss_reasons_{};HRESULT last_query_read_hresult_{};bool readiness_emitted_{};
    bool owner(ID3D11DeviceContext*)const{return true;}
    void reject_owner(LossReason,ID3D11DeviceContext*){assert(false);}
"""
TESTS = r"""
};
static std::array<ID3D11Query,8> queries{{{1},{2},{3},{4},{5},{6},{7},{8}}};
static std::unique_ptr<Probe> setup(unsigned count=1) {
    auto probe=std::make_unique<Probe>();
    for(unsigned index=0;index<count;++index) {
        auto& frame=probe->frames_[index];frame.pending=true;frame.serial=index+1;frame.present=90+index;frame.epoch=7;
        frame.clock.pointer=&queries[0];frame.start.pointer=&queries[1];frame.end.pointer=&queries[2];
        frame.spans_used=frame.marks_used=1;frame.spans[0].query={{&queries[3]},{&queries[4]}};frame.marks[0].query.pointer=&queries[5];
        auto& hud=probe->hud_[index];hud.frame_serial=frame.serial;hud.tag.ticket=0xfedcba9876543001ull+index;
        hud.list=0xabcdef1234567890ull;hud.origin_present=89;hud.origin_epoch=6;hud.executions=1;hud.tag.closed=true;
        hud.query={{&queries[6]},{&queries[7]}};
    }
    probe->hud_used_=count;return probe;
}
static std::uint64_t field(const Probe& probe,unsigned category,unsigned phase,std::uint64_t present=90) {
    for(unsigned index=0;index<probe.rows_used_;++index) {
        const auto& row=probe.rows_[index];if(row.category==category && row.phase==phase && row.frame==present)return row.id;
    }
    assert(false);return 0;
}
static unsigned durations(const Probe& probe) {
    unsigned count{};for(unsigned index=0;index<probe.rows_used_;++index)if(probe.rows_[index].category<15)++count;return count;
}
int main() {
    for(unsigned stage=1;stage<=8;++stage) for(HRESULT result:{S_FALSE,E_FAIL}) {
        auto probe=setup();ID3D11DeviceContext context;context.results[stage]=result;probe->poll_locked(&context);
        assert(field(*probe,15,5)==stage && field(*probe,15,6)==static_cast<std::uint32_t>(result));
        assert(field(*probe,15,1)==1 && field(*probe,15,2)==100 && field(*probe,15,3)==10);
        assert(field(*probe,15,4)==reinterpret_cast<std::uintptr_t>(&context));
        const std::vector<unsigned> expected=stage<=3?std::vector<unsigned>{1,2,3}:std::vector<unsigned>{1,2,3,4,5,6,7,8};
        assert(context.calls==expected);assert(durations(*probe)==0);
        assert(field(*probe,15,17)==(stage<=3?7u:255u));
        assert(probe->frames_[0].pending==(result==S_FALSE));
        if(stage>=7) {assert(field(*probe,15,8)==0xfedcba9876543001ull);assert(field(*probe,17,0)==0xabcdef1234567890ull);assert(field(*probe,17,1)==89);assert(field(*probe,17,2)==6);}
        assert(field(*probe,18,0)==1 && field(*probe,18,1)==0xfedcba9876543001ull);
    }
    {
        auto probe=setup();ID3D11DeviceContext context;probe->poll_locked(&context);
        assert(field(*probe,15,5)==0 && field(*probe,15,6)==S_OK);
        assert((context.calls==std::vector<unsigned>{1,2,3,4,5,6,7,8,4,5,6,7,8}));assert(durations(*probe)==4);
        assert(!probe->frames_[0].pending);
    }
    {
        auto probe=setup();ID3D11DeviceContext context;context.results[1]=S_FALSE;context.results[2]=E_FAIL;probe->poll_locked(&context);
        assert(field(*probe,15,5)==2 && field(*probe,15,6)==static_cast<std::uint32_t>(E_FAIL));
    }
    {
        auto probe=setup();ID3D11DeviceContext context;context.disjoint=true;probe->poll_locked(&context);
        assert(field(*probe,15,5)==9 && (field(*probe,15,12)&4));assert(!probe->frames_[0].pending && durations(*probe)==0);
    }
    {
        auto probe=setup(8);ID3D11DeviceContext context;context.results[1]=S_FALSE;probe->poll_locked(&context);
        const auto first_rows=probe->rows_used_;assert(first_rows<=8*38 && context.calls.size()==24);
        for(unsigned iteration=1;iteration<60;++iteration) {probe->seen_present_=100+iteration;probe->poll_locked(&context);assert(probe->rows_used_==first_rows);}
        probe->seen_present_=160;probe->poll_locked(&context);assert(probe->rows_used_==2*first_rows);
        context.results[1]=S_OK;probe->seen_present_=161;probe->poll_locked(&context);
        for(const auto& frame:probe->frames_)assert(!frame.pending);
        assert(durations(*probe)==32);
    }
    {
        auto probe=setup();ID3D11DeviceContext context;probe->readiness_emitted_=true;probe->readiness_last_present_=100;
        context.results[1]=E_FAIL;probe->poll_locked(&context);assert(probe->rows_used_==0 && probe->readiness_cache_[0].terminal);
        probe->frames_[0].pending=true;probe->frames_[0].serial=2;context.results[1]=S_FALSE;probe->seen_present_=101;probe->poll_locked(&context);
        assert(probe->readiness_cache_[0].serial==1 && probe->readiness_cache_[0].coalesced==1);
        probe->seen_present_=160;probe->poll_locked(&context);assert(field(*probe,15,1)==1 && field(*probe,15,14)==2);
    }
    {
        auto probe=setup();ID3D11DeviceContext context;context.results[7]=E_FAIL;
        probe->readiness_emitted_=true;probe->readiness_last_present_=100;probe->poll_locked(&context);
        assert(probe->rows_used_==0 && probe->readiness_cache_[0].terminal);
        const auto calls=context.calls;probe->report_readiness(true);
        assert(field(*probe,15,5)==7 && field(*probe,15,6)==static_cast<std::uint32_t>(E_FAIL));
        const auto rows=probe->rows_used_;assert(rows>0);probe->report_readiness(true);
        assert(probe->rows_used_==rows && context.calls==calls);
    }
    {
        auto probe=setup();ID3D11DeviceContext context;context.results[1]=S_FALSE;
        probe->readiness_emitted_=true;probe->readiness_last_present_=1000;probe->seen_present_=20;
        probe->poll_locked(&context);assert(field(*probe,15,16)==20);
        const auto rows=probe->rows_used_;probe->seen_present_=21;probe->poll_locked(&context);
        assert(probe->rows_used_==rows);probe->seen_present_=80;probe->poll_locked(&context);assert(probe->rows_used_==2*rows);
    }
    {
        auto probe=setup();ID3D11DeviceContext context;context.results[7]=S_FALSE;
        probe->readiness_emitted_=true;probe->readiness_last_present_=100;probe->poll_locked(&context);
        probe->rows_used_=Probe::row_count;probe->report_readiness(true);
        assert(probe->rows_used_==Probe::row_count && probe->queued_rows_lost_==38 && probe->dropped_==38);
        assert(!probe->readiness_cache_[0].valid);
    }
    std::cout<<"PASS: actual extracted readiness/poll with each pending/error gate, ready paths, bounded snapshots, identities and terminal coalescing\n";
}
"""


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1] / "src/render_perf_gpu.hpp")
    parser.add_argument("--emit", type=pathlib.Path)
    args = parser.parse_args()
    header_source = args.header.read_text()
    clear_source = extract(header_source, "void clear_locked()")
    drain_source = extract(header_source, "void drain(std::ostream& out,bool final=false)")
    assert clear_source.index("report_readiness(true)") < clear_source.index("for(auto& frame_record:frames_) frame_record=Frame{}")
    assert "readiness_emitted_=false;readiness_last_present_=0" in clear_source
    assert drain_source.index("report_readiness(true)") < drain_source.index("for(;;)")
    assert "if(final)" in drain_source
    actual = fixture(header_source)
    if args.emit:
        args.emit.write_text(actual)
        return
    with tempfile.TemporaryDirectory(prefix="d2vr-readiness-") as temporary:
        directory = pathlib.Path(temporary)
        source = directory / "readiness.cpp"
        source.write_text(actual)
        executable = directory / "readiness.exe"
        subprocess.run(["cl", "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX", "/O2", "/MT", "/analyze", str(source), "/Fo:" + str(directory / "readiness.obj"), "/Fe:" + str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
