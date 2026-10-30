#include "native_pickup.h"
#include "native_rng.h"
bool NativePickup::Begin(int t,int v,int sub,int cell,unsigned int selectedSeed,unsigned int selectedFrame,const NativePoint& at){
    //Identity words are preserved bit-for-bit; the spawn plan already bounds
    //the cell and owns the seed. Do not turn a valid selected record into a
    //construction error because a future pickup table gives it a signed range.
    if(t!=5||cell<0)return false;
    type=t;variant=v;subtype=sub;grid=cell;seed=selectedSeed;frame=selectedFrame;position=at;ready=true;collected=false;return true;
}
void NativePickup::Swap(NativePickup& o){
    int i=type;type=o.type;o.type=i;i=variant;variant=o.variant;o.variant=i;i=subtype;subtype=o.subtype;o.subtype=i;
    i=grid;grid=o.grid;o.grid=i;unsigned int u=seed;seed=o.seed;o.seed=u;u=frame;frame=o.frame;o.frame=u;
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
