// PrevMvpTable: paired stereo draws L then R each frame; the engine caches each draw's MVP from the last rendered view.
#include "../src/object_motion.hpp"
#include "../linux/proxy/vs_layouts.hpp"
#include <d3dcompiler.h>
#include <cstdio>
#include <map>
#include <memory>

static int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c);++failures;}}while(0)

using M=std::array<float,16>;
static M mvp(unsigned eye,unsigned frame,unsigned object) { // a distinct matrix per eye, frame and object
    M m{};for(unsigned i=0;i<16;++i) m[i]=float(eye*100000+frame*100+object)+float(i)*.125f;
    return m;
}
using Kind=d2vr::PrevMvpTable::Kind;

int main() {
    auto table=std::make_unique<d2vr::PrevMvpTable>();
    // Case A: one cache per draw, renewed by every view. Object 1 is culled in the right eye after frame 5.
    std::map<unsigned,M> cache;
    for(unsigned frame=10;frame<30;++frame) for(unsigned eye=0;eye<2;++eye) for(unsigned object=0;object<3;++object) {
        if(eye==1 && object==1 && frame>15) continue;
        const M cur=mvp(eye,frame,object);
        const M engine=cache.count(object)?cache[object]:cur;cache[object]=cur;
        for(int pass=0;pass<2;++pass) { // depth/velocity passes write the same rows again
            M prev=engine;
            const Kind kind=table->fix(eye,frame,cur.data(),prev.data());
            if(frame==10 && eye==0) {CHECK(kind==Kind::same);continue;}
            if(frame==10) {CHECK(kind==Kind::unproven || kind==Kind::miss);CHECK(prev==engine);continue;} // no own last frame yet
            if(eye==0 && object==1 && frame>16) {CHECK(kind==Kind::own_last);CHECK(prev==engine);continue;} // the cache kept L's own
            CHECK(kind==(eye?Kind::other_now:Kind::other_last));
            CHECK(prev==mvp(eye,frame-1,object));
        }
    }
    // Case B: the cache is renewed once a frame from the right eye: R is already right, L must not be "fixed".
    auto b=std::make_unique<d2vr::PrevMvpTable>();
    for(unsigned frame=40;frame<50;++frame) for(unsigned eye=0;eye<2;++eye) {
        const M cur=mvp(eye,frame,7),engine=frame==40?cur:mvp(1,frame-1,7);
        M prev=engine;const Kind kind=b->fix(eye,frame,cur.data(),prev.data());
        if(frame>41) CHECK(kind==(eye?Kind::own_last:Kind::unproven));
        CHECK(prev==engine);
    }
    // Generations are reused four frames later without stale hits.
    M prev=mvp(1,25,0);CHECK(!table->find(1,25+d2vr::PrevMvpTable::gens,mvp(1,25,0).data(),prev.data()));
    // Reflected layout (linux/proxy/vs_layouts.hpp): rows stored x,z,y,w, another constant, previous rows without z.
    static const char hlsl[]=
        "cbuffer PerInstance:register(REG){float4 cb_mvpmatrixx;float4 cb_mvpmatrixz;float4 cb_mvpmatrixy;float4 cb_mvpmatrixw;"
        "float4 cb_other;float4 cb_previousmvpmatrixx;float4 cb_previousmvpmatrixy;float4 cb_previousmvpmatrixw;};"
        "struct O{float4 p:SV_Position;float4 q:TEXCOORD0;};"
        "O main(float4 v:POSITION){O o;o.p=float4(dot(cb_mvpmatrixx,v),dot(cb_mvpmatrixy,v),dot(cb_mvpmatrixz,v),dot(cb_mvpmatrixw,v));"
        "o.q=float4(dot(cb_previousmvpmatrixx,v),dot(cb_previousmvpmatrixy,v),cb_other.x,dot(cb_previousmvpmatrixw,v));return o;}";
    const auto layout_of=[](const char* reg) {
        const D3D_SHADER_MACRO macros[]={{"REG",reg},{nullptr,nullptr}};
        ID3DBlob* code{};ID3DBlob* errors{};
        const HRESULT hr=D3DCompile(hlsl,sizeof hlsl-1,nullptr,macros,nullptr,"main","vs_5_0",0,0,&code,&errors);
        if(errors) errors->Release();
        if(FAILED(hr)) return ~0ull;
        const std::uint64_t packed=d2vr::vs_layouts::reflect(code->GetBufferPointer(),code->GetBufferSize());
        code->Release();return packed;
    };
    const std::uint64_t layout=layout_of("b2");
    CHECK(layout==0x07ff060503010200ull);
    CHECK(layout_of("b3")==0); // not the per-instance slot
    // One write's rows by that layout; the missing previous z row is the eye VP's (0,0,0,near).
    const auto mul=[](const float* a,const float* b2) {M m{};for(int i=0;i<4;++i) for(int j=0;j<4;++j) for(int k=0;k<4;++k) m[i*4+j]+=a[i*4+k]*b2[k*4+j];return m;};
    const float proj[16]={1.2f,0,.1f,0, 0,1.6f,-.05f,0, 0,0,0,.05f, 0,0,-1,0},view[16]={.8f,0,-.6f,3, 0,1,0,-1.7f, .6f,0,.8f,40, 0,0,0,1};
    const float shift[16]={1,0,0,-.064f, 0,1,0,0, 0,0,1,0, 0,0,0,1},model[16]={0,-1,0,12, 1,0,0,-7, 0,0,1,.5f, 0,0,0,1},model2[16]={0,-1,0,12.1f, 1,0,0,-7, 0,0,1,.5f, 0,0,0,1};
    const M vp=mul(proj,view),vp2=mul(proj,mul(shift,view).data()),cur=mul(vp.data(),model),old=mul(vp2.data(),model2);
    float data[32];for(int i=0;i<32;++i) data[i]=.37f*float(i)-3.f;
    for(int r:{0,1,2,3}) std::memcpy(data+4*(r==1?2:r==2?1:r),cur.data()+4*r,16);
    for(int r:{0,1,3}) std::memcpy(data+4*(r==3?7:5+r),old.data()+4*r,16);
    float c[16],p[16];
    CHECK(d2vr::ObjectMotion::gather(layout,data,128,vp.data()+8,c,p));
    CHECK(!std::memcmp(c,cur.data(),64) && !std::memcmp(p,old.data(),64));
    CHECK(!d2vr::ObjectMotion::gather(layout,data,112,vp.data()+8,c,p)); // the buffer ends before the previous w row
    float inv[4];
    CHECK(d2vr::ObjectMotion::inverse_last_row(vp.data(),false,inv) && d2vr::ObjectMotion::affine(inv,c) && !d2vr::ObjectMotion::affine(inv,data));
    float out[32];std::memcpy(out,data,sizeof out);
    const M own=mul(vp.data(),model2);d2vr::ObjectMotion::scatter(layout,own.data(),out);
    for(int i=0;i<32;++i) {const int row=i/4;CHECK(out[i]==(row==5?own[i-20]:row==6?own[i-20]:row==7?own[12+i%4]:data[i]));}
    // View correction: a previous MVP cached from the other eye's view, moved into this eye's view.
    float inverse_source[16],correction[16],moved[16];
    CHECK(d2vr::ObjectMotion::inverse(vp2.data(),inverse_source));
    d2vr::ObjectMotion::mul(vp.data(),inverse_source,correction);d2vr::ObjectMotion::mul(correction,old.data(),moved);
    {const M want=mul(vp.data(),model2);bool same=true;for(int i=0;i<16;++i) same=same && std::abs(moved[i]-want[i])<=1e-3f*(1.f+std::abs(want[i]));CHECK(same);}
    // v950: the eye key of a draw is its VS b1 when that is an eye's TAA per-view buffer (two per eye; one both eyes bind names none).
    int buffers[6]{};using O=d2vr::ObjectMotion;
    CHECK(O::b1_eye(&buffers[0])==-1 && O::b1_eye(nullptr)==-1);
    O::note_taa_b1(0,&buffers[0]);O::note_taa_b1(1,&buffers[1]);O::note_taa_b1(0,&buffers[2]);O::note_taa_b1(0,&buffers[0]);O::note_taa_b1(2,&buffers[5]);
    CHECK(O::b1_eye(&buffers[0])==0 && O::b1_eye(&buffers[2])==0 && O::b1_eye(&buffers[1])==1 && O::b1_eye(&buffers[5])==-1);
    O::note_taa_b1(0,&buffers[3]);CHECK(O::b1_eye(&buffers[3])==0 && O::b1_eye(&buffers[2])==0 && O::b1_eye(&buffers[0])==-1); // the oldest goes
    O::note_taa_b1(1,&buffers[3]);CHECK(O::b1_eye(&buffers[3])==-1 && O::b1_eye(&buffers[1])==1);
    std::printf(failures?"object_motion_check FAILED\n":"object_motion_check OK\n");
    return failures?1:0;
}
