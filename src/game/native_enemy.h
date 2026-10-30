#ifndef GENERIC_NATIVE_ENEMY_H
#define GENERIC_NATIVE_ENEMY_H
#include "native_entity_body.h"
struct NativeRoomState;
class NativeEnemyShotSystem;
//Prepared enemy bodies for the room actors reached so far. Movement follows
//the original per-type family behavior (walk, erratic fly, shooting pooter,
//charge families); exact native AI timing remains a maintained approximation.
class NativeEnemy {
public:
    NativeEntityBody body;
    bool ready,dead;
    int type;
    unsigned int seed;
    NativeEnemy();
    bool Begin(int type,int variant,unsigned int seed,const NativePoint& position,unsigned int frame,int stage);
    void Update(const NativeRoomState&,const NativeEntityBody& player,NativeEnemyShotSystem* shots=0);
    void Damage(float amount);
    void Swap(NativeEnemy&);
};
struct NativeEnemyList {
    NativeEnemy* entries[64];
    int count;
    NativeEnemyList():count(0){for(int i=0;i<64;++i)entries[i]=0;}
    void Clear();
};
#endif
