#include "stereo_trial.h"
#include <cstdio>
int main() {
    using namespace stereo_trial;
    ToggleKey key;
    if(key.Press(true,true)||key.Press(true,false)||!key.Press(true,true)||key.Press(true,true))return 4;
    if(key.Press(true,false)||!key.Press(true,true))return 5;
    if(key.Press(false,true)||key.Press(true,true)||key.Press(true,false)||!key.Press(true,true))return 6;
    deadline.store(1000);
    for(uint64_t n=0;n<=80;++n) {
        command.store(n);
        float expected=n ? (((n-1)/holdFrames)%2 ? .032f:-.032f):0;
        if(Offset(true,999)!=expected || Offset(false,999)!=(RenderAllowed(false)?expected:0) || Offset(true,1000)!=0) return 1;
        int eye=CapturedEye(n+captureLag);
        if(eye!=(n && n%holdFrames==0 ? static_cast<int>((n/holdFrames-1)%2):-1)) return 2;
        if(eye>=0) {
            // All commands in this held interval agree; covers measured one-frame lag.
            for(uint64_t lag=0;lag<holdFrames;++lag) {
                command.store(n-lag);
                if(Offset(true,999)!=(eye ? .032f:-.032f)) return 3;
            }
        }
    }
    deadline.store(0);command.store(1);
    if(Offset(true,3600000)!=-.032f||Offset(false,3600000)!=(RenderAllowed(false)?-.032f:0))return 7;
    command.store(0);if(Offset(true,3600000)!=0||Offset(false,3600000)!=0)return 8;
    // Simulate a game camera callback running while compositor handoff blocks.
    // It must see the NEXT eye, not the just-captured view's old command.
    for(uint64_t next=1;next<20;++next){
        command=next-1;
        const int result=PublishThenFinish(next,[&]{
            return command==next && Offset(true,100)==(((next-1)/holdFrames)%2 ? .032f:-.032f) ? -17:0;
        });
        if(result!=-17)return 13; // Preserve failure result for caller's stop path.
    }
    // Explicit warmup/capture sequence, independent of the formula above.
    const int oneFrame[]={-1,0,1,0,1,0,1,0,1};
    const int twoFrames[]={-1,-1,0,-1,1,-1,0,-1,1};
    for(unsigned n=0;n<9;++n)if(CapturedEye(n+captureLag)!=(holdFrames==1?oneFrame[n]:twoFrames[n]))return 9;
    for(uint64_t present=0;present<100;++present){
        const auto image=ImageCommand(present);
        if(present<=captureLag && (image || CapturedEye(present)!=-1 || CaptureGeneration(present)))return 10;
        if(image && CaptureGeneration(present)!=(image-1)/(holdFrames*2)+1)return 11;
        // Model the observed one-frame pipeline independently of eye labels.
        if(captureLag==1 && CapturedEye(present)>=0){
            command=present-1;
            if(Offset(true,100)!=(CapturedEye(present)? .032f:-.032f))return 12;
        }
    }
    printf("PASS hold=%llu scheduling; repeat toggle edges; held/focus guards; unlimited and stopped offsets. Pixel/pose pairing still requires game validation.\n",holdFrames);
}
