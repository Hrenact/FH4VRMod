#include "pose_wait_policy.h"
#include <cstdio>
int main(){PoseWaitPolicy p;
 if(!p.NeedWait(0))return 1;
 // Failed waits must not consume the required initial or post-submit wait.
 if(!p.NeedWait(0))return 2;
 p.WaitSucceeded(0);
 for(unsigned i=0;i<100;++i)if(p.NeedWait(0))return 3;
 if(!p.NeedWait(1))return 4;p.WaitSucceeded(1);
 if(p.NeedWait(1)||!p.NeedWait(2))return 5;
 p={};if(!p.NeedWait(0))return 6;
 puts("PASS initial wait, dropped-pair sampling, post-submit wait, failed-wait retry and reset");}
