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
    float colors[3][11]; //Tear, laser, and body colors; full native11-float owners.
    NativePoint size[3];
    int native17c0,native17c4;
    NativePlayerStats();
    bool EvaluateFresh(NativePlayerState&,const NativePlayerCreation&,NativePlayerManager&,const NativePlayerStartup&,NativeEntityBody&,NativePlayerSprites&,unsigned int* next);
    void Swap(NativePlayerStats&);
    unsigned int StateHash() const;
};
#endif
