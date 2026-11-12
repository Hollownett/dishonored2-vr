// PrevMvpTable: paired stereo draws L then R each frame; the engine caches each draw's MVP from the last rendered view.
#include "../src/object_motion.hpp"
#if __has_include("../linux/proxy/vs_layouts.hpp")
#include "../linux/proxy/vs_layouts.hpp"
#else
#include "fixtures/vs_layouts_private_copy.hpp" // this private copy has no linux/proxy tree; the lab reflects the same shader either way
#endif
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
    // v1206 alternate (one eye per engine frame) through fix_render: each render's receipt names its previous render (own_frame). The engine's
    // raw previous rows are the last render that DREW the object. Frame-counter skips (a render 3 frames after the other eye's), an object culled
    // in the other eye (own_last), the rig (never own_last), and a wrong own_frame never yield any rows but own_frame's.
    {
        auto t=std::make_unique<d2vr::PrevMvpTable>();
        struct R {unsigned eye;unsigned frame;};
        const R renders[]{{0,100},{1,101},{0,102},{1,103},{0,104},{1,107},{0,108},{1,109},{0,112},{1,113},{0,114},{1,115}}; // skips at 105-106, 110-111
        std::map<unsigned,M> raw;std::map<unsigned,unsigned> last_frame[2];
        for(const R& r:renders) for(unsigned object=0;object<3;++object) {
            if(r.eye==1 && object==1 && r.frame>103) continue; // object 1: culled in the right eye from 107
            const M cur=mvp(r.eye,r.frame,object);const M engine=raw.count(object)?raw[object]:cur;raw[object]=cur;
            const bool has_own=last_frame[r.eye].count(object)!=0;const unsigned own=has_own?last_frame[r.eye][object]:0;
            // the receipt's previous render: this eye's previous render frame (independent of the object)
            unsigned prev_render=0;for(const R& q:renders) if(q.eye==r.eye && q.frame<r.frame) prev_render=q.frame;
            for(int pass=0;pass<2;++pass) {
                M prev=engine;const bool rig=object==2;
                const Kind kind=prev_render?t->fix_render(r.eye,r.frame,prev_render,cur.data(),prev.data(),!rig):Kind::miss;
                if(!prev_render) {t->insert(r.eye,r.frame,cur.data(),engine.data());continue;}
                CHECK(kind==Kind::same || prev==engine || prev==mvp(r.eye,prev_render,object)); // nothing but own_frame's rows (or unchanged)
                if(kind==Kind::other_last || kind==Kind::own_last) CHECK(prev==mvp(r.eye,prev_render,object));
                if(r.frame-prev_render>d2vr::PrevMvpTable::gens) {CHECK(kind==Kind::miss);continue;}       // older than the table: camera-only
                if(has_own && own==prev_render && object!=1) CHECK(kind==Kind::other_last);                  // the 2- and 3-frame steps chain
                if(object==1 && r.eye==0 && r.frame>=108) CHECK(kind==Kind::own_last);                        // culled in the other eye: already own
                if(rig) CHECK(kind!=Kind::own_last);
            }
            last_frame[r.eye][object]=r.frame;
        }
        // A wrong own_frame (the slot reuse / timing label hazard: frame-4 instead of frame-2) never returns frame-2's rows, and a
        // receipt naming frame-4 never takes frame-2's rows either
        auto u=std::make_unique<d2vr::PrevMvpTable>();
        for(unsigned f=200;f<=205;++f) {const unsigned e=f&1u;M prev=f>200?mvp(e^1u,f-1,5):mvp(e,f,5);u->fix_render(e,f,f-2,mvp(e,f,5).data(),prev.data(),true);}
        M cur=mvp(0,206,5),prev=mvp(1,205,5);
        CHECK(u->fix_render(0,206,202,cur.data(),prev.data(),true)==Kind::unproven && prev==mvp(1,205,5)); // receipt says 202: 204's rows refused
        prev=mvp(1,205,5);CHECK(u->fix_render(0,206,204,cur.data(),prev.data(),true)==Kind::other_last && prev==mvp(0,204,5));
        { // v1207 prove_render: a rig draw without a content view; its raw rows (the other eye's 205) name exactly one candidate render
            const d2vr::PrevMvpTable::RenderKey keys[]{{1,207,205},{0,204,202},{0,206,204},{1,205,203}};
            M raw=mvp(1,205,5);CHECK(u->prove_render(keys,4,raw.data())==2);
            raw=mvp(1,201,5);CHECK(u->prove_render(keys,4,raw.data())==-1); // rows of a render older than every candidate's previous one
            raw=mvp(1,205,9);CHECK(u->prove_render(keys,4,raw.data())==-1); // never recorded (another pass's rows): nothing proves
        }
        cur=mvp(1,209,5);prev=mvp(0,208,5); // the other eye's 208 was never recorded: a miss, rows unchanged (camera-only follows)
        CHECK(u->fix_render(1,209,207,cur.data(),prev.data(),true)==Kind::miss && prev==mvp(0,208,5));
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
    CHECK(d2vr::vs_layouts::has_previous(layout));
    CHECK(layout_of("b3")==0); // not the per-instance slot
    // v1103: a vertex shader with the current MVP rows only (the held weapon's colour/forward variant). The proxy used to return 0 for
    // it (it required cb_previousmvpmatrix), so ObjectMotion never saw the colour draw. It must now return the four current rows with
    // all four previous bytes 0xff, and a b3 layout must still be rejected. The row order here is x,y,z,w at float4 0..3.
    static const char current_only_hlsl[]=
        "cbuffer PerInstance:register(REG){float4 cb_mvpmatrixx;float4 cb_mvpmatrixy;float4 cb_mvpmatrixz;float4 cb_mvpmatrixw;};"
        "struct O{float4 p:SV_Position;};"
        "O main(float4 v:POSITION){O o;o.p=float4(dot(cb_mvpmatrixx,v),dot(cb_mvpmatrixy,v),dot(cb_mvpmatrixz,v),dot(cb_mvpmatrixw,v));return o;}";
    const auto current_only_layout_of=[](const char* reg) {
        const D3D_SHADER_MACRO macros[]={{"REG",reg},{nullptr,nullptr}};
        ID3DBlob* code{};ID3DBlob* errors{};
        const HRESULT hr=D3DCompile(current_only_hlsl,sizeof current_only_hlsl-1,nullptr,macros,nullptr,"main","vs_5_0",0,0,&code,&errors);
        if(errors) errors->Release();
        if(FAILED(hr)) return ~0ull;
        const std::uint64_t packed=d2vr::vs_layouts::reflect(code->GetBufferPointer(),code->GetBufferSize());
        code->Release();return packed;
    };
    const std::uint64_t current_only=current_only_layout_of("b2");
    CHECK(current_only==0xffffffff03020100ull);
    CHECK(!d2vr::vs_layouts::has_previous(current_only));
    CHECK(current_only_layout_of("b3")==0);
    CHECK(current_only_layout_of("b2")!=0); // the v1103 regression: not zero for a current-only layout
    std::printf("current-only layout 0x%016llx previous=%d b3=%d\n",
        static_cast<unsigned long long>(current_only),int(d2vr::vs_layouts::has_previous(current_only)),int(current_only_layout_of("b3")));
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
    // v1101: the current rows alone (a depth/velocity-only vertex-shader layout has no previous rows); the previous rows are gathered
    // separately. The current-transform placement must not depend on the previous rows at all, so the two halves must agree exactly.
    {
        float only_cur[16]{},cur2[16]{},prev2[16]{};
        CHECK(d2vr::ObjectMotion::gather_current(layout,data,128,vp.data()+8,only_cur));
        CHECK(!std::memcmp(only_cur,cur.data(),64));
        CHECK(d2vr::ObjectMotion::gather_previous(layout,data,128,vp.data()+8,prev2));
        CHECK(!std::memcmp(prev2,old.data(),64));
        // a layout with no previous rows at all: the current rows still gather (0xff in every previous byte), the previous half fails.
        // v1103: this is the proxy's own encoding of the current-only shader above, not a hand-written word. Its rows 0..3 are the
        // current matrix; a current-only draw has no previous rows to gather or to rewrite.
        float current_only_data[16];
        for(int r=0;r<4;++r) std::memcpy(current_only_data+4*r,cur.data()+4*r,16);
        CHECK(current_only==0xffffffff03020100ull);
        CHECK(d2vr::ObjectMotion::gather_current(current_only,current_only_data,64,vp.data()+8,cur2));
        CHECK(!std::memcmp(cur2,cur.data(),64));
        CHECK(!d2vr::ObjectMotion::gather_previous(current_only,current_only_data,64,vp.data()+8,prev2));
        CHECK(!d2vr::ObjectMotion::gather(current_only,current_only_data,64,vp.data()+8,cur2,prev2));
        float scattered[16];std::memcpy(scattered,current_only_data,64);
        d2vr::ObjectMotion::scatter(current_only,old.data(),scattered);
        CHECK(!std::memcmp(scattered,current_only_data,64)); // no previous rows: nothing to rewrite
        d2vr::ObjectMotion::scatter_cur(current_only,old.data(),scattered);
        CHECK(!std::memcmp(scattered,old.data(),64));        // all four declared rows are current rows
    }
    // v1101: the placement gate of build/no-weapon-draw-place (weapon_mode 0) and of object-motion-mode 0 (diagnostic only).
    CHECK(d2vr::ObjectMotion::weapon_draw_on(1,1) && !d2vr::ObjectMotion::weapon_draw_on(0,1) &&
          !d2vr::ObjectMotion::weapon_draw_on(1,0) && !d2vr::ObjectMotion::weapon_draw_on(0,-1));
    // v1140: a held prop is placed with weapons opt-out (weapon_mode 0); neither = no placement; object-motion-mode 0 still wins.
    CHECK(d2vr::ObjectMotion::weapon_draw_on(d2vr::ObjectMotion::placing_mode(0,true),1) && !d2vr::ObjectMotion::weapon_draw_on(d2vr::ObjectMotion::placing_mode(0,false),1) &&
          d2vr::ObjectMotion::weapon_draw_on(d2vr::ObjectMotion::placing_mode(1,false),1) && !d2vr::ObjectMotion::weapon_draw_on(d2vr::ObjectMotion::placing_mode(0,true),0));
    // v1101: the publisher's frame and per-hand counters (the weapon lab drives place() itself).
    {
        static const float frame[15]={0,0,0, 1,0,0, 0,1,0, 0,0,1, 1,1,1};
        const d2vr::weapon_draw::Mat delta=d2vr::weapon_draw::identity();
        const float* raw[2]={frame,nullptr};const d2vr::weapon_draw::Mat* deltas[2]={&delta,nullptr};
        const std::uint32_t published_frame=0x51u;
        d2vr::weapon_draw::Place::publish(published_frame,{raw[0],raw[1]},{deltas[0],deltas[1]});
        CHECK(d2vr::weapon_draw::Place::latest_frame()==published_frame);
        CHECK(d2vr::weapon_draw::Place::published(0)==1 && d2vr::weapon_draw::Place::published(1)==0);
        CHECK(d2vr::weapon_draw::Place::slot_of(published_frame) && d2vr::weapon_draw::Place::slot_of(published_frame)->side[1].valid==false);
        d2vr::weapon_draw::Place::clear();
        CHECK(d2vr::weapon_draw::Place::latest_frame()==0 && d2vr::weapon_draw::Place::slot_of(published_frame)==nullptr);
    }
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
    { // v1040: has_previous is gather_previous's availability test (the early-out for current-only draws relies on it)
        float rows[256]{},zrow[4]{};
        for(unsigned long long lay:{0x0807060504030201ull,0xffffffffffffffffull,0xffffffff03020100ull,0x0807ff0504030201ull,0x08070605ffffffffull,0x07ff0605ffff0302ull})
            for(UINT size:{16u,64u,96u,128u,256u})
                CHECK(O::has_previous(lay,size)==O::gather_previous(lay,rows,size,zrow,rows+128));
        CHECK(!O::has_previous(0xffffffff03020100ull,256) && O::has_previous(0x0807060504030201ull,144) && !O::has_previous(0x0807060504030201ull,100));
    }
    { // v998: ObjectMotion's hook order. Its bookkeeping runs before the wide filter and sees an elided call; the filter only
      // decides whether the original setter is forwarded.
        unsigned observed=0;bool forwarded=false;
        const bool skip=O::observe_then_filter([&] {++observed;},[&] {return true;});
        CHECK(skip && observed==1 && !forwarded);
        const bool forward=O::observe_then_filter([&] {++observed;},[&] {forwarded=true;return false;});
        CHECK(!forward && observed==2 && forwarded);
    }
    std::printf(failures?"object_motion_check FAILED\n":"object_motion_check OK\n");
    return failures?1:0;
}
