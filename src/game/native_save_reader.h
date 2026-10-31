#ifndef GENERIC_NATIVE_SAVE_READER_H
#define GENERIC_NATIVE_SAVE_READER_H
#include "native_item_pools.h"
#include <vector>
//Read-only summary of the current-state GSR0142 writer layout. The summary is
//not a second save format: every field below is a value emitted by the native
//writer's current-state capture. Unknown/optional sections are skipped only by
//their verified encoded length.
struct NativeSaveSummary {
    bool valid,hasPlayer,hasPools;
    unsigned int seed;
    int playerId,stage,stageType,currentGrid,dimension,roomDefinition;
    int redHearts,maxRedHearts,soulHearts,blackHeartMask;
    int keys,bombs,coins;
    std::vector<int> collectibles;
    std::vector<unsigned char> trinkets;
    int activeItems[4],heldTrinkets[2],heldCard,heldPill;
    bool hasExtras;
    float positionX,positionY,fireDelay,damage,moveSpeed,luck;
    NativeItemPoolSnapshot pools;
    NativeSaveSummary();
};
bool NativeReadSaveSummary(const char* path,NativeSaveSummary& output);
#endif
