#ifndef GENERIC_NATIVE_SECRET_H
#define GENERIC_NATIVE_SECRET_H
#include "native_layout.h"
struct NativeSecretContext {
    NativePlacementContext placement;int stage,startGridIndex;
    bool configuredType7,configuredType29,held102,held589;
};
enum NativeSecretResult { NSR_OK,NSR_BAD_REQUEST,NSR_MISSING_ROOM,NSR_BAD_DATA,NSR_DESCRIPTOR_FAILED };
struct NativeSecretEntry {int definition,topologyIndex,layoutIndex,selectionCalls,selectorDraws;unsigned int seed;bool fallback;};
struct NativeSecretReport {int requested,placed;unsigned int entrySeed,localState;NativeSecretEntry entries[3];};
//006ef0c0: source metadata doors, not graph doors; already selected boss,
//super-secret and secret rooms block all their applicable adjacent cells.
bool NativeSecretBlockedCells(const NativeLevelLayout&,const NativeSecretContext&,unsigned char blocked[169]);
//006f25b5..006f273c uses the retained006efeb4 seed, with index1 between
//requests. It never draws from the floor layout owner.
NativeSecretResult NativePlaceSecretRooms(NativeTopology&,NativeRoomPool&,NativeLevelLayout&,
    unsigned int retainedSeed,const NativeSecretContext&,NativeSecretReport*);
//006f27e0..006f289a clones the SAME retained seed with shift index71.
NativeSecretResult NativePlaceType29(NativeTopology&,NativeRoomPool&,NativeLevelLayout&,
    unsigned int retainedSeed,const NativeSecretContext&,NativeSecretReport*);
#endif
