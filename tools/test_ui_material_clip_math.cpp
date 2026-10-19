#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include "../src/ui_material_clip_math.h"

namespace m=ui_material_clip_math;
using Matrix=std::array<float,16>;
static Matrix Identity(){Matrix x{};for(unsigned i=0;i<4;++i)x[5*i]=1;return x;}
static Matrix Multiply(const Matrix& a,const Matrix& b){Matrix out;assert(m::detail::Product(a.data(),b.data(),out.data()));return out;}
static Matrix Rotation(float yaw,float pitch,float roll){
    auto y=Identity(),p=Identity(),r=Identity();
    y[0]=y[10]=std::cos(yaw);y[2]=std::sin(yaw);y[8]=-std::sin(yaw);
    p[5]=p[10]=std::cos(pitch);p[6]=std::sin(pitch);p[9]=-std::sin(pitch);
    r[0]=r[5]=std::cos(roll);r[1]=std::sin(roll);r[4]=-std::sin(roll);
    return Multiply(Multiply(y,p),r);
}
static Matrix Projection(float width,float height,float yaw,float pitch,float roll,float sx,float sy){
    Matrix p=Identity();p[0]=p[10]=2/height;p[5]=-2/height;p[12]=-width/height;p[13]=1;
    auto out=p,basis=Rotation(yaw,pitch,roll);float hh=.932615316f,hw=hh*width/height;
    for(unsigned row=0;row<4;++row){float position[3]{};
        for(unsigned j=0;j<3;++j)position[j]=p[4*row]*hw*basis[j]+p[4*row+1]*hh*basis[4+j]+p[4*row+3]*2*basis[8+j];
        out[4*row]=position[0]/(2*1.27994163f*width/height)+sx*position[2]-.016f*p[4*row+3];
        out[4*row+1]=position[1]/(2*1.27994163f)+sy*position[2];out[4*row+3]=position[2]/2;
    }
    return out;
}
static Matrix CapturedA(){auto a=Identity();a[14]=4.167492866516113f;return a;}
static Matrix CapturedD(){return {4.167492866516113f,0,0,0,0,4.167492866516113f,0,0,0,0,1.0000399351119995f,1,0,0,-.3000119924545288f,0};}
static unsigned checks=0;
static double worstMvp=0,worstScreen=0;
static void CheckPlanes(const Matrix& local,const m::Result& out,const float* rectangle){
    const float points[][4]={{0,0,0,1},{1,0,0,1},{1,1,0,1},{0,1,0,1},{.37f,.68f,.11f,1}};
    for(const auto& p:points){double src[4]{},q[4]{};
        for(unsigned c=0;c<4;++c)for(unsigned k=0;k<4;++k){src[c]+=double(p[k])*local[4*k+c];q[c]+=double(p[k])*out.mv[4*k+c];}
        assert(std::abs(q[3]-1)<1e-6);
        const double expected[]={src[1]-rectangle[1],src[0]-rectangle[0],rectangle[1]+rectangle[3]-src[1],rectangle[0]+rectangle[2]-src[0]};
        for(unsigned i=0;i<4;++i){double actual=-out.planes[4*i+3];for(unsigned j=0;j<3;++j)actual+=q[j]/q[3]*out.planes[4*i+j];
            assert(std::abs(actual-expected[i])<=.002+2e-5*std::abs(expected[i]));++checks;}
    }
}
static void RequireSuccess(const Matrix& local,const Matrix& c,const Matrix& d,const Matrix& d0,const float* rectangle){
    const auto mv=Multiply(local,c);m::Result out;
    if(!m::Compute(mv.data(),c.data(),d.data(),d0.data(),rectangle,out)){puts("FAIL: finite supported affine-local fixture rejected");std::abort();}
    assert(out.mv[3]==0 && out.mv[7]==0 && out.mv[11]==0 && out.mv[15]==1);
    worstMvp=std::fmax(worstMvp,out.mvpError);worstScreen=std::fmax(worstScreen,out.screenError);
    CheckPlanes(local,out,rectangle);++checks;
}
static void Deterministic(){
    const float rect[]={0,288,714,90};auto scale=Identity();scale[0]=714;scale[5]=90;scale[10]=.0001f;
    auto local=Identity();local[13]=288;
    auto b=Identity();b[0]=b[5]=1.71816671f;b[10]=1.6520834f;b[12]=967.658264f;b[13]=1027.4967f;
    const auto d=CapturedD();auto d0=d;d0[0]*=.87f;d0[5]*=1.13f;d0[8]=.023f;d0[9]=-.041f;
    for(float z:{0.f,1247.99011f,1249.69714f})for(float yaw:{-.65f,0.f,.65f}){
        local[14]=z;auto c=Multiply(Multiply(b,Projection(3172,3172,yaw,.17f,-.09f,.031f,-.019f)),CapturedA());
        RequireSuccess(Multiply(scale,local),c,d,d0,rect);
    }
    // Unmodified affine projection must leave both downstream projections intact.
    auto c=Identity();c[0]=.002f;c[5]=-.003f;c[10]=.001f;c[12]=-1;c[13]=1;c[14]=4.16749287f;
    m::Result out;auto mv=Multiply(scale,c);assert(m::Compute(mv.data(),c.data(),d.data(),d0.data(),rect,out));
    assert(!std::memcmp(out.mv,mv.data(),64));
    for(unsigned i=0;i<16;++i){assert(std::abs(out.projection[i]-d[i])<1e-6f);assert(std::abs(out.screenProjection[i]-d0[i])<1e-6f);}
    ++checks;puts("PASS: actual captured A/D, real nonzero local Z depths, asymmetric projections and native affine baseline");
}
static void Random(){
    std::mt19937 rng(0x33092);std::uniform_real_distribution<float> unit(0,1);
    auto sample=[&](float lo,float hi){return lo+(hi-lo)*unit(rng);};
    for(unsigned i=0;i<2000;++i){
        auto scale=Identity();scale[0]=sample(20,900);scale[5]=sample(20,700);scale[10]=.0001f;
        auto local=Rotation(sample(-.15f,.15f),sample(-.1f,.1f),sample(-.25f,.25f));local[12]=sample(-100,1500);local[13]=sample(-100,1500);local[14]=sample(-100,1500);
        auto b=Identity();b[0]=sample(.6f,2);b[5]=sample(.6f,2);b[10]=sample(.8f,1.8f);b[12]=sample(-100,1000);b[13]=sample(-100,1000);
        auto c=Multiply(Multiply(b,Projection(sample(1800,4200),sample(1800,4200),sample(-.65f,.65f),sample(-.4f,.4f),sample(-.2f,.2f),sample(-.05f,.05f),sample(-.05f,.05f))),CapturedA());
        auto d=CapturedD(),d0=d;d0[0]*=sample(.5f,2);d0[5]*=sample(.5f,2);d0[8]=sample(-.2f,.2f);d0[9]=sample(-.2f,.2f);
        float rectangle[]={sample(-100,1500),sample(-100,1500),sample(5,1000),sample(5,1000)};
        RequireSuccess(Multiply(scale,local),c,d,d0,rectangle);
    }
    puts("PASS: 2,000 random affine local transforms, rotations, depth and asymmetric head projections");
}
static void Reject(const float* mv,const float* c,const float* d,const float* d0,const float* r){
    m::Result out;std::memset(&out,0xa5,sizeof(out));unsigned char before[sizeof(out)];std::memcpy(before,&out,sizeof(out));
    assert(!m::Compute(mv,c,d,d0,r,out));assert(!std::memcmp(before,&out,sizeof(out)));++checks;
}
static void Invalid(){
    auto id=Identity(),d=CapturedD();float rect[]={0,0,100,80};
    Reject(nullptr,id.data(),d.data(),d.data(),rect);Reject(id.data(),nullptr,d.data(),d.data(),rect);
    Reject(id.data(),id.data(),nullptr,d.data(),rect);Reject(id.data(),id.data(),d.data(),nullptr,rect);Reject(id.data(),id.data(),d.data(),d.data(),nullptr);
    for(unsigned target=0;target<4;++target)for(unsigned j=0;j<16;++j)for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}){
        Matrix value[4]={id,id,d,d};value[target][j]=bad;Reject(value[0].data(),value[1].data(),value[2].data(),value[3].data(),rect);
    }
    for(unsigned j=0;j<4;++j){float r[4];std::memcpy(r,rect,16);r[j]=std::numeric_limits<float>::quiet_NaN();Reject(id.data(),id.data(),d.data(),d.data(),r);}
    for(unsigned j:{2u,3u})for(float bad:{0.f,-1.f}){float r[4];std::memcpy(r,rect,16);r[j]=bad;Reject(id.data(),id.data(),d.data(),d.data(),r);}
    auto singular=id;singular[5]=0;Reject(id.data(),singular.data(),d.data(),d.data(),rect);
    auto nearSingular=id;nearSingular[0]=1e-15f;Reject(id.data(),nearSingular.data(),d.data(),d.data(),rect);
    // Inconsistent/projective local input breaks the compensation identity.
    auto wrong=id;wrong[3]=.3f;Reject(wrong.data(),id.data(),d.data(),d.data(),rect);
    auto huge=id;huge[0]=std::numeric_limits<float>::max();Reject(huge.data(),id.data(),d.data(),d.data(),rect);
    puts("PASS: null/nonfinite, singular, nonpositive clips, projective-local mismatch, overflow and untouched output");
}
// Optional plain-float real-packet fixture: count, then MV,C,D,D0,rect per record.
static void CapturedFile(const char* name){
    std::ifstream in(name);assert(in);unsigned count=0;in>>count;assert(count>0);unsigned accepted=0,empty=0;
    for(unsigned n=0;n<count;++n){Matrix mv,c,d,d0;float rect[4];for(auto ptr:{mv.data(),c.data(),d.data(),d0.data()})for(unsigned i=0;i<16;++i)in>>ptr[i];for(float& value:rect)in>>value;assert(in);
        if(rect[2]<=0 || rect[3]<=0){Reject(mv.data(),c.data(),d.data(),d0.data(),rect);++empty;continue;}
        m::Result out;if(!m::Compute(mv.data(),c.data(),d.data(),d0.data(),rect,out)){printf("FAIL: real packet %u rejected\n",n);std::abort();}
        worstMvp=std::fmax(worstMvp,out.mvpError);worstScreen=std::fmax(worstScreen,out.screenError);++accepted;++checks;
    }
    printf("PASS: real packets=%u accepted=%u intentional_empty_rejected=%u\n",count,accepted,empty);
}
int main(int argc,char** argv){Deterministic();Random();Invalid();if(argc>1)CapturedFile(argv[1]);
    printf("PASS ui_material_clip_math checks=%u worst_float_MVP_error=%.9g worst_float_screen_error=%.9g\n",checks,worstMvp,worstScreen);}
