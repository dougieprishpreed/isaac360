#include "native_persistence.h"
#include "native_world.h"
#include "native_room_effects.h"
#include <string.h>
namespace {
unsigned int FloatBits(float value){unsigned int n;memcpy(&n,&value,4);return n;}
void Grid(NativePersistentFields& f,int type,int variant,int state,unsigned int count,unsigned int seed,unsigned int auxiliary,bool initialized,unsigned int tail){
    //Native grid+4..23, not the host layout. Initialization count at+10
    //is incremented by6c573d/79c77f/79c96a AFTER virtual Init.
    f.Put(0,type);f.Put(4,variant);f.Put(8,state);f.Put(12,count);f.Put(16,seed);f.Put(20,auxiliary);f.Put(24,initialized?1:0,1);
    f.Unwritten(25,3);f.Put(28,tail);
}
bool Ambient(const NativeRoomEffect& effect,NativePersistentFields& f){
    const NativeEntityBody& b=effect.body;
    if(!effect.ready||!b.ready||b.type!=1000||(b.variant!=21&&b.variant!=33&&b.variant!=68)||b.parentToken||b.childToken)return false;
    //79d7b6..838's actual initial record. Nullable reference wrappers are
    //logical empty owners; their host addresses are never saved.
    for(unsigned int at=0;at<0x50;at+=4)f.Put(at,0);
    f.Put(0x18,0xffffffffU);f.Put(0x1c,0xffffffffU);f.Put(0x44,0x42700000);f.Put(0x48,0x3f800000);
    f.Put(0x50,1,1);f.Unwritten(0x51,3);
    for(unsigned int at=0x54;at<0x64;at+=4)f.Put(at,0);
    f.Unwritten(0x64,4); //No fresh common-record store in79d7b6..838.
    f.Put(0,b.type);f.Put(4,b.variant);f.Put(8,b.subtype);
    f.Put(0x28,FloatBits(b.position.x));f.Put(0x2c,FloatBits(b.position.y));
    f.Put(0x34,b.initialSeed);f.Put(0x38,b.random.state);f.Put(0x3c,b.native34);f.Put(0x40,b.native38);f.Put(0x20,b.frameCount,2);
    //79cf73's special effect payload applies to116/120/159/39. These three
    //admitted ambient variants use only the common record above.
    return true;
}
}
bool NativeRoomPersistent::CaptureInitial(const NativeRoomState& room,const NativeWorldResident& live,const NativePlacedRoom& descriptor,unsigned int* next){
    if(next)*next=0x79d610;
    if(room.native6f60>0||!descriptor.visitedCount||descriptor.gridIndex==-3||!(descriptor.flags&1)||
       !live.effects||!live.doors||!live.gridPostConstructed||room.native11ec||room.geometry.cells<1)return false;
    //79d700 clears the actual descriptor's saved entity vector. Every cell
    //gets its live grid record or79d73a's empty record, in native cell order.
    std::vector<NativePersistentFields> newGrids(defaultGridCount),newEntities;
    for(unsigned int cell=0;cell<defaultGridCount;++cell)Grid(newGrids[cell],0,0,0,0,0x16a9de81U,0,false,0x16a9de81U);
    for(int cell=0;cell<room.geometry.cells;++cell){NativePersistentFields& f=newGrids[cell];bool found=false;
        for(int slot=0;slot<8;++slot)if((live.doors->attached&(1U<<slot))&&live.doors->actors[slot].grid==cell){
            const NativeDoorActor& d=live.doors->actors[slot];Grid(f,d.type,d.variant,d.state,d.initializations,d.seed,d.auxiliary,d.initialized,0x16a9de81U);found=true;break;}
        if(found)continue;
        for(int i=0;i<room.staticGridCount;++i)if(room.staticGrids[i].grid==cell){const NativeStaticGrid& g=room.staticGrids[i];
            Grid(f,g.type,g.variant,g.state,g.initializations,g.seed,g.native18,g.initialized,g.native20);found=true;break;}
        if(found)continue;
        for(int i=0;i<room.decorationCount;++i)if(room.decorations[i].cell==cell){const NativeDecoration& d=room.decorations[i];
            Grid(f,1,d.variant,0,1,d.initialSeed,0,true,0x16a9de81U);found=true;break;}
        if(found)continue;
        if(room.collision.occupied[cell]){if(next)*next=0x79d72c;return false;}
        Grid(f,0,0,0,0,0x16a9de81U,0,false,0x16a9de81U);
    }
    //The room's actor order is registration order: first player0, then the
    //spawned ambient actors.79ca10 excludes player1; clear=true admits all
    //three ambient variants. Removed/no-persist/child filters precede it.
    for(unsigned int i=0;i<live.effects->entities.size();++i){const NativeRoomEffect& e=*live.effects->entities[i];
        if(e.body.removed||(e.body.flagsHigh&0x20)||e.body.childToken)continue;
        NativePersistentFields f;if(!Ambient(e,f)){if(next)*next=0x79cb60;return false;}newEntities.push_back(f);}
    grids.swap(newGrids);entities.swap(newEntities);captured=true;if(next)*next=0;return true;
}
bool NativeWorld::CaptureInitialRoom(unsigned int* next){
    const NativePlacedRoom* descriptor=CurrentDescriptor();NativeRoomState* room=Current();
    if(!descriptor||!room)return false;NativeWorldResident& live=resident[currentList];
    NativeRoomPersistent value;if(!value.CaptureInitial(*room,live,*descriptor,next))return false;
    if(!live.persistent)live.persistent=new NativeRoomPersistent;*live.persistent=value;return true;
}
