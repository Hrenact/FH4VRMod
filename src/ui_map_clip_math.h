#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>

// Pure map clip-coordinate conversion. The caller must keep the native MVP
// byte-for-byte: map MV is also projective before VR, so affine(MV) is invalid.
namespace ui_map_clip_math {
struct Result {
    float mv[16]{};
    float planes[16]{};
    double maxWRelativeError=0;
    double maxClipRelativeError=0;
};
namespace detail {
inline bool Inverse(const double* source,double* result) {
    double a[4][8]{},scale=0;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        const double v=source[4*r+c];if(!std::isfinite(v))return false;
        a[r][c]=v;scale=(std::max)(scale,std::abs(v));a[r][c+4]=r==c?1:0;
    }
    if(!(scale>0))return false;
    for(unsigned c=0;c<4;++c){
        unsigned pivot=c;for(unsigned r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
        if(std::abs(a[pivot][c])<=scale*1e-12)return false;
        if(pivot!=c)for(unsigned j=0;j<8;++j)std::swap(a[pivot][j],a[c][j]);
        const double denominator=a[c][c];for(double& v:a[c])v/=denominator;
        for(unsigned r=0;r<4;++r)if(r!=c){const double factor=a[r][c];
            for(unsigned j=0;j<8;++j)a[r][j]-=factor*a[c][j];}
    }
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        const double v=a[r][c+4];if(!std::isfinite(v))return false;result[4*r+c]=v;
    }
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        double left=0,right=0;for(unsigned k=0;k<4;++k){
            left+=source[4*r+k]*result[4*k+c];right+=result[4*r+k]*source[4*k+c];}
        const double expected=r==c?1:0;
        if(!std::isfinite(left) || !std::isfinite(right) ||
           std::abs(left-expected)>1e-8 || std::abs(right-expected)>1e-8)return false;
    }
    return true;
}
inline void Product(const double* a,const double* b,double* out) {
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){
        double value=0;for(unsigned k=0;k<4;++k)value+=a[4*r+k]*b[4*k+c];out[4*r+c]=value;
    }
}
}
inline bool Compute(const float* mv,const float* combined,const float* rectangle,Result& output) {
    if(!mv || !combined || !rectangle)return false;
    double m[16],c[16],affine[16],inverse[16],affineInverse[16],k[16];
    for(unsigned i=0;i<16;++i){m[i]=mv[i];c[i]=affine[i]=combined[i];
        if(!std::isfinite(m[i]) || !std::isfinite(c[i]))return false;}
    for(unsigned i=0;i<4;++i)if(!std::isfinite(rectangle[i]))return false;
    if(!(rectangle[2]>0) || !(rectangle[3]>0))return false;
    affine[3]=affine[7]=affine[11]=0;affine[15]=1;
    if(!detail::Inverse(c,inverse) || !detail::Inverse(affine,affineInverse))return false;
    detail::Product(m,inverse,k);
    Result candidate;std::memcpy(candidate.mv,mv,sizeof(candidate.mv));
    // M*inv(C)*aff(C) has the exact original xyz columns. Only w is replaced
    // with K.w; preserve xyz bytes instead of multiplying them back and rounding.
    for(unsigned r=0;r<4;++r){
        const double w=k[4*r+3];candidate.mv[4*r+3]=static_cast<float>(w);
        if(!std::isfinite(w) || !std::isfinite(candidate.mv[4*r+3]))return false;
        const double error=std::abs(static_cast<double>(candidate.mv[4*r+3])-w);
        candidate.maxWRelativeError=(std::max)(candidate.maxWRelativeError,error/(1+std::abs(w)));
        if(error>2e-6+2e-6*std::abs(w))return false;
    }
    const double x=rectangle[0],y=rectangle[1],width=rectangle[2],height=rectangle[3];
    // Homogeneous source half-spaces; shader stores (normal,-homogeneous.w).
    const double source[4][4]={{0,1,0,-y},{1,0,0,-x},{0,-1,0,y+height},{-1,0,0,x+width}};
    for(unsigned p=0;p<4;++p)for(unsigned r=0;r<4;++r){
        double value=0;for(unsigned j=0;j<4;++j)value+=affineInverse[4*r+j]*source[p][j];
        if(r==3)value=-value;candidate.planes[4*p+r]=static_cast<float>(value);
        if(!std::isfinite(value) || !std::isfinite(candidate.planes[4*p+r]))return false;
    }
    // Validate the float shader constants as a polynomial in all four input
    // coordinates, not only the vertices selected by a test fixture. Numerator
    // must remain K*sourcePlane, while the separate check preserves denominator K.w.
    for(unsigned p=0;p<4;++p)for(unsigned r=0;r<4;++r){
        double desired=0,actual=0;
        for(unsigned j=0;j<4;++j){desired+=k[4*r+j]*source[p][j];
            actual+=static_cast<double>(candidate.mv[4*r+j])*candidate.planes[4*p+j]*(j==3?-1:1);}
        if(!std::isfinite(desired) || !std::isfinite(actual))return false;
        const double error=std::abs(actual-desired);
        candidate.maxClipRelativeError=(std::max)(candidate.maxClipRelativeError,error/(1+std::abs(desired)));
        if(error>2e-4+2e-5*std::abs(desired))return false;
    }
    output=candidate;return true;
}
} // namespace ui_map_clip_math
