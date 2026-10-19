#pragma once
#include <cmath>
#include <cstring>
#include <utility>
namespace ui_clip_projection {
// Game plane storage is (normal.xyz, distance), not homogeneous (normal, -distance).
// Row-vector positions use p' = p*M, so homogeneous plane columns use q' = inverse(M)*q.
inline bool RectPlanes(const float* matrix,const float* rect,float* output) {
    double a[4][8]{};
    for(unsigned r=0;r<4;++r){
        if(!std::isfinite(rect[r]))return false;
        for(unsigned c=0;c<4;++c){if(!std::isfinite(matrix[4*r+c]))return false;a[r][c]=matrix[4*r+c];}
        a[r][r+4]=1;
    }
    if(rect[2]<0 || rect[3]<0)return false;
    for(unsigned c=0;c<4;++c){
        unsigned pivot=c;
        for(unsigned r=c+1;r<4;++r)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
        if(std::abs(a[pivot][c])<1e-12)return false;
        for(unsigned j=0;j<8;++j)std::swap(a[c][j],a[pivot][j]);
        double divisor=a[c][c];for(double& v:a[c])v/=divisor;
        for(unsigned r=0;r<4;++r)if(r!=c){double f=a[r][c];for(unsigned j=0;j<8;++j)a[r][j]-=f*a[c][j];}
    }
    const double q[4][4]={{0,1,0,-double(rect[1])},{1,0,0,-double(rect[0])},
        {0,-1,0,double(rect[1])+rect[3]},{-1,0,0,double(rect[0])+rect[2]}};
    float result[16];
    for(unsigned plane=0;plane<4;++plane)for(unsigned r=0;r<4;++r){
        double value=0;for(unsigned c=0;c<4;++c)value+=a[r][c+4]*q[plane][c];
        if(r==3)value=-value;
        if(!std::isfinite(value)||std::abs(value)>1e10)return false;
        result[plane*4+r]=static_cast<float>(value);
    }
    memcpy(output,result,sizeof(result));return true;
}
}
