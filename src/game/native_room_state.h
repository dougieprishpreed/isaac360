#ifndef GENERIC_NATIVE_ROOM_STATE_H
#define GENERIC_NATIVE_ROOM_STATE_H
#include "native_room_grid.h"
#include "native_room_space.h"
#include "native_static_grid.h"
//Prepared room component state. No movement/combat/reward approximations.
//The native actor/grid factories and complete Room::Init remain separate work.
struct NativeDecoration {int cell,animation;unsigned int seed;};
struct NativeRoomState {
    unsigned int type,variant,shape;
    NativeRoomGrid geometry;
    NativeRoomSpace space;
    NativeRoomCollision collision;
    bool doors[8]; //Native slots; no route-specific remapping.
    NativeDecoration decorations[kNativeRoomGridLimit];
    int decorationCount,decorationTick;
    NativeStaticGrid staticGrids[kNativeRoomGridLimit];
    int staticGridCount;
};
#endif
