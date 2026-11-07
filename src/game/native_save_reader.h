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
    struct Grid {
        int type,variant,state,count;
        unsigned int seed,auxiliary,tail;
        Grid():type(0),variant(0),state(0),count(0),seed(0),auxiliary(0),tail(0){}
    };
    //Every 90c0c0 common entity record is retained, including identities that
    //this bounded Continue implementation cannot reconstruct yet. fields[]
    //uses the writer's byte offsets as array indexes.
    struct Entity {
        unsigned int fields[0x60];
        Entity();
    };
    struct Pickup {
        int type,variant,subtype,grid;
        unsigned int currentSeed,initialSeed;
        unsigned int native46c,native478,native480,native484,native27c,native474,native476;
        unsigned int native4cc,native470,native34,native38,native360,native4e0;
        unsigned int native47c,nativeExtraCount,nativeChildCount;
        float x,y,nativeD8;
    };
    struct SavedRoom {
        int grid,safeGrid,listIndex,dimension,definitionStage,definitionType,definitionVariant;
        unsigned int displayFlags,visitedCount,flags,native48,clearEventCount;
        std::vector<Grid> grids;
        std::vector<Entity> entities;
        std::vector<Pickup> pickups;
        SavedRoom():grid(0),safeGrid(0),listIndex(0),dimension(0),definitionStage(0),definitionType(0),
            definitionVariant(0),displayFlags(0),visitedCount(0),flags(0),
            native48(0),clearEventCount(0){}
    };
    bool valid,hasPlayer,hasPools,hasCurrentRoom,greed;
    unsigned int seed;
    //0/1 ordinary/hard. Greed remains identified by its saved metadata and
    //takes modes2/3 at the shared Continue caller.
    int nativeMode,playerId,stage,stageType,currentGrid,dimension,roomDefinition,roomDefinitionType,roomDefinitionVariant,roomCount,greedWave,greedWaveCount,greedBossWave;
    int redHearts,maxRedHearts,soulHearts,blackHeartMask;
    int keys,bombs,coins;
    std::vector<int> collectibles;
    std::vector<unsigned char> trinkets;
    std::vector<Effect> effects;
    std::vector<SavedRoom> savedRooms;
    std::vector<Entity> currentRoomEntities;
    std::vector<Pickup> currentRoomPickups;
    int currentSavedRoomIndex;
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
