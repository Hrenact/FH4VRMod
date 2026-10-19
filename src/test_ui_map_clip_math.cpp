#include "ui_map_clip_math.h"
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace math=ui_map_clip_math;
using Matrix=std::array<float,16>;
static unsigned checks=0;
#define REQUIRE(value) do{++checks;if(!(value)){std::printf("FAIL line %d: %s\n",__LINE__,#value);return false;}}while(0)
static Matrix Identity(){Matrix m{};for(unsigned i=0;i<4;++i)m[5*i]=1;return m;}
static Matrix Product(const Matrix& a,const Matrix& b){Matrix c{};
    for(unsigned r=0;r<4;++r)for(unsigned col=0;col<4;++col)
        c[4*r+col]=(a[4*r+3]*b[12+col]+a[4*r+1]*b[4+col])+(a[4*r+2]*b[8+col]+a[4*r]*b[col]);
    return c;}
static std::array<double,4> Point(const std::array<double,4>& p,const float* m){std::array<double,4> q{};
    for(unsigned j=0;j<4;++j)for(unsigned i=0;i<4;++i)q[j]+=p[i]*m[4*i+j];return q;}
static bool FailUnchanged(const Matrix& m,const Matrix& c,const float* rect){
    math::Result out;std::memset(&out,0x3f,sizeof(out));auto before=out;
    REQUIRE(!math::Compute(m.data(),c.data(),rect,out));REQUIRE(!std::memcmp(&out,&before,sizeof(out)));return true;
}
static bool Basic(){
    const float rect[4]={3,5,708,372};auto c=Identity(),m=Identity();m[7]=-1;m[15]=650;
    math::Result out;REQUIRE(math::Compute(m.data(),c.data(),rect,out));REQUIRE(!std::memcmp(m.data(),out.mv,64));
    REQUIRE(out.planes[0]==0 && out.planes[1]==1 && out.planes[3]==5);
    REQUIRE(out.planes[4]==1 && out.planes[7]==3);REQUIRE(out.planes[9]==-1 && out.planes[11]==-377);
    REQUIRE(out.planes[12]==-1 && out.planes[15]==-711);
    auto sentinel=out;REQUIRE(!math::Compute(nullptr,c.data(),rect,out));REQUIRE(!std::memcmp(&out,&sentinel,sizeof(out)));
    REQUIRE(!math::Compute(m.data(),nullptr,rect,out));REQUIRE(!std::memcmp(&out,&sentinel,sizeof(out)));
    REQUIRE(!math::Compute(m.data(),c.data(),nullptr,out));REQUIRE(!std::memcmp(&out,&sentinel,sizeof(out)));
    for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}){
        for(unsigned i=0;i<16;++i){auto bm=m,bc=c;bm[i]=bad;bc[i]=bad;REQUIRE(FailUnchanged(bm,c,rect));REQUIRE(FailUnchanged(m,bc,rect));}
        for(unsigned i=0;i<4;++i){float r[4];std::memcpy(r,rect,sizeof(r));r[i]=bad;REQUIRE(FailUnchanged(m,c,r));}}
    for(unsigned dimension:{2u,3u})for(float bad:{0.f,-1.f}){float r[4];std::memcpy(r,rect,sizeof(r));r[dimension]=bad;REQUIRE(FailUnchanged(m,c,r));}
    auto singular=c;singular[0]=0;REQUIRE(FailUnchanged(m,singular,rect));
    auto affineSingular=c;affineSingular[0]=affineSingular[15]=0;affineSingular[3]=affineSingular[12]=1;
    REQUIRE(FailUnchanged(m,affineSingular,rect));
    auto nearSingular=c;nearSingular[0]=1e-20f;REQUIRE(FailUnchanged(m,nearSingular,rect));
    // Partial-pivot inversion alone accepts this near-dependent pair of rows.
    // The inverse's two-sided identity residual exposes the unstable result.
    Matrix inverseResidual={
        -1.1001814604f,-1.5611560345f,1.2173522711f,-.0036405483261f,
        -.7073293328f,-2.4010648727f,.3211746514f,2.0372440815f,
        -.6639692783f,-.3339187801f,.9346319437f,.3058075607f,
        -1.1001814604f,-1.5611560345f,1.2173522711f,-.00364055344835f};
    auto residualMap=Identity();residualMap[0]=residualMap[5]=residualMap[10]=.1f;residualMap[7]=-1;
    REQUIRE(FailUnchanged(residualMap,inverseResidual,rect));
    float overflow[4]={FLT_MAX,0,FLT_MAX,1};REQUIRE(FailUnchanged(m,c,overflow));
    auto singularMap=m;for(unsigned i=0;i<4;++i)singularMap[4*i+2]=0;
    REQUIRE(math::Compute(singularMap.data(),c.data(),rect,out));REQUIRE(!std::memcmp(singularMap.data(),out.mv,64));
    puts("PASS identity C preserves native projective/singular map K; invalid/null/degenerate inputs leave output unchanged");return true;
}
static bool Random(){
    std::mt19937 rng(0x49064);std::uniform_real_distribution<float> v(-1.f,1.f);unsigned accepted=0,rejected=0;
    for(unsigned n=0;n<4000;++n){
        auto k=Identity();k[0]=150+30*v(rng);k[5]=160+20*v(rng);k[10]=.02f;
        k[1]=20*v(rng);k[4]=20*v(rng);k[8]=10*v(rng);k[9]=10*v(rng);
        k[3]=.2f*v(rng);k[7]=-1;k[11]=.2f*v(rng);k[15]=650+50*v(rng);
        k[12]=700*v(rng);k[13]=500*v(rng);k[14]=10*v(rng);
        auto c=Identity();c[0]=.001f*(1+.25f*v(rng));c[5]=-.001f*(1+.25f*v(rng));c[10]=.001f;
        c[1]=.0002f*v(rng);c[4]=.0002f*v(rng);c[3]=.0005f*v(rng);c[7]=.0005f*v(rng);
        c[12]=.5f*v(rng);c[13]=.5f*v(rng);c[14]=4.16f;c[15]=1+.2f*v(rng);
        auto m=Product(k,c);const float rect[4]={3,5,708,372};math::Result out;
        if(!math::Compute(m.data(),c.data(),rect,out)){++rejected;continue;}++accepted;
        for(unsigned i=0;i<16;++i)if(i%4!=3)REQUIRE(!std::memcmp(m.data()+i,out.mv+i,4));
        REQUIRE(out.mv[15]!=1 && out.mv[7]!=0);
        for(unsigned i=0;i<4;++i)REQUIRE(std::abs(out.mv[4*i+3]-k[4*i+3])<.002*(1+std::abs(k[4*i+3])));
        for(unsigned point=0;point<4;++point){std::array<double,4> p={10*v(rng),10*v(rng),10*v(rng),1};
            auto native=Point(p,k.data()),candidate=Point(p,out.mv);REQUIRE(native[3]>500 && candidate[3]>500);
            const double expected[4]={native[1]/native[3]-5,native[0]/native[3]-3,377-native[1]/native[3],711-native[0]/native[3]};
            for(unsigned plane=0;plane<4;++plane){double actual=-out.planes[4*plane+3];
                for(unsigned j=0;j<3;++j)actual+=candidate[j]/candidate[3]*out.planes[4*plane+j];
                REQUIRE(std::abs(actual-expected[plane])<.003);}}
    }
    REQUIRE(accepted>3900);std::printf("PASS randomized native projective K: accepted=%u conservative_rejected=%u\n",accepted,rejected);return true;
}
static bool Fixtures(const char* path){
    std::ifstream f(path,std::ios::binary);auto read=[&](void* dst,size_t size){return bool(f.read(reinterpret_cast<char*>(dst),size));};
    char magic[8];uint32_t count=0;REQUIRE(read(magic,8)&&!std::memcmp(magic,"FH4MAPR2",8));REQUIRE(read(&count,4)&&count<=4096);
    double wError=0,clipError=0;unsigned accepted=0;
    for(uint32_t i=0;i<count;++i){char name[96];Matrix m,mvp,c,k,expected,source,old,full,planes;float screen[2],vertices[18];
        float rect[4];REQUIRE(read(name,96)&&read(rect,sizeof(rect)));for(auto p:{&m,&mvp,&c,&k,&expected,&source,&old,&full,&planes})REQUIRE(read(p->data(),64));
        REQUIRE(read(screen,8)&&read(vertices,sizeof(vertices)));
        math::Result out;REQUIRE(math::Compute(m.data(),c.data(),rect,out));++accepted;
        for(unsigned j=0;j<16;++j)REQUIRE(std::abs(out.mv[j]-expected[j])<1e-5*(1+std::abs(expected[j])));
        for(unsigned j=0;j<16;++j)REQUIRE(std::abs(out.planes[j]-planes[j])<1e-5*(1+std::abs(planes[j])));
        wError=(std::max)(wError,out.maxWRelativeError);clipError=(std::max)(clipError,out.maxClipRelativeError);
    }
    std::printf("PASS actual 49064 fixture %u/%u; maxWRelativeError=%.9g maxClipRelativeError=%.9g\n",accepted,count,wError,clipError);return true;
}
int main(int argc,char** argv){if(!Basic()||!Random())return 1;if(argc>1&&!Fixtures(argv[1]))return 1;
    std::printf("ui_map_clip_math PASS %u checks\n",checks);return 0;}
