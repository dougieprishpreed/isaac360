#ifndef GENERIC_NATIVE_ENEMY_H
#define GENERIC_NATIVE_ENEMY_H
#include "native_entity_body.h"
struct NativeRoomState;
//Authentic type10 Gaper physical record subset. Movement/AI and presentation
//remain maintained gameplay services, not complete native enemy behavior.
class NativeEnemy {
public:
    NativeEntityBody body;
    bool ready,dead;
    unsigned int seed;
    NativeEnemy();
    bool Begin(int variant,unsigned int seed,const NativePoint& position,unsigned int frame,int stage);
    void Update(const NativeRoomState&,const NativeEntityBody& player);
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
