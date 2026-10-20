#include "../src/xr_layer_pose.hpp"
#include <limits>
#include <cstdio>
#include <cstdint>
#include <vector>
using namespace d2vr;
static int failures=0;
#define CHECK(c) do{if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);++failures;}}while(0)
static XrPosef pose(float x,float y,float z,float w){XrPosef p{};p.orientation={x,y,z,w};p.position={1,2,3};return p;}
static float norm(const XrPosef& p){auto& q=p.orientation;return std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);}
static XrCompositionLayerQuad quad(XrPosef p){XrCompositionLayerQuad q{};q.type=XR_TYPE_COMPOSITION_LAYER_QUAD;q.pose=p;return q;}
static const XrCompositionLayerBaseHeader* H(const void* l){return reinterpret_cast<const XrCompositionLayerBaseHeader*>(l);}
int main() {
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    {auto p=pose(0,0,0,1);CHECK(repair_layer_pose(p)==0);}
    {auto p=pose(0,0,0,2);CHECK(repair_layer_pose(p)==1);CHECK(std::fabs(norm(p)-1)<1e-6f);CHECK(p.orientation.w>.999f);}
    {auto p=pose(1,2,3,4);CHECK(repair_layer_pose(p)==1);CHECK(std::fabs(norm(p)-1)<1e-6f);}
    {auto p=pose(0,0,0,0);CHECK(repair_layer_pose(p)==-1);}
    {auto p=pose(0,0,0,nan);CHECK(repair_layer_pose(p)==-1);}
    {auto p=pose(inf,0,0,1);CHECK(repair_layer_pose(p)==-1);}
    {auto p=pose(0,0,0,1e30f);CHECK(repair_layer_pose(p)==-1);}
    {auto p=pose(0,0,0,1);p.position.y=nan;CHECK(repair_layer_pose(p)==-1);}
    // overlay drop keeps order, projection normalized
    {
        XrCompositionLayerProjectionView views[2]{};views[0].pose=pose(0,0,0,1);views[1].pose=pose(0,0,0,3);
        XrCompositionLayerProjection proj{};proj.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION;proj.viewCount=2;proj.views=views;
        auto a=quad(pose(0,0,0,1)),bad=quad(pose(0,0,0,0)),c=quad(pose(0,5,0,0));
        XrCompositionLayerCylinderKHR cyl{};cyl.type=XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;cyl.pose=pose(0,0,0,nan);
        const XrCompositionLayerBaseHeader* in[5]={H(&proj),H(&a),H(&bad),H(&cyl),H(&c)};
        const XrCompositionLayerBaseHeader* out[5]{};
        XrFrameEndInfo end{};end.type=XR_TYPE_FRAME_END_INFO;end.layerCount=5;end.layers=in;
        const auto g=guard_frame_layers(end,out,5);
        CHECK(end.layerCount==3);CHECK(end.layers==out);CHECK(out[0]==H(&proj)&&out[1]==H(&a)&&out[2]==H(&c));
        CHECK(std::fabs(norm(views[1].pose)-1)<1e-6f);CHECK(std::fabs(norm(c.pose)-1)<1e-6f);
        CHECK(g.repaired[size_t(LayerKind::projection)]==1);CHECK(g.repaired[size_t(LayerKind::quad)]==1);
        CHECK(g.dropped[size_t(LayerKind::quad)]==1);CHECK(g.dropped[size_t(LayerKind::cylinder)]==1);CHECK(!g.submit_none);
        CHECK(g.bad_kind==int(LayerKind::quad));
    }
    // projection failure -> no layers
    {
        XrCompositionLayerProjectionView views[2]{};views[0].pose=pose(0,0,0,1);views[1].pose=pose(nan,0,0,1);
        XrCompositionLayerProjection proj{};proj.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION;proj.viewCount=2;proj.views=views;
        auto a=quad(pose(0,0,0,1));
        const XrCompositionLayerBaseHeader* in[2]={H(&proj),H(&a)},*out[2]{};
        XrFrameEndInfo end{};end.layerCount=2;end.layers=in;
        const auto g=guard_frame_layers(end,out,2);
        CHECK(end.layerCount==0);CHECK(g.submit_none);CHECK(g.dropped[size_t(LayerKind::projection)]==1);
    }
    // single layer via end.layers=&layer
    {
        auto q=quad(pose(0,0,0,4));const XrCompositionLayerBaseHeader* layer=H(&q),*out[1]{};
        XrFrameEndInfo end{};end.layerCount=1;end.layers=&layer;
        auto g=guard_frame_layers(end,out,1);
        CHECK(end.layerCount==1&&end.layers[0]==H(&q));CHECK(g.repaired[size_t(LayerKind::quad)]==1);
        auto b=quad(pose(0,0,0,0));layer=H(&b);end.layerCount=1;end.layers=&layer;
        g=guard_frame_layers(end,out,1);CHECK(end.layerCount==0);CHECK(g.dropped[size_t(LayerKind::quad)]==1);
    }
    {XrFrameEndInfo end{};const XrCompositionLayerBaseHeader* out[1]{};auto g=guard_frame_layers(end,out,1);CHECK(end.layerCount==0&&!g.submit_none);}
    {   // the failure dump names every layer with its pose and sub-image rect
        auto q=quad(pose(0,0,0,1));q.size={.2f,.15f};q.subImage.imageRect={{4,8},{32,16}};
        const XrCompositionLayerBaseHeader* layer=H(&q);XrFrameEndInfo end{};end.layerCount=1;end.layers=&layer;
        const auto text=describe_frame_layers(end);
        CHECK(text.find("#0 quad")==0);CHECK(text.find("q=(0,0,0,1)")!=std::string::npos);CHECK(text.find("rect=(4,8 32x16)")!=std::string::npos);
        end.layerCount=0;CHECK(describe_frame_layers(end).empty());
    }
    {   // full-extent guard: only quad/cylinder rects that cover their whole swapchain pass
        int tag[3]{};XrSwapchain a=reinterpret_cast<XrSwapchain>(&tag[0]),b=reinterpret_cast<XrSwapchain>(&tag[1]),unknown=reinterpret_cast<XrSwapchain>(&tag[2]);
        ChainSizes sizes{{a,{448,384}},{b,{2020,1136}}};
        auto q1=quad(pose(0,0,0,1));q1.subImage.swapchain=a;q1.subImage.imageRect={{0,0},{448,384}};
        auto q2=quad(pose(0,0,0,1));q2.subImage.swapchain=b;q2.subImage.imageRect={{0,0},{2020,1136}};
        XrCompositionLayerCylinderKHR cyl{};cyl.type=XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR;cyl.subImage=q2.subImage;
        XrCompositionLayerProjection proj{};proj.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION; // projections are not counted
        const XrCompositionLayerBaseHeader* in[4]={H(&proj),H(&q1),H(&q2),H(&cyl)};
        XrFrameEndInfo end{};end.layerCount=4;end.layers=in;CHECK(count_subrect_layers(end,sizes)==0);
        q1.subImage.imageRect={{0,0},{434,322}};CHECK(count_subrect_layers(end,sizes)==1);   // smaller extent
        q1.subImage.imageRect={{0,0},{448,384}};q2.subImage.imageRect={{0,512},{2020,1136}};cyl.subImage.imageRect=q2.subImage.imageRect;
        CHECK(count_subrect_layers(end,sizes)==2);                                           // offset (quad and cylinder)
        q2.subImage.imageRect={{0,0},{2020,1136}};cyl.subImage.imageRect=q2.subImage.imageRect;q2.subImage.swapchain=unknown;
        CHECK(count_subrect_layers(end,sizes)==1);                                           // unknown chain
        end.layerCount=0;CHECK(count_subrect_layers(end,sizes)==0);
    }
    if(failures) return 1;
    std::puts("xr_layer_pose_check passed");return 0;
}
