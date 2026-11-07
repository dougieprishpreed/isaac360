#include "native_static_grid.h"
#include <string.h>
int NativeSerializedGridType(int rawType) {
    //0079c30f/0079c33f,0079becf/0079bf09,0079bf16,0079bf30,
    //0079bf4f,0079c27e,c1b3 and the1400 switch entry respectively.
    if(rawType==1000)return 4;
    if(rawType==1001)return 5;
    if(rawType==1002)return 6;
    if(rawType==1008)return 26;
    if(rawType==1010)return 25;
    //Class14 is GRID_POOP (6ced40 loads grid_poop.anm2). Raw1500 is normal
    //poop; 1490/1495/1496/1494/1497 are the forced variants1..5.
    if(rawType==1500||rawType==1490||rawType==1495||rawType==1496||rawType==1494||rawType==1497)return 14;
    if(rawType==1400)return 13;
    //Class21 is the teleporter grid. Its Init7e0310 loads
    //grid_teleporter*.anm2; raw5000/5001 are teleporter variants0/1.
    if(rawType==5000||rawType==5001)return 21;
    //Class24 is GRID_PILLAR, a rock-derived grid.
    if(rawType==1901)return 24;
    if(rawType==1999)return 15;
    return 0;
}
int NativeSerializedGridVariant(int rawType,unsigned int rawVariant) {
    //These classes force the variant during the room-spawn dispatch, so the
    //room record's variant field must not overwrite it.
    if(rawType==1490)return 1;
    if(rawType==1495)return 2;
    if(rawType==1496)return 3;
    if(rawType==1494)return 4;
    if(rawType==1497)return 5;
    if(rawType==1500)return 0;
    if(rawType==5000)return 0;
    if(rawType==5001)return 1;
    if(rawType==1901)return 0;
    return (int)rawVariant;
}
int NativeRawGridType(int serializedType) {
    if(serializedType==4)return 1000;
    if(serializedType==5)return 1001;
    if(serializedType==6)return 1002;
    if(serializedType==26)return 1008;
    if(serializedType==25)return 1010;
    if(serializedType==14)return 1500;
    if(serializedType==13)return 1400;
    if(serializedType==21)return 5000;
    if(serializedType==24)return 1901;
    if(serializedType==15)return 1999;
    return 0;
}
int NativeRawGridTypeForVariant(int serializedType,unsigned int variant) {
    if(serializedType==14){
        if(variant==0)return 1500;
        if(variant==1)return 1490;
        if(variant==2)return 1495;
        if(variant==3)return 1496;
        if(variant==4)return 1494;
        if(variant==5)return 1497;
        return 0;
    }
    if(serializedType==21){
        if(variant==0)return 5000;
        if(variant==1)return 5001;
        return 0;
    }
    if(serializedType==24)return (variant==0||variant==1)?1901:0;
    return NativeRawGridType(serializedType);
}
bool NativeSelectedWall(int roomType,int grid,unsigned int variant,unsigned int subtype,
    unsigned int seed,unsigned int frame,NativeStaticGrid* output) {
    if(!output||roomType==16||roomType<0||grid<0||grid>=kNativeRoomGridLimit||variant>65535||subtype>65535||!seed)return false;
    NativeStaticGrid wall;memset(&wall,0,sizeof(wall));
    wall.grid=grid;wall.type=1999;wall.nativeType=15;wall.variant=(int)variant;
    //006c5670 constructor sentinel survives initialization in native+20.
    //0079c347..380 writes variant, native18=0, and the current selected seed.
    //Subtype is not used for this dispatch (only type23 consumes it).
    wall.seed=seed;wall.native20=0x16a9de81U;
    wall.collisionClass=4; //006d8a74; native76c=1000 is written by the room.
    wall.initializations=1;wall.initialized=true;wall.initializedFrame=frame;
    *output=wall;return true;
}
bool NativePerimeterWall(int roomType,int grid,unsigned int seed,unsigned int frame,NativeStaticGrid* output){
    //79c72a..79c78c shares the same ordinary type15 constructor/Init. Its
    //seed comes from the global MT, not the selected main-pass award clone.
    return NativeSelectedWall(roomType,grid,0,0,seed,frame,output);
}
bool NativeRestoreSerializedGrid(int grid,int serializedType,unsigned int variant,unsigned int state,
    unsigned int count,unsigned int seed,unsigned int tail,unsigned int auxiliary,unsigned int frame,
    NativeStaticGrid* output){
    if(!output||grid<0||grid>=kNativeRoomGridLimit)return false;
    const int raw=NativeRawGridTypeForVariant(serializedType,variant);
    //007a1130 factory/Init classes established by the shared fresh scene
    //construction: ordinary rock1000/1001/1002/1008/1010, poop class14
    //(1500/1490/1495/1496/1494/1497), fireplace1400, statue class21
    //(5000/5001), pillar class24 (1901) and perimeter wall1999. The saved
    //record is authoritative; no new content class is synthesized here.
    if(serializedType!=4&&serializedType!=5&&serializedType!=6&&serializedType!=13&&
       serializedType!=14&&serializedType!=15&&serializedType!=21&&serializedType!=24&&
       serializedType!=25&&serializedType!=26)return false;
    NativeStaticGrid value;memset(&value,0,sizeof(value));
    value.grid=grid;value.type=raw;value.nativeType=serializedType;
    value.variant=(int)variant;value.state=(int)state;value.seed=seed;
    value.native18=auxiliary;value.native20=tail;
    //vtable b2a7d4 virtual+28/6ced40 state0 writes class3 and live cost0x5dc
    //for poop class14. A destroyed poop (state1000) is class0 in the retail
    //GRIDDUMP. Teleporter class21 is walkable; types4/15/24 retain class4.
    const bool destructibleState=(serializedType==6||serializedType==14);
    value.collisionClass=destructibleState?(state==1000?0:3):(serializedType==21?0:4);
    //007a1195 calls virtual+28;007a119d increments native+10;007a11a0
    //marks+native1c initialized;007a11aa writes game+1a2e1c to native+28.
    value.initializations=(int)count+1;value.initialized=true;value.initializedFrame=frame;
    *output=value;return true;
}
