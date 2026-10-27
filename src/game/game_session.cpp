#include "game_session.h"
#include <string.h>
PortNormalRuntime::PortNormalRuntime():phase(PNR_IDLE),missingNativePrerequisite(0),completePlayerInitializations(0),completePlayerTicks(0) {
    memset(&run,0,sizeof(run));memset(&entryIssue,0,sizeof(entryIssue));
}
PortSessionStart PortGameSession::StartNormalCharacter(unsigned int seed,PortNormalRuntime& runtime) {
    //Begin commits only a valid start. An unsupported/locked selection must
    //not discard the existing resident world.
    NormalRunFlow* next=new NormalRunFlow;memset(next,0,sizeof(*next));
    NativePlayerStartup startup;NativeEntityBody body;NativePlayerSprites sprites;NativePlayerState state;NativePlayerCreation creation;
    PortSessionStart start=BeginNormalCharacter(seed,*next);
    if(start.result==NPS_WORLD_PENDING&&!creation.PreparePrefix(next->player,next->seeds.playerInit,startup,body,sprites,state))start.result=NPS_BAD_REQUEST;
    if(start.result==NPS_WORLD_PENDING){runtime.run=*next;runtime.world.Reset();memset(&runtime.entryIssue,0,sizeof(runtime.entryIssue));
        runtime.playerStartup.Swap(startup);
        runtime.playerBody.Swap(body);
        runtime.playerSprites.Swap(sprites);
        runtime.playerState.Swap(state);
        runtime.playerCreation.Swap(creation);
        //70a994's appearance service precedes starting resources/grants/stats,
        //904a2a placement/registration, and006b4b5e generation. Holding here
        //prevents the former scaffold from generating with projected health.
        runtime.missingNativePrerequisite=0x0076ea50U;runtime.phase=PNR_NEED_NATIVE_INIT;
        runtime.completePlayerInitializations=runtime.completePlayerTicks=0;}
    delete next;
    frontend.CharacterStarted();
    return start;
}
NormalRunResult PortGameSession::AdvanceNormalSetup(PortNormalRuntime& runtime,const NativeSceneFiles& files) {
    if(runtime.phase!=PNR_GENERATING&&runtime.phase!=PNR_ROOM_COMPONENTS)return NFR_BAD_REQUEST;
    if(runtime.phase==PNR_GENERATING){
        NormalRunResult r=runtime.run.Step();
        if(runtime.run.phase==NRF_BLOCKED){runtime.phase=PNR_ERROR;frontend.CharacterStarted();return r;}
        if(runtime.run.phase!=NRF_NEED_ROOM_ENTRY)return r;
        runtime.phase=PNR_ROOM_COMPONENTS;
    }
    //The real selected descriptor, seeds, mode, backdrop and config stage
    //come from this session. No test-supplied evaluated entity is involved.
    NativeWorldResult r=PrepareNormalWorld(runtime.run,runtime.world,files,&runtime.entryIssue);
    if(r==NWR_OK){runtime.phase=PNR_NEED_NATIVE_INIT;
        //Once the pre-generation native lifecycle is completed, the real
        //caller still has its post-generation76c990 pass before room entry.
        runtime.missingNativePrerequisite=0x0076c990U;
    }else runtime.phase=r==NWR_CONSTRUCTION_GAP?PNR_FACTORY_GAP:r==NWR_SOURCE_ERROR?PNR_SOURCE_ERROR:PNR_ERROR;
    frontend.CharacterStarted();return r==NWR_OK||r==NWR_CONSTRUCTION_GAP?NFR_PENDING:r==NWR_SOURCE_ERROR?NFR_MISSING_DATA:NFR_BAD_REQUEST;
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
