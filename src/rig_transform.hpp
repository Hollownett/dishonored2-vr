#pragma once
#include "game_rig.hpp"
#include "tracked_pose.hpp"

namespace d2vr::rig {
// Exact native boundaries: 0x09b980 packs Havok columns into affine rows;
// 0x25e790 maps joint translation through model scale, axis rows and origin.
// These helpers operate on owned values. They never touch game animation.
inline Vec3 position(const Transform& t) {return {t.f[12],t.f[13],t.f[14]};}
inline Vec3 column(const Transform& t,unsigned i) {return {t.f[i*4],t.f[i*4+1],t.f[i*4+2]};}
inline std::array<float,12> affine_rows(const Transform& t) {
    if(!valid(t)) throw std::invalid_argument("Invalid native joint transform");
    std::array<float,12> result{};
    for(unsigned row=0;row<3;++row) for(unsigned col=0;col<4;++col) result[row*4+col]=t.f[col*4+row];
    return result;
}
struct ModelFrame {
    Vec3 origin;
    std::array<Vec3,3> axes;
    std::array<double,3> scale;
    explicit ModelFrame(const std::array<float,15>& raw) {
        origin={raw[0],raw[1],raw[2]};
        if(!finite(origin)) throw std::invalid_argument("Invalid model origin");
        for(unsigned i=0;i<3;++i) {
            axes[i]={raw[3+i*3],raw[4+i*3],raw[5+i*3]};scale[i]=raw[12+i];
            if(!finite(axes[i]) || !std::isfinite(scale[i]) || scale[i]<1e-4 || scale[i]>16)
                throw std::invalid_argument("Invalid model basis or scale");
            for(unsigned j=0;j<=i;++j) if(std::abs(dot(axes[i],axes[j])-(i==j?1.:0.))>.01)
                throw std::invalid_argument("Model basis is not orthonormal");
        }
        if(dot(cross(axes[0],axes[1]),axes[2])<.99) throw std::invalid_argument("Reflected model basis");
    }
    Vec3 world_point(Vec3 p) const {
        if(!finite(p)) throw std::invalid_argument("Invalid joint point");
        return origin+axes[0]*(p.x*scale[0])+axes[1]*(p.y*scale[1])+axes[2]*(p.z*scale[2]);
    }
    Vec3 model_point(Vec3 p) const {
        if(!finite(p)) throw std::invalid_argument("Invalid world point");
        const auto delta=p-origin;
        return {dot(delta,axes[0])/scale[0],dot(delta,axes[1])/scale[1],dot(delta,axes[2])/scale[2]};
    }
    Vec3 world_direction(Vec3 v) const {return axes[0]*v.x+axes[1]*v.y+axes[2]*v.z;}
    Vec3 model_direction(Vec3 v) const {return {dot(v,axes[0]),dot(v,axes[1]),dot(v,axes[2])};}
};
// Apply one rigid delta to a model-space joint. Descendants use the same delta
// so finger animation and weapon attachment offsets survive wrist retargeting.
// The caller must supply the verified hierarchy; joint-number guesses are not
// enough to choose which native palette entries belong to this subtree.
inline Transform rigid_retarget(const Transform& joint,const Transform& from,const Transform& to) {
    if(!valid(joint) || !valid(from) || !valid(to)) throw std::invalid_argument("Invalid rigid retarget pose");
    const auto rotate_delta=[&](Vec3 v) {
        return column(to,0)*dot(v,column(from,0))+column(to,1)*dot(v,column(from,1))+column(to,2)*dot(v,column(from,2));
    };
    Transform result=joint;
    for(unsigned i=0;i<3;++i) {
        const auto v=rotate_delta(column(joint,i));
        result.f[i*4]=static_cast<float>(v.x);result.f[i*4+1]=static_cast<float>(v.y);result.f[i*4+2]=static_cast<float>(v.z);
    }
    const auto p=position(to)+rotate_delta(position(joint)-position(from));
    result.f[12]=static_cast<float>(p.x);result.f[13]=static_cast<float>(p.y);result.f[14]=static_cast<float>(p.z);
    if(!valid(result)) throw std::invalid_argument("Retarget pose overflow");
    return result;
}
} // namespace d2vr::rig
