#include "native_pickup.h"
#include "native_rng.h"
bool NativePickup::Begin(int t,int v,int sub,int cell,unsigned int selectedSeed,unsigned int selectedFrame,const NativePoint& at){
    //Identity words are preserved bit-for-bit; the spawn plan already bounds
    //the cell and owns the seed. Do not turn a valid selected record into a
    //construction error because a future pickup table gives it a signed range.
    if(t!=5||cell<0)return false;
    type=t;variant=v;subtype=sub;grid=cell;currentSeed=selectedSeed;initialSeed=selectedSeed;
    frame=selectedFrame;position=at;
    //0066e910 initializes34/38 to0.0066f440 copies the selected seed to both
    //32c initial and33c current.006a1900 initializes the pickup words copied by
    //0079cb60; key30 and bomb40 skip its variant100 extra/child branches.
    native46c=0;native478=0;native480=0;native484=0xffffffffU;native27c=0xffffffffU;native474=0;native476=0;
    native4cc=0xffffffffU;native470=0;native34=0;native38=0;native360=0;native4e0=0;
    native47c=1;nativeExtraCount=0;nativeChildCount=0;nativeD8=1.0f;
    ready=true;collected=false;return true;
}
void NativePickup::Morph(){
    //006a4140's flag-off path, constants at009e9adc={1,21,20}. The reached
    //ordinary key/bomb lifecycle never calls Morph; the owner remains ready
    //for an explicitly reached caller without deriving state from frame.
    currentSeed^=currentSeed>>1;
    currentSeed^=currentSeed<<21;
    currentSeed^=currentSeed>>20;
}
void NativePickup::Swap(NativePickup& o){
    int i=type;type=o.type;o.type=i;i=variant;variant=o.variant;o.variant=i;i=subtype;subtype=o.subtype;o.subtype=i;
    i=grid;grid=o.grid;o.grid=i;unsigned int u=currentSeed;currentSeed=o.currentSeed;o.currentSeed=u;
    u=initialSeed;initialSeed=o.initialSeed;o.initialSeed=u;u=frame;frame=o.frame;o.frame=u;
    u=native46c;native46c=o.native46c;o.native46c=u;u=native478;native478=o.native478;o.native478=u;
    u=native480;native480=o.native480;o.native480=u;u=native484;native484=o.native484;o.native484=u;
    u=native27c;native27c=o.native27c;o.native27c=u;
    u=native474;native474=o.native474;o.native474=u;u=native476;native476=o.native476;o.native476=u;
    u=native4cc;native4cc=o.native4cc;o.native4cc=u;u=native470;native470=o.native470;o.native470=u;
    u=native34;native34=o.native34;o.native34=u;u=native38;native38=o.native38;o.native38=u;
    u=native360;native360=o.native360;o.native360=u;u=native4e0;native4e0=o.native4e0;o.native4e0=u;
    u=native47c;native47c=o.native47c;o.native47c=u;u=nativeExtraCount;nativeExtraCount=o.nativeExtraCount;o.nativeExtraCount=u;
    u=nativeChildCount;nativeChildCount=o.nativeChildCount;o.nativeChildCount=u;
    float f=nativeD8;nativeD8=o.nativeD8;o.nativeD8=f;
    NativePoint p=position;position=o.position;o.position=p;bool b=ready;ready=o.ready;o.ready=b;b=collected;collected=o.collected;o.collected=b;
}
int NativePickupVariant(int roomType,unsigned int selectedSeed){
    struct Entry{int variant,weight;};
    Entry table[11];int count=0,total=0;
    if(roomType!=3){table[count].variant=20;table[count++].weight=20;}
    table[count].variant=10;table[count++].weight=15;
    table[count].variant=30;table[count++].weight=15;
    table[count].variant=40;table[count++].weight=15;
    if(roomType!=1&&roomType!=3&&roomType!=4){table[count].variant=50;table[count++].weight=5;table[count].variant=60;table[count++].weight=5;}
    table[count].variant=70;table[count++].weight=4;
    table[count].variant=80;table[count++].weight=4;
    table[count].variant=90;table[count++].weight=2;
    if(roomType!=4){table[count].variant=350;table[count++].weight=1;}
    if(roomType==0){table[count].variant=100;table[count++].weight=1;}
    for(int i=0;i<count;++i)total+=table[i].weight;
    NativeMt mt;mt.Seed(selectedSeed?selectedSeed:1U);
    unsigned int roll=mt.Next()%(unsigned int)total;
    for(int i=0;i<count;++i){if(roll<(unsigned int)table[i].weight)return table[i].variant;roll-=(unsigned int)table[i].weight;}
    return table[count-1].variant;
}
void NativePickupList::Clear(){
    for(int i=0;i<count;++i){delete entries[i];entries[i]=0;}count=0;
    for(int i=0;i<64;++i)entries[i]=0;
}
