#ifndef GENERIC_NATIVE_DOORS_H
#define GENERIC_NATIVE_DOORS_H
#include "native_room_grid.h"
#include "native_rng.h"
//0079faf0 computes every slot, including normally disabled thin/second slots.
//The descriptor's allowed mask determines which actors are actually made.
bool NativeDoorGrid(int shape,const NativeRoomGrid&,int slot,int* grid);
struct NativeDoorActor {
    //Known factory/reset/selection fields only. Animation resources and later
    //door behavior are separate services, not an accepted complete grid actor.
    int grid,slot,direction,type,variant,state,auxiliary;
    int sourceType,targetType,targetGrid,previousState,previousVariant;
    unsigned int seed,initializations,spawnFrame,flags;
    NativeRng local;
    float x,y,rotation,overlayRotation;
    bool initialized,native278,native279,native27a;
};
struct NativeDoorSet {
    unsigned int attached,draws;
    int potentialGrid[8],native76c[kNativeRoomGridLimit]; //Only attached-cell writes are defined here.
    NativeDoorActor actors[8];
};
enum NativeDoorResult { NDR_OK,NDR_BAD_REQUEST,NDR_ALREADY_ATTACHED,NDR_MISSING_TARGET };
//0079c7b0/006cbe50: one global MT value per initialized door, even when a
//saved grid door object is reused. Attached slots return before any draw.
NativeDoorResult NativeCreateDoor(int slot,int grid,unsigned int frame,NativeMt&,NativeDoorSet*,const NativeDoorActor* savedGridDoor=0);
bool NativeInitDoorSet(int shape,const NativeRoomGrid&,unsigned int allowed,unsigned int frame,NativeMt&,NativeDoorSet*);
//006cbe50 reset only; the room's post-construction virtual+4 dispatch is
//407240 for doors and must not repeat this Init or its factory's MT draw.
void NativeResetDoor(NativeDoorActor&);
//Evaluated caller inputs for006cb870, before asset setup006c8140 and the
//animation/interaction services. No default entitlement/inventory predicates.
struct NativeDoorBinding {
    int sourceType,targetType,sourceVariant,targetVariant;
    unsigned int sourceFlags,targetFlags;
    int stage,stageType,nativeMode,currentGrid,targetGrid,dimension;
    int treasureGrid,generatedRoomCount,challenge;
    bool reflectedStage,seedEffect33,room711c;
    bool sourceData,targetData;
};
struct NativeDoorSelection {
    int sourceType,targetType,variant,auxiliary;
    bool openAfterAssetSetup;
};
//Complete integer selection portion of006cb870. It consumes no RNG. Variant
//numbers are native values; selecting one does not implement its interactions.
bool NativeSelectDoor(const NativeDoorBinding&,int existingVariant,int auxiliary,NativeDoorSelection*);
#endif
