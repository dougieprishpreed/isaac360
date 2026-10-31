#include "native_persistence.h"
#include "native_world.h"
#include "normal_run.h"
#include "native_nightmares.h"
#include "native_player_manager.h"
#include "native_room_effects.h"
#include <string.h>

namespace {
void Copy(NativePersistentFields& target,unsigned int to,const NativePersistentFields& source,unsigned int from,unsigned int count){
    for(unsigned int i=0;i<count;++i){unsigned int value;if(source.Get(from+i,1,&value))target.Put(to+i,value,1);else target.Unwritten(to+i,1);}
}
}
NativeDescriptorPersistent::NativeDescriptorPersistent():definitionPresent(false){
    for(int i=0;i<3;++i)definition[i]=0; //90b3ff..43c's separate three-word config Reset record.
    //6aece0 initializes the descriptor's scalar storage and its four owned
    //containers. The 1c..38 region and94..ab links are initially unwritten.
    fields.Put(0,0xffffffffU);fields.Put(4,0xffffffffU);fields.Put(8,0xffffffffU);
    fields.Put(12,0);fields.Put(16,0);fields.Put(20,0);fields.Put(24,0);
    fields.Unwritten(0x1c,0x20);
    for(unsigned int at=0x3c;at<0x68;at+=4)fields.Put(at,0);
    fields.Unwritten(0x94,24);fields.Put(0xac,0xffffffffU);fields.Put(0xb0,0);fields.Put(0xb4,0xffffffffU);
    fields.Put(0x84,0); //6aee0c, the actual newly constructed tree's count.
    fields.Put(0x88,0);fields.Put(0x8c,0);fields.Put(0x90,0); //6aee2b..3f, secondary entity vector.
    //No host container descriptors/pointers enter the logical capture.
}
NativeGamePersistent::NativeGamePersistent():captured(false){
    //Avoid reading indeterminate C++ storage when replacing an invalid
    //checkpoint on restart. None of these POD members is a captured native
    //value until CaptureInitial succeeds; addressed native unwritten bytes
    //are represented independently by NativePersistentFields.
    memset(&seeds,0,sizeof(seeds));memset(&bosses,0,sizeof(bosses));memset(owners,0,sizeof(owners));
    pools.seed=pools.counter=pools.random=pools.backup=0;pools.treasureMultiplier=pools.devilMultiplier=0;
    for(int i=0;i<31;++i)pools.poolCounters[i]=0;
    for(int i=0;i<15;++i){pools.pillEffects[i]=0;pools.pillIdentified[i]=false;}
    memset(pools.pocketFlags,0,sizeof(pools.pocketFlags));
}
NativeRunCheckpoint::NativeRunCheckpoint():valid(false),action(-1),frame(0){memset(events,0,sizeof(events));}
bool NativeDescriptorPersistent::CaptureInitial(const NativePlacedRoom* descriptor,const NativeRoomPersistent* persistent){
    if(!descriptor)return true; //6fccd3 skips the -3 source, retaining90b1e0's Reset record.
    const NativePlacedRoom& p=*descriptor;const NativeRoomDef* d=p.definition>=0?NativeRoom(p.definition):0;
    if(p.definition>=0&&!d)return false;
    fields.Put(0,p.gridIndex);fields.Put(4,p.safeGridIndex);fields.Put(8,p.listIndex);fields.Put(12,p.dimension);
    fields.Put(0x10,0); //6fccfa clears the source RoomConfig pointer AFTER6b6c50.
    fields.Put(0x18,p.doors);fields.Put(0x3c,p.displayFlags);fields.Put(0x40,p.visitedCount);fields.Put(0x44,p.flags);
    fields.Put(0x58,p.seeds.spawn);fields.Put(0x5c,p.seeds.award);fields.Put(0x60,p.seeds.decoration);fields.Put(0x64,p.seeds.auxiliary);
    fields.Put(0xb4,p.nativeB4);
    if(p.linksPrepared)for(int slot=0;slot<8;++slot){fields.Put(0x94+slot*2,(unsigned short)p.doorTargets[slot],2);fields.Put(0xa4+slot,(unsigned char)p.doorTargetSlots[slot],1);}
    if(d){
        //6fc1b8..1ed: normal room-config stage/set0, type and variant.
        //The preserved definition binding also resolves the source ordinal;
        //host object addresses are never serialized as the cleared pointer.
        if(d->stage<0||d->stage>65535)return false;
        definition[0]=(unsigned int)d->stage;definition[1]=d->type;definition[2]=d->variant;definitionPresent=true;
    }
    if(persistent)room=*persistent;
    return true;
}
NativeDormantStagePersistent::NativeDormantStagePersistent(){
    //6af060 constructs FOUR full descriptor owners, not a zero block. Its
    //four optional three-word positions are unwritten until the mode is used.
    fields.Unwritten(0x170,24);fields.Unwritten(0x2f8,24);
    fields.Put(0x310,0);fields.Put(0x314,0);fields.Put(0x318,0);fields.Put(0x31c,0,1);
}
bool NativeRunHistory::BeginFresh(const NativeRunStart& start,bool debugConsole){
    if(ready||!NativeRunStartValid(start))return false;
    //6b2ff0/6b3140: live game counters, not profile counters. They remain
    //owned here so capture and later whole ticks observe the same values.
    fields.Put(0x1a2e1c,0);fields.Put(0x1a2e20,0);fields.Put(0x1a2e18,0);
    fields.Put(0x1a2e2c,0); //6b3019 screen-shake timer.
    fields.Put(0x1b470,0);fields.Put(0x1b7bc,0); //6b065c/6b073e: distinct transition-state owners.
    for(unsigned int at=0x1c2eb4;at<=0x1c2ed4;at+=4)fields.Put(at,0);
    fields.Put(0x243350,0);fields.Put(0x243354,1);fields.Put(0x24335c,0);fields.Put(0x1c312c,0);
    fields.Put(0x243370,0);fields.Put(0x243374,0);fields.Put(0x283b24,0);fields.Put(0x283b5c,0);fields.Put(0x285140,0);
    //6b3d20/2e,6fb836. The actual first floor is ordinary style0..2.
    fields.Put(0x1a2e24,0x8ca0);fields.Put(0x1a2e28,0xd2f0);
    //Normal challenge0 and the actual Options+4d value copied at6b45de.
    fields.Put(0x1c2edc,0);fields.Put(0x1c2ee0,0,1);fields.Put(0x243358,start.mode);fields.Put(0x243360,debugConsole?1:0,1);
    fields.Put(0x1c2f18,0,1);fields.Put(0x1c2f1c,0); //Game ctor6b0c42/49.
    //924f30 resets the real six totals and two30-word arrays. Visits are
    //updated by Room::Init's live NormalRunFlow owner and read from it below.
    for(unsigned int at=0x1c3018;at<=0x1c3124;at+=4)fields.Put(at,0);
    checkpointGuard=restoreGuard=false;activeCheckpoint=-1;ready=true;return true;
}
bool NativeGamePersistent::CaptureInitial(const NormalRunFlow& run,NativeWorld& world,const NativeRng& gameRandom,const NativeItemPools& itemPools,const NativeNightmares& nightmares,
        const NativeRunHistory& history,NativePlayerState& player,const NativePlayerStartup& startup,NativePlayerCreation& creation,NativePlayerSprites& sprites,const NativeEntityBody& body,const NativePlayerManager& manager,unsigned int* next){
    return CaptureState(run,world,gameRandom,itemPools,nightmares,history,player,startup,creation,sprites,body,manager,true,false,next);
}
bool NativeGamePersistent::CaptureBeforeFileSave(const NormalRunFlow& run,NativeWorld& world,const NativeRng& gameRandom,const NativeItemPools& itemPools,const NativeNightmares& nightmares,
        const NativeRunHistory& history,NativePlayerState& player,const NativePlayerStartup& startup,NativePlayerCreation& creation,NativePlayerSprites& sprites,const NativeEntityBody& body,const NativePlayerManager& manager,unsigned int* next){
    return CaptureState(run,world,gameRandom,itemPools,nightmares,history,player,startup,creation,sprites,body,manager,false,false,next);
}
bool NativeGamePersistent::CaptureCurrent(const NormalRunFlow& run,NativeWorld& world,const NativeRng& gameRandom,const NativeItemPools& itemPools,const NativeNightmares& nightmares,
        const NativeRunHistory& history,NativePlayerState& player,const NativePlayerStartup& startup,NativePlayerCreation& creation,NativePlayerSprites& sprites,const NativeEntityBody& body,const NativePlayerManager& manager,unsigned int* next){
    return CaptureState(run,world,gameRandom,itemPools,nightmares,history,player,startup,creation,sprites,body,manager,false,true,next);
}
bool NativeGamePersistent::CaptureState(const NormalRunFlow& run,NativeWorld& world,const NativeRng& gameRandom,const NativeItemPools& itemPools,const NativeNightmares& nightmares,
        const NativeRunHistory& history,NativePlayerState& player,const NativePlayerStartup& startup,NativePlayerCreation& creation,NativePlayerSprites& sprites,const NativeEntityBody& body,const NativePlayerManager& manager,bool checkpoint,bool allowCurrent,unsigned int* next){
    if(next)*next=0x6b6ef0;
    const NativePlacedRoom* selected=world.CurrentDescriptor();
    if(captured||!history.ready||history.restoreGuard||!selected||!nightmares.reset||
       run.context.stage<1||run.context.stage>12||run.context.stageType<0||run.context.stageType>5||run.context.challenge||
       !run.seeds.Valid()||!gameRandom.state||!manager.initialized||manager.members.size()!=1||
       manager.worldMembers.size()!=manager.worldTypes.size()||!world.Current())return false;
    if(!allowCurrent&&(history.checkpointGuard!=checkpoint||run.phase!=NRF_NEED_ROOM_ENTRY||run.context.stage!=1||
       run.context.stageType>2||gameRandom.index!=35||!player.runInitializing))return false;
    for(unsigned int i=0;i<manager.worldTypes.size();++i)if(manager.worldTypes[i]!=1&&manager.worldTypes[i]!=1000)return false;
    //90b1e0 resets destination ownership before925ce0/925df0 clone the
    //live seed manager. No reconstruction of RNG state from START is used.
    fields.Clear();seeds=run.seeds;fields.Put(0x1e4c8,seeds.seeded?1:0,1);
    fields.Put(0x1e524,startup.manager.state);fields.Put(0x1e528,gameRandom.state);
    const unsigned int map[][3]={{0x1c2edc,0,4},{0x1c2ee0,4,1},{0x243360,5,1},{0x243358,8,4},
        {0x1c2eb4,0x40,4},{0x1c2eb8,0x44,4},{0x1c2ebc,0x48,4},{0x1c2ec0,0x4c,4},{0x1c2ec4,0x50,4},
        {0x1c2ec8,0x54,4},{0x1c2ed4,0x58,4},{0x1c2ecc,0x5c,4},{0x1c2ed0,0x60,4},
        {0x1a2e24,0x64,4},{0x1a2e28,0x68,4},{0x1a2e1c,0x6c,4},{0x1a2e20,0x70,4},{0x243354,0x74,4},
        {0x1c312c,0x190,4},{0x243350,0x194,4},{0x24335c,0x1e52c,4},{0x283b24,0x1e530,4},
        {0x243370,0x1e54c,4},{0x243374,0x1e550,4}};
    for(unsigned int i=0;i<sizeof(map)/sizeof(map[0]);++i){unsigned int value;
        if(!history.fields.Get(map[i][0],map[i][2],&value))return false;fields.Put(map[i][1],value,map[i][2]);}
    fields.Put(0xc,49,2);for(unsigned int i=0;i<49;++i)fields.Put(0xe+i,i<32?(run.runFlagsLow>>i)&1:(run.runFlagsHigh>>(i-32))&1,1);
    //6b7104..179. Constructor/reset-owned lists243364,283b28 and283b34
    //are empty before any gameplay. Their logical owners remain distinct.
    if(!history.native243364.empty())return false;
    auxiliary=history.native243364;effects.clear();extraConfigurations.clear();
    //All first-floor definitions belong to the immutable normal catalog.
    //The held-item/challenge/custom-seed clone branches did not execute.
    levelConfigurations.clear();
    if(next)*next=0x6b7147;if(!itemPools.Capture(pools))return false;
    if(next)*next=0x6b7159;if(!NativeCaptureBossState(run.bosses,&bosses))return false;
    if(allowCurrent){unsigned int ignored=0;(void)world.CaptureInitialRoom(&ignored);}
    else{if(next)*next=0x6b71bf;if(!world.CaptureInitialRoom(next))return false;}
    //6fca60 transfers every descriptor slot, all507 grid owners and the
    //level's actual fields. Unwritten/inactive source bytes remain so; they
    //are not asserted to be zero merely to make a capture succeed.
    if(next)*next=0x6fca60;
    //Current-state saves refresh the active room's persistent record when the
    //live geometry still admits it. Failure leaves the descriptor's reset
    //record intact rather than inventing a payload.
    fields.Put(0x198,run.context.stage);fields.Put(0x19c,run.context.stageType);fields.Put(0x1a0,world.currentGrid);
    fields.Put(0x1a4,run.bossSequence.lastLayoutIndex);fields.Put(0x1a8,run.context.effectiveCurses);
    fields.Put(0x1ac,run.levelSeed);fields.Put(0x1b0,run.floor.plan.streams.secondary.state);fields.Unwritten(0x1b4,4);
    fields.Put(0x1b8,0,1);fields.Put(0x1b9,0,1);fields.Put(0x1ba,0,1);fields.Unwritten(0x1bb,1);
    fields.Put(0x1bc,24,2);for(unsigned int i=0;i<24;++i)fields.Put(0x1be+i,0,1);
    fields.Put(0x1d8,0);for(unsigned int at=0x1dc;at<0x218;at+=4)fields.Put(at,0);
    fields.Unwritten(0x20c,4); //18200's ordinary-mode source is never assigned or queried on this path.
    fields.Put(0x250,world.layout.count);
    descriptors.clear();descriptors.resize(525);
    for(int i=0;i<525;++i){const NativePlacedRoom* p=i<507?&world.layout.rooms[i]:&world.layout.offGrid[i-507];
        const NativeWorldResident* live=world.At(i);
        descriptors[i].fields.Put(8,i); //90b449 sets each destination's stable list identity.
        if(p->gridIndex==-3)p=0;
        if(!descriptors[i].CaptureInitial(p,live?live->persistent:0))return false;
    }
    for(int d=0;d<3;++d)for(int cell=0;cell<169;++cell)owners[d][cell]=world.layout.owners[d][cell];
    fields.Put(0x19c48,world.currentGrid);fields.Put(0x19c4c,world.previousGrid);
    fields.Put(0x19c50,world.layout.currentDimension,1);fields.Put(0x19c51,world.layout.currentDimension,1);
    fields.Put(0x19c54,0);fields.Put(0x19c58,0);fields.Unwritten(0x19c5c,4);
    fields.Put(0x19c60,0xffffffffU);fields.Put(0x19c64,0);fields.Put(0x19c68,0,1);fields.Unwritten(0x19c69,3);
    fields.Put(0x19c6c,0);fields.Put(0x19c70,0,1);fields.Unwritten(0x19c71,3);fields.Unwritten(0x19c74,4);
    fields.Put(0x19c78,0xfffe7961U);fields.Unwritten(0x19c7c,8);fields.Put(0x19c84,0xfffe7961U);fields.Unwritten(0x19c88,8);
    //6b6930 copies all four dormant descriptor owners even though31c=false.
    //Their ctor values and indeterminate optional positions are retained.
    unsigned int dormantCount,flags;
    if(!history.fields.Get(0x283b5c,4,&dormantCount)||!history.fields.Get(0x285140,2,&flags)||dormantCount)return false;
    fields.Put(0x1e554,(int)dormantCount>0?1:0,1);fields.Put(0x1fb38,flags,2);
    if(next)*next=0x907350;
    players.clear();players.resize(manager.members.size());
    if(allowCurrent){if(!players[0].CaptureCurrent(player,startup,creation,body,next))return false;}
    else if(checkpoint){if(!players[0].CaptureInitial(player,startup,creation,body,next))return false;}
    else{
        if(!creation.initialSaveNormalized&&!creation.NormalizeInitialSave(player,sprites,next))return false;
        if(!players[0].CaptureAfterInitialSetup(player,startup,creation,body,next))return false;
    }
    fields.Put(0x19c9c,(unsigned int)players.size());
    fields.Put(0x19c90,0); //90b528 Reset; later wrapper supplies19c94.
    fields.Put(0x19c94,allowCurrent?(unsigned int)run.player.player:0U);
    fields.Put(0x19c98,0,1); //90b539 Reset; no secondary player sets it here.
    for(unsigned int i=0;i<14;++i)fields.Put(0x218+4*i,nightmares.stageBackdrops[i]);
    Copy(fields,0x78,history.fields,0x1c2f18,1);unsigned int n;if(!history.fields.Get(0x1c2f1c,4,&n))return false;
    fields.Put(0x7c,(int)n>0?n-1:n);Copy(fields,0x80,history.fields,0x1c3018,24);
    Copy(fields,0x98,history.fields,0x1c3030,120);Copy(fields,0x110,history.fields,0x1c30a8,4);
    for(unsigned int i=0;i<30;++i)fields.Put(0x114+4*i,run.roomTypeVisits[i]);Copy(fields,0x18c,history.fields,0x1c3124,4);
    captured=true;if(next)*next=0;return true;
}
