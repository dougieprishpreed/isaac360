#ifndef GENERIC_NATIVE_PICKUP_H
#define GENERIC_NATIVE_PICKUP_H
#include "native_room_space.h"
//Room-spawn type5 identity. The actual pickup variant/subtype/choice and native
//collection/reward services remain separate; this owner preserves the selected
//identity and position so rooms with pickups construct without blocking.
struct NativePickup {
    int type,variant,subtype,grid;
    unsigned int seed,frame;
    NativePoint position;
    bool ready,collected;
    NativePickup():type(5),variant(0),subtype(0),grid(-1),seed(0),frame(0),ready(false),collected(false){position.x=position.y=0;}
    bool Begin(int type,int variant,int subtype,int grid,unsigned int seed,unsigned int frame,const NativePoint& position);
    void Swap(NativePickup&);
};
struct NativePickupList {
    NativePickup* entries[64];
    int count;
    NativePickupList():count(0){for(int i=0;i<64;++i)entries[i]=0;}
    void Clear();
};
#endif
