#include "native_world.h"
#include "normal_run.h"
#include "native_player_state.h"
#include "native_player_manager.h"
#include "native_player_creation.h"
#include "native_room_effects.h"
#include "native_room_view.h"
#include "native_door_resources.h"
#include "native_room_update.h"

namespace {
bool Absent(const NativePlayerState& player,int item){bool held;return player.HasCollectible(item,&held)&&!held;}
float Add(float a,float b){volatile float f=a+b;return f;}
float Sub(float a,float b){volatile float f=a-b;return f;}
float Mul(float a,float b){volatile float f=a*b;return f;}
float GlobalFloat(NativeMt& r){volatile float f=(float)r.Next();return Mul(f,1.0f/4294967296.0f);}
}
NativeWorldResult NativeWorld::ContinueInitialRoom(NormalRunFlow& run,const NativePlayerState& player,const NativeEntityBody& body,const NativePlayerCreation& creation,NativePlayerManager& manager,const NativeRoomFx& fx,NativeMt& global,unsigned int* next){
    NativeRoomState* room=Current();const NativePlacedRoom* p=CurrentDescriptor();
    if(next)*next=0x7a16e4;
    if(!next||!room||!p||!resident[currentList].perimeterReady||!resident[currentList].setupKnown||
       run.phase!=NRF_NEED_ROOM_ENTRY||run.context.stage!=1||run.context.stageType>2||run.context.nativeMode<0||run.context.nativeMode>1||
       run.currentGrid!=currentGrid||run.currentGrid!=run.previousGrid||room->type!=1||room->shape!=1||!player.resourcesReady)return NWR_BAD_REQUEST;
    NativeWorldResident& live=resident[currentList];
    //Reached ordinary New Run. The original descriptor lists for saved grids,
    //entities and particles are empty at placement. Main-pass construction has
    //not admitted any live-actor factory. Unsupported candidates stop there.
    if(!live.roomInitAddress)live.roomInitAddress=0x7a16e4;
    if(live.roomInitAddress==0x7a16e4){
        if(p->visitedCount||!run.levelSeed)return NWR_BAD_REQUEST;
        live.tintedRockGrid=-1;live.tintedRockRoom=-1;
        NativeRng choice;choice.Set(run.levelSeed,35);unsigned int value;
        for(int i=0;i<3&&live.tintedRockRoom<0;++i){
            if(!choice.Int((unsigned int)layout.count,&value))return NWR_BAD_REQUEST;
            const NativeRoomDef* d=NativeRoom(layout.rooms[value].definition);if(!d)return NWR_BAD_REQUEST;
            if(d->type!=5&&d->type!=11&&d->type!=6)live.tintedRockRoom=(int)value;
        }
        if(live.tintedRockRoom==p->listIndex){
            for(int i=0;i<10&&live.tintedRockGrid<0;++i){
                if(!choice.Int((unsigned int)room->geometry.cells,&value))return NWR_BAD_REQUEST;
                if(value<448&&room->collision.occupied[value]&&room->collision.types[value]!=15)live.tintedRockGrid=(int)value;
            }
        }
        //7a1810: this actual first chapter's starting descriptor skips the
        //random extra-grid branch. Mode0 skips7a1c93's alternate-mode grids.
        //7a1ec4 invokes virtual+4 (not Init at+28). Doors dispatch407240;
        //walls dispatch6d8ce0, which returns at6d8ef5 for room type!=16.
        //Type1 decorations dispatch407240 too:6c67e0's case1 at6c681a
        //installs b2a864, whose virtual+4 is the same no-op as doors.
        //Their Init+28 is8df570 (b2a864+28);6ce270 is virtual+18.
        live.gridPostConstructed=true;
        //7cd3e0 scans only room entities, presently empty; marker45/37 also
        //have no candidate actors.79dab0 has no saved descriptors to restore.
        //7a1f9e saved-particle list is empty.6f9ee0/6f9eee wrote the two
        //teleport destinations to -99999, so6f6100 cannot select this room.
        live.roomInitAddress=0x7a20f0;
    }
    if(live.roomInitAddress==0x7a20f0){
        //Reached manager inventory queries in original order.
        //Post-generation starting grants have not run yet (6b4b88).
        const int items[]={660,586,701};const unsigned int calls[]={0x7a2129,0x7a25dd,0x7a267e};
        for(int i=0;i<3;++i)if(!Absent(player,items[i])){*next=calls[i];return NWR_CONSTRUCTION_GAP;}
        //Room1 bypasses7/8,14,16-specific branches.6f9ecc cleared the
        //level flags at1822c; generation has not set bit16 here. Seed effects
        //48/71 and challenge31 are absent in ordinary numeric-seed New Run.
        if(run.context.challenge||(run.runFlagsHigh&0x10000U)){*next=0x7a2bbe;return NWR_CONSTRUCTION_GAP;}
        room->native1210=0; //7a2cc3, before the ambient-selection owner.
        live.update->active=true; //7a2ccd.
        //7a2e18 retains descriptor+5c (award) in a separate local
        //5/9/7 owner. One advance precedes the optional ambient-effect branch.
        live.roomInitRandom.Set(p->seeds.award,35);unsigned int value;
        if(!live.roomInitRandom.Next(&value))return NWR_BAD_REQUEST;
        live.roomInitAddress=(value&1)?0x7a3b8a:0x7a300e;
    }
    if(live.roomInitAddress==0x7a300e){
        if(!Absent(player,148)||!Absent(player,234)){*next=0x7a302a;return NWR_CONSTRUCTION_GAP;}
        unsigned int value;
        //Room definition's first cell equals the actual starting room, so
        //each nested count is shifted once. The unused fourth nested result
        //is still consumed at7a3146/53/60. Backdrops1/2 both have walls>0.
        for(int i=0;i<3;++i){
            if(!live.roomInitRandom.Int(i==0?15:10,&value)||!live.roomInitRandom.Int(value+1,&value))return NWR_BAD_REQUEST;
            live.ambientCounts[i]=(int)(value>>1);
        }
        if(!live.roomInitRandom.Int(20,&value)||!live.roomInitRandom.Int(value+1,&value)||!live.roomInitRandom.Int(value+1,&value))return NWR_BAD_REQUEST;
        if(!live.effects)return NWR_BAD_REQUEST;
        live.ambientGroup=live.ambientIndex=0;live.roomInitAddress=0x7a31f1;
    }
    if(live.roomInitAddress==0x7a31f1){
        const int variants[]={21,33,68};const unsigned int calls[]={0x7a32a2,0x7a3373,0x7a33c1};
        while(live.ambientGroup<3){int group=live.ambientGroup;
            while(live.ambientIndex<live.ambientCounts[group]){
                NativePoint position={1,1};
                if(group<2){float y=GlobalFloat(global),x=GlobalFloat(global);
                    position.x=Add(Mul(Sub(room->space.right,room->space.left),x),room->space.left);
                    position.y=Add(Mul(Sub(room->space.bottom,room->space.top),y),room->space.top);}
                unsigned int seed=global.Next();*next=calls[group];
                if(!live.effects->Spawn(variants[group],seed,position,live.configStage,live.backdrop,global,manager,next)){live.roomInitAddress=*next;return NWR_CONSTRUCTION_GAP;}
                NativeRoomEffect* effect=live.effects->entities.back();
                //Each virtual Update returns BEFORE the next actor's seed.
                if(!effect->Update(*room,body,player,manager,live.effects->roomEffects,global,0)){*next=effect->nextAddress;live.roomInitAddress=*next;return NWR_CONSTRUCTION_GAP;}
                ++live.ambientIndex;
            }
            ++live.ambientGroup;live.ambientIndex=0;
        }
        //7a33d2..37cc: configStage1 bypasses the womb/flies/leech factories.
        //Room1 has no sacrifice room subtype or CurseOfTheLost bit; backdrop
        //1/2 is not27 and water0 excludes tadpoles/ghosts.
        live.roomInitAddress=0x7a3b8a;
    }
    if(live.roomInitAddress==0x7a3b8a){
        //Challenge31 is excluded above. No combatant/blocker was constructed,
        //so7a3d04..27 marks this real descriptor clear. Shape1 adds one visit
        //to the ordinary room-type counter at1c30ac+4*type.
        NativePlacedRoom& descriptor=layout.rooms[currentList];++descriptor.visitedCount;
        ++run.roomTypeVisits[room->type];
        descriptor.flags|=1U;live.setup.flags=descriptor.flags;
        live.roomInitAddress=0x7a4155;
    }
    if(live.roomInitAddress==0x7a4155){
        //7cd7d0: waterAmount0 takes its empty-water return. Stage1 is not
        //7cd770's backdrop30, and the level48/color/type29 branches do not run.
        if(live.setup.waterAmount!=0){*next=0x7a416e;return NWR_CONSTRUCTION_GAP;}
        live.roomInitAddress=0x7a4409;
    }
    if(live.roomInitAddress==0x7a4409){
        if(!live.view)live.view=new NativeRoomView;
        if(!live.view->PrepareLighting(fx,p->flags)){*next=0x7ccd50;return NWR_CONSTRUCTION_GAP;}
        if(!room->space.RefreshBounds(room->geometry,room->collision))return NWR_BAD_REQUEST;
        if(!live.view->PrepareCamera(*room)){*next=0x88b8f0;return NWR_CONSTRUCTION_GAP;}
        live.roomInitAddress=0x7a4525;
    }
    if(live.roomInitAddress==0x7a4525){
        if(!live.view->background.Prepare(*room,live.backdrop,p->seeds.spawn)){
            *next=live.view->background.nextAddress;return NWR_CONSTRUCTION_GAP;}
        //7a452a..4573 is profiling/timing output only. Backdrops1/2 cannot
        //enter the backdrop62/mode3 tail. Room::Init returns true at7a459a.
        //6f401f skips the type29 repair; normal mode0 skips6f4208's late
        //alternate-stage pit manipulation. Next: bind each actual door.
        live.roomInitAddress=0x6f43dc;
    }
    if(live.roomInitAddress==0x6f43dc){
        if(!live.doorVisuals)live.doorVisuals=new NativeDoorVisual[8];
        for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){
            if(live.doorVisuals[slot].ready)continue;
            const NativePlacedRoom* target=layout.DoorTarget(currentList,slot);
            const NativeRoomDef* d=target?NativeRoom(target->definition):0;if(!d)return NWR_BAD_REQUEST;
            NativeDoorBinding c={0};c.sourceType=room->type;c.targetType=d->type;
            c.sourceVariant=room->variant;c.targetVariant=d->variant;c.sourceFlags=p->flags;c.targetFlags=target->flags;
            c.stage=run.context.stage;c.stageType=run.context.stageType;c.nativeMode=run.context.nativeMode;
            c.currentGrid=currentGrid;c.targetGrid=p->doorTargets[slot];c.dimension=layout.currentDimension;
            c.treasureGrid=-1;c.generatedRoomCount=layout.count;c.challenge=run.context.challenge;c.sourceData=c.targetData=true;
            //Only ordinary stage types0/1 enter this caller; reflection and
            //seed effect33 have no registrations. First-stage free doors do
            //not inspect the alternate-mode treasure-grid operand.
            if(SelectDoorVariant(slot,c)!=NDR_OK)return NWR_BAD_REQUEST;
            NativeDoorVisual& v=live.doorVisuals[slot];
            if(!v.BindFresh(live.doors->actors[slot],live.backdrop,p->flags,target->flags,player)){
                live.doorFailureSlot=slot;live.doorFailureKind=live.doors->actors[slot].targetType;
                live.doorFailureVariant=live.doors->actors[slot].variant;live.doorFailureAux=live.doors->actors[slot].auxiliary;
                //A presentation-only door target whose sprite binding is not
                //yet maintained keeps its actor, collision and target rules.
                //It remains an explicit rendering gap, not a skipped gate.
                if(v.unsupportedTarget){++live.doorVisualGaps;continue;}
                *next=v.nextAddress;live.roomInitAddress=*next;return NWR_CONSTRUCTION_GAP;}
        }
        live.roomInitAddress=0x6f4471;
    }
    if(live.roomInitAddress==0x6f4471){
        //6f443d: Room::Init marked this actual descriptor clear. These are
        //six distinct factory calls, not a command to open its ordinary doors.
        if(!(p->flags&1U))return NWR_BAD_REQUEST;
        const NativePlacedRoom* boss=layout.ByList(run.bossSequence.lastLayoutIndex);
        const NativeRoomDef* bossData=boss?NativeRoom(boss->definition):0;
        if(!bossData||bossData->type!=5)return NWR_BAD_REQUEST;
        //7a4750(false,false): chapter1 skips the chapter5/6 starting-room
        //branch.7a48dd compares list identities before any RNG or item query.
        if(p->listIndex==boss->listIndex){*next=0x7a48f2;return NWR_CONSTRUCTION_GAP;}
        //7a4f80(false,false)->6fe550 returns false for this ordinary first
        //chapter (not stage9, alternate stages4/5 or special registration17).
        //7a62f0(false,false) repeats the actual boss-list test at7a6398.
        //7ca150(false) returns at7ca28a when the effective chapter is not9.
        //Main+c is PersistentGameData: Main+12e is award234 at+c+38.
        //7c9e00 queries175 BEFORE the boss-list early return when unlocked.
        if(run.profile.Award(234)){int multiplier;
            if(!player.TrinketMultiplier(175,&multiplier)){*next=0x7c9e7f;return NWR_CONSTRUCTION_GAP;}}
        //7a4e60(false) only changes room type2; this selected room is type1.
        //No dimension2/alternate configuration33 FX and no negative-grid
        //twin-room repair are reached. Next is the manager entry service.
        live.roomInitAddress=0x6f4a67;
    }
    if(live.roomInitAddress==0x6f4a67){
        if(!manager.PrepareInitialRoomEntry(player,body,next)){live.roomInitAddress=*next;return NWR_CONSTRUCTION_GAP;}
        live.roomInitAddress=*next;
    }
    if(live.roomInitAddress==0x6f4a7f){
        //The preceding manager call returned THROUGH the native checkpoint.
        //8d8470(true) immediately returns for7971dc's disabled emitter.
        if(room->particlesEnabled){*next=0x8d848b;return NWR_CONSTRUCTION_GAP;}
        //7d0270 scans the four active and four pocket slots, then the real
        //580 predicate, BEFORE handling its eight nullable door references.
        for(int i=0;i<4;++i)if(player.activeItems[i]){*next=0x7d02f4;return NWR_CONSTRUCTION_GAP;}
        const unsigned int pocket[]={0x14b4,0x14d0,0x14ec,0x1508};
        //These fresh pockets were reset by709330 and no post-generation
        //grant has occurred. Their type0 path cannot carry477 or a history.
        for(int i=0;i<4;++i){unsigned int id;
            if(creation.postGenerationPrepared||!creation.constructorFields.Scalar(pocket[i],&id)||id){*next=0x7d0320;return NWR_CONSTRUCTION_GAP;}}
        if(!player.native166c.empty()||!Absent(player,580)){*next=0x7d0394;return NWR_CONSTRUCTION_GAP;}
        for(int slot=0;slot<8;++slot)if(room->native7628[slot]){*next=0x7d03b8;return NWR_CONSTRUCTION_GAP;}
        //6f4ab0's Update range10..999 excludes the actual player1 and all
        //ambient1000 owners. This must not give them a second initial tick.
        for(unsigned int i=0;i<manager.worldTypes.size();++i)if(manager.worldTypes[i]>=10&&manager.worldTypes[i]<=999){*next=0x6f4ad6;return NWR_CONSTRUCTION_GAP;}
        if(!manager.EmptyRoomInitCallback()){*next=0x7f2d50;return NWR_CONSTRUCTION_GAP;}
        live.roomInitAddress=0x7990b0; //6f3b30 returned;6fab1b selects room music.
    }
    *next=live.roomInitAddress;return NWR_CONSTRUCTION_GAP;
}
