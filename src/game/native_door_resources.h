#ifndef GENERIC_NATIVE_DOOR_RESOURCES_H
#define GENERIC_NATIVE_DOOR_RESOURCES_H
#include "native_sprite.h"
#include "native_doors.h"
class NativePlayerState;
struct NativeRoomState;
class NativeEntityBody;
struct NativeDoorVisual {
    NativeSprite main,overlay;
    bool ready,unsupportedTarget;unsigned int nextAddress;
    NativeDoorVisual():ready(false),unsupportedTarget(false),nextAddress(0){}
    //Reached first-chapter assets and native closed state1. Gameplay opening
    //and per-frame door update are separate lifecycle operations.
    bool BindFresh(NativeDoorActor&,int backdrop,unsigned int sourceFlags,unsigned int targetFlags,const NativePlayerState&);
    bool UpdateInitialClosed(NativeDoorActor&,const NativeRoomState&,unsigned int targetFlags,unsigned int effectiveCurses,const NativeEntityBody&,const NativePlayerState&,unsigned int* next);
};
const NativeSpriteAsset* NativePreparedDoorSprite(unsigned int);
const NativeSpriteSheet* NativePreparedDoorSheet(unsigned int);
#endif
