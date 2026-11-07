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
    //007cc890's room actor container starts at+1218. Common registration
    //007e5820 through00414bc0 increments the +a8 view (room+12c0) for each
    //actor whose virtual+3c participates;007cc7e0 applies the matching
    //decrement. The reached80/29 actors use this owner path.
    int native12c0; //+a8 pending registrations;00414bc0 increments,00414fxx commits.
    int native12c4; //+ac:0041578a..a2 = +a8 + active participants, then +a8=0.
    int native7130; //799033 initializes -1; reached ordinary actor path leaves it idle.
    //7b1cc0 owns descriptor+44 bit0 and descriptor+4a's event count. Keep the
    //selection result with the live room so ticks/reentry cannot reroll it.
    bool rewardResolved;
    int rewardVariant,rewardSubtype;
    bool bossDead; //Set by the shared actor finalizer before the clear gate.
    int list9Count; //Count of represented room+720c list9 component owners.
    int native1210; //7a2cc3 music-intensity state.
    bool clear; //Room-clear owner; written only by the shared native clear transaction.
    bool encounter; //A resolved actor set owns clear state.
    bool particlesEnabled; //7971dc, Room1798+8;8d8470's initial early return.
    unsigned int native7628[8]; //797840.. constructors; optional door effect references.
    //Boss-room trapdoor. Grid type9000 variant0 is the room-record owner;
    //state1 is the opening animation, state2 is usable. trapdoorCell is the
    //native spawn cell for the reached room record.
    int trapdoorState,trapdoorTimer,trapdoorCell;
    float trapdoorX,trapdoorY;
};
#endif
