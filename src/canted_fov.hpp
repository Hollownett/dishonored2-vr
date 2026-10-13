#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace d2vr {
// Canted symmetric eye frustum.
// The engine must keep a symmetric frustum: its per-view constant cb_globalviewinfos is
// (tan(hfov/2), tan(vfov/2), ...) (cbuffer filler 0x358a74), so screen-space passes that rebuild view
// rays from it assume the optical centre is the image centre. The enclosing symmetric frustum of an
// asymmetric headset eye wastes pixels. Turning the eye view toward the middle of its optical FOV and
// fitting a symmetric frustum around the four optical corner rays covers the same view with fewer pixels.
// Inputs are the per-eye FOV the OpenXR runtime reports (xrLocateViews), so any headset works; a symmetric
// eye gets no turn. (Example, Quest 3 via Virtual Desktop: 79% of the enclosing symmetric frustum's pixels.)
// The OpenXR projection layer then carries the turned pose and the symmetric FOV (the runtime reprojects).
// Eye space is OpenXR's: +x right, +y up, -z forward. Angles in radians.
struct CantedFov {
    float yaw{},pitch{};                // turn of the view: +yaw toward +x (right), +pitch toward +y (up)
    std::array<float,4> rotation{0,0,0,1}; // x,y,z,w; eye-local, compose as eye_orientation * rotation
    float half_x{},half_y{};             // symmetric half angles of the canted frustum
};
inline std::optional<CantedFov> canted_fov(float left,float right,float up,float down) {
    for(float a:{left,right,up,down}) if(!std::isfinite(a) || std::abs(a)>=1.5f) return std::nullopt;
    if(left>=right || down>=up) return std::nullopt;
    // Symmetric tangent extents (tx, ty) around the four optical corner rays for a view turned by yaw/pitch.
    // R = Ry(-yaw) * Rx(pitch); canted-frame direction = R^T * eye direction. Area tx*ty is the pixel cost.
    auto fit=[&](double yaw,double pitch,double& tx,double& ty) {
        const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
        tx=ty=0;
        for(double ax:{static_cast<double>(left),static_cast<double>(right)}) for(double ay:{static_cast<double>(down),static_cast<double>(up)}) {
            const double x=std::tan(ax),y=std::tan(ay),z=-1;
            const double x1=cy*x+sy*z,z1=-sy*x+cy*z,y2=cp*y+sp*z1,z2=-sp*y+cp*z1;
            if(z2>=0) return false;
            tx=std::max(tx,std::abs(x1/-z2));ty=std::max(ty,std::abs(y2/-z2));
        }
        return true;
    };
    // Coarse-to-fine search from the middle of the optical angles (that guess alone costs 2.5% for Quest 3).
    double yaw=(static_cast<double>(left)+right)*.5,pitch=(static_cast<double>(up)+down)*.5,tx{},ty{};
    if(!fit(yaw,pitch,tx,ty)) return std::nullopt;
    for(double step=.02;step>2e-5;step*=.25) {
        const double y0=yaw,p0=pitch;
        for(int i=-4;i<=4;++i) for(int j=-4;j<=4;++j) {
            double x,y;
            if(fit(y0+i*step,p0+j*step,x,y) && x*y<tx*ty-1e-12) {yaw=y0+i*step;pitch=p0+j*step;tx=x;ty=y;}
        }
    }
    CantedFov c;c.yaw=static_cast<float>(yaw);c.pitch=static_cast<float>(pitch);
    c.half_x=static_cast<float>(std::atan(tx));c.half_y=static_cast<float>(std::atan(ty));
    // q = qy(-yaw) * qx(pitch)
    const double hy=-yaw*.5,hp=pitch*.5;
    const double qyy=std::sin(hy),qyw=std::cos(hy),qxx=std::sin(hp),qxw=std::cos(hp);
    c.rotation={static_cast<float>(qyw*qxx),static_cast<float>(qyy*qxw),static_cast<float>(-qyy*qxx),static_cast<float>(qyw*qxw)};
    return c;
}
// Pixels per eye that keep the runtime's recommended tangent-space density (given for the optical
// frustum) inside the canted frustum, rounded up to even sizes.
inline std::array<unsigned,2> canted_size(unsigned recommended_w,unsigned recommended_h,float left,float right,float up,float down,const CantedFov& c) {
    const double w=recommended_w*2*std::tan(c.half_x)/(std::tan(right)-std::tan(left));
    const double h=recommended_h*2*std::tan(c.half_y)/(std::tan(up)-std::tan(down));
    return {static_cast<unsigned>(std::ceil(w*.5))*2,static_cast<unsigned>(std::ceil(h*.5))*2};
}
} // namespace d2vr
