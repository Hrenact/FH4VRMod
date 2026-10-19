#include "eye_offset.h"
#include <cstdio>
#include <limits>
#define CHECK(x) do {if(!(x)) {printf("FAIL %d\n",__LINE__);return 1;}}while(0)
int main() {
    float center[16]={0,0,-1,0, 0,1,0,0, 1,0,0,0, -12,3,7,1};
    float left[16],right[16];memcpy(left,center,sizeof(center));memcpy(right,center,sizeof(center));
    CHECK(ApplyEyeOffset(left,-.032f));CHECK(ApplyEyeOffset(right,.032f));
    for(int i=0;i<16;++i) if(i!=12) CHECK(left[i]==center[i] && right[i]==center[i]);
    CHECK(std::abs((left[12]-right[12])-.064f)<.00001f);
    // Perspective disparity scales inversely with depth, with parallel camera axes.
    float nearDisparity=(left[12]-right[12])/1,farDisparity=(left[12]-right[12])/20;
    CHECK(nearDisparity>farDisparity*19.99f);
    CHECK(!ApplyEyeOffset(left,.1f));CHECK(!ApplyEyeOffset(left,std::numeric_limits<float>::quiet_NaN()));
    float invalid[16];memcpy(invalid,center,sizeof(center));invalid[3]=1;
    float saved[16];memcpy(saved,invalid,sizeof(saved));CHECK(!ApplyEyeOffset(invalid,.032f));CHECK(memcmp(saved,invalid,sizeof(saved))==0);
    double pos[4]={10000.5,-200.25,300.75,1},old[4];memcpy(old,pos,sizeof(pos));
    CHECK(ApplyEyePosition(pos,center,.032f));
    CHECK(pos[0]==old[0] && pos[1]==old[1] && pos[3]==old[3]);
    CHECK(std::abs(pos[2]-old[2]-.032)<1e-8);
    CHECK(ApplyEyePosition(pos,center,-.032f));
    CHECK(std::abs(pos[2]-old[2])<1e-10);
    memcpy(saved,center,sizeof(saved));saved[0]=2;
    CHECK(!ApplyEyePosition(pos,saved,.032f));CHECK(memcmp(pos,old,sizeof(pos))==0);
    EyeOffsetGate gate;
    CHECK(gate.offset(true,true,false,100)==0); // Initial held keys cannot activate.
    CHECK(gate.offset(true,false,false,101)==0);
    CHECK(gate.offset(true,true,false,102)<0);
    CHECK(gate.offset(true,true,false,5102)==0);
    CHECK(gate.offset(true,true,false,5200)==0);
    CHECK(gate.offset(true,false,false,5201)==0);
    CHECK(gate.offset(true,false,true,5202)>0);
    CHECK(gate.offset(false,false,true,5203)==0);
    CHECK(gate.offset(true,false,true,5204)==0);
    CHECK(gate.offset(true,false,false,5205)==0);
    CHECK(gate.offset(true,true,true,5206)==0);
    CHECK(gate.offset(true,true,false,5207)==0);
    puts("PASS: rotated view offsets, parallel axes, disparity sign/depth, invalid inputs, focus and bounded hold gate");
}
