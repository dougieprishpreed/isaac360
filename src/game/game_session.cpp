#include "game_session.h"
#include "native_room_update.h"
#include "native_rooms.h"
#include "native_rng.h"
#include "native_save_reader.h"
#include <string.h>
#include <stdio.h>
PortNormalRuntime::PortNormalRuntime():audio(0),initialSaveFileWritten(false),suppressInitialNewRunPublish(false),pendingRestoreStage(0),pendingRestoreGrid(-1),pendingRestoreDimension(-1),phase(PNR_IDLE),missingNativePrerequisite(0),completePlayerInitializations(0),completePlayerTicks(0),completeWholeGameplayTicks(0),enemyKills(0),doorsOpened(0),roomTransitions(0),playerHits(0),playerDeathTicks(0),doorsUnlocked(0),gameOverCount(0),pickupsCollected(0),collectiblesCollected(0),playerInvulnerable(0),lastCollectible(0),playerDead(false),bossDefeated(false),descendRequested(false),startingItemsGranted(false),greedMode(false),greedComplete(false),greedWave(0),greedWaveCount(0),greedBossWave(0),greedConfigStage(0),bossAwardIdentity(0),bossAwardAchievement(0),doorTransitionGrace(0),tickFrame(0),nativeSaveCaptureAddress(0),playerTailTrailSegments(0),playerTailRenderCallbacks(0),playerTailTicks(0),roomEntryPrefixPrepared(false) {
    memset(&run,0,sizeof(run));memset(&entryIssue,0,sizeof(entryIssue));
    gameRandom.Default();
}
PortSessionStart PortGameSession::StartNormalCharacter(unsigned int seed,PortNormalRuntime& runtime) {
    //Begin commits only a valid start. An unsupported/locked selection must
    //not discard the existing resident world.
    NormalRunFlow* next=new NormalRunFlow;memset(next,0,sizeof(*next));
    NativePlayerStartup startup;NativeEntityBody body;NativePlayerSprites sprites;NativePlayerState state;NativePlayerCreation creation;
    NativePlayerManager manager;
    NativeRunHistory history;
    NativeRoomFx roomFx;
    NativeRng gameRandom;gameRandom.Default();NativeItemPools itemPools;unsigned int ownerSeed;
    NativeSoundState sounds=runtime.sounds;
    PortSessionStart start=BeginNormalCharacter(seed,*next);
    NativeNightmares nightmares;NativeMt startGlobal=globalRandom;
    //8a1d58..76 is the actual wrapper of6b43f0: reset the14 stage IDs and
    //reshuffle all17 normal nightmare filenames with the existing global MT.
    if(start.result==NPS_WORLD_PENDING&&!nightmares.ResetForNewRun(startGlobal))start.result=NPS_BAD_REQUEST;
    if(start.result==NPS_WORLD_PENDING&&!history.BeginFresh(next->player,frontend.debugConsoleEnabled))start.result=NPS_BAD_REQUEST;
    //6b4678 owns the first sequence draw after928190's14 stage draws and
    //player Init seed.6b47e9 draws again for6eb5e0 before any player exists.
    //Do not change SetStart's standalone contract or replay playerInit.
    if(start.result==NPS_WORLD_PENDING&&(!next->seeds.NextSequence(&ownerSeed)||!gameRandom.Set(ownerSeed,35)||
        !next->seeds.NextSequence(&ownerSeed)||!itemPools.InitializeOrdinary(ownerSeed,next->player,next->profile,sounds)))start.result=NPS_BAD_REQUEST;
    if(start.result==NPS_WORLD_PENDING&&(!creation.PreparePrefix(next->player,next->seeds.playerInit,startup,body,sprites,state,manager,frontend.controller)||
        !manager.RegisterWorld(body)||!manager.FinishSetup(next->profile)||!manager.RefreshFreshHud(body,creation.native153c)||
        !manager.RefreshFreshHud(body,frontend.controller)||!next->SupplyPlayerState(state)))start.result=NPS_BAD_REQUEST;
    if(start.result==NPS_WORLD_PENDING){int stageType;
        if(!next->InitialStageType(&stageType)||!nightmares.SetInitialStage(stageType,next->player.mode))start.result=NPS_BAD_REQUEST;
        else {NormalRunResult result=next->StartInitialFloor();if(result!=NFR_OK&&result!=NFR_PENDING)start.result=NPS_BAD_REQUEST;}}
    if(start.result==NPS_WORLD_PENDING){runtime.run=*next;runtime.world.Reset();memset(&runtime.entryIssue,0,sizeof(runtime.entryIssue));
        runtime.gameRandom=gameRandom;runtime.itemPools.Swap(itemPools);runtime.sounds.native199.swap(sounds.native199);
        runtime.nightmares.Swap(nightmares);globalRandom=startGlobal;
        runtime.history=history;for(int i=0;i<2;++i)runtime.checkpoints[i]=NativeRunCheckpoint();
        runtime.initialSave=NativeGamePersistent();
        runtime.initialSaveFile=NativeSaveWriter();runtime.initialSaveFileWritten=false;
        runtime.playerStartup.Swap(startup);
        runtime.playerBody.Swap(body);
        runtime.playerSprites.Swap(sprites);
        runtime.playerState.Swap(state);
        runtime.playerCreation.Swap(creation);
        runtime.playerManager.Swap(manager);
        runtime.playerUpdate=NativePlayerUpdate();runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();runtime.deathFx.Clear();
        runtime.roomFx.Swap(roomFx);
        runtime.minimaps.Reset();
        runtime.pendingSavedRooms.clear();runtime.pendingRestoreStage=0;
        runtime.pendingRestoreGrid=runtime.pendingRestoreDimension=-1;
        //Virtual Init, the reached resource/callback services and manager
        //registration have returned before006b4b5e generation begins.
        runtime.missingNativePrerequisite=0;runtime.phase=PNR_GENERATING;runtime.tickFrame=0;
        runtime.completePlayerInitializations=runtime.completePlayerTicks=0;runtime.completeWholeGameplayTicks=0;runtime.enemyKills=runtime.doorsOpened=runtime.roomTransitions=0;runtime.playerHits=runtime.playerDeathTicks=runtime.doorsUnlocked=runtime.gameOverCount=runtime.pickupsCollected=runtime.collectiblesCollected=0;runtime.lastCollectible=0;runtime.playerInvulnerable=0;runtime.playerDead=false;runtime.bossDefeated=false;runtime.descendRequested=false;runtime.startingItemsGranted=false;runtime.greedMode=false;runtime.greedComplete=false;runtime.greedWave=0;runtime.greedWaveCount=0;runtime.greedBossWave=0;runtime.greedConfigStage=0;runtime.bossAwardIdentity=0;runtime.bossAwardAchievement=0;runtime.doorTransitionGrace=0;
        runtime.playerTailTrailSegments=0;runtime.playerTailRenderCallbacks=0;runtime.playerTailTicks=0;}
    if(start.result==NPS_WORLD_PENDING)runtime.roomEntryPrefixPrepared=false;
    delete next;
    frontend.CharacterStarted();
    if(start.result==NPS_WORLD_PENDING&&runtime.phase==PNR_GENERATING)frontend.characterPending=true;
    return start;
}
namespace {
struct PendingRestoreCleanup {
    PortNormalRuntime* runtime;
    explicit PendingRestoreCleanup(PortNormalRuntime& value):runtime(&value){}
    ~PendingRestoreCleanup(){runtime->pendingSavedRooms.clear();runtime->pendingRestoreStage=0;
        runtime->pendingRestoreGrid=runtime->pendingRestoreDimension=-1;
        runtime->suppressInitialNewRunPublish=false;}
private:
    PendingRestoreCleanup(const PendingRestoreCleanup&);
    PendingRestoreCleanup& operator=(const PendingRestoreCleanup&);
};
bool GreedWaveDefinitions(int stage,unsigned int seed,const NativeSceneFiles& files,unsigned int wanted,std::vector<int>& output){
    output.clear();
    for(unsigned int variant=0;variant<2048&&output.size()<wanted;++variant){
        int definition=-1;if(NativeFindRoomVariant(stage,11,variant,&definition,NC_GREED)!=NR_OK)continue;
        unsigned char* raw=0;unsigned int length=0,hash=0;
        if(NativeSceneReadRecord(files,definition,&raw,&length,&hash)!=NSC_OK){delete[] raw;continue;}
        RoomRecord record;bool opened=record.Open(raw,length,RRF_STB1)==RRR_OK;
        bool use=false;
        if(opened){
            NativeRoomSeeds seeds;if(NativeInitRoomSeeds(seed+variant*0x9e3779b9U,&seeds)){
                NativeSpawnPlan plan;
                if(NativePlanRoomSpawns(record,seeds,&plan)==NRS_OK){
                    NativeSceneDependencies dependencies;
                    if(NativeSceneInspectPlan(record,plan,files.backdrop,2,&dependencies)==NSC_OK&&dependencies.count==0){
                        for(int i=0;i<plan.count&&!use;++i){
                            const NativeSpawnChoice& choice=plan.choices[i];
                            if(choice.present&&choice.entry.type!=5&&NativePreparedEnemyBody(choice.entry.type,choice.entry.variant))use=true;
                        }
                    }
                }
            }
        }
        delete[] raw;
        if(use)output.push_back(definition);
    }
    return output.size()==wanted;
}
}
PortSessionStart PortGameSession::StartGreedCharacter(unsigned int seed,PortNormalRuntime& runtime,const NativeSceneFiles& files){
    const int requestedMode=frontend.characterMode;
    frontend.characterMode=0;
    NativeSceneFiles normalFiles=files;normalFiles.backdrop=NativeStageBackdrop(1);normalFiles.nativeMode=0;
    PortSessionStart start=StartNormalCharacter(seed,runtime);
    if(start.result!=NPS_WORLD_PENDING){frontend.characterMode=requestedMode;return start;}
    for(int step=0;step<2000&&(runtime.phase==PNR_GENERATING||runtime.phase==PNR_ROOM_COMPONENTS);++step){
        NativeActionFrame frame;frame.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,normalFiles.frame+step);
        frame.SupplyConnection(true);frame.SupplyDrop(false);
        for(int a=0;a<12;++a){frame.SupplyHeld(a,false);frame.SupplyPressed(a,false);if(a<8)frame.SupplyValue(a,0);}
        NativeFrameActions generated(frame);
        if(AdvanceNormalSetup(runtime,normalFiles,&generated)==NFR_BAD_REQUEST)break;
    }
    if(runtime.phase!=PNR_READY){frontend.characterMode=requestedMode;start.result=NPS_BAD_REQUEST;return start;}
    NativeSceneFiles greedFiles=files;greedFiles.backdrop=NativeStageBackdrop(1);greedFiles.nativeMode=2;
    int configStage=2;if(!NativeRoomConfigStage(1,11,-1,2,&configStage))configStage=2;
    const unsigned int waveCount=11+(requestedMode==3?1:0);
    std::vector<int> definitions;
    if(!GreedWaveDefinitions(1,seed,greedFiles,waveCount,definitions)){
        frontend.characterMode=requestedMode;start.result=NPS_BAD_REQUEST;return start;
    }
    int bossDefinition=-1;
    if(NativeFindRoomVariant(25,5,1000,&bossDefinition,NC_GREED)!=NR_OK){
        frontend.characterMode=requestedMode;start.result=NPS_BAD_REQUEST;return start;
    }
    std::vector<int> rooms=definitions;rooms.push_back(bossDefinition);
    NativeLevelLayout layout;layout.Reset();layout.currentDimension=0;layout.count=(int)rooms.size();
    for(unsigned int i=0;i<rooms.size();++i){
        NativePlacedRoom& room=layout.rooms[i];
        const int origin=(int)i*2;
        room.gridIndex=origin;room.safeGridIndex=origin;room.listIndex=(int)i;room.dimension=0;
        room.definition=rooms[i];room.doors=0;room.flags=1;room.visitedCount=0;room.nativeB4=0xffffffffU;
        room.placementSeed=seed+i*0x9e3779b9U+(i?1:0);
        if(!NativeInitRoomSeeds(room.placementSeed,&room.seeds)){frontend.characterMode=requestedMode;start.result=NPS_BAD_REQUEST;return start;}
        NativeCell offsets[4];int footprint=NativeShapeFootprint(NativeRoom(room.definition)->shape,offsets);
        if(!footprint){frontend.characterMode=requestedMode;start.result=NPS_BAD_REQUEST;return start;}
        for(int k=0;k<footprint;++k){
            NativeCell cell={origin%13+offsets[k].x,origin/13+offsets[k].y};int index=NativeCellIndex(cell);
            if(index<0){frontend.characterMode=requestedMode;start.result=NPS_BAD_REQUEST;return start;}
            layout.owners[0][index]=(int)i;
        }
    }
    NativeWorldIssue issue;
    if(runtime.world.Start(layout,0,0,greedFiles,&issue,0)!=NWR_OK){
        runtime.missingNativePrerequisite=0x670001U|((unsigned int)issue.source<<16);frontend.characterMode=requestedMode;start.result=NPS_BAD_REQUEST;return start;
    }
    runtime.run.layout=layout;runtime.run.currentGrid=0;runtime.run.previousGrid=0;runtime.run.placement.dimension=0;
    runtime.run.context.stage=1;runtime.run.context.stageType=0;runtime.run.context.nativeMode=requestedMode;
    runtime.run.context.challenge=false;runtime.run.context.effectiveCurses=0;runtime.run.context.gameFlags=0;
    runtime.run.context.runCounter=0;runtime.run.context.held599=false;runtime.run.context.reservationProfileGate=false;
    runtime.run.floor.phase=NF_COMPLETE;runtime.run.floor.plan.configStage=configStage;runtime.run.phase=NRF_NEED_ROOM_ENTRY;
    runtime.run.player.mode=requestedMode;
    NativeRoomState* room=runtime.world.Current();
    if(!room){frontend.characterMode=requestedMode;start.result=NPS_BAD_REQUEST;return start;}
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    if(!live.gridPostConstructed&&room->type!=16)live.gridPostConstructed=true;
    if(runtime.world.RecordActivation(runtime.run)!=NWR_OK){
        runtime.missingNativePrerequisite=0x7a3b8a;frontend.characterMode=requestedMode;start.result=NPS_BAD_REQUEST;return start;
    }
    runtime.playerBody.position.x=(room->space.left+room->space.right)*0.5f;
    runtime.playerBody.position.y=(room->space.top+room->space.bottom)*0.5f;
    runtime.playerBody.previousPosition=runtime.playerBody.position;
    runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;runtime.playerBody.removed=false;
    runtime.playerInvulnerable=0;runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();runtime.deathFx.Clear();
    runtime.greedMode=true;runtime.greedComplete=false;runtime.greedWave=0;runtime.greedWaveCount=(int)definitions.size();
    runtime.greedBossWave=(int)definitions.size();runtime.greedConfigStage=configStage;
    runtime.startingItemsGranted=false;runtime.phase=PNR_READY;runtime.missingNativePrerequisite=0;
    frontend.characterMode=requestedMode;
    return start;
}
bool PortGameSession::AdvanceGreedWave(PortNormalRuntime& runtime,const NativeSceneFiles& files){
    if(!runtime.greedMode||runtime.greedComplete)return true;
    if(runtime.world.Current()&&!runtime.world.Current()->clear)return true;
    if(runtime.greedWave>=runtime.greedWaveCount){runtime.greedComplete=true;return true;}
    const int next=runtime.greedWave+1;
    if(next>=runtime.run.layout.count){runtime.greedComplete=true;return true;}
    NativeSceneFiles greedFiles=files;greedFiles.backdrop=NativeStageBackdrop(1);greedFiles.nativeMode=2;
    NativeWorldIssue issue;
    int grid=runtime.run.layout.rooms[next].gridIndex;
    if(runtime.world.Enter(grid,0,greedFiles,&issue,0)!=NWR_OK){
        runtime.missingNativePrerequisite=0x670002U|((unsigned int)issue.source<<16);return false;
    }
    runtime.greedWave=next;runtime.run.currentGrid=grid;runtime.run.previousGrid=grid;
    NativeRoomState* room=runtime.world.Current();
    if(room){
        NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
        if(!live.gridPostConstructed&&room->type!=16)live.gridPostConstructed=true;
        if(runtime.world.RecordActivation(runtime.run)!=NWR_OK){
            runtime.missingNativePrerequisite=0x7a3b8a;return false;
        }
        NativePoint at={(room->space.left+room->space.right)*0.5f,(room->space.top+room->space.bottom)*0.5f};
        runtime.playerBody.position=at;runtime.playerBody.previousPosition=at;
        runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;runtime.playerBody.removed=false;
    }
    runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();runtime.deathFx.Clear();
    {unsigned int sample=0;if(!runtime.gameRandom.Next(&sample))sample=1;runtime.playerState.ResetRoomConditionalCache(sample);}
    runtime.doorTransitionGrace=0;
    return true;
}
PortSessionStart PortGameSession::RestartNormal(unsigned int seed,PortNormalRuntime& runtime,const NativeSceneFiles& files,NativeActionSource* actions){
    PortSessionStart start=StartNormalCharacter(seed,runtime);
    if(start.result!=NPS_WORLD_PENDING)return start;
    for(int step=0;step<1000&&(runtime.phase==PNR_GENERATING||runtime.phase==PNR_ROOM_COMPONENTS);++step){
        NormalRunResult r=AdvanceNormalSetup(runtime,files,actions);
        if(r==NFR_BAD_REQUEST)break;
    }
    return start;
}
bool PortGameSession::DescendNormal(PortNormalRuntime& runtime,NativeSceneFiles& files,NativeActionSource* actions){
    (void)actions;
    if(runtime.phase!=PNR_READY||runtime.playerDead)return false;
    const int nextStage=(int)runtime.run.context.stage+1;
    //Basement and Cellar are the prepared graphics; later stages keep their
    //explicit unsupported-context boundary instead of substituting art.
    if(nextStage<2||nextStage>12)return false;
    NativeFloorContext context=runtime.run.context;
    context.stage=nextStage;context.stageType=0;context.effectiveCurses=0;
    context.challenge=false;context.gameFlags=0;context.runCounter=0;
    context.held599=false;context.reservationProfileGate=false;
    //The run loop returns to its floor-context staging point for the next
    //floor; generation consumes the persisted seeds and boss sequence. Build
    //the candidate flow separately so a failed generation leaves the active
    //run and world untouched.
    NormalRunFlow next=runtime.run;
    next.phase=NRF_NEED_FLOOR_CONTEXT;next.result=NFR_OK;
    NormalRunResult result=next.SupplyFloorContext(context);
    if(result!=NFR_OK){runtime.missingNativePrerequisite=0x6401U|((unsigned int)result<<8)|((unsigned int)next.lastCode<<16);return false;}
    int guard=0;
    while(next.phase!=NRF_NEED_ROOM_ENTRY&&guard++<100000){
        NormalRunPhase before=next.phase;result=next.Step();
        if(next.phase==NRF_BLOCKED){runtime.missingNativePrerequisite=0x6402U|((unsigned int)next.lastCode<<16)|((unsigned int)before<<24);return false;}
    }
    if(next.phase!=NRF_NEED_ROOM_ENTRY){runtime.missingNativePrerequisite=0x6403;return false;}
    files.backdrop=NativeStageBackdrop(nextStage);
    runtime.world.Reset();runtime.roomEntryPrefixPrepared=false;memset(&runtime.entryIssue,0,sizeof(runtime.entryIssue));
    const std::vector<NativeSaveSummary::SavedRoom>* restored=
        runtime.pendingRestoreStage==next.context.stage&&!runtime.pendingSavedRooms.empty()?&runtime.pendingSavedRooms:0;
    NativeWorldResult wr=PrepareNormalWorld(next,runtime.world,files,&runtime.entryIssue,restored);
    if(wr!=NWR_OK){runtime.missingNativePrerequisite=0x6404;return false;}
    runtime.run=next;
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    if(!live.doors&&runtime.world.PrepareDoorActors(globalRandom,files.frame)!=NDR_OK){runtime.missingNativePrerequisite=0x6405;return false;}
    if(!live.perimeterReady&&runtime.world.PreparePerimeter(globalRandom,files.frame)!=NWR_OK){runtime.missingNativePrerequisite=0x6406;return false;}
    //Bind the linked door types/variants so clear-state opening and passage
    //rules see the actual targets on the first tick (the initial-room flow
    //performs the same binding inside ContinueInitialRoom).
    runtime.world.PrepareDoorVisuals(runtime.run.context.stage,runtime.run.context.stageType,
        runtime.run.context.nativeMode,runtime.run.context.challenge,runtime.playerState);
    NativeRoomState* room=runtime.world.Current();if(!room){runtime.missingNativePrerequisite=0x6407;return false;}
    //7a1ec4's grid virtual+4 is the direct006d8ef5 return for this reached
    //non-type16 ordinary room, and the later type>=10 entity pass is empty.
    //Descend completes the same post-construction phase before7a3b8a.
    if(!live.gridPostConstructed&&room->type!=16)live.gridPostConstructed=true;
    if(runtime.world.RecordActivation(runtime.run)!=NWR_OK){runtime.missingNativePrerequisite=0x7a3b8a;return false;}
    //Descend replaces the active floor, so the map owners start from that
    //floor's descriptor and actual first room rather than retaining floor N.
    runtime.minimaps.Reset();
    unsigned int minimapNext=0;
    if(!runtime.minimaps.EnterLinked(runtime.run,runtime.world,runtime.playerState,&minimapNext)){
        runtime.missingNativePrerequisite=minimapNext?minimapNext:0x6f4e20;return false;
    }
    runtime.playerBody.position.x=(room->space.left+room->space.right)*0.5f;
    runtime.playerBody.position.y=(room->space.top+room->space.bottom)*0.5f;
    runtime.playerBody.previousPosition=runtime.playerBody.position;
    runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;
    runtime.playerBody.removed=false;
    runtime.playerInvulnerable=0;runtime.doorTransitionGrace=0;
    runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();runtime.deathFx.Clear();
    {unsigned int sample=0;if(!runtime.gameRandom.Next(&sample))sample=1;runtime.playerState.ResetFloorConditionalCache(sample);}
    bool missingNo=false;
    if(runtime.playerState.HasCollectible(258,&missingNo)&&missingNo){
        unsigned int sample=0;if(!runtime.gameRandom.Next(&sample))sample=1;
        runtime.playerState.ApplyMissingNo(runtime.itemPools,sample);
    }
    runtime.descendRequested=false;runtime.bossDefeated=false;
    runtime.roomTransitions=0;runtime.missingNativePrerequisite=0;
    runtime.phase=PNR_READY;
    return true;
}
NormalRunResult PortGameSession::AdvanceNormalSetup(PortNormalRuntime& runtime,const NativeSceneFiles& files,NativeActionSource* actions) {
    if(runtime.phase==PNR_READY)return NFR_PENDING; //Initialized resident; ticks own subsequent frames.
    if(runtime.phase!=PNR_GENERATING&&runtime.phase!=PNR_ROOM_COMPONENTS&&runtime.phase!=PNR_NEED_NATIVE_INIT)return NFR_BAD_REQUEST;
    if(runtime.phase==PNR_GENERATING){
        NormalRunResult r=runtime.run.Step();
        if(runtime.run.phase==NRF_BLOCKED){runtime.phase=PNR_ERROR;frontend.CharacterStarted();return r;}
        if(runtime.run.phase!=NRF_NEED_ROOM_ENTRY)return r;
        if(runtime.pendingRestoreStage==runtime.run.context.stage){
            const NativePlacedRoom* actual=runtime.run.layout.ByCell(runtime.pendingRestoreGrid,runtime.pendingRestoreDimension);
            const NativeRoomDef* definition=actual?NativeRoom(actual->definition):0;
            const NativeSaveSummary::SavedRoom* saved=0;
            for(unsigned int i=0;actual&&definition&&i<runtime.pendingSavedRooms.size();++i){
                const NativeSaveSummary::SavedRoom& candidate=runtime.pendingSavedRooms[i];
                if(candidate.listIndex==actual->listIndex&&candidate.grid==actual->gridIndex&&
                   candidate.safeGrid==actual->safeGridIndex&&candidate.dimension==actual->dimension&&
                   candidate.definitionStage==definition->stage&&candidate.definitionType==definition->type&&
                   candidate.definitionVariant==(int)definition->variant){
                    if(saved){runtime.missingNativePrerequisite=0x6fa670;runtime.phase=PNR_ERROR;return NFR_BAD_REQUEST;}
                    saved=&candidate; }
            }
            if(!actual||!definition||!saved){runtime.missingNativePrerequisite=!actual||!definition?0x6faa68:0x7a1130;
                runtime.phase=PNR_ERROR;return NFR_BAD_REQUEST;}
            //Commit the saved actual entry identity only after its generated
            //descriptor is available, and before the first resident is made.
            runtime.run.currentGrid=runtime.run.previousGrid=runtime.pendingRestoreGrid;
            runtime.run.placement.dimension=runtime.pendingRestoreDimension;
        }
        //NRF_NEED_ROOM_ENTRY is still INSIDE6f9d20, at6fab0a ->6f3b30.
        //The explicit6b4b88 ->76c990 follows the entire generation/entry
        //return.8a1450 is a save caller, not selected-room entry.
        runtime.phase=PNR_ROOM_COMPONENTS;
    }
    //The real selected descriptor, seeds, mode, backdrop and config stage
    //come from this session. No test-supplied evaluated entity is involved.
    if(!runtime.roomEntryPrefixPrepared){
        runtime.phase=PNR_NEED_NATIVE_INIT;
        //6f3b30 precedes Room::Init. Single ordinary player0: no secondary
        //variant1/17 owner is eligible in9082a0/9084b0. Preserve query order.
        const NativePlacedRoom* selected=runtime.run.layout.ByCell(runtime.run.currentGrid,runtime.run.placement.dimension);
        bool held;int multiplier;
        runtime.missingNativePrerequisite=0x6f3b9a;
        if(!selected||runtime.playerManager.members.size()!=1||runtime.playerBody.variant||
            !runtime.playerState.TrinketMultiplier(84,&multiplier)||multiplier)return NFR_PENDING;
        runtime.missingNativePrerequisite=0x6f3be6;
        if(!runtime.playerState.HasCollectible(414,&held)||held)return NFR_PENDING;
        runtime.missingNativePrerequisite=0x6f3c07;
        if(!runtime.playerState.TrinketMultiplier(187,&multiplier)||multiplier)return NFR_PENDING;
        //Challenge flags1b470 are absent in normal New Run. No flag16 means
        //no replacement treasure definition; the real selected room survives.
        //The initial on-grid descriptor has listIndex<507, bypassing3ef5.
        if(runtime.run.context.challenge||(selected->flags&16)||selected->listIndex>=507)return NFR_PENDING;
        runtime.roomEntryPrefixPrepared=true;runtime.missingNativePrerequisite=0;runtime.phase=PNR_ROOM_COMPONENTS;
    }
    //The reached FX loader precedes the main-pass factories. It owns the
    //earlier global MT draw at8d5641 and retains its separate local children.
    if(!runtime.roomFx.PrepareInitial(runtime.run,globalRandom)){
        runtime.phase=PNR_NEED_NATIVE_INIT;
        runtime.missingNativePrerequisite=runtime.roomFx.nextAddress?runtime.roomFx.nextAddress:0x7a0ce3;
        frontend.CharacterStarted();return NFR_PENDING;
    }
    const std::vector<NativeSaveSummary::SavedRoom>* restored=
        runtime.pendingRestoreStage==runtime.run.context.stage&&!runtime.pendingSavedRooms.empty()?&runtime.pendingSavedRooms:0;
    NativeWorldResult r=runtime.world.Current()?NWR_OK:PrepareNormalWorld(runtime.run,runtime.world,files,&runtime.entryIssue,restored);
    if(r!=NWR_OK){runtime.missingNativePrerequisite=runtime.entryIssue.nativeNext?runtime.entryIssue.nativeNext:0x6404;runtime.phase=PNR_ERROR;return NFR_BAD_REQUEST;}
    if(r==NWR_OK){runtime.phase=PNR_NEED_NATIVE_INIT;
        //7cc580 finds no group records: every successfully constructed
        //main-pass factory here is a wall, decoration or no-actor marker.
        //7a6760(false) requires stage11;7d09d0 requires stageType4/5. Both
        //are excluded by this actual first-floor context. Door creation now
        //precedes7c8a90's perimeter in the shared global MT's original order.
        NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
        if(!live.doors&&runtime.world.PrepareDoorActors(globalRandom,files.frame)!=NDR_OK){
            runtime.missingNativePrerequisite=live.restoreAddress?live.restoreAddress:0x6cbe50;
            runtime.phase=PNR_ERROR;return NFR_BAD_REQUEST;}
        r=live.perimeterReady?NWR_OK:runtime.world.PreparePerimeter(globalRandom,files.frame);
        if(r!=NWR_OK){runtime.phase=r==NWR_CONSTRUCTION_GAP?PNR_FACTORY_GAP:PNR_ERROR;return NFR_BAD_REQUEST;}
        if(runtime.world.CurrentRoomRestored()&&!live.gridPostConstructed){
            //The restored saved-grid pass has created every present grid. The
            //reached 7a1ec4 virtual+4 calls are no-ops for this ordinary
            //non-16 room; its later entity list must already be empty. Do not
            //mark this ready for rooms whose entity lifecycle is unsupported.
            if(live.room->type==16||live.enemies.count){runtime.missingNativePrerequisite=0x7a1ec4;runtime.phase=PNR_ERROR;return NFR_BAD_REQUEST;}
            live.gridPostConstructed=true;
        }
        r=runtime.world.ContinueInitialRoom(runtime.run,runtime.playerState,runtime.playerBody,runtime.playerCreation,runtime.playerManager,runtime.roomFx,globalRandom,&runtime.missingNativePrerequisite);
        if(r!=NWR_OK&&r!=NWR_CONSTRUCTION_GAP){runtime.phase=PNR_ERROR;return NFR_BAD_REQUEST;}
        if(r==NWR_CONSTRUCTION_GAP&&runtime.missingNativePrerequisite==0x9062b1){
            //6c2080(-1), first buffer0 after6b3313's active=-1. The
            //guard is live during6b6ef0/907350, suppressing757a50 here.
            runtime.history.activeCheckpoint=0;runtime.history.checkpointGuard=true;
            NativeRunCheckpoint& checkpoint=runtime.checkpoints[0];
            if(!checkpoint.state.CaptureInitial(runtime.run,runtime.world,runtime.gameRandom,runtime.itemPools,runtime.nightmares,runtime.history,
                runtime.playerState,runtime.playerStartup,runtime.playerCreation,runtime.playerSprites,runtime.playerBody,runtime.playerManager,&runtime.missingNativePrerequisite))return NFR_PENDING;
            unsigned int frame;if(!runtime.history.fields.Get(0x1a2e1c,4,&frame)){runtime.missingNativePrerequisite=0x6c216e;return NFR_PENDING;}
            checkpoint.action=-1;checkpoint.frame=frame;
            for(int i=0;i<496;++i)checkpoint.events[i]=runtime.run.profile.eventCounters[i];
            checkpoint.valid=true;runtime.history.checkpointGuard=false;
            runtime.world.resident[runtime.world.currentList].roomInitAddress=0x6f4a7f;
            r=runtime.world.ContinueInitialRoom(runtime.run,runtime.playerState,runtime.playerBody,runtime.playerCreation,runtime.playerManager,runtime.roomFx,globalRandom,&runtime.missingNativePrerequisite);
        }
        if(r==NWR_CONSTRUCTION_GAP){const unsigned int address=runtime.world.resident[runtime.world.currentList].roomInitAddress;
            if(address==0x7990b0||address==0x6f4e20||address==0x905cd0||address==0x88c610||address==0x76c990||address==0x8a1450||address==0x861d60||address==0x8a163d||address==0x8a1745||address==0x906310||address==0x901240||address==0x7ae620)
                r=ContinueInitialGeneration(runtime,actions);}
    }else runtime.phase=r==NWR_CONSTRUCTION_GAP?PNR_FACTORY_GAP:r==NWR_SOURCE_ERROR?PNR_SOURCE_ERROR:PNR_ERROR;
    if(r==NWR_OK&&runtime.world.Current()&&runtime.world.resident[runtime.world.currentList].roomInitAddress==0){
        runtime.phase=PNR_READY;runtime.missingNativePrerequisite=0;runtime.tickFrame=0;
    }
    frontend.CharacterStarted();return r==NWR_OK||r==NWR_CONSTRUCTION_GAP?NFR_PENDING:r==NWR_SOURCE_ERROR?NFR_MISSING_DATA:NFR_BAD_REQUEST;
}
void PortGameSession::AwardBossProgress(PortNormalRuntime& runtime){
    if(!runtime.bossDefeated||runtime.bossAwardAchievement)return;
    const NativePlacedRoom* descriptor=runtime.world.CurrentDescriptor();
    if(!descriptor)return;
    const NativeRoomDef* room=NativeRoom(descriptor->definition);
    if(!room||room->type!=5)return;
    const int identity=(int)room->subtype;
    if(identity<0||identity>=kNativeBossIds)return;
    int achievement=-1;
    for(int i=0;i<runtime.run.bosses.entryCount;++i)
        if((int)runtime.run.bosses.entries[i].id==identity){achievement=runtime.run.bosses.entries[i].achievement;break;}
    if(achievement<=0||achievement>=kNativeAwardCount)return;
    const bool changed=!frontend.profile.awards[achievement];
    frontend.profile.awards[achievement]=true;
    runtime.bossAwardIdentity=identity;runtime.bossAwardAchievement=achievement;
    if(changed){
        unsigned int checksum=0;runtime.initialSave.fields.Get(0x1fb48,4,&checksum);
        unsigned int next=0;
        frontend.profileStorage.SaveAfterNewRun(frontend.profile,checksum,frontend.saveDirectory,frontend.selectedSlot,&next);
    }
}
NormalRunResult PortGameSession::TickNormal(PortNormalRuntime& runtime,const NativeSceneFiles& files,NativeActionSource* actions){
    if(runtime.phase==PNR_GAME_OVER)return NFR_PENDING;
    if(runtime.phase==PNR_DYING){
        ++runtime.playerDeathTicks;
        if(runtime.playerDeathTicks>=60){runtime.phase=PNR_GAME_OVER;++runtime.gameOverCount;}
        return NFR_PENDING;
    }
    if(runtime.playerDead){runtime.phase=PNR_DYING;runtime.playerDeathTicks=0;return NFR_PENDING;}
    if(runtime.phase!=PNR_READY||runtime.world.currentList<0||runtime.world.currentList>=kNativeWorldRoomLimit)return NFR_BAD_REQUEST;
    if(!runtime.startingItemsGranted){
        //Character starting collectibles are granted after the fresh initial
        //save normalization; ownership and non-stat effects apply now, while
        //active-item slots and stat-cache arithmetic remain separate services.
        const NativePlayerDef* def=NativePlayer(runtime.playerState.playerType);
        if(def)for(int i=0;i<def->itemCount;++i)
            if(def->items[i]>0)runtime.playerState.GrantCollectible(def->items[i]);
        runtime.startingItemsGranted=true;
    }
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    if(!live.update||!runtime.playerBody.ready)return NFR_BAD_REQUEST;
    ++runtime.tickFrame;runtime.history.fields.Put(0x1a2e1c,runtime.tickFrame);
    if(!live.update->TickActors(runtime,actions,&files,&globalRandom,&runtime.missingNativePrerequisite))return NFR_PENDING;
    AwardBossProgress(runtime);
    if(runtime.greedMode&&!runtime.greedComplete&&live.room&&live.room->clear){
        if(!AdvanceGreedWave(runtime,files))return NFR_PENDING;
    }
    runtime.missingNativePrerequisite=0;return NFR_OK;
}
PortSessionStart PortGameSession::StartNormalCharacter(unsigned int seed,NormalRunFlow& flow) {
    //Retained generation extraction caller. It is not the owned normal
    //session and cannot establish native player or gameplay acceptance.
    PortSessionStart out=BeginNormalCharacter(seed,flow);
    if(out.result==NPS_WORLD_PENDING){NormalRunResult r=flow.StartInitialFloor();if(r!=NFR_OK&&r!=NFR_PENDING)out.result=NPS_BAD_REQUEST;}
    frontend.CharacterStarted();
    if(out.result==NPS_WORLD_PENDING&&flow.phase!=NRF_NEED_FLOOR_CONTEXT)frontend.characterPending=true;
    return out;
}
PortSessionStart PortGameSession::BeginNormalCharacter(unsigned int seed,NormalRunFlow& flow) {
    PortSessionStart out={NPS_BAD_REQUEST,frontend.characterChoice,-1};
    bool randomOk=out.choice!=0||NativeRandomCharacter(frontend.profile,frontend.characterPage,&globalRandom,&out.choice);
    const NativeCharacterDef* character=NativeCharacter(frontend.characterPage,out.choice);if(character)out.player=character->player;
    NativeRunStart start;
    if(randomOk)out.result=NativePlayerStart(frontend.profile,frontend.characterPage,out.choice,frontend.characterMode,&start);
    if(out.result==NPS_OK)out.result=flow.Begin(seed,start,frontend.profile)?NPS_WORLD_PENDING:NPS_BAD_REQUEST;
    return out;
}
NormalRunResult PortGameSession::AdvanceNormalSetup(NormalRunFlow& flow) {
    if(!frontend.characterPending)return NFR_BAD_REQUEST;
    NormalRunResult result=flow.Step();
    if(flow.phase==NRF_NEED_FLOOR_CONTEXT||flow.phase==NRF_NEED_ROOM_ENTRY||flow.phase==NRF_BLOCKED)frontend.CharacterStarted();
    return result;
}
NativeWorldResult PortGameSession::PrepareNormalWorld(const NormalRunFlow& flow,NativeWorld& world,const NativeSceneFiles& files,NativeWorldIssue* issue,
    const std::vector<NativeSaveSummary::SavedRoom>* restored) {
    if(flow.phase!=NRF_NEED_ROOM_ENTRY||flow.floor.phase!=NF_COMPLETE||flow.currentGrid!=flow.previousGrid||
        files.nativeMode!=flow.context.nativeMode)return NWR_BAD_REQUEST;
    //This caller currently enters only the confirmed first ordinary chapter.
    //Backdrop6's option and alternate-stage flags require their actual inputs.
    //The first two standard floor graphics are prepared (basement, cellar).
    //Later stages retain their explicit unsupported-context boundary.
    if(flow.context.stage<1||flow.context.stage>12||flow.context.stageType<0||flow.context.stageType>2||files.backdrop!=NativeStageBackdrop(flow.context.stage))return NWR_BAD_REQUEST;
    NativeRoomSetupContext setup={files.backdrop,flow.floor.plan.configStage,false,0};
    return world.Start(flow.layout,flow.currentGrid,flow.placement.dimension,files,issue,&setup,restored);
}
namespace {
bool SelectedPlayer(const NormalRunFlow& flow,const NativeWorld& world,const NativePlayerInputContext& c) {
    const NativePlacedRoom* actual=world.CurrentDescriptor();const NativePlacedRoom* selected=flow.layout.ByCell(world.currentGrid,world.layout.currentDimension);
    if(flow.phase!=NRF_NEED_ROOM_ENTRY||flow.floor.phase!=NF_COMPLETE||!actual||!selected||actual->definition!=selected->definition||actual->placementSeed!=selected->placementSeed||
        actual->seeds.spawn!=selected->seeds.spawn||actual->seeds.award!=selected->seeds.award||actual->seeds.decoration!=selected->seeds.decoration||actual->seeds.auxiliary!=selected->seeds.auxiliary||
        c.playerType!=flow.player.player||c.stage!=flow.context.stage||c.stageType!=flow.context.stageType||c.dimension!=world.layout.currentDimension)return false;
    return true;
}
}
NativePlayerInputResult PortGameSession::EvaluateNormalInput(const NormalRunFlow& flow,const NativeWorld& world,const NativePlayerInputContext& c,NativeActionSource& source,bool shooting,NativePoint* output) {
    if(!SelectedPlayer(flow,world,c))return NPI_BAD_REQUEST;
    return shooting?NativeShootingInput(c,source,output):NativeMovementInput(c,source,output);
}
NativePlayerVelocityResult PortGameSession::EvaluateNormalVelocity(const NormalRunFlow& flow,const NativeWorld& world,const NativePlayerVelocityContext& c,NativeActionSource& actions,NativeMovementEffects& effects,NativePlayerVelocityState* output,NativePlayerVelocityIssue* issue) {
    if(!SelectedPlayer(flow,world,c.input)){
        if(issue){issue->nextAddress=0;issue->missingEffect=-1;issue->sourceResult=NPI_BAD_REQUEST;}return NPV_BAD_REQUEST;
    }
    return NativePlayerVelocityCore(c,actions,effects,output,issue);
}

