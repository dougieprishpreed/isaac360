#ifndef GENERIC_NATIVE_SAVE_READER_H
#define GENERIC_NATIVE_SAVE_READER_H
#include "native_item_pools.h"
#include <vector>
//Read-only summary of the current-state GSR0142 writer layout. The summary is
//not a second save format: every field below is a value emitted by the native
//writer's current-state capture. Unknown/optional sections are skipped only by
//their verified encoded length.
struct NativeSaveSummary {
    struct Effect {int type,id,count,timer;};
    bool valid,hasPlayer,hasPools,greed;
    unsigned int seed;
    int playerId,stage,stageType,currentGrid,dimension,roomDefinition,roomCount,greedWave,greedWaveCount,greedBossWave;
    int redHearts,maxRedHearts,soulHearts,blackHeartMask;
    int keys,bombs,coins;
    std::vector<int> collectibles;
    std::vector<unsigned char> trinkets;
    std::vector<Effect> effects;
    int activeItems[4],heldTrinkets[2],heldCard,heldPill;
    bool hasExtras,hasTriggerCache;
    float positionX,positionY,fireDelay,damage,moveSpeed,luck;
    float permDamageBonus,permFireRateBonus,permRangeBonus,permShotSpeedBonus,permSpeedBonus,permLuckBonus;
    float bloodOathDamage,bloodOathSpeed,roomDamageBonus,roomSpeedBonus;
    int damageTakenThisFloor,purityAura;bool purityActive,crownOfLightDisabled;
    NativeItemPoolSnapshot pools;
    NativeSaveSummary();
};
bool NativeReadSaveSummary(const char* path,NativeSaveSummary& output);
#endif
