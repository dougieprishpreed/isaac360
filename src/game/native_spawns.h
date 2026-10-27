#ifndef GENERIC_NATIVE_SPAWNS_H
#define GENERIC_NATIVE_SPAWNS_H
#include "../content/room_record.h"
enum NativeSpawnResult { NS_OK,NS_BAD_REQUEST,NS_BAD_WEIGHT,NS_EMPTY,NS_NO_ENTRY };
// Pure RoomConfig::Spawn::PickEntry arithmetic (007d53e0). The caller supplies
// the sample and cached total: this helper consumes NO RNG. Stage/marker/room
// traversal and loader filtering/order are separate, still incomplete behavior.
NativeSpawnResult NativePickSpawn(const RoomRecordStack& stack,float sample,float totalWeight,
    unsigned int* selected,RoomRecordOption* entry=0);
// Ordered float sum of the raw entries, for unmodified prepared stacks. Native
// loader modifications/eligibility must be applied before supplying a cache.
NativeSpawnResult NativeSpawnWeight(const RoomRecordStack& stack,float* totalWeight);
#endif