bool PortGameSession::SaveContinue(const char* path,PortNormalRuntime& runtime){
    if(!path||runtime.phase!=PNR_READY||runtime.playerDead||!runtime.world.Current())return false;
    FILE* f=fopen(path,"wb");
    if(!f)return false;
    fprintf(f,"BOI360_C1\n");
    fprintf(f,"%u %d %d %d %d\n",runtime.run.seeds.start,runtime.run.context.stage,
        frontend.characterPage,frontend.characterChoice,frontend.characterMode);
    fprintf(f,"%d %d %d %d %d %d %d\n",runtime.playerState.redHearts,runtime.playerState.maxRedHearts,
        runtime.playerState.soulHearts,runtime.playerState.blackHeartMask,runtime.playerState.keys,
        runtime.playerState.bombs,runtime.playerState.coins);
    fprintf(f,"%.3f %.3f\n",runtime.playerBody.position.x,runtime.playerBody.position.y);
    const NativePlacedRoom* d=runtime.world.CurrentDescriptor();
    fprintf(f,"%d %d %d\n",d?d->definition:-1,d?d->gridIndex:-1,d?d->dimension:-1);
    unsigned int items=0;
    for(unsigned int i=0;i<runtime.playerState.collectibleCounts.size();++i)
        if(runtime.playerState.collectibleCounts[i])++items;
    fprintf(f,"%u\n",items);
    for(unsigned int i=0;i<runtime.playerState.collectibleCounts.size();++i)
        if(runtime.playerState.collectibleCounts[i])fprintf(f,"%u %d\n",i,runtime.playerState.collectibleCounts[i]);
    unsigned int trinkets=0;
    for(unsigned int i=0;i<runtime.playerState.trinketCounts.size();++i)
        if(runtime.playerState.trinketCounts[i])++trinkets;
    fprintf(f,"%u\n",trinkets);
    for(unsigned int i=0;i<runtime.playerState.trinketCounts.size();++i)
        if(runtime.playerState.trinketCounts[i])fprintf(f,"%u %d\n",i,(int)runtime.playerState.trinketCounts[i]);
    fprintf(f,"%d %d %d %d %d %d\n",runtime.playerState.activeItems[0],runtime.playerState.activeItems[1],
        runtime.playerState.activeItems[2],runtime.playerState.activeItems[3],
        runtime.playerState.heldCard,runtime.playerState.heldPill);
    fprintf(f,"GREED %d %d %d %d\n",runtime.greedMode?1:0,runtime.greedWave,runtime.greedWaveCount,runtime.greedBossWave);
    fclose(f);return true;
}
bool PortGameSession::ContinueSavedRun(const char* path,PortNormalRuntime& runtime,NativeSceneFiles& files,NativeActionSource* actions){
    if(!path)return false;
    FILE* f=fopen(path,"rb");
    if(!f)return false;
    char header[16]={0};
    if(!fgets(header,sizeof(header),f)||strncmp(header,"BOI360_C1",9)){fclose(f);return false;}
    unsigned int seed=0;int stage=1,page=0,choice=1,mode=0;
    int hp=0,maxHp=0,soul=0,black=0,keys=0,bombs=0,coins=0;
    float px=320.0f,py=380.0f;int definition=-1,grid=-1,dimension=-1;
    if(fscanf(f,"%u %d %d %d %d",&seed,&stage,&page,&choice,&mode)!=5){fclose(f);return false;}
    if(fscanf(f,"%d %d %d %d %d %d %d",&hp,&maxHp,&soul,&black,&keys,&bombs,&coins)!=7){fclose(f);return false;}
    if(fscanf(f,"%f %f",&px,&py)!=2){fclose(f);return false;}
    if(fscanf(f,"%d %d %d",&definition,&grid,&dimension)!=3){fclose(f);return false;}
    unsigned int items=0;if(fscanf(f,"%u",&items)!=1){fclose(f);return false;}
    std::vector<int> itemIds,itemCounts;
    for(unsigned int i=0;i<items;++i){unsigned int id;int count=0;
        if(fscanf(f,"%u %d",&id,&count)!=2){fclose(f);return false;}
        itemIds.push_back((int)id);itemCounts.push_back(count);}
    unsigned int trinkets=0;if(fscanf(f,"%u",&trinkets)!=1){fclose(f);return false;}
    std::vector<int> trinketIds,trinketCounts;
    for(unsigned int i=0;i<trinkets;++i){unsigned int id;int count=0;
        if(fscanf(f,"%u %d",&id,&count)!=2){fclose(f);return false;}
        trinketIds.push_back((int)id);trinketCounts.push_back(count);}
    int active[4]={0,0,0,0},card=0,pill=-1;
    if(fscanf(f,"%d %d %d %d %d %d",&active[0],&active[1],&active[2],&active[3],&card,&pill)!=6){fclose(f);return false;}
    int greed=0,greedWave=0,greedWaveCount=0,greedBossWave=0;
    char key[32];while(fscanf(f,"%31s",key)==1){if(!strcmp(key,"GREED")){if(fscanf(f,"%d %d %d %d",&greed,&greedWave,&greedWaveCount,&greedBossWave)!=4){fclose(f);return false;}break;}}
    fclose(f);
    files.backdrop=NativeStageBackdrop(1);
    frontend.characterPage=page;frontend.characterChoice=choice;frontend.characterMode=mode;
    PortSessionStart start;
    if(greed){
        start=StartGreedCharacter(seed,runtime,files);
        if(start.result!=NPS_WORLD_PENDING)return false;
        if(runtime.phase!=PNR_READY)return false;
        if(greedWaveCount>0)runtime.greedWaveCount=greedWaveCount;
        while(runtime.greedWave<greedWave){
            NativeRoomState* current=runtime.world.Current();if(!current)return false;
            current->clear=true;
            if(!AdvanceGreedWave(runtime,files)||runtime.greedWave>greedWave)return false;
        }
    }else{
        start=StartNormalCharacter(seed,runtime);
        if(start.result!=NPS_WORLD_PENDING)return false;
        for(int step=0;step<2000&&(runtime.phase==PNR_GENERATING||runtime.phase==PNR_ROOM_COMPONENTS);++step){
            NativeActionFrame frame;frame.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame+step);
            frame.SupplyConnection(true);frame.SupplyDrop(false);
            for(int a=0;a<12;++a){frame.SupplyHeld(a,false);frame.SupplyPressed(a,false);if(a<8)frame.SupplyValue(a,0);}
            NativeFrameActions generated(frame);
            NormalRunResult r=AdvanceNormalSetup(runtime,files,actions?actions:&generated);
            if(r==NFR_BAD_REQUEST)return false;
        }
        if(runtime.phase!=PNR_READY)return false;
        while(runtime.run.context.stage<stage){
            files.backdrop=NativeStageBackdrop(runtime.run.context.stage+1);
            if(!DescendNormal(runtime,files,actions))return false;
        }
    }
    runtime.playerState.collectibleCounts.assign(runtime.playerState.collectibleCounts.size()?runtime.playerState.collectibleCounts.size():733,0);
    for(unsigned int i=0;i<itemIds.size();++i)
        if(itemIds[i]>0&&(unsigned int)itemIds[i]<runtime.playerState.collectibleCounts.size())
            runtime.playerState.collectibleCounts[itemIds[i]]=itemCounts[i];
    runtime.playerState.trinketCounts.assign(runtime.playerState.trinketCounts.size()?runtime.playerState.trinketCounts.size():190,0);
    for(unsigned int i=0;i<trinketIds.size();++i)
        if(trinketIds[i]>=0&&(unsigned int)trinketIds[i]<runtime.playerState.trinketCounts.size())
            runtime.playerState.trinketCounts[trinketIds[i]]=(unsigned char)trinketCounts[i];
    for(int i=0;i<4;++i)runtime.playerState.activeItems[i]=active[i];
    runtime.playerState.heldCard=card;runtime.playerState.heldPill=pill;
    runtime.playerState.maxRedHearts=maxHp;runtime.playerState.redHearts=hp;
    runtime.playerState.soulHearts=soul;runtime.playerState.blackHeartMask=black;
    runtime.playerState.keys=keys;runtime.playerState.bombs=bombs;runtime.playerState.coins=coins;
    runtime.startingItemsGranted=true;
    if(!greed&&definition>=0){
        for(int i=0;i<runtime.world.layout.count;++i){
            const NativePlacedRoom& p=runtime.world.layout.rooms[i];
            if(p.definition==definition&&p.gridIndex>=0){
                NativeWorldIssue issue;
                if(runtime.world.Enter(p.gridIndex,p.dimension,files,&issue,0)==NWR_OK)break;
            }
        }
    }
    NativeRoomState* room=runtime.world.Current();
    if(room){
        NativePoint at={px,py},clamped;
        if(room->space.Clamp(at,2,2,2,2,&clamped))at=clamped;
        runtime.playerBody.position=at;runtime.playerBody.previousPosition=at;
        runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;
    }
    runtime.phase=PNR_READY;runtime.doorTransitionGrace=0;
    runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();runtime.deathFx.Clear();
    return true;
}

