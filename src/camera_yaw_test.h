#pragma once
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

struct HoldYawGate {
    // 0 requires key release; 1 armed; >=2 is press start tick + 2.
    std::atomic<uint64_t> state{0};
    float angle(bool foreground, bool pressed, uint64_t now) {
        if (!foreground) { state.store(0); return 0; }
        if (!pressed) { state.store(1); return 0; }
        uint64_t start=state.load();
        if (start==1) {
            state.compare_exchange_strong(start,now+2);
            start=state.load();
        }
        if (start<2 || now<start-2 || now-(start-2)>=5000) return 0;
        return 0.0872664626f; // five degrees; one fixed offset, never accumulated.
    }
};

// Row-vector view matrix post-multiplied by a camera-space Y rotation.
// Validate a scratch copy before writing; reject non-rigid / non-affine output.
inline bool ApplyViewYaw(float* matrix,float angle) {
    if (!std::isfinite(angle) || std::abs(angle)>0.09f) return false;
    float copy[16]; memcpy(copy,matrix,sizeof(copy));
    for (float value:copy) if (!std::isfinite(value)) return false;
    if (std::abs(copy[3])>0.001f || std::abs(copy[7])>0.001f ||
        std::abs(copy[11])>0.001f || std::abs(copy[15]-1)>0.001f) return false;
    for (unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) {
        float dot=0;
        for (unsigned k=0;k<3;++k) dot+=copy[k*4+i]*copy[k*4+j];
        if (std::abs(dot-(i==j ? 1.0f : 0.0f))>0.01f) return false;
    }
    const float cosine=std::cos(angle),sine=std::sin(angle);
    for (unsigned row=0;row<4;++row) {
        const float x=copy[row*4],z=copy[row*4+2];
        copy[row*4]=cosine*x-sine*z;
        copy[row*4+2]=sine*x+cosine*z;
    }
    memcpy(matrix,copy,sizeof(copy));
    return true;
}
