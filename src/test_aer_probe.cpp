#include "aer_probe.h"
#include <cstdio>
#define CHECK(x) do {if(!(x)) {printf("FAIL %d\n",__LINE__);return 1;}}while(0)
int main() {
    aer_probe::State s;CHECK(s.Read(true,1)==0);CHECK(s.Start(100));CHECK(!s.Start(101));
#ifdef FH4VR_AER_DEFERRED
    float expected[]={0,0,-.032f,-.032f,.032f,-.032f,.032f,.032f,-.032f,.032f};
#else
    float expected[]={0,0,-.032f,.032f,-.032f,.032f,-.032f,.032f};
#endif
    for(unsigned p=1;p<=aer_probe::PhaseCount;++p) {
        CHECK(s.Read(true,100+p)==p);CHECK(s.Read(false,100+p)==0);CHECK(aer_probe::Eye(p)==expected[p]);
        CHECK(s.Advance(p,100+p)==(p<aer_probe::PhaseCount));
    }
    CHECK(s.Read(true,110)==0);CHECK(!s.Start(111));
    aer_probe::State t;CHECK(t.Start(100));CHECK(t.Read(true,10100)==0);CHECK(!t.Advance(1,10100));
    CHECK(t.phase.load()==0);
    aer_probe::State u;CHECK(u.Start(100));u.Stop();CHECK(!u.Advance(1,101));CHECK(u.phase.load()==0);
    puts("PASS: center/L/R phases, one-shot activation, timeout, focus gating, cancellation");
}
