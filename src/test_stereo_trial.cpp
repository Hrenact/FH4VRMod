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
        if(Offset(true,999)!=expected || Offset(false,999)!=0 || Offset(true,1000)!=0) return 1;
        int eye=CapturedEye(n);
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
    if(Offset(true,3600000)!=-.032f||Offset(false,3600000)!=0)return 7;
    command.store(0);if(Offset(true,3600000)!=0)return 8;
    puts("PASS eye scheduling; repeat toggle edges; held/focus guards; unlimited and stopped offsets");
}