namespace {
unsigned int SaveArithmeticRight(unsigned int n){return (n>>1)|(n&0x80000000U);}
unsigned int SaveCrcEntry(unsigned int n){
    n=(n>>1)^((n&1)?0xedb88320U:0);
    for(int i=1;i<8;++i)n=SaveArithmeticRight(n)^((n&1)?0xedb88320U:0);
    return n;
}
bool NativeSaveSeed(const char* path,unsigned int* seed){
    if(!path||!seed)return false;
    FILE* f=fopen(path,"rb");if(!f)return false;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return false;}
    long size=ftell(f);if(size<64||fseek(f,0,SEEK_SET)!=0){fclose(f);return false;}
    std::vector<unsigned char> data((unsigned int)size);
    size_t got=fread(&data[0],1,(size_t)size,f);fclose(f);
    if(got!=(size_t)size||memcmp(&data[0],"ISAACNG_GSR0142",15))return false;
    unsigned int checksum=0xfedcba76U;
    for(long i=16;i<size-4;++i){
        unsigned int n=~checksum;n=(n>>8)^SaveCrcEntry((n&255)^data[i]);checksum=~n;
    }
    unsigned int stored=(unsigned int)data[size-4]|((unsigned int)data[size-3]<<8)|
        ((unsigned int)data[size-2]<<16)|((unsigned int)data[size-1]<<24);
    if(stored!=(checksum^0x96696996U))return false;
    long first=size-320;if(first<16)first=16;
    for(long i=first;i+10<=size;++i){
        if(data[i+9]!=10)continue;
        char text[10];memcpy(text,&data[i],9);text[9]=0;
        if(NativeSeedDecode(text,seed))return true;
    }
    return false;
}
bool ValidateCurrentRoomPickups(const NativeSaveSummary& summary,PortNormalRuntime& runtime){
    const NativePlacedRoom* descriptor=runtime.world.CurrentDescriptor();
    if(!descriptor||runtime.world.currentList<0||runtime.world.currentList>=kNativeWorldRoomLimit)return false;
    const NativeRoomDef* definition=NativeRoom(descriptor->definition);
    const NativeSaveSummary::SavedRoom* savedRoom=runtime.world.RestoredRoom(runtime.world.currentList);
    if(!definition||!summary.hasCurrentRoom||summary.currentSavedRoomIndex<0||!savedRoom||
       savedRoom->listIndex!=runtime.world.currentList||
       savedRoom->grid!=descriptor->gridIndex||savedRoom->safeGrid!=descriptor->safeGridIndex||
       savedRoom->dimension!=descriptor->dimension||
       runtime.world.layout.currentDimension!=summary.dimension||
       savedRoom->definitionStage!=definition->stage||savedRoom->definitionType!=definition->type||
       savedRoom->definitionVariant!=(int)definition->variant)return false;
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    std::vector<bool> used(live.pickups.count?live.pickups.count:0,false);
    std::vector<int> matches(summary.currentRoomPickups.size(),-1);
    for(unsigned int savedIndex=0;savedIndex<summary.currentRoomPickups.size();++savedIndex){
        const NativeSaveSummary::Pickup& saved=summary.currentRoomPickups[savedIndex];
        int matched=-1;
        for(int liveIndex=0;liveIndex<live.pickups.count;++liveIndex){
            const NativePickup* pickup=live.pickups.entries[liveIndex];
            if(!pickup||used[liveIndex]||pickup->type!=saved.type||pickup->variant!=saved.variant||
               pickup->subtype!=saved.subtype||pickup->grid!=saved.grid||
               pickup->initialSeed!=saved.initialSeed)continue;
            if(matched>=0)return false; //Ambiguous duplicate live identity.
            matched=liveIndex;
        }
        if(matched<0)return false; //Saved live owner has no reconstructed lifecycle owner.
        used[matched]=true;matches[savedIndex]=matched;
    }
    if(live.pickups.count!=(int)summary.currentRoomPickups.size())return false;
    //NativeWorld::Enter has already applied the one authoritative saved
    //pickup reconciliation. This second pass verifies every resulting owner
    //without mutating or re-running callbacks/RNG.
    for(unsigned int savedIndex=0;savedIndex<summary.currentRoomPickups.size();++savedIndex){
        const NativeSaveSummary::Pickup& saved=summary.currentRoomPickups[savedIndex];
        NativePickup* pickup=live.pickups.entries[matches[savedIndex]];
        if(!pickup||!pickup->ready||pickup->collected||pickup->type!=saved.type||pickup->variant!=saved.variant||
           pickup->subtype!=saved.subtype||pickup->grid!=saved.grid||pickup->currentSeed!=saved.currentSeed||
           pickup->initialSeed!=saved.initialSeed||memcmp(&pickup->position.x,&saved.x,4)||memcmp(&pickup->position.y,&saved.y,4)||
           pickup->native46c!=saved.native46c||pickup->native478!=saved.native478||pickup->native480!=saved.native480||
           pickup->native484!=saved.native484||pickup->native27c!=saved.native27c||pickup->native474!=saved.native474||
           pickup->native476!=saved.native476||pickup->native4cc!=saved.native4cc||pickup->native470!=saved.native470||
           pickup->native34!=saved.native34||pickup->native38!=saved.native38||pickup->native360!=saved.native360||
           pickup->native4e0!=saved.native4e0||pickup->native47c!=saved.native47c||
           pickup->nativeExtraCount!=saved.nativeExtraCount||pickup->nativeChildCount!=saved.nativeChildCount||
           memcmp(&pickup->nativeD8,&saved.nativeD8,4))return false;
    }
    return true;
}
}
bool PortGameSession::ContinueSelectedNativeRun(PortNormalRuntime& runtime,NativeSceneFiles& files,NativeActionSource* actions){
    std::string primary,rerun;
    if(!frontend.LocalRunFilenames(&primary,&rerun))return false;
    return ContinueNativeSave(primary.c_str(),runtime,files,actions);
}
bool PortGameSession::SaveSelectedNativeContinue(PortNormalRuntime& runtime){
    std::string primary,rerun;
    if(!frontend.LocalRunFilenames(&primary,&rerun))return false;
    return SaveNativeContinue(primary.c_str(),runtime);
}
bool PortGameSession::ContinueNativeSave(const char* path,PortNormalRuntime& runtime,NativeSceneFiles& files,NativeActionSource* actions){
    unsigned int seed=0;
    if(!NativeSaveSeed(path,&seed))return false;
    NativeSaveSummary summary;
    //A native Continue is authoritative. Decode and validate the complete
    //supported GSR0142 payload before touching frontend or runtime state.
    if(!NativeReadSaveSummary(path,summary)||!summary.valid||!summary.hasPlayer||summary.seed!=seed)return false;
    PendingRestoreCleanup pendingCleanup(runtime);
    const int savedMode=summary.greed?(summary.greedWaveCount>11?3:2):summary.nativeMode;
    if((summary.greed&&(savedMode<2||savedMode>3))||(!summary.greed&&(savedMode<0||savedMode>1)))return false;
    //The native load path cannot execute8a163d/8a1745's fresh-run publication.
    //Keep the selected canonical file untouched while the existing bootstrap
    //reconstructs the owned run and room records in memory.
    runtime.suppressInitialNewRunPublish=true;
    const int savedPlayer=summary.playerId;
    if(savedPlayer<0)return false;
    int savedPage=-1,savedChoice=-1;
    for(int page=0;page<2&&savedPage<0;++page)for(int choice=0;choice<kNativeCharacterCount;++choice){
        const NativeCharacterDef* c=NativeCharacter(page,choice);
        if(c&&c->player==savedPlayer){savedPage=page;savedChoice=choice;break;}
    }
    if(savedPage<0||savedChoice<0)return false;
    frontend.characterPage=savedPage;frontend.characterChoice=savedChoice;frontend.characterMode=savedMode;
    files.backdrop=NativeStageBackdrop(1);
    PortSessionStart start;
    if(summary.greed){
        start=StartGreedCharacter(seed,runtime,files);
        if(start.result!=NPS_WORLD_PENDING)return false;
        if(runtime.phase!=PNR_READY)return false;
    }else{
        start=StartNormalCharacter(seed,runtime);
        if(start.result!=NPS_WORLD_PENDING)return false;
        files.nativeMode=runtime.run.context.nativeMode;
        if(runtime.phase!=PNR_GENERATING||runtime.run.context.nativeMode!=savedMode||
           summary.currentGrid<0||summary.dimension<0||summary.dimension>2){
            runtime.missingNativePrerequisite=0x6faa68;return false;}
        runtime.pendingSavedRooms=summary.savedRooms;
        runtime.pendingRestoreStage=summary.stage;
        runtime.pendingRestoreGrid=summary.currentGrid;
        runtime.pendingRestoreDimension=summary.dimension;
        for(int step=0;step<2000&&(runtime.phase==PNR_GENERATING||runtime.phase==PNR_ROOM_COMPONENTS);++step){
            NativeActionFrame frame;frame.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame+step);
            frame.SupplyConnection(true);frame.SupplyDrop(false);
            for(int a=0;a<12;++a){frame.SupplyHeld(a,false);frame.SupplyPressed(a,false);if(a<8)frame.SupplyValue(a,0);}
            NativeFrameActions generated(frame);
            //The load path rebuilds the restored player identity before this
            //loop. A pre-load caller frame still carries the old controller/
            //token owner, so it cannot service the restored player's first
            //setup tick. Use the frame constructed from the restored creation;
            //the next live gameplay frame is built normally after Continue.
            NormalRunResult r=AdvanceNormalSetup(runtime,files,&generated);
            if(r==NFR_BAD_REQUEST)return false;
        }
        if(runtime.phase!=PNR_READY)return false;
    }
    files.nativeMode=runtime.run.context.nativeMode;
    if(summary.greed){
        const int targetWave=summary.greedWave;
        if(targetWave<0||targetWave>runtime.greedWaveCount)return false;
        while(runtime.greedWave<targetWave){
            NativeRoomState* current=runtime.world.Current();if(!current)return false;
            current->clear=true;
            if(!AdvanceGreedWave(runtime,files)||runtime.greedWave>targetWave)return false;
        }
    }else{
        if(summary.stage<1||summary.stage>12)return false;
        while(runtime.run.context.stage<summary.stage){
            files.backdrop=NativeStageBackdrop(runtime.run.context.stage+1);
            if(!DescendNormal(runtime,files,actions))return false;
        }
    }
    //Replay the saved collectible counts through the maintained grant path so
    //stats/active slots see the same item owners, then restore the exact
    //numeric resources and pocket identities from the native record.
    runtime.playerState.collectibleCounts.assign(runtime.playerState.collectibleCounts.size(),0);
    for(int i=0;i<4;++i)runtime.playerState.activeItems[i]=0;
    runtime.playerState.pendingCollectibles.clear();runtime.playerState.collectibleOverrides.clear();
    for(unsigned int id=0;id<summary.collectibles.size();++id){
        const int count=summary.collectibles[id];
        if(count<0)return false;
        for(int n=0;n<count;++n)if(!runtime.playerState.GrantCollectible((int)id))return false;
    }
    runtime.playerState.trinketCounts.assign(runtime.playerState.trinketCounts.size(),0);
    for(unsigned int i=0;i<summary.trinkets.size()&&i<runtime.playerState.trinketCounts.size();++i)
        runtime.playerState.trinketCounts[i]=summary.trinkets[i];
    runtime.playerState.heldTrinkets[0]=summary.heldTrinkets[0];
    runtime.playerState.heldTrinkets[1]=summary.heldTrinkets[1];
    for(int i=0;i<4;++i)runtime.playerState.activeItems[i]=summary.activeItems[i];
    runtime.playerState.heldCard=summary.heldCard;
    runtime.playerState.heldPill=summary.heldPill;
    for(unsigned int i=0;i<summary.effects.size();++i){
        const NativeSaveSummary::Effect& e=summary.effects[i];
        const NativeEffectDef* def=NativePreparedEffect(e.type,e.id);
        if(!def||runtime.playerState.effects.Add(def,e.count,false,0)!=NEF_OK)return false;
        if(!runtime.playerState.effects.SetTimer(e.type,e.id,e.timer))return false;
    }
    if(summary.hasTriggerCache){
        NativePlayerState& s=runtime.playerState;
        s.permDamageBonus=summary.permDamageBonus;s.permFireRateBonus=summary.permFireRateBonus;
        s.permRangeBonus=summary.permRangeBonus;s.permShotSpeedBonus=summary.permShotSpeedBonus;
        s.permSpeedBonus=summary.permSpeedBonus;s.permLuckBonus=summary.permLuckBonus;
        s.damageTakenThisFloor=summary.damageTakenThisFloor;
        s.bloodOathDamage=summary.bloodOathDamage;s.bloodOathSpeed=summary.bloodOathSpeed;
        s.roomDamageBonus=summary.roomDamageBonus;s.roomSpeedBonus=summary.roomSpeedBonus;
        s.purityAura=summary.purityAura;s.purityActive=summary.purityActive;
        s.crownOfLightDisabled=summary.crownOfLightDisabled;
    }
    runtime.playerState.maxRedHearts=summary.maxRedHearts;runtime.playerState.redHearts=summary.redHearts;
    runtime.playerState.soulHearts=summary.soulHearts;runtime.playerState.blackHeartMask=summary.blackHeartMask;
    runtime.playerState.keys=summary.keys;runtime.playerState.bombs=summary.bombs;runtime.playerState.coins=summary.coins;
    runtime.startingItemsGranted=true;
    if(summary.hasPools&&!runtime.itemPools.ApplySnapshot(summary.pools))return false;
    if(!summary.greed&&summary.currentGrid>=0&&(runtime.world.currentGrid!=summary.currentGrid||
       runtime.world.layout.currentDimension!=summary.dimension)){
        NativeWorldIssue issue;NativeRoomSetupContext setup={files.backdrop,runtime.run.floor.plan.configStage,false,0};
        if(runtime.world.Enter(summary.currentGrid,summary.dimension,files,&issue,&setup)!=NWR_OK){
            runtime.missingNativePrerequisite=issue.nativeNext?issue.nativeNext:0x6f5037;return false;}
    }
    if(!summary.greed){
        //The save stores descriptor origins, while currentGrid is the actual
        //entered cell. Resolve through the generated layout so a large-room
        //footprint cannot alias another descriptor.
        const NativePlacedRoom* actual=runtime.world.CurrentDescriptor();
        const NativeSaveSummary::SavedRoom* saved=0;
        int savedIndex=-1;
        for(unsigned int i=0;actual&&i<summary.savedRooms.size();++i){
            const NativeSaveSummary::SavedRoom& candidate=summary.savedRooms[i];
            if(candidate.listIndex==runtime.world.currentList&&candidate.grid==actual->gridIndex&&
               candidate.safeGrid==actual->safeGridIndex&&candidate.dimension==actual->dimension){
                if(saved)return false;
                saved=&candidate;savedIndex=(int)i;
            }
        }
        if(!actual||!saved)return false;
        summary.currentSavedRoomIndex=savedIndex;
        summary.roomDefinition=saved->definitionStage;summary.roomDefinitionType=saved->definitionType;
        summary.roomDefinitionVariant=saved->definitionVariant;
        summary.hasCurrentRoom=saved->visitedCount!=0;
        if(summary.hasCurrentRoom){
            const NativeSaveSummary::SavedRoom* restored=runtime.world.RestoredRoom(runtime.world.currentList);
            if(!restored||restored->listIndex!=saved->listIndex||restored->grid!=saved->grid)return false;
            summary.currentRoomPickups=saved->pickups;
        }
    }
    if(!summary.greed&&summary.hasCurrentRoom&&!ValidateCurrentRoomPickups(summary,runtime))return false;
    NativeRoomState* room=runtime.world.Current();
    if(summary.hasExtras){
        runtime.playerBody.position.x=summary.positionX;runtime.playerBody.position.y=summary.positionY;
        runtime.playerBody.previousPosition=runtime.playerBody.position;
        runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;runtime.playerBody.removed=false;
        runtime.playerState.stats.fireDelay=summary.fireDelay;
        runtime.playerState.stats.damage=summary.damage;
        runtime.playerState.stats.moveSpeed=summary.moveSpeed;runtime.playerState.stats.luck=summary.luck;
        runtime.playerState.stats.dirty=0xffffU;
    }else if(room){
        NativePoint at={(room->space.left+room->space.right)*0.5f,(room->space.top+room->space.bottom)*0.5f};
        runtime.playerBody.position=at;runtime.playerBody.previousPosition=at;
        runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;runtime.playerBody.removed=false;
    }
    runtime.phase=PNR_READY;runtime.doorTransitionGrace=0;runtime.descendRequested=false;
    runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();runtime.deathFx.Clear();
    return true;
}
bool PortGameSession::SaveNativeContinue(const char* path,PortNormalRuntime& runtime){
    if(!path||runtime.phase!=PNR_READY||runtime.playerDead)return false;
    NativeGamePersistent snapshot;unsigned int next=0;
    const bool captured=snapshot.CaptureCurrent(runtime.run,runtime.world,runtime.gameRandom,runtime.itemPools,runtime.nightmares,runtime.history,
        runtime.playerState,runtime.playerStartup,runtime.playerCreation,runtime.playerSprites,runtime.playerBody,runtime.playerManager,&next);
    runtime.nativeSaveCaptureAddress=next;
    if(!captured)return false;
    NativeSaveWriter writer;
    if(!writer.InitialGame(snapshot)){
        runtime.nativeSaveCaptureAddress=writer.nextAddress;return false;
    }
    FILE* file=fopen(path,"wb");
    if(!file)return false;
    size_t wrote=writer.data.empty()?0:fwrite(&writer.data[0],1,writer.data.size(),file);
    bool closed=fclose(file)==0;
    return wrote==writer.data.size()&&closed;
}
