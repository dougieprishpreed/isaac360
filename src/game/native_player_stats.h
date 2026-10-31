#ifndef GENERIC_NATIVE_PLAYER_STATS_H
#define GENERIC_NATIVE_PLAYER_STATS_H
#include "native_room_space.h"
class NativePlayerState;
class NativePlayerCreation;
class NativePlayerManager;
class NativePlayerStartup;
class NativePlayerSprites;
class NativeEntityBody;
//Live evaluated cache, separate from7087b0's constructor snapshot.
class NativePlayerStats {
public:
    bool ready,flying;
    unsigned int dirty,tearFlags[4];
    bool weapons[16];
    float fireDelay,shotSpeed,damage,baseDamage,tearHeight,fallingSpeed,fallingAcceleration,range,moveSpeed,luck;
    //Character-owned base values captured by the fresh evaluation. The item
    //cache operator recomputes the live values from this baseline plus the
    //numeric item table and the maintained temporary (pill/card/active) layer.
    bool cacheBaseReady;
    float cacheBaseFireDelay,cacheBaseShotSpeed,cacheBaseRange,cacheBaseMoveSpeed,cacheBaseLuck;
    float cacheBaseTearHeight,cacheBaseFallingAcceleration;
    //Temporary modifiers that are not owned by collectibles.
    float bonusTears,bonusFireRate,bonusDamage,bonusRange,bonusMoveSpeed,bonusShotSpeed,bonusLuck;
    float colors[3][11]; //Tear, laser, and body colors; full native11-float owners.
    NativePoint size[3];
    int native17c0,native17c4;
    NativePlayerStats();
    bool EvaluateFresh(NativePlayerState&,const NativePlayerCreation&,NativePlayerManager&,const NativePlayerStartup&,NativeEntityBody&,NativePlayerSprites&,unsigned int* next);
    //00716840's numeric cache result for the owned collectible inventory.
    //Only numeric passive stats are applied here; conditional/trigger items
    //remain explicit gaps rather than unconditional approximations.
    bool RecomputeCache(const NativePlayerState&);
    void FinalizeDerived();
    void Swap(NativePlayerStats&);
    unsigned int StateHash() const;
};
#endif
