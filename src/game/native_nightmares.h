#ifndef GENERIC_NATIVE_NIGHTMARES_H
#define GENERIC_NATIVE_NIGHTMARES_H
#include "native_rng.h"
#include <vector>
//Main+212a4. The normal New Run wrapper resets stage backdrop IDs and
//reshuffles this original XML filename order BEFORE calling6b43f0.
//This owns that state; cinematic Sprite/drawing/transition updates are separate.
class NativeNightmares {
public:
    bool reset;std::vector<int> order;unsigned int stageBackdrops[14];
    NativeNightmares():reset(false){}
    bool ResetForNewRun(NativeMt&);
    bool SetInitialStage(int stageType,int nativeMode);
    void Swap(NativeNightmares&);
};
const char* NativePreparedNightmareFilename(unsigned int ordinal);
#endif
