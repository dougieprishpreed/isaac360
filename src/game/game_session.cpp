#include "game_session.h"
#include "native_room_update.h"
#include <string.h>
PortNormalRuntime::PortNormalRuntime():initialSaveFileWritten(false),phase(PNR_IDLE),missingNativePrerequisite(0),completePlayerInitializations(0),completePlayerTicks(0),completeWholeGameplayTicks(0),tickFrame(0),playerTailTrailSegments(0),playerTailRenderCallbacks(0),playerTailTicks(0),roomEntryPrefixPrepared(false) {
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
        runtime.playerUpdate=NativePlayerUpdate();runtime.projectiles.Reset();
        runtime.roomFx.Swap(roomFx);
        runtime.minimaps.Reset();
        //Virtual Init, the reached resource/callback services and manager
        //registration have returned before006b4b5e generation begins.
        runtime.missingNativePrerequisite=0;runtime.phase=PNR_GENERATING;runtime.tickFrame=0;
        runtime.completePlayerInitializations=runtime.completePlayerTicks=0;runtime.completeWholeGameplayTicks=0;
        runtime.playerTailTrailSegments=0;runtime.playerTailRenderCallbacks=0;runtime.playerTailTicks=0;}
    if(start.result==NPS_WORLD_PENDING)runtime.roomEntryPrefixPrepared=false;
    delete next;
    frontend.CharacterStarted();
    if(start.result==NPS_WORLD_PENDING&&runtime.phase==PNR_GENERATING)frontend.characterPending=true;
    return start;
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
NormalRunResult PortGameSession::TickNormal(PortNormalRuntime& runtime,const NativeSceneFiles&,NativeActionSource* actions){
    if(runtime.phase!=PNR_READY||runtime.world.currentList<0||runtime.world.currentList>=kNativeWorldRoomLimit)return NFR_BAD_REQUEST;
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    if(!live.update||!runtime.playerBody.ready)return NFR_BAD_REQUEST;
    ++runtime.tickFrame;runtime.history.fields.Put(0x1a2e1c,runtime.tickFrame);
    if(!live.update->TickActors(runtime,actions,&runtime.missingNativePrerequisite))return NFR_PENDING;
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
    if(flow.context.stage!=1||flow.context.stageType<0||flow.context.stageType>2||files.backdrop!=flow.context.stageType+1)return NWR_BAD_REQUEST;
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
