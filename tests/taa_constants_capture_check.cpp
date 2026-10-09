#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/taa_constants_capture.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
void require(bool condition,const std::string& message) {if(!condition) throw std::runtime_error(message);}
constexpr unsigned block=0x320;
const char* expected_header="frame,eye,field,cb,ctx_type,f00,f01,f02,f03,f04,f05,f06,f07,f08,f09,f10,f11,f12,f13,f14,f15";
const std::vector<std::string> field_names{"viewprojection","view","projection","previous_viewprojection",
    "previous_view","previous_projection","jittervectors","globalviewinfos","framenumber"};

static void put_float(void* data,unsigned offset,float value) {
    std::memcpy(static_cast<unsigned char*>(data)+offset,&value,sizeof value);
}
// Deterministic per (frame,eye) content: any eye mix-up changes a value.
static void fill(void* data,unsigned long long frame,int eye) {
    std::memset(data,0,block);
    const auto f=static_cast<float>(frame),e=static_cast<float>(eye);
    put_float(data,0x020,f*100+e*10+1);put_float(data,0x060,f*100+e*10+2);put_float(data,0x0b0,f*100+e*10+3);
    put_float(data,0x0f0,f*1000+e*10+1);put_float(data,0x130,f*1000+e*10+2);put_float(data,0x170,f*1000+e*10+3);
    for(unsigned i=0;i<4;++i) put_float(data,0x1d0+i*4,f*10000+e*100+static_cast<float>(i+1));
    for(unsigned i=0;i<4;++i) put_float(data,0x2a0+i*4,f*100000+e*1000+static_cast<float>(i+1));
    put_float(data,0x318,f*10+e);
}
static double expected(unsigned long long frame,int eye,const std::string& field,unsigned index) {
    const double f=static_cast<double>(frame),e=static_cast<double>(eye);
    if(field=="viewprojection") return index?0:f*100+e*10+1;
    if(field=="view") return index?0:f*100+e*10+2;
    if(field=="projection") return index?0:f*100+e*10+3;
    if(field=="previous_viewprojection") return index?0:f*1000+e*10+1;
    if(field=="previous_view") return index?0:f*1000+e*10+2;
    if(field=="previous_projection") return index?0:f*1000+e*10+3;
    if(field=="jittervectors") return index<4?f*10000+e*100+index+1:0;
    if(field=="globalviewinfos") return index<4?f*100000+e*1000+index+1:0;
    if(field=="framenumber") return index?0:f*10+e;
    throw std::runtime_error("Unknown CSV field "+field);
}
struct Row {unsigned long long frame{};int eye{};std::string field,cb;int ctx_type{};double values[16]{};};
static std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> parts;std::size_t at=0;
    for(;;) {const auto next=line.find(',',at);parts.push_back(line.substr(at,next-at));if(next==std::string::npos) return parts;at=next+1;}
}
static std::vector<Row> parse(const std::string& text) {
    std::istringstream input(text);std::string line;std::vector<Row> rows;bool first=true;
    while(std::getline(input,line)) {
        if(first) {first=false;require(line==expected_header,"CSV header differs");continue;}
        if(line.empty()) continue;
        const auto parts=split(line);require(parts.size()==21,"CSV column count differs");
        Row row;row.frame=std::strtoull(parts[0].c_str(),nullptr,10);row.eye=std::atoi(parts[1].c_str());
        row.field=parts[2];row.cb=parts[3];row.ctx_type=std::atoi(parts[4].c_str());
        for(unsigned i=0;i<16;++i) row.values[i]=std::strtod(parts[5+i].c_str(),nullptr);
        rows.push_back(row);
    }
    return rows;
}
int main() {
    try {
        ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> immediate,deferred;
        require(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,
            &device,nullptr,&immediate)),"Cannot create a WARP device");
        require(SUCCEEDED(device->CreateDeferredContext(0,&deferred)),"Cannot create a deferred context");
        D3D11_BUFFER_DESC desc{};desc.ByteWidth=block;desc.Usage=D3D11_USAGE_DYNAMIC;
        desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Buffer> cb;
        require(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&cb)),"Cannot create the per-view constant buffer");

        d2vr::TaaConstantsCapture capture;
        std::ostringstream csv;
        auto stage=[&](unsigned long long frame,int eye) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            require(SUCCEEDED(deferred->Map(cb.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapped)),"Cannot map the constant buffer");
            fill(mapped.pData,frame,eye);deferred->Unmap(cb.Get(),0);
            auto* target=cb.Get();deferred->CSSetConstantBuffers(1,1,&target);
        };
        auto execute=[&]() {
            ComPtr<ID3D11CommandList> list;
            require(SUCCEEDED(deferred->FinishCommandList(FALSE,&list)),"Cannot finish the command list");
            immediate->ExecuteCommandList(list.Get(),FALSE);
        };
        // The staged copies are already part of executed lists, so a pump only
        // exercises the DO_NOT_WAIT retry; the real three-frame delay is applied
        // by the capture itself and is covered by drain(immediate,20,...) below.
        auto pump=[&](d2vr::TaaConstantsCapture& target,unsigned long long current,std::uint64_t rows,std::ostream& out) {
            for(unsigned attempt=0;attempt<4000 && target.written_rows.load()<rows;++attempt) {
                immediate->Flush();target.drain(immediate.Get(),current,out);
                if(target.written_rows.load()<rows) Sleep(1);
            }
            require(target.written_rows.load()==rows,"The capture did not drain every recorded slot");
        };
        // Six frames, both eyes per frame, each copy in its own command list.
        // A frame is drained only after both eyes are recorded, as in the game
        // where drain runs three frames behind.
        std::uint64_t rows_expected=0;
        for(unsigned long long frame=1;frame<=6;++frame) {
            for(int eye=0;eye<2;++eye) {stage(frame,eye);capture.record(deferred.Get(),eye,frame);execute();}
            rows_expected+=18;pump(capture,frame+3,rows_expected,csv);
        }
        capture.drain(immediate.Get(),20,csv);
        require(capture.recorded.load()==12 && capture.drops.load()==0 && capture.written_rows.load()==108,
            "Unexpected capture counters");
        require(capture.misses.load()==0,"A bound CS slot 1 was counted as a miss");
        require(!capture.complete(),"Six of sixty-four frames completed the capture");
        const auto rows=parse(csv.str());
        require(rows.size()==108,"CSV row count differs");
        std::string cb_value;
        for(const auto& row:rows) {
            require(row.eye==0 || row.eye==1,"CSV eye is out of range");
            require(row.frame>=1 && row.frame<=6,"CSV frame is out of range");
            require(row.ctx_type==static_cast<int>(D3D11_DEVICE_CONTEXT_DEFERRED),"CSV context type is not deferred");
            require(row.cb.rfind("0x",0)==0 && row.cb!="0x0","CSV constant-buffer pointer is missing");
            if(cb_value.empty()) cb_value=row.cb;else require(row.cb==cb_value,"CSV mixes constant buffers");
            for(unsigned i=0;i<16;++i)
                require(row.values[i]==expected(row.frame,row.eye,row.field,i),"CSV value differs for "+row.field);
        }
        // Every field of every (frame,eye) appears exactly once: that is the proof
        // that each deferred WRITE_DISCARD version reached its own staging copy.
        for(unsigned long long frame=1;frame<=6;++frame) for(int eye=0;eye<2;++eye)
            for(const auto& field:field_names) {
                unsigned found=0;
                for(const auto& row:rows) if(row.frame==frame && row.eye==eye && row.field==field) ++found;
                require(found==1,"A per-eye field row is missing or duplicated");
            }

        ID3D11Buffer* unbound=nullptr;deferred->CSSetConstantBuffers(1,1,&unbound);
        capture.record(deferred.Get(),0,7);
        require(capture.misses.load()==1 && capture.recorded.load()==12,"An unbound CS slot 1 was not a miss");
        capture.record(deferred.Get(),2,7);
        require(capture.misses.load()==1 && capture.recorded.load()==12,"An invalid eye was not ignored");
        D3D11_BUFFER_DESC short_desc{};short_desc.ByteWidth=16;short_desc.Usage=D3D11_USAGE_DYNAMIC;
        short_desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;short_desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        ComPtr<ID3D11Buffer> short_cb;
        require(SUCCEEDED(device->CreateBuffer(&short_desc,nullptr,&short_cb)),"Cannot create a short constant buffer");
        auto* short_target=short_cb.Get();deferred->CSSetConstantBuffers(1,1,&short_target);
        capture.record(deferred.Get(),0,7);
        require(capture.misses.load()==2,"A block shorter than 0x320 was not a miss");

        d2vr::TaaConstantsCapture limited(2);
        std::ostringstream limited_csv;
        require(!limited.complete(),"A fresh capture is already complete");
        rows_expected=0;
        for(unsigned long long frame=1;frame<=6;++frame) {
            for(int eye=0;eye<2;++eye) {stage(frame,eye);limited.record(deferred.Get(),eye,frame);execute();}
            if(frame<=2) rows_expected+=18;
            pump(limited,frame+3,rows_expected,limited_csv);
        }
        limited.drain(immediate.Get(),20,limited_csv);
        require(limited.recorded.load()==4 && limited.written_rows.load()==36 && limited.drops.load()==0,
            "The frame-limited capture did not stop at two frames");
        require(limited.complete(),"The frame-limited capture did not complete");
        limited.record(deferred.Get(),0,9);
        require(limited.recorded.load()==4,"Recording resumed after completion");
        require(parse(limited_csv.str()).size()==36,"The frame-limited CSV row count differs");
        std::cout<<"PASS: 12 deferred per-eye copies drained with exact per-eye viewprojection/previous/jitter values, "
            "header plus 21 columns, misses 0 bound / 1 unbound / 2 short, drops 0, and recording stopped after two "
            "frames with complete() set\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
