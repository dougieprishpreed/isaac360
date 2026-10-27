#ifndef GENERIC_NATIVE_ROOM_SETUP_H
#define GENERIC_NATIVE_ROOM_SETUP_H
#include "native_rng.h"
#include "../content/room_record.h"
//007a0c40..007a0de7 preliminary-pass actions. Other identities in this
//interval are real spawn candidates, not grounds for rejecting the pass.
struct NativeRoomMarkers {
    unsigned int flags;
    bool waterPits,noWaterPits,water,noWater,native45,native37;
    bool directionSet;
    int direction;
};
void NativeApplyRoomMarker(const RoomRecordOption&,NativeRoomMarkers*);
//Prepared normal backdrops.xml, inherited through the original resource rules.
//A missing backdrop is an unresolved input, never an invented mode0.
bool NativeBackdropWaterPitsMode(int backdrop,int* mode);
struct NativeRoomSetupContext {
    int backdrop,configStage;
    bool drownedWaterDisabled;
    unsigned int levelFlags;
};
struct NativeRoomSetupState {
    unsigned int flags,waterSeed,styleState;
    int waterDraws,styleDraws;
    float waterAmount;
    bool firstVisit,shopVisit;
    NativeRoomMarkers markers;
};
//007a0ce8..007a10cf. Local seeds only: descriptor seeds and caller owners
//are immutable. Visit increment/clear detection occur later in native Init.
//Shop/profile awards are reported for that caller, not silently applied here.
bool NativeSetupRoom(unsigned int roomType,unsigned int descriptorFlags,unsigned int visitedCount,
    const NativeRoomSeeds&,const NativeRoomMarkers&,const NativeRoomSetupContext&,NativeRoomSetupState*);
#endif
