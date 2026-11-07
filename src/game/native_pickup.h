#ifndef GENERIC_NATIVE_PICKUP_H
#define GENERIC_NATIVE_PICKUP_H
#include "native_room_space.h"
//Room-spawn type5 state reached by the ordinary key30/bomb40 constructors.
//These are the live owners copied by0079cb60 into the common82-byte entity
//record. Native fields not stored here remain separate unsupported services.
struct NativePickup {
    int type,variant,subtype,grid;
    unsigned int currentSeed,initialSeed,frame;
    NativePoint position;
    //006a1900 and0066e910/0066f440 stores copied by0079cb60. The values are
    //kept explicitly rather than reconstructed in the save writer.
    unsigned int native46c,native478,native480,native484,native27c,native474,native476;
    unsigned int native4cc,native470,native34,native38,native360,native4e0;
    unsigned int native47c,nativeExtraCount,nativeChildCount;
    float nativeD8;
    bool ready,collected;
    NativePickup():type(5),variant(0),subtype(0),grid(-1),currentSeed(0),initialSeed(0),frame(0),
        native46c(0),native478(0),native480(0),native484(0xffffffffU),native27c(0xffffffffU),native474(0),native476(0),
        native4cc(0xffffffffU),native470(0),native34(0),native38(0),native360(0),native4e0(0),
        native47c(1),nativeExtraCount(0),nativeChildCount(0),nativeD8(1.0f),ready(false),collected(false){position.x=position.y=0;}
    bool Begin(int type,int variant,int subtype,int grid,unsigned int seed,unsigned int frame,const NativePoint& position);
    void Morph();
    void Swap(NativePickup&);
};
struct NativePickupList {
    NativePickup* entries[64];
    int count;
    NativePickupList():count(0){for(int i=0;i<64;++i)entries[i]=0;}
    void Clear();
};
//0069f3c0 weighted type5 variant selection for the ordinary room-type table.
//The exact native MT mixing remains an equivalence limit; the variant weights
//and room-type exclusions are transcribed from the saved instructions.
int NativePickupVariant(int roomType,unsigned int seed);
#endif
