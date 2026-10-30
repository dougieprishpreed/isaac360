#include "../game/game_session.h"
#include "../game/native_room_effects.h"
#include "../game/native_room_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
namespace {
unsigned int RunMovementTicks(PortGameSession& session,PortNormalRuntime& runtime,float x,float y,unsigned int count){
    unsigned int done=0;
    for(unsigned int i=0;i<count;++i){
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",runtime.run.context.stageType+1,runtime.run.context.nativeMode,0};
        NativeActionFrame frame;frame.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame+i);
        frame.SupplyConnection(true);frame.SupplyDrop(false);
        for(int action=0;action<12;++action){frame.SupplyHeld(action,false);frame.SupplyPressed(action,false);}
        for(int action=0;action<8;++action)frame.SupplyValue(action,0);
        if(x>0)frame.SupplyValue(1,x);else if(x<0)frame.SupplyValue(0,-x);
        if(y>0)frame.SupplyValue(3,y);else if(y<0)frame.SupplyValue(2,-y);
        NativeFrameActions actions(frame);
        if(session.TickNormal(runtime,files,&actions)!=NFR_OK)break;
        ++done;
    }
    return done;
}
unsigned int AdvanceToBoundary(PortGameSession& session,PortNormalRuntime& runtime){
    unsigned int steps=0;
    while(runtime.phase==PNR_GENERATING||runtime.phase==PNR_ROOM_COMPONENTS){
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",runtime.run.context.stageType+1,runtime.run.context.nativeMode,0};
        //An explicit connected virtual device with released buttons. This
        //supplies only external input; initialized gameplay state is owned
        //and produced by the shared New Run, not by the host adapter.
        NativeActionFrame frame;frame.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame);
        frame.SupplyConnection(true);frame.SupplyDrop(false);
        for(int action=0;action<12;++action){frame.SupplyHeld(action,false);frame.SupplyPressed(action,false);if(action<8)frame.SupplyValue(action,0);}
        NativeFrameActions actions(frame);session.AdvanceNormalSetup(runtime,files,&actions);++steps;
        if(steps==1000000)return 0;
    }
    return steps;
}
}
int main(int argc,char** argv){
    unsigned int seed=argc>1?(unsigned int)strtoul(argv[1],0,0):1;
    NativeMt random;random.ColdStart();PortFrontend ui;ui.Init(false);
    if(!ui.BindSaveDirectory("build/gameplay/profile/"))return 13;
    PortMenuInput accept={0},empty={0};accept.accept=true;
    ui.Step(accept);ui.Step(empty);if(ui.Step(accept)!=UI_SELECT_SLOT)return 1;
    ui.SlotLoaded(true,false);ui.Step(empty);ui.Step(accept);ui.Step(empty);
    if(ui.Step(accept)!=UI_START_CHARACTER)return 2;
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(seed,*runtime);
    printf("NEW_RUN result=%d phase=%d resources=%d HP=%d/%d bombs=%d manager=%d\n",start.result,runtime->phase,
        runtime->playerState.resourcesReady,runtime->playerState.redHearts,runtime->playerState.maxRedHearts,runtime->playerState.bombs,runtime->playerManager.initialized);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 3;}
    printf("OWNERS sequence=%u playerInit=%u game=%u/%u pools=%u random=%u/%u backup=%u/%u trinkets=%d soundResets=%u\n",
        runtime->run.seeds.sequence,runtime->run.seeds.playerInit,runtime->gameRandom.state,runtime->gameRandom.index,
        runtime->itemPools.seed,runtime->itemPools.random.state,runtime->itemPools.random.index,
        runtime->itemPools.backup.state,runtime->itemPools.backup.index,runtime->itemPools.availableTrinkets,(unsigned int)runtime->itemPools.soundResets.size());
    printf("PILL_COLORS");for(int i=0;i<15;++i)printf(" %d",runtime->itemPools.pillEffects[i]);printf("\n");
    printf("PLAYER active=%d registered=%d collision=%d gridCollision=%d depth=%d initializing=%d controller=%d HUDrefreshes=%u\n",
        runtime->playerBody.native16a,runtime->playerBody.native168,runtime->playerBody.entityCollisionClass,runtime->playerBody.gridCollisionClass,
        runtime->playerBody.renderDepth,runtime->playerState.runInitializing,runtime->playerCreation.native153c,runtime->playerManager.hudRefreshes);
    unsigned int steps=AdvanceToBoundary(session,*runtime);if(!steps){delete runtime;return 4;}
    printf("SESSION phase=%d floor=%d steps=%u selected=%d residents=%d next=%08x players=%u ticks=%u\n",runtime->phase,runtime->run.phase,steps,
        runtime->world.CurrentDescriptor()?runtime->world.CurrentDescriptor()->definition:-1,runtime->world.residentCount,runtime->missingNativePrerequisite,
        runtime->completePlayerInitializations,runtime->completePlayerTicks);
    printf("SETUP position=(%.9g,%.9g) native1734=%d postGeneration=%d stats=%d speed=%.9g damage=%.9g saveNormalized=%d saveCaptured=%d\n",
        runtime->playerBody.position.x,runtime->playerBody.position.y,runtime->playerState.native1734,runtime->playerCreation.postGenerationPrepared,
        runtime->playerState.stats.ready,runtime->playerState.stats.moveSpeed,runtime->playerState.stats.damage,
        runtime->playerCreation.initialSaveNormalized,runtime->initialSave.captured);
    printf("PLAYER_UPDATE address=%08x missingOffset=%x active=%d returned=%d time=%.9g updateMembers=%u\n",
        runtime->playerUpdate.address,runtime->playerUpdate.missingOffset,runtime->playerUpdate.active,runtime->playerUpdate.returned,
        runtime->playerBody.time,(unsigned int)runtime->playerManager.updateMembers.size());
    {
        static const unsigned int scalars[]={0x1304,0x1524,0x1528,0x19fc,0x1808,0x1e24,0x1320,0x134c,0x14a0,0x1280,0x1284,0x1e88,0x1548,0x154c,0x1550,0x1554,0x1558,0x1560,0x1564,0x1d70,0x1dd8,0x1e68};
        static const unsigned int bytes[]={0x1735,0x1f80,0x12e2,0x12e0,0x12e1,0x155c,0x1734,0x1804};
        printf("FIELDS bodyType=%d subtype=%d variant=%d flags=%08x/%08x position=(%.6g,%.6g) velocity=(%.6g,%.6g)\n",
            runtime->playerBody.type,runtime->playerBody.subtype,runtime->playerBody.variant,runtime->playerBody.flagsLow,runtime->playerBody.flagsHigh,
            runtime->playerBody.position.x,runtime->playerBody.position.y,runtime->playerBody.velocity.x,runtime->playerBody.velocity.y);
        for(unsigned int i=0;i<sizeof(scalars)/sizeof(scalars[0]);++i){unsigned int v;
            printf("FIELD %x=%s%u",scalars[i],runtime->playerCreation.constructorFields.Scalar(scalars[i],&v)?"":"?",runtime->playerCreation.constructorFields.Scalar(scalars[i],&v)?v:0);
            if((i&3)==3)printf("\n");else printf(" ");}
        printf("\n");
        for(unsigned int i=0;i<sizeof(bytes)/sizeof(bytes[0]);++i){unsigned char v;
            printf("BYTE %x=%s%u",bytes[i],runtime->playerCreation.constructorFields.Byte(bytes[i],&v)?"":"?",runtime->playerCreation.constructorFields.Byte(bytes[i],&v)?v:0);
            if((i&3)==3)printf("\n");else printf(" ");}
        printf("\n");
    }
    printf("SAVE bytes=%u published=%d indeterminateBytes=%u codecNext=%08x missingOffset=%x\n",
        (unsigned int)runtime->initialSaveFile.data.size(),runtime->initialSaveFileWritten,runtime->initialSaveFile.indeterminateBytes,
        runtime->initialSaveFile.nextAddress,runtime->initialSaveFile.missingOffset);
    if(runtime->checkpoints[0].valid){const NativeRunCheckpoint& c=runtime->checkpoints[0];unsigned int hp,bombs;
        const NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
        const NativePlayerPersistent& p=c.state.players[0];
        if(!p.fields.Get(4,4,&hp)||!p.fields.Get(0x28,4,&bombs)||hp!=(unsigned int)runtime->playerState.redHearts||bombs!=(unsigned int)runtime->playerState.bombs||
           !live.persistent||live.persistent->grids.size()!=448||c.state.descriptors.size()!=525||
           p.words.find(0x190)==p.words.end()||p.words.find(0x190)->second[97]!=runtime->playerStartup.banks[NPB_TRINKET][97].state||
           runtime->history.checkpointGuard||c.action!=-1){delete runtime;return 10;}
        for(int i=0;i<496;++i)if(c.events[i]!=runtime->run.profile.eventCounters[i]){delete runtime;return 11;}
        printf("CHECKPOINT buffer=%d frame=%u action=%d players=%u HP=%u bombs=%u gridRecords=%u entityRecords=%u descriptors=%u events=496\n",
            runtime->history.activeCheckpoint,c.frame,c.action,(unsigned int)c.state.players.size(),hp,bombs,(unsigned int)live.persistent->grids.size(),
            (unsigned int)live.persistent->entities.size(),(unsigned int)c.state.descriptors.size());
        const NativeRoomState* room=runtime->world.Current();NativeMt before=random;
        unsigned int callbackCount=runtime->playerManager.roomInitCallbacks;
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",runtime->run.context.stageType+1,runtime->run.context.nativeMode,0};
        if(session.AdvanceNormalSetup(*runtime,files)!=NFR_PENDING||room!=runtime->world.Current()||
           memcmp(&before,&random,sizeof(random))||callbackCount!=runtime->playerManager.roomInitCallbacks||!runtime->checkpoints[0].valid){delete runtime;return 12;}
        printf("RESUME residentRetained=1 RNGretained=1 roomCallbacks=%u next=%08x\n",callbackCount,runtime->missingNativePrerequisite);
    }
    if(runtime->phase==PNR_READY){
        const NativeRng rngBeforeTail=runtime->gameRandom;
        const NativePoint start=runtime->playerBody.position;
        const unsigned int right=RunMovementTicks(session,*runtime,1,0,40);
        const NativePoint afterRight=runtime->playerBody.position;
        const unsigned int wall=RunMovementTicks(session,*runtime,1,0,240);
        const NativePoint atWall=runtime->playerBody.position;
        const unsigned int settle=RunMovementTicks(session,*runtime,1,0,40);
        const NativePoint settled=runtime->playerBody.position;
        const unsigned int left=RunMovementTicks(session,*runtime,-1,0,40);
        const NativePoint afterLeft=runtime->playerBody.position;

        const NativeRoomState& room=*runtime->world.Current();
        printf("TICKS ready=%d right=%u start=(%.9g,%.9g) afterRight=(%.9g,%.9g) wall=%u atWall=(%.9g,%.9g) settle=%u settled=(%.9g,%.9g) left=%u afterLeft=(%.9g,%.9g) ticks=%u extent=(%.9g,%.9g,%.9g,%.9g)\n",
            runtime->phase==PNR_READY,right,start.x,start.y,afterRight.x,afterRight.y,wall,atWall.x,atWall.y,settle,settled.x,settled.y,left,afterLeft.x,afterLeft.y,
            runtime->completePlayerTicks,room.space.left,room.space.top,room.space.right,room.space.bottom);
        if(right<10||afterRight.x<=start.x+1||wall<10||settle<10||fabsf(settled.x-atWall.x)>0.01f||
            left<10||afterLeft.x>=atWall.x-1||runtime->completePlayerTicks<right+wall+settle+left)return 14;
        printf("GAMEPLAY complete=%u ticks=%u collisionStop=(%.9g,%.9g) stable=%d\n",
            runtime->completePlayerInitializations,runtime->completePlayerTicks,settled.x,settled.y,fabsf(settled.x-atWall.x)<=0.01f);
        printf("TAIL_RNG unchanged=%d\n",memcmp(&rngBeforeTail,&runtime->gameRandom,sizeof(NativeRng))==0);
        unsigned int headTimerX=0,headTimerY=0,headCalls=0,headDirection=0;
        const NativePlayerCreationCache& headFields=runtime->playerCreation.constructorFields;
        const bool headPresent=headFields.Scalar(0x1280,&headTimerX)&&headFields.Scalar(0x1284,&headTimerY)&&
            headFields.Scalar(0x1e88,&headCalls)&&headFields.Scalar(0x154c,&headDirection);
        printf("HEAD_STATE timers=%d/%d calls=%u/%u direction=%u\n",(int)headTimerX,(int)headTimerY,headCalls,runtime->completePlayerTicks,headDirection);
        //The reached 72c430 ordinary path decrements0x1280/0x1284 to-1 on the
        //first update and increments0x1e88 once per whole tick.
        if(!headPresent||headTimerX!=0xffffffffU||headTimerY!=0xffffffffU||headCalls!=runtime->completePlayerTicks)return 16;
        unsigned int t12c0=0,t2cc=0,t12bc=0,t1ca4=0;float f2cc=0;
        const NativePlayerCreationCache& cf=runtime->playerCreation.constructorFields;
        if(cf.Scalar(0x2cc,&t2cc))memcpy(&f2cc,&t2cc,4);
        unsigned int t1548=0;
        bool p12c0=cf.Scalar(0x12c0,&t12c0),p2cc=cf.Scalar(0x2cc,&t2cc),p12bc=cf.Scalar(0x12bc,&t12bc),p1ca4=cf.Scalar(0x1ca4,&t1ca4);
        cf.Scalar(0x1548,&t1548);
        printf("TAIL ticks=%u trailSegments=%u renderCallbacks=%u fields=12c0/2cc/12bc/1ca4 present=%d%d%d%d values=%u/%.9g/%u/%u direction=%d spriteFlags=%08x\n",
            runtime->playerTailTicks,runtime->playerTailTrailSegments,runtime->playerTailRenderCallbacks,
            p12c0?1:0,p2cc?1:0,p12bc?1:0,p1ca4?1:0,
            t12c0,f2cc,t12bc,t1ca4,(int)t1548,runtime->playerSprites.sprites[0].renderFlags);
        //The final left tick leaves the native direction flag set, the body
        //damage at the actual record value, and the reached clear word zero.
        if(!p12c0||!p2cc||!p12bc||!p1ca4||t12c0!=2U||f2cc!=0.0f||t12bc!=0U||t1ca4!=0U||
           runtime->playerTailTrailSegments!=1U||runtime->playerTailTicks!=runtime->completePlayerTicks||
           (runtime->playerSprites.sprites[0].renderFlags&8U)==0)return 15;
    }
    if(runtime->world.Current()){
        NativeRoomState& room=*runtime->world.Current();
        NativeWorldResident& resident=runtime->world.resident[runtime->world.currentList];
        printf("ROOM type=%u variant=%u shape=%u grids=%d props=%d perimeter=%d MTindex=%d extent=(%.9g,%.9g,%.9g,%.9g)\n",room.type,room.variant,room.shape,room.staticGridCount,room.decorationCount,
            runtime->world.resident[runtime->world.currentList].perimeterReady,random.index,room.space.left,room.space.top,room.space.right,room.space.bottom);
        if(resident.effects)for(unsigned int i=0;i<resident.effects->entities.size();++i){const NativeRoomEffect& effect=*resident.effects->entities[i];
            printf("AMBIENT token=%u variant=%d seed=%u updates=%u time=%g position=(%.9g,%.9g) velocity=(%.9g,%.9g) next=%08x\n",
                effect.body.entityToken,effect.body.variant,effect.body.initialSeed,effect.updates,effect.body.time,
                effect.body.position.x,effect.body.position.y,effect.body.velocity.x,effect.body.velocity.y,effect.nextAddress);}
        if(resident.view)printf("VIEW lighting=%d camera=%d viewport=(%g,%g) center=(%g,%g)\n",resident.view->lightingReady,resident.view->cameraReady,
            resident.view->width,resident.view->height,resident.view->cameraCenter.x,resident.view->cameraCenter.y);
        const NativePlacedRoom* p=runtime->world.CurrentDescriptor();
        if(p)printf("SEEDS level=%u placement=%u spawn=%u award=%u decoration=%u auxiliary=%u list=%d count=%d\n",runtime->run.levelSeed,p->placementSeed,p->seeds.spawn,p->seeds.award,p->seeds.decoration,p->seeds.auxiliary,p->listIndex,runtime->world.layout.count);
        if(p)for(int slot=0;slot<8;++slot)if(p->doors&(1U<<slot)){
            const NativePlacedRoom* target=runtime->world.layout.DoorTarget(runtime->world.currentList,slot);
            const NativeRoomDef* d=target?NativeRoom(target->definition):0;
            printf("DOOR slot=%d grid=%d type=%d variant=%u\n",slot,p->doorTargets[slot],d?d->type:-1,d?d->variant:0);
        }
    }
    bool failed=runtime->phase==PNR_ERROR||runtime->phase==PNR_SOURCE_ERROR;
    if(!failed&&argc>2&&!strcmp(argv[2],"--restart")){
        const NativePlacedRoom* selected=runtime->world.CurrentDescriptor();
        if(!selected){delete runtime;return 6;}
        const int definition=selected->definition,list=selected->listIndex;
        const unsigned int poolRandom=runtime->itemPools.random.state,sequence=runtime->run.seeds.sequence;
        const unsigned int playerHash=runtime->playerState.StateHash(),creationHash=runtime->playerCreation.StateHash();
        const NativeRoomState* live=runtime->world.Current();NativeMt before=random;
        const std::map<int,bool> soundFlags=runtime->sounds.native199;
        const PortNormalPhase phase=runtime->phase;
        PortSessionStart rejected=session.StartNormalCharacter(0,*runtime);
        if(rejected.result!=NPS_BAD_REQUEST||runtime->phase!=phase||runtime->world.Current()!=live||
            runtime->itemPools.random.state!=poolRandom||runtime->run.seeds.sequence!=sequence||
            runtime->playerState.StateHash()!=playerHash||runtime->playerCreation.StateHash()!=creationHash||
            runtime->sounds.native199!=soundFlags||memcmp(&before,&random,sizeof(random))){delete runtime;return 7;}
        PortSessionStart restarted=session.StartNormalCharacter(seed,*runtime);
        if(restarted.result!=NPS_WORLD_PENDING||runtime->phase!=PNR_GENERATING||runtime->world.residentCount||
            runtime->roomEntryPrefixPrepared||runtime->run.seeds.sequence!=sequence||runtime->itemPools.random.state!=poolRandom||
            runtime->playerManager.worldMembers.size()!=1||runtime->completePlayerInitializations||runtime->completePlayerTicks){delete runtime;return 8;}
        steps=AdvanceToBoundary(session,*runtime);selected=runtime->world.CurrentDescriptor();
        if(!steps||!selected||selected->definition!=definition||selected->listIndex!=list||
            runtime->phase==PNR_ERROR||runtime->phase==PNR_SOURCE_ERROR){delete runtime;return 9;}
        printf("RESTART invalidRetained=1 selected=%d residents=%d next=%08x players=%u ticks=%u MTindex=%d\n",selected->definition,
            runtime->world.residentCount,runtime->missingNativePrerequisite,runtime->completePlayerInitializations,runtime->completePlayerTicks,random.index);
    }
    delete runtime;return failed?5:0;
}
