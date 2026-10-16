#pragma once
#include "native_world_trace.hpp"
#include <cmath>
#include <cstdint>
#include <optional>

namespace d2vr {
class NativeDoorContact {
public:
    struct Surface {
        std::uintptr_t entity{};
        std::uint32_t full_body{};
        std::uint32_t shape_key{};
        Vec3 point{};
        Vec3 normal{};
        double depth{};
    };
    static std::optional<Surface> select(const WorldTraceQueryResult& query,std::uint32_t source_body,
                                         std::uintptr_t target_entity,double max_depth=.08) noexcept {
        if(!query.complete || !query.initial || !query.regular || !valid_body(source_body) || !target_entity ||
           !std::isfinite(max_depth) || max_depth<=0.0 || max_depth>1.0 ||
           query.initial->count>query.initial->hits.size() || query.regular->count>query.regular->hits.size())return {};
        const WorldTraceInitialHit* selected{};
        const NativeInitialContact* selected_contact{};
        std::uint32_t selected_body{},selected_key{};
        Vec3 reference_normal{};
        bool target_found=false;
        for(unsigned index=0;index<query.initial->count;++index) {
            const auto& hit=query.initial->hits[index];
            const auto& contact=hit.contact;
            const double magnitude=length(contact.normal);
            if(contact.source_body!=source_body || !valid_body(contact.body) || contact.body==source_body || !finite(contact.point) || !finite(contact.normal) ||
               !std::isfinite(contact.signed_separation) || contact.signed_separation>0.f || !contact.normal_valid ||
               !std::isfinite(magnitude) || std::abs(magnitude-1.0)>.02)return {};
            if(hit.entity!=target_entity) {
                if(contact.signed_separation<0.f)return {};
                continue;
            }
            const double depth=-static_cast<double>(contact.signed_separation);
            if(depth==0.0)continue;
            if(depth>max_depth)return {};
            if(!target_found) {
                selected=&hit;selected_contact=&contact;selected_body=contact.body;selected_key=contact.shape_key;
                reference_normal=contact.normal;target_found=true;
                continue;
            }
            if(contact.body!=selected_body || contact.shape_key!=selected_key || dot(reference_normal,contact.normal)<.98)return {};
            if(contact.signed_separation<selected_contact->signed_separation) {selected=&hit;selected_contact=&contact;}
        }
        if(!target_found || !selected || !selected_contact)return {};
        bool matching_regular=false;
        for(unsigned index=0;index<query.regular->count;++index) {
            const auto& hit=query.regular->hits[index];
            const auto magnitude=length(hit.normal);
            if(!finite(hit.point) || !finite(hit.normal) || !std::isfinite(hit.fraction) || hit.fraction<0.0 || hit.fraction>1.0 ||
               !valid_body(hit.body_handle) || hit.body_handle==source_body || hit.body!=(hit.body_handle&0xffffffu) ||
               !std::isfinite(magnitude))return {};
            const bool matching_initial=has_initial(query,source_body,hit.entity,hit.body_handle,hit.shape_key);
            if(hit.normal_valid) {
                if(std::abs(magnitude-1.0)>.02)return {};
            } else if(hit.fraction>1e-6 || !matching_initial)return {};
            if(hit.entity==target_entity && hit.body_handle==selected_body && hit.shape_key==selected_key)matching_regular=true;
        }
        if(!matching_regular)return {};
        return Surface{selected->entity,selected_body,selected_key,selected_contact->point,selected_contact->normal,
                       -static_cast<double>(selected_contact->signed_separation)};
    }
private:
    static bool valid_body(std::uint32_t body) noexcept {return body!=0xffffffffu && (body&0xffffffu)!=0xffffffu;}
    static bool has_initial(const WorldTraceQueryResult& query,std::uint32_t source_body,std::uintptr_t entity,
                            std::uint32_t full_body,std::uint32_t shape_key) noexcept {
        for(unsigned index=0;index<query.initial->count;++index) {
            const auto& hit=query.initial->hits[index];
            if(hit.entity==entity && hit.contact.source_body==source_body && hit.contact.body==full_body && hit.contact.shape_key==shape_key &&
               hit.contact.normal_valid && hit.contact.signed_separation<=0.f && finite(hit.contact.point) && finite(hit.contact.normal) &&
               std::isfinite(hit.contact.signed_separation))return true;
        }
        return false;
    }
};
}
