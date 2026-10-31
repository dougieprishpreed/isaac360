#include "game_session.h"
#include "native_room_update.h"
#include "native_rng.h"
#include "native_save_reader.h"
#include <string.h>
#include <stdio.h>
PortNormalRuntime::PortNormalRuntime():initialSaveFileWritten(false),phase(PNR_IDLE),missingNativePrerequisite(0),completePlayerInitializations(0),completePlayerTicks(0),completeWholeGameplayTicks(0),enemyKills(0),doorsOpened(0),roomTransitions(0),playerHits(0),playerDeathTicks(0),doorsUnlocked(0),gameOverCount(0),pickupsCollected(0),collectiblesCollected(0),playerInvulnerable(0),lastCollectible(0),playerDead(false),bossDefeated(false),descendRequested(false),startingItemsGranted(false),doorTransitionGrace(0),tickFrame(0),nativeSaveCaptureAddress(0),playerTailTrailSegments(0),playerTailRenderCallbacks(0),playerTailTicks(0),roomEntryPrefixPrepared(false) {
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
        runtime.playerUpdate=NativePlayerUpdate();runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();
        runtime.roomFx.Swap(roomFx);
        runtime.minimaps.Reset();
        //Virtual Init, the reached resource/callback services and manager
        //registration have returned before006b4b5e generation begins.
        runtime.missingNativePrerequisite=0;runtime.phase=PNR_GENERATING;runtime.tickFrame=0;
        runtime.completePlayerInitializations=runtime.completePlayerTicks=0;runtime.completeWholeGameplayTicks=0;runtime.enemyKills=runtime.doorsOpened=runtime.roomTransitions=0;runtime.playerHits=runtime.playerDeathTicks=runtime.doorsUnlocked=runtime.gameOverCount=runtime.pickupsCollected=runtime.collectiblesCollected=0;runtime.lastCollectible=0;runtime.playerInvulnerable=0;runtime.playerDead=false;runtime.bossDefeated=false;runtime.descendRequested=false;runtime.startingItemsGranted=false;runtime.doorTransitionGrace=0;
        runtime.playerTailTrailSegments=0;runtime.playerTailRenderCallbacks=0;runtime.playerTailTicks=0;}
    if(start.result==NPS_WORLD_PENDING)runtime.roomEntryPrefixPrepared=false;
    delete next;
    frontend.CharacterStarted();
    if(start.result==NPS_WORLD_PENDING&&runtime.phase==PNR_GENERATING)frontend.characterPending=true;
    return start;
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
    NativeWorldResult wr=PrepareNormalWorld(next,runtime.world,files,&runtime.entryIssue);
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
    runtime.playerBody.position.x=(room->space.left+room->space.right)*0.5f;
    runtime.playerBody.position.y=(room->space.top+room->space.bottom)*0.5f;
    runtime.playerBody.previousPosition=runtime.playerBody.position;
    runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;
    runtime.playerBody.removed=false;
    runtime.playerInvulnerable=0;runtime.doorTransitionGrace=0;
    runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();
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
    NativeWorldResult r=runtime.world.Current()?NWR_OK:PrepareNormalWorld(runtime.run,runtime.world,files,&runtime.entryIssue);
    if(r==NWR_OK){runtime.phase=PNR_NEED_NATIVE_INIT;
        //7cc580 finds no group records: every successfully constructed
        //main-pass factory here is a wall, decoration or no-actor marker.
        //7a6760(false) requires stage11;7d09d0 requires stageType4/5. Both
        //are excluded by this actual first-floor context. Door creation now
        //precedes7c8a90's perimeter in the shared global MT's original order.
        NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
        if(!live.doors&&runtime.world.PrepareDoorActors(globalRandom,files.frame)!=NDR_OK){runtime.phase=PNR_ERROR;return NFR_BAD_REQUEST;}
        r=live.perimeterReady?NWR_OK:runtime.world.PreparePerimeter(globalRandom,files.frame);
        if(r!=NWR_OK){runtime.phase=r==NWR_CONSTRUCTION_GAP?PNR_FACTORY_GAP:PNR_ERROR;return NFR_BAD_REQUEST;}
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
NativeWorldResult PortGameSession::PrepareNormalWorld(const NormalRunFlow& flow,NativeWorld& world,const NativeSceneFiles& files,NativeWorldIssue* issue) {
    if(flow.phase!=NRF_NEED_ROOM_ENTRY||flow.floor.phase!=NF_COMPLETE||flow.currentGrid!=flow.previousGrid||
        files.nativeMode!=flow.context.nativeMode)return NWR_BAD_REQUEST;
    //This caller currently enters only the confirmed first ordinary chapter.
    //Backdrop6's option and alternate-stage flags require their actual inputs.
    //The first two standard floor graphics are prepared (basement, cellar).
    //Later stages retain their explicit unsupported-context boundary.
    if(flow.context.stage<1||flow.context.stage>12||flow.context.stageType<0||flow.context.stageType>2||files.backdrop!=NativeStageBackdrop(flow.context.stage))return NWR_BAD_REQUEST;
    NativeRoomSetupContext setup={files.backdrop,flow.floor.plan.configStage,false,0};
    return world.Start(flow.layout,flow.currentGrid,flow.placement.dimension,files,issue,&setup);
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
    fclose(f);
    files.backdrop=NativeStageBackdrop(1);
    frontend.characterPage=page;frontend.characterChoice=choice;frontend.characterMode=mode;
    PortSessionStart start=StartNormalCharacter(seed,runtime);
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
    if(definition>=0){
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
    runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();
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
}
namespace { bool NativeSavePlayerId(const char*,int*); }
bool PortGameSession::ContinueNativeSave(const char* path,PortNormalRuntime& runtime,NativeSceneFiles& files,NativeActionSource* actions){
    unsigned int seed=0;
    if(!NativeSaveSeed(path,&seed))return false;
    NativeSaveSummary summary;
    const bool exact=NativeReadSaveSummary(path,summary)&&summary.valid&&summary.hasPlayer&&summary.seed==seed;
    
    int savedPlayer=-1;
    if(exact)savedPlayer=summary.playerId;
    else if(!NativeSavePlayerId(path,&savedPlayer))savedPlayer=-1;
    if(savedPlayer>=0){
        bool found=false;
        for(int page=0;page<2&&!found;++page)for(int choice=0;choice<kNativeCharacterCount;++choice){
            const NativeCharacterDef* c=NativeCharacter(page,choice);
            if(c&&c->player==savedPlayer){frontend.characterPage=page;frontend.characterChoice=choice;found=true;break;}
        }
    }
    files.backdrop=NativeStageBackdrop(1);
    PortSessionStart start=StartNormalCharacter(seed,runtime);
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
    if(!exact)return true;
    if(summary.stage<1||summary.stage>12)return false;
    while(runtime.run.context.stage<summary.stage){
        files.backdrop=NativeStageBackdrop(runtime.run.context.stage+1);
        if(!DescendNormal(runtime,files,actions))return false;
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
    runtime.playerState.maxRedHearts=summary.maxRedHearts;runtime.playerState.redHearts=summary.redHearts;
    runtime.playerState.soulHearts=summary.soulHearts;runtime.playerState.blackHeartMask=summary.blackHeartMask;
    runtime.playerState.keys=summary.keys;runtime.playerState.bombs=summary.bombs;runtime.playerState.coins=summary.coins;
    runtime.startingItemsGranted=true;
    if(summary.hasPools&&!runtime.itemPools.ApplySnapshot(summary.pools))return false;
    if(summary.currentGrid>=0&&(runtime.world.currentGrid!=summary.currentGrid||
       runtime.world.layout.currentDimension!=summary.dimension)){
        NativeWorldIssue issue;NativeRoomSetupContext setup={files.backdrop,runtime.run.floor.plan.configStage,false,0};
        if(runtime.world.Enter(summary.currentGrid,summary.dimension,files,&issue,&setup)!=NWR_OK)return false;
    }
    NativeRoomState* room=runtime.world.Current();
    if(summary.hasExtras){
        runtime.playerBody.position.x=summary.positionX;runtime.playerBody.position.y=summary.positionY;
        runtime.playerBody.previousPosition=runtime.playerBody.position;
        runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;runtime.playerBody.removed=false;
        runtime.playerState.stats.fireDelay=summary.fireDelay;
        runtime.playerState.stats.damage=summary.damage;runtime.playerState.stats.baseDamage=summary.damage;
        runtime.playerState.stats.moveSpeed=summary.moveSpeed;runtime.playerState.stats.luck=summary.luck;
        runtime.playerState.stats.dirty=0xffffU;
    }else if(room){
        NativePoint at={(room->space.left+room->space.right)*0.5f,(room->space.top+room->space.bottom)*0.5f};
        runtime.playerBody.position=at;runtime.playerBody.previousPosition=at;
        runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;runtime.playerBody.removed=false;
    }
    runtime.phase=PNR_READY;runtime.doorTransitionGrace=0;runtime.descendRequested=false;
    runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();
    return true;
}
bool PortGameSession::SaveNativeContinue(const char* path,PortNormalRuntime& runtime){
    if(!path||runtime.phase!=PNR_READY||runtime.playerDead)return false;
    NativeGamePersistent snapshot;unsigned int next=0;
    if(!snapshot.CaptureCurrent(runtime.run,runtime.world,runtime.gameRandom,runtime.itemPools,runtime.nightmares,runtime.history,
        runtime.playerState,runtime.playerStartup,runtime.playerCreation,runtime.playerSprites,runtime.playerBody,runtime.playerManager,&next))return false;
    runtime.nativeSaveCaptureAddress=next;
    NativeSaveWriter writer;
    if(!writer.InitialGame(snapshot)){runtime.nativeSaveCaptureAddress=writer.nextAddress;return false;}
    FILE* file=fopen(path,"wb");
    if(!file)return false;
    size_t wrote=writer.data.empty()?0:fwrite(&writer.data[0],1,writer.data.size(),file);
    bool closed=fclose(file)==0;
    return wrote==writer.data.size()&&closed;
}

namespace {
class NativeSaveCursor {
public:
    const unsigned char* data;size_t size,pos;bool ok;
    NativeSaveCursor(const unsigned char* d,size_t n):data(d),size(n),pos(16),ok(true){}
    bool Skip(unsigned int count){
        if(!ok||pos+count>size-4){ok=false;return false;}
        pos+=count;return true;
    }
    bool Read(unsigned int count,unsigned int* value){
        if(!ok||!value||count>4||pos+count>size-4){ok=false;return false;}
        unsigned int n=0;for(unsigned int i=0;i<count;++i)n|=(unsigned int)data[pos+i]<<(8*i);
        pos+=count;*value=n;return true;
    }
};
bool SkipRoom(NativeSaveCursor& c){
    if(!c.Skip(12+2+2+2+1+4+32+12+2+2+4+20))return false;
    for(int i=0;i<448;++i){
        unsigned int type=0;if(!c.Read(2,&type))return false;
        if(type&&!c.Skip(8+2+4+4+4))return false;
    }
    unsigned int entities=0;if(!c.Read(4,&entities))return false;
    if(entities>4096)return false;
    if(!c.Skip(entities*(24+1+8+1+32+1+8+2+4+1)))return false;
    return c.Skip(4+4+2+2+16+8+4+2);
}
bool NativeSavePlayerId(const char* path,int* playerId){
    if(!path||!playerId)return false;
    FILE* f=fopen(path,"rb");if(!f)return false;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return false;}long size=ftell(f);
    if(size<64||fseek(f,0,SEEK_SET)!=0){fclose(f);return false;}
    std::vector<unsigned char> bytes((unsigned int)size);
    size_t got=fread(&bytes[0],1,(size_t)size,f);fclose(f);
    if(got!=(size_t)size||memcmp(&bytes[0],"ISAACNG_GSR0142",15))return false;
    NativeSaveCursor c(&bytes[0],(size_t)size);
    if(!c.Skip(4+1+1+1+4))return false;
    unsigned int flags=0;if(!c.Read(2,&flags)||flags>49)return false;
    if(!c.Skip(flags+14*4+4+4+1+4+6*4+30*4+30*4+4+4+8*4+4))return false;
    unsigned int levelFlags=0;if(!c.Read(2,&levelFlags)||levelFlags>24)return false;
    if(!c.Skip(levelFlags+4+12*4+3*4+14*4))return false;
    unsigned int configs=0,rooms=0;if(!c.Read(4,&configs)||!c.Skip(configs*4))return false;
    if(!c.Read(4,&rooms)||rooms>507)return false;
    for(unsigned int i=0;i<rooms+18;++i)if(!SkipRoom(c))return false;
    unsigned int ownerWords=0;if(!c.Read(2,&ownerWords)||!c.Skip(ownerWords*2))return false;
    static const unsigned int sizes[]={4,1,1,4,4,4,4,4,4,4,1,1,4,4,4,4,4,1,1,4,4,1};
    int id=-1;
    for(unsigned int i=0;i<22;++i){unsigned int value=0;if(!c.Read(sizes[i],&value))return false;if(i==20)id=(int)value;}
    if(id<0||id>255)return false;
    *playerId=id;return true;
}
}
