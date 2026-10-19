#pragma once
#include <cmath>
#include <cstring>
#include <utility>
#include "ui_clip_projection.h"

// Pure compensation math. No hooks, process access, material selection, or
// assumption that changing matMV is safe for every material/lighting variant.
namespace ui_material_clip_math {
struct Result {
    float mv[16]{};
    float projection[16]{};
    float screenProjection[16]{};
    float planes[16]{};
    double mvpError=0,screenError=0;
};
inline constexpr double AbsoluteTolerance=2e-6;
inline constexpr double RelativeTolerance=2e-5;

namespace detail {
inline bool Finite(const float* values,unsigned count) {
    if(!values)return false;
    for(unsigned i=0;i<count;++i)if(!std::isfinite(values[i]))return false;
    return true;
}
inline void Affine(const float* input,float* output) {
    std::memcpy(output,input,16*sizeof(float));
    output[3]=output[7]=output[11]=0;output[15]=1;
}
inline bool Inverse(const float* matrix,double* output) {
    double a[4][8]{};
    for(unsigned r=0;r<4;++r){for(unsigned c=0;c<4;++c)a[r][c]=matrix[4*r+c];a[r][4+r]=1;}
    for(unsigned c=0;c<4;++c){
        unsigned pivot=c;
        for(unsigned r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
        if(!std::isfinite(a[pivot][c]) || std::abs(a[pivot][c])<1e-12)return false;
        for(unsigned j=0;j<8;++j)std::swap(a[c][j],a[pivot][j]);
        const double divisor=a[c][c];for(double& value:a[c])value/=divisor;
        for(unsigned r=0;r<4;++r)if(r!=c){const double factor=a[r][c];for(unsigned j=0;j<8;++j)a[r][j]-=factor*a[c][j];}
    }
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        const double value=a[r][c+4];if(!std::isfinite(value))return false;output[4*r+c]=value;
    }
    return true;
}
inline bool Compensate(const double* inverse,const float* combined,const float* projection,float* output) {
    double transfer[16]{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k)
        transfer[4*r+c]+=inverse[4*r+k]*combined[4*k+c];
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        double value=0;for(unsigned k=0;k<4;++k)value+=transfer[4*r+k]*projection[4*k+c];
        if(!std::isfinite(value))return false;output[4*r+c]=static_cast<float>(value);
        if(!std::isfinite(output[4*r+c]))return false;
    }
    return true;
}
// Match the actual binder SSE addition tree at 0x1a826ae..0x1a826d6:
// (a3*b3+a1*b1)+(a2*b2+a0*b0). A double-only or differently associated
// equality check would miss rounding in the actual submitted constants.
inline bool Product(const float* a,const float* b,float* output) {
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        const float first=a[4*r+3]*b[12+c]+a[4*r+1]*b[4+c];
        const float second=a[4*r+2]*b[8+c]+a[4*r]*b[c];
        const float value=first+second;
        if(!std::isfinite(value))return false;output[4*r+c]=value;
    }
    return true;
}
inline bool Preserved(const float* originalMv,const float* originalProjection,
                      const float* candidateMv,const float* candidateProjection,double& error) {
    float before[16],after[16];
    if(!Product(originalMv,originalProjection,before) || !Product(candidateMv,candidateProjection,after))return false;
    error=0;
    for(unsigned i=0;i<16;++i){
        const double difference=std::abs(double(before[i])-after[i]);
        const double scale=std::fmax(std::abs(double(before[i])),std::abs(double(after[i])));
        if(difference>error)error=difference;
        if(difference>AbsoluteTolerance+RelativeTolerance*scale)return false;
    }
    return true;
}
}

// Row-vector convention: C=B*P*A, original MV=K*C for an affine local K.
// MV'=affine(MV)=K*affine(C), D'=inverse(affine(C))*C*D.
// The final float products must still match, including the separate screen D0.
// Empty clips and unsupported/ill-conditioned inputs are rejected. The caller
// must preserve native behavior on false; output is untouched on every failure.
inline bool Compute(const float* mv,const float* combined,const float* projection,
                    const float* screenProjection,const float* rectangle,Result& output) {
    if(!detail::Finite(mv,16) || !detail::Finite(combined,16) ||
       !detail::Finite(projection,16) || !detail::Finite(screenProjection,16) ||
       !detail::Finite(rectangle,4) || rectangle[2]<=0 || rectangle[3]<=0)return false;
    Result candidate;float affineCombined[16];double inverse[16];
    detail::Affine(mv,candidate.mv);detail::Affine(combined,affineCombined);
    if(!detail::Inverse(affineCombined,inverse) ||
       !detail::Compensate(inverse,combined,projection,candidate.projection) ||
       !detail::Compensate(inverse,combined,screenProjection,candidate.screenProjection) ||
       !ui_clip_projection::RectPlanes(affineCombined,rectangle,candidate.planes) ||
       !detail::Preserved(mv,projection,candidate.mv,candidate.projection,candidate.mvpError) ||
       !detail::Preserved(mv,screenProjection,candidate.mv,candidate.screenProjection,candidate.screenError))return false;
    output=candidate;return true;
}
}
