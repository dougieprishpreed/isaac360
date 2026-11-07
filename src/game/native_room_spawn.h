#ifndef GENERIC_NATIVE_ROOM_SPAWN_H
#define GENERIC_NATIVE_ROOM_SPAWN_H
#include "native_rng.h"
#include "native_room_grid.h"
#include "native_room_setup.h"
#include "../content/room_record.h"
enum NativeRoomSpawnResult { NRS_OK,NRS_BAD_DATA,NRS_MARKER,NRS_ZERO_SEED,NRS_CAPACITY };
struct NativeSpawnChoice {
    int x,y,grid; bool present;
    unsigned int option,seed;
    RoomRecordOption entry;
};
struct NativeSpawnPlan {
    int count;
    unsigned int preliminaryState,finalState;
    NativeRoomMarkers markers;
    NativeSpawnChoice choices[kNativeRoomGridLimit];
};
// Fresh rooms of all12 prepared shapes, unmodified weights and preliminary
// marker actions. Each pass has its own award-seeded2/7/7 clone. Empty stacks
// still draw. Main actor/grid construction and saved skip cells are separate.
// All prepared records fit448 choices. Capacity errors never truncate a plan.
NativeRoomSpawnResult NativePlanRoomSpawns(const RoomRecord& room,const NativeRoomSeeds& seeds,NativeSpawnPlan* output);
// Ordinary room decoration owned by the backdrop's props animation set. The
// native grid includes the one-cell border. Local5/9/7 seed is
// descriptor.spawn + grid; no room/global owner advances. Count0 means the
// backdrop has no inherited props set and cannot own decoration records.
int NativeDecorationAnimationCount(int backdrop);
bool NativeRoomDecoration(unsigned int spawnSeed,const NativeRoomGrid& geometry,int grid,
                          int backdrop,unsigned int* seed,int* animation);
#endif
