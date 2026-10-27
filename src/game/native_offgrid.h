#ifndef GENERIC_NATIVE_OFFGRID_H
#define GENERIC_NATIVE_OFFGRID_H
#include "native_layout.h"
struct NativeOffgridContext {
    int stage,stageType,nativeMode,configStage;
    bool profile152,profile153,profile154,held550;
};
struct NativeOffgridEntry {
    int gridIndex,definition,type,stage,subtype,ownerIndex;
    unsigned int ownerEntry,selectionSeed,ownerFinal;
    NativeRoomResult selectionResult;
};
struct NativeOffgridReport {
    int count,layoutDraws,localDraws,selectorDraws;
    unsigned int entrySeed,finalSeed;
    float bossRolls[2];
    NativeOffgridEntry entries[8];
};
enum NativeOffgridResult { NOG_OK,NOG_BAD_REQUEST,NOG_UNSUPPORTED_CONTEXT,NOG_BAD_DATA };
//006f2a9d..006f33f3, first chapter. Native substitution budget and stage>3
//boss changes are inapplicable; the two floating draws still occur. Negative
//descriptors keep their separate storage and actual seed owner.
NativeOffgridResult NativePrepareOffgridRooms(NativeRoomPool&,NativeRng&,NativeLevelLayout&,
    const NativeOffgridContext&,NativeOffgridReport*);
#endif
