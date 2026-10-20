#include "right_eye_probe.h"
#include <cstdio>
int main(){
    right_eye_probe::Gate gate;
    if(gate.Active(true,10)||gate.Active(false,11)||!gate.Active(true,12))return 1;
    if(!gate.Active(true,5011)||gate.Active(true,5012)||gate.Active(true,6000))return 2;
    if(gate.Active(false,6001)||!gate.Active(true,6002)||gate.Active(true,6001))return 3;
    struct Bounds{float u0,v0,u1,v1;};
    Bounds b[2]={{.1f,.2f,.4f,.8f},{.6f,.15f,.9f,.85f}};
    right_eye_probe::Apply(false,b);if(b[0].u0!=.1f||b[1].u0!=.6f)return 4;
    right_eye_probe::Apply(true,b);
    if(b[0].u0!=b[1].u0||b[0].v0!=b[1].v0||b[0].u1!=b[1].u1||b[0].v1!=b[1].v1)return 5;
    gate={};if(gate.Active(true,7000))return 6;
    puts("PASS duplicate-right bounds; inactive preservation; release/rearm; 5-second expiry; restart guard");
}
