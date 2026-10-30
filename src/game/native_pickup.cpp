#include "native_pickup.h"
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
void NativePickupList::Clear(){
    for(int i=0;i<count;++i){delete entries[i];entries[i]=0;}count=0;
    for(int i=0;i<64;++i)entries[i]=0;
}
