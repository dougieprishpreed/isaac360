#ifndef GENERIC_NATIVE_ROOM_STATE_H
#define GENERIC_NATIVE_ROOM_STATE_H
#include "native_room_grid.h"
#include "native_room_space.h"
#include "native_static_grid.h"
//Prepared room component state. No movement/combat/reward approximations.
//The native actor/grid factories and complete Room::Init remain separate work.
struct NativeDecoration {int cell,animation,variant;unsigned int seed,initialSeed;};
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
    int native6f60,native11ec; //7a0b19/7a10e7, consumed by79d610.
    int native1210; //7a2cc3 music-intensity state.
    bool particlesEnabled; //7971dc, Room1798+8;8d8470's initial early return.
    unsigned int native7628[8]; //797840.. constructors; optional door effect references.
};
#endif
