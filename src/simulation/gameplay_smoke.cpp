#include "../game/game_session.h"
#include "../game/native_room_effects.h"
#include "../game/native_room_view.h"
#include "../game/native_rooms.h"
#include "../game/native_static_grid.h"
#include "../game/native_audio.h"
#include "../game/native_door_resources.h"
#include "../game/native_save_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include <vector>
namespace {
unsigned int RunMovementTicks(PortGameSession& session,PortNormalRuntime& runtime,float x,float y,unsigned int count){
    unsigned int done=0;
    for(unsigned int i=0;i<count;++i){
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime.run.context.stage),runtime.run.context.nativeMode,0};
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
unsigned int RunProjectileTicks(PortGameSession& session,PortNormalRuntime& runtime,int shootA,int shootB,unsigned int count){
    unsigned int done=0;
    for(unsigned int i=0;i<count;++i){
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime.run.context.stage),runtime.run.context.nativeMode,0};
        NativeActionFrame frame;frame.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame+i);
        frame.SupplyConnection(true);frame.SupplyDrop(false);
        for(int action=0;action<12;++action){frame.SupplyHeld(action,false);frame.SupplyPressed(action,false);}
        for(int action=0;action<8;++action)frame.SupplyValue(action,0);
        if(shootA>=0)frame.SupplyValue(shootA,1.0f);
        if(shootB>=0)frame.SupplyValue(shootB,1.0f);
        NativeFrameActions actions(frame);
        if(session.TickNormal(runtime,files,&actions)!=NFR_OK)break;
        ++done;
    }
    return done;
}
unsigned int AdvanceToBoundary(PortGameSession& session,PortNormalRuntime& runtime){
    unsigned int steps=0;
    while(runtime.phase==PNR_GENERATING||runtime.phase==PNR_ROOM_COMPONENTS){
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime.run.context.stage),runtime.run.context.nativeMode,0};
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
unsigned int DiscoveredRoomCount(const NativeLevelLayout& layout){
    unsigned int count=0;
    for(int i=0;i<layout.count;++i)if(layout.rooms[i].displayFlags)++count;
    for(int i=0;i<18;++i)if(layout.offGrid[i].definition>=0&&layout.offGrid[i].displayFlags)++count;
    return count;
}
unsigned int ClearedVisitedRoomCount(const NativeLevelLayout& layout){
    unsigned int count=0;
    for(int i=0;i<layout.count;++i)if(layout.rooms[i].visitedCount&&(layout.rooms[i].flags&1U))++count;
    for(int i=0;i<18;++i)if(layout.offGrid[i].definition>=0&&layout.offGrid[i].visitedCount&&(layout.offGrid[i].flags&1U))++count;
    return count;
}
#include "generated_pickup_probe.inc"

static bool ReadProbeFile(const char* path,std::vector<unsigned char>& data){
    FILE* file=fopen(path,"rb");if(!file)return false;
    if(fseek(file,0,SEEK_END)!=0){fclose(file);return false;}long size=ftell(file);
    if(size<64||fseek(file,0,SEEK_SET)!=0){fclose(file);return false;}
    data.resize((size_t)size);size_t got=fread(&data[0],1,data.size(),file);fclose(file);
    return got==data.size();
}
static bool WriteProbeFile(const char* path,const std::vector<unsigned char>& data){
    if(data.empty())return false;FILE* file=fopen(path,"wb");if(!file)return false;
    size_t wrote=fwrite(&data[0],1,data.size(),file);bool closed=fclose(file)==0;
    return wrote==data.size()&&closed;
}
static unsigned int ProbeU32(const std::vector<unsigned char>& data,size_t at){
    return (unsigned int)data[at]|((unsigned int)data[at+1]<<8)|
        ((unsigned int)data[at+2]<<16)|((unsigned int)data[at+3]<<24);
}
static void ProbePutU32(std::vector<unsigned char>& data,size_t at,unsigned int value){
    for(unsigned int i=0;i<4;++i)data[at+i]=(unsigned char)(value>>(8*i));
}
static float ProbeFloat(unsigned int bits){float value;memcpy(&value,&bits,4);return value;}
static unsigned int ProbeArithmeticRight(unsigned int n){return (n>>1)|(n&0x80000000U);}
static unsigned int ProbeCrcEntry(unsigned int n){
    n=(n>>1)^((n&1)?0xedb88320U:0);
    for(int i=1;i<8;++i)n=ProbeArithmeticRight(n)^((n&1)?0xedb88320U:0);
    return n;
}
static bool RewriteProbeSaveChecksum(std::vector<unsigned char>& data){
    if(data.size()<64)return false;unsigned int checksum=0xfedcba76U;
    for(size_t i=16;i<data.size()-4;++i){
        unsigned int n=~checksum;n=(n>>8)^ProbeCrcEntry((n&255)^data[i]);checksum=~n;
    }
    ProbePutU32(data,data.size()-4,checksum^0x96696996U);
    return true;
}
static bool FindProbePickupRecord(const std::vector<unsigned char>& data,int variant,int grid,size_t* output){
    if(!output||data.size()<82)return false;int found=0;
    for(size_t at=16;at+82<=data.size()-4;++at){
        if(ProbeU32(data,at)!=5||ProbeU32(data,at+4)!=(unsigned int)variant||
           ProbeU32(data,at+8)!=0||ProbeU32(data,at+12)!=(unsigned int)grid||
           data[at+24]!=1||data[at+29]!=0xff||data[at+30]!=0xff||
           data[at+31]!=0xff||data[at+32]!=0xff)continue;
        *output=at;++found;
    }
    return found==1;
}
static bool RejectNativeContinueWithoutMutation(const char* path,NativeMt& random,PortFrontend& ui){
    PortNormalRuntime runtime;PortGameSession session(random,ui);
    const int page=session.frontend.characterPage,choice=session.frontend.characterChoice,mode=session.frontend.characterMode;
    const NativeMusicState music=session.frontend.music;
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",777,777,777};
    if(session.ContinueNativeSave(path,runtime,files,0))return false;
    return runtime.phase==PNR_IDLE&&runtime.world.residentCount==0&&runtime.completePlayerTicks==0&&
        !runtime.suppressInitialNewRunPublish&&
        session.frontend.characterPage==page&&session.frontend.characterChoice==choice&&session.frontend.characterMode==mode&&
        !memcmp(&session.frontend.music,&music,sizeof(music))&&
        files.backdrop==777&&files.nativeMode==777&&files.frame==777;
}
class ProbeCountingActions:public NativeActionSource{
    NativeFrameActions delegate;
public:
    unsigned int calls,valueCalls,pressedCalls,dropCalls,heldCalls,connectedCalls;
    explicit ProbeCountingActions(const NativeActionFrame& frame):delegate(frame),calls(0),valueCalls(0),
        pressedCalls(0),dropCalls(0),heldCalls(0),connectedCalls(0){}
    NativePlayerInputResult Value(int action,int controller,unsigned int owner,float* output){
        ++calls;++valueCalls;return delegate.Value(action,controller,owner,output);}
    NativePlayerInputResult Pressed(int action,int controller,unsigned int owner,bool* output){
        ++calls;++pressedCalls;if(action==11)++dropCalls;return delegate.Pressed(action,controller,owner,output);}
    NativePlayerInputResult Held(int action,int controller,unsigned int owner,bool* output){
        ++calls;++heldCalls;return delegate.Held(action,controller,owner,output);}
    NativePlayerInputResult Connected(int controller,bool* output){
        ++calls;++connectedCalls;return delegate.Connected(controller,output);}
};
static int RunSelectedRunIntentProbe(){
    std::vector<unsigned char> source;
    std::vector<unsigned char> consumedSource;
    if(!ReadProbeFile("build/gameplay/profile/current_gamestate1.dat",source)||
       !ReadProbeFile("build/gameplay/generated_pickup_native.dat",consumedSource))return 336;
    const char* freshPath="build/gameplay/gamestate1.dat";
    const char* validPath="build/gameplay/gamestate2.dat";
    const char* slotThreePath="build/gameplay/gamestate3.dat";
    if(!WriteProbeFile(freshPath,source)||!WriteProbeFile(validPath,source))return 337;
    std::vector<unsigned char> invalid=source;invalid[invalid.size()-1]^=1U;
    if(!WriteProbeFile(slotThreePath,invalid))return 338;

    PortFrontend freshUi;freshUi.Init(false);
    if(!freshUi.BindSaveDirectory("build/gameplay/"))return 339;
    freshUi.selectedSlot=0;
    std::string primary,rerun;
    if(!freshUi.LocalRunFilenames(&primary,&rerun)||primary.size()<14||
       primary.substr(primary.size()-14)!="gamestate1.dat")return 340;
    freshUi.hasContinue=true;freshUi.OpenCharacters();freshUi.characterChoice=1;freshUi.characterMode=0;
    PortMenuInput accept={0};accept.accept=true;
    const PortAction freshAction=freshUi.Step(accept);
    const NativeCharacterDef* freshCharacter=NativeCharacter(freshUi.characterPage,freshUi.characterChoice);
    NativeMt freshRandom;freshRandom.Seed(9300);PortNormalRuntime freshRuntime;PortGameSession freshSession(freshRandom,freshUi);
    const unsigned int freshSeed=424242U;
    const PortSessionStart freshStart=freshSession.StartNormalCharacter(freshSeed,freshRuntime);
    if(freshAction!=UI_START_CHARACTER||!freshCharacter||freshStart.result!=NPS_WORLD_PENDING||
       freshRuntime.run.seeds.start!=freshSeed||freshRuntime.run.player.player!=freshCharacter->player||
       freshUi.music.currentTrack!=95||freshUi.music.slot!=1)return 341;
    const NativeMusicVoice freshVoice=freshUi.music.voices[freshUi.music.slot];
    if(freshVoice.current!=0||freshVoice.target!=freshUi.music.volume||
       freshVoice.rate!=ProbeFloat(0x3da3d70aU))return 341;
    if(!AdvanceToBoundary(freshSession,freshRuntime)||freshRuntime.phase!=PNR_READY||
       freshUi.music.queuedTrack==0||freshUi.music.native348!=0||freshUi.music.native364!=0)return 341;

    PortFrontend continueUi;continueUi.Init(false);
    if(!continueUi.BindSaveDirectory("build/gameplay/"))return 342;
    if(continueUi.music.currentTrack!=0)return 342;
    continueUi.selectedSlot=1;continueUi.hasContinue=true;continueUi.OpenMain();continueUi.selection=MAIN_CONTINUE;
    if(continueUi.Step(accept)!=UI_CONTINUE_RUN)return 343;
    NativeMt continueRandom;continueRandom.Seed(9301);PortNormalRuntime continued;PortGameSession continueSession(continueRandom,continueUi);
    NativeSceneFiles continueFiles={"runtime/game/file_index.bin","runtime/game/",1,2,0};
    NativeActionFrame staleFrame;staleFrame.Clear(0,0,0);staleFrame.SupplyConnection(true);staleFrame.SupplyDrop(false);
    for(int a=0;a<12;++a){staleFrame.SupplyHeld(a,false);staleFrame.SupplyPressed(a,false);if(a<8)staleFrame.SupplyValue(a,0);}
    ProbeCountingActions staleActions(staleFrame);
    const bool selectedContinue=continueSession.ContinueSelectedNativeRun(continued,continueFiles,&staleActions);
    std::vector<unsigned char> afterFirst;const bool retainedFirst=ReadProbeFile(validPath,afterFirst)&&afterFirst==source;
    NativeSaveSummary decodedCurrent;
    const bool decodedCurrentOk=NativeReadSaveSummary(validPath,decodedCurrent);
    if(!selectedContinue||continued.phase!=PNR_READY||
       continued.run.seeds.start!=1U||continued.run.context.stage!=3||continued.run.player.player!=0||
       continued.run.context.nativeMode!=0||continueFiles.nativeMode!=0||
       staleFrame.controller!=continued.playerCreation.native153c||
       staleFrame.playerToken!=continued.playerBody.entityToken||staleActions.calls!=0||
       staleActions.valueCalls!=0||staleActions.pressedCalls!=0||staleActions.dropCalls!=0||
       staleActions.heldCalls!=0||staleActions.connectedCalls!=0||
       staleFrame.controller||staleFrame.playerToken||continueUi.music.slot!=1||
       continueUi.music.currentTrack==0||continueUi.music.currentTrack==64||continueUi.music.currentTrack==95||
       continueUi.music.native348!=0||continueUi.music.native364!=-100||!retainedFirst||
       !decodedCurrentOk||decodedCurrent.nativeMode!=0){
        printf("SELECTED_RUN_CONTINUE_DIAG accepted=%d phase=%d next=%08x seed=%u stage=%d player=%d mode=%d filesMode=%d calls=%u/%u/%u/%u/%u/%u owner=%u/%u/%u/%u slot=%u track=%d n348=%d n364=%d retained=%d decodedMode=%d\n",
            selectedContinue?1:0,continued.phase,continued.missingNativePrerequisite,continued.run.seeds.start,
            continued.run.context.stage,continued.run.player.player,continued.run.context.nativeMode,continueFiles.nativeMode,
            staleActions.calls,staleActions.valueCalls,staleActions.pressedCalls,staleActions.dropCalls,
            staleActions.heldCalls,staleActions.connectedCalls,staleFrame.controller,staleFrame.playerToken,
            continued.playerCreation.native153c,continued.playerBody.entityToken,continueUi.music.slot,
            continueUi.music.currentTrack,continueUi.music.native348,continueUi.music.native364,
            retainedFirst?1:0,decodedCurrent.nativeMode);
        return 344;}
    const NativeMusicVoice coldVoice=continueUi.music.voices[continueUi.music.slot];
    if(coldVoice.current!=0||coldVoice.target!=continueUi.music.volume||
       coldVoice.rate!=ProbeFloat(0x3da3d70aU))return 344;

    PortFrontend secondUi;secondUi.Init(false);
    if(!secondUi.BindSaveDirectory("build/gameplay/"))return 348;
    if(secondUi.music.currentTrack!=0)return 348;
    secondUi.selectedSlot=1;secondUi.hasContinue=true;secondUi.OpenMain();secondUi.selection=MAIN_CONTINUE;
    if(secondUi.Step(accept)!=UI_CONTINUE_RUN)return 348;
    NativeMt secondRandom;secondRandom.Seed(9303);PortNormalRuntime secondContinued;PortGameSession secondSession(secondRandom,secondUi);
    NativeSceneFiles secondFiles={"runtime/game/file_index.bin","runtime/game/",1,2,0};
    std::vector<unsigned char> beforeSecond;if(!ReadProbeFile(validPath,beforeSecond))return 348;
    if(!secondSession.ContinueSelectedNativeRun(secondContinued,secondFiles,0)||secondContinued.phase!=PNR_READY||
       secondContinued.run.seeds.start!=continued.run.seeds.start||secondContinued.run.context.stage!=3||
       secondContinued.run.context.nativeMode!=0||secondFiles.nativeMode!=0||
       secondContinued.playerState.keys!=continued.playerState.keys||
       secondContinued.playerState.bombs!=continued.playerState.bombs||
       secondContinued.playerState.coins!=continued.playerState.coins||secondUi.music.slot!=1||
       secondUi.music.currentTrack==0||secondUi.music.currentTrack==64||secondUi.music.currentTrack==95||
       secondUi.music.native364!=-100)return 348;
    std::vector<unsigned char> afterSecond;if(!ReadProbeFile(validPath,afterSecond)||afterSecond!=beforeSecond)return 348;

    PortFrontend invalidUi;invalidUi.Init(false);
    if(!invalidUi.BindSaveDirectory("build/gameplay/"))return 345;
    invalidUi.selectedSlot=2;invalidUi.hasContinue=true;invalidUi.OpenMain();invalidUi.selection=MAIN_CONTINUE;
    if(invalidUi.Step(accept)!=UI_CONTINUE_RUN)return 346;
    NativeMt invalidRandom;invalidRandom.Seed(9302);PortNormalRuntime invalidRuntime;
    NativeSceneFiles invalidFiles={"runtime/game/file_index.bin","runtime/game/",777,777,777};
    const bool invalidAccepted=PortGameSession(invalidRandom,invalidUi).ContinueSelectedNativeRun(invalidRuntime,invalidFiles,0);
    const bool invalidRejected=!invalidAccepted&&invalidRuntime.phase==PNR_IDLE&&invalidRuntime.world.residentCount==0&&
        invalidRuntime.completePlayerTicks==0&&invalidUi.scene==UI_MAIN&&invalidUi.hasContinue&&
        invalidUi.music.currentTrack==0&&invalidFiles.backdrop==777&&invalidFiles.nativeMode==777&&invalidFiles.frame==777;
    if(!invalidRejected)return 347;

    if(!WriteProbeFile(slotThreePath,consumedSource))return 349;
    int consumedFirstPickups=-1,consumedSecondPickups=-1;
    for(int pass=0;pass<2;++pass){
        PortFrontend consumedUi;consumedUi.Init(false);
        if(!consumedUi.BindSaveDirectory("build/gameplay/"))return 350;
        consumedUi.selectedSlot=2;consumedUi.hasContinue=true;consumedUi.OpenMain();consumedUi.selection=MAIN_CONTINUE;
        if(consumedUi.Step(accept)!=UI_CONTINUE_RUN)return 350;
        std::vector<unsigned char> before,after;
        if(!ReadProbeFile(slotThreePath,before))return 350;
        NativeMt consumedRandom;consumedRandom.Seed(9310U+(unsigned int)pass);PortNormalRuntime consumedRuntime;
        PortGameSession consumedSession(consumedRandom,consumedUi);NativeSceneFiles consumedFiles={"runtime/game/file_index.bin","runtime/game/",1,2,0};
        if(!consumedSession.ContinueSelectedNativeRun(consumedRuntime,consumedFiles,0)||consumedRuntime.phase!=PNR_READY||
           consumedRuntime.run.seeds.start!=12352U||consumedRuntime.run.context.nativeMode!=0||
           consumedRuntime.playerState.keys!=1||consumedRuntime.playerState.bombs!=0||
           !consumedRuntime.world.Current()||consumedRuntime.world.resident[consumedRuntime.world.currentList].pickups.count!=0||
           !ReadProbeFile(slotThreePath,after)||after!=before)return 351;
        if(pass==0)consumedFirstPickups=consumedRuntime.world.resident[consumedRuntime.world.currentList].pickups.count;
        else consumedSecondPickups=consumedRuntime.world.resident[consumedRuntime.world.currentList].pickups.count;
    }
    remove(freshPath);remove(validPath);remove(slotThreePath);
    printf("SELECTED_RUN_INTENT newWithSave=1 seed=%u player=%d continueSlot=%d mode=0 seed=%u stage=%d staleActions=%u preloadValue=%u preloadPressed=%u preloadDrop=%u preloadHeld=%u preloadConnected=%u preloadOwner=%u/%u restoredOwner=%u/%u coldTrack=%d native364=%d stingerQueued=%d stinger364=%d coldSlot=%u savedMode=%d retained=1 second=1 invalidNoFallback=1 consumedTwice=%d/%d\n",
        freshSeed,freshCharacter->player,continueUi.selectedSlot,continued.run.seeds.start,continued.run.context.stage,
        staleActions.calls,staleActions.valueCalls,staleActions.pressedCalls,staleActions.dropCalls,
        staleActions.heldCalls,staleActions.connectedCalls,staleFrame.controller,staleFrame.playerToken,
        continued.playerCreation.native153c,continued.playerBody.entityToken,continueUi.music.currentTrack,
        continueUi.music.native364,freshUi.music.queuedTrack,freshUi.music.native364,
        (unsigned int)continueUi.music.slot,decodedCurrent.nativeMode,consumedFirstPickups,consumedSecondPickups);
    return 0;
}
static int RunNativePickupCodecProbe(NativeMt& random,PortFrontend& ui){
    std::vector<unsigned char> source;
    if(!ReadProbeFile("build/gameplay/generated_pickup_native_control.dat",source))return 320;
    size_t key=(size_t)-1;
    if(!FindProbePickupRecord(source,30,42,&key))return 321;
    std::vector<unsigned char> codec=source;
    ProbePutU32(codec,key+46,0x12345678U);
    ProbePutU32(codec,key+71,0x40000000U);
    ProbePutU32(codec,key+77,0x89abcdefU);
    if(!RewriteProbeSaveChecksum(codec)||!WriteProbeFile("build/gameplay/generated_pickup_codec_fixture.dat",codec))return 322;
    NativeSaveSummary summary;
    if(!NativeReadSaveSummary("build/gameplay/generated_pickup_codec_fixture.dat",summary)||!summary.valid)return 323;
    bool found=false;
    for(unsigned int i=0;i<summary.currentRoomPickups.size();++i){
        const NativeSaveSummary::Pickup& p=summary.currentRoomPickups[i];
        if(p.variant!=30||p.grid!=42)continue;
        unsigned int d8=0;memcpy(&d8,&p.nativeD8,4);
        if(p.native470!=0x12345678U||p.native4e0!=0x89abcdefU||d8!=0x40000000U)return 324;
        found=true;break;
    }
    if(!found)return 325;

    std::vector<unsigned char> badCrc=source;badCrc[badCrc.size()-1]^=1U;
    if(!WriteProbeFile("build/gameplay/generated_pickup_invalid_crc.dat",badCrc)||
       !RejectNativeContinueWithoutMutation("build/gameplay/generated_pickup_invalid_crc.dat",random,ui))return 326;

    std::vector<unsigned char> unsupported=source;unsupported[key+81]=1U;
    if(!RewriteProbeSaveChecksum(unsupported)||
       !WriteProbeFile("build/gameplay/generated_pickup_unsupported_extra.dat",unsupported))return 327;
    NativeSaveSummary rejected;
    if(NativeReadSaveSummary("build/gameplay/generated_pickup_unsupported_extra.dat",rejected)||
       !RejectNativeContinueWithoutMutation("build/gameplay/generated_pickup_unsupported_extra.dat",random,ui))return 328;

    NativeStaticGrid type4,type6,type15,type5,type26,type25,type14,type21,type24,type13,unsupportedGrid;
    const bool grid4=NativeRestoreSerializedGrid(7,4,2,0,5,0x11223344U,0x16a9de81U,0x55667788U,17,&type4)&&
        type4.type==1000&&type4.nativeType==4&&type4.collisionClass==4&&type4.initializations==6&&
        type4.initializedFrame==17&&type4.native18==0x55667788U&&type4.native20==0x16a9de81U;
    const bool grid6=NativeRestoreSerializedGrid(8,6,0,0,2,0x22334455U,0x7a11ee00U,0,18,&type6)&&
        type6.type==1002&&type6.nativeType==6&&type6.collisionClass==3&&type6.initializations==3&&
        type6.initializedFrame==18&&type6.native20==0x7a11ee00U;
    const bool grid15=NativeRestoreSerializedGrid(9,15,0,0,1,0x33445566U,0x16a9de81U,0,19,&type15)&&
        type15.type==1999&&type15.nativeType==15&&type15.collisionClass==4&&type15.initializations==2&&
        type15.initializedFrame==19&&type15.native20==0x16a9de81U;
    const bool grid5=NativeRestoreSerializedGrid(10,5,0,0,0,0x44556677U,0,0,20,&type5)&&
        type5.type==1001&&type5.nativeType==5&&type5.collisionClass==4&&type5.initializations==1;
    const bool grid26=NativeRestoreSerializedGrid(11,26,0,0,0,0x55667788U,0,0,21,&type26)&&
        type26.type==1008&&type26.nativeType==26&&type26.collisionClass==4&&type26.initializations==1;
    const bool grid25=NativeRestoreSerializedGrid(12,25,0,0,0,0x66778899U,0,0,22,&type25)&&
        type25.type==1010&&type25.nativeType==25&&type25.collisionClass==4&&type25.initializations==1;
    const bool grid14=NativeRestoreSerializedGrid(13,14,4,0,0,0x778899aaU,0,0,23,&type14)&&
        type14.type==1494&&type14.nativeType==14&&type14.variant==4&&type14.collisionClass==3&&type14.initializations==1;
    const bool grid21=NativeRestoreSerializedGrid(14,21,1,0,0,0x8899aabbU,0,0,24,&type21)&&
        type21.type==5001&&type21.nativeType==21&&type21.variant==1&&type21.collisionClass==0&&type21.initializations==1;
    const bool grid24=NativeRestoreSerializedGrid(15,24,0,0,0,0x99aabbccU,0,0,25,&type24)&&
        type24.type==1901&&type24.nativeType==24&&type24.variant==0&&type24.collisionClass==4&&type24.initializations==1;
    const bool grid13=NativeRestoreSerializedGrid(16,13,0,0,0,0x8899aabbU,0,0,26,&type13)&&
        type13.type==1400&&type13.nativeType==13&&type13.collisionClass==4&&type13.initializations==1;
    const bool unsupportedGrids=!NativeRestoreSerializedGrid(17,7,0,0,0,1,1,0,0,&unsupportedGrid)&&
        !NativeRestoreSerializedGrid(17,18,0,0,0,1,1,0,0,&unsupportedGrid);
    if(!grid4||!grid6||!grid15||!grid5||!grid26||!grid25||!grid14||!grid21||!grid24||!grid13||!unsupportedGrids)return 329;

    printf("CODEC_FIXTURE pickup470=%08x pickup4e0=%08x d8=%08x fullword=1 invalidCrcRejected=1 unsupportedExtraRejected=1\n",
        0x12345678U,0x89abcdefU,0x40000000U);
    printf("GRID_CODEC_FIXTURE type4=1 type6=1 type15=1 type5=1 type26=1 type25=1 type14=1 type21=1 type24=1 type13=1 unsupported=1 postInitCounts=6/3/2/1/1/1/1/1/1/1 frame=17/18/19/20/21/22/23/24/25/26 tails=%08x/%08x/%08x\n",
        0x16a9de81U,0x7a11ee00U,0x16a9de81U);
    return 0;
}
static bool VerifyRestartedVirginPickupRoom(PortGameSession& session,PortNormalRuntime& runtime){
    if(runtime.phase!=PNR_READY||runtime.world.currentGrid!=84||runtime.pendingRestoreStage!=0||
       !runtime.pendingSavedRooms.empty()||!runtime.world.restoredRooms.empty())return false;
    const int start=runtime.world.currentList;int target=-1,slot=-1;
    for(int i=0;i<runtime.world.layout.count;++i)if(runtime.world.layout.rooms[i].definition==268501785){
        target=i;break;}
    for(int i=0;i<8;++i){const NativePlacedRoom* linked=runtime.world.layout.DoorTarget(start,i);
        if(linked&&linked->listIndex==target){slot=i;break;}}
    if(target<0||slot<0)return false;
    unsigned int ticks=0;
    if(!GeneratedPickupMoveToDoor(session,runtime,slot,&ticks)||runtime.world.currentList!=target)return false;
    if(!GeneratedPickupEnterRoom(session,runtime,&ticks))return false;
    const NativeWorldResident& live=runtime.world.resident[target];
    NativeRoomState* room=runtime.world.Current();
    if(!room||live.pickups.count!=2||live.room!=room)return false;
    int key=0,bomb=0;
    for(int i=0;i<live.pickups.count;++i)if(live.pickups.entries[i]){
        if(live.pickups.entries[i]->variant==30&&!live.pickups.entries[i]->collected)++key;
        if(live.pickups.entries[i]->variant==40&&!live.pickups.entries[i]->collected)++bomb;}
    return key==1&&bomb==1&&GeneratedPickupGridPresent(*room,77)&&room->collision.occupied[77];
}
static int RunPendingRestartProbe(NativeMt& random,PortFrontend& ui){
    std::vector<unsigned char> source;size_t key=(size_t)-1;
    if(!ReadProbeFile("build/gameplay/generated_pickup_native_control.dat",source)||
       !FindProbePickupRecord(source,30,42,&key))return 330;
    ProbePutU32(source,key,6); //Valid common entity encoding, unsupported reached owner.
    if(!RewriteProbeSaveChecksum(source)||
       !WriteProbeFile("build/gameplay/generated_pickup_unsupported_actor.dat",source))return 331;
    NativeSaveSummary decoded;
    if(!NativeReadSaveSummary("build/gameplay/generated_pickup_unsupported_actor.dat",decoded)||!decoded.valid||
       decoded.currentGrid!=71||decoded.currentSavedRoomIndex<0)return 332;
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",1,0,0};

    PortNormalRuntime failed;PortGameSession failedSession(random,ui);
    if(failedSession.ContinueNativeSave("build/gameplay/generated_pickup_unsupported_actor.dat",failed,files))return 333;
    const unsigned int failedStop=failed.missingNativePrerequisite;
    const bool failedCleared=failed.pendingRestoreStage==0&&failed.pendingSavedRooms.empty()&&
        failed.pendingRestoreGrid<0&&failed.pendingRestoreDimension<0;
    NativeActionFrame failedFrame;failedFrame.Clear(failed.playerCreation.native153c,failed.playerBody.entityToken,files.frame);
    failedFrame.SupplyConnection(true);failedFrame.SupplyDrop(false);
    for(int a=0;a<12;++a){failedFrame.SupplyHeld(a,false);failedFrame.SupplyPressed(a,false);if(a<8)failedFrame.SupplyValue(a,0);}
    NativeFrameActions failedActions(failedFrame);
    PortSessionStart failedRestart=failedSession.RestartNormal(12352,failed,files,&failedActions);
    const bool failedFresh=failedRestart.result==NPS_WORLD_PENDING&&
        VerifyRestartedVirginPickupRoom(failedSession,failed);

    PortNormalRuntime successful;PortGameSession successfulSession(random,ui);
    if(!successfulSession.ContinueNativeSave("build/gameplay/generated_pickup_native_control.dat",successful,files)||
       successful.phase!=PNR_READY)return 334;
    NativeActionFrame successFrame;successFrame.Clear(successful.playerCreation.native153c,successful.playerBody.entityToken,files.frame);
    successFrame.SupplyConnection(true);successFrame.SupplyDrop(false);
    for(int a=0;a<12;++a){successFrame.SupplyHeld(a,false);successFrame.SupplyPressed(a,false);if(a<8)successFrame.SupplyValue(a,0);}
    NativeFrameActions successActions(successFrame);
    PortSessionStart successRestart=successfulSession.RestartNormal(12352,successful,files,&successActions);
    const bool successFresh=successRestart.result==NPS_WORLD_PENDING&&
        VerifyRestartedVirginPickupRoom(successfulSession,successful);
    printf("PENDING_RESTART failedStop=%08x failedCleared=%d failedVirgin=%d successVirgin=%d\n",
        failedStop,failedCleared?1:0,failedFresh?1:0,successFresh?1:0);
    return failedStop==0x79daf0&&failedCleared&&failedFresh&&successFresh?0:335;
}
}
int RunLockProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING)return 50;
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY)return 51;
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];NativeRoomState* room=runtime->world.Current();
    int treasure=-1;float tx=0,ty=0;
    if(live.doors)for(int i=0;i<8;++i)if((live.doors->attached&(1U<<i))&&live.doors->actors[i].targetType==10){
        float px,py;if(room->geometry.Position(live.doors->actors[i].grid,&px,&py)){treasure=i;tx=px+20.0f;ty=py+20.0f;break;}
    }
    if(treasure<0)return 52;
    runtime->playerBody.position.x=tx;runtime->playerBody.position.y=ty;runtime->playerState.keys=0;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1))return 53;
    bool closedNoKey=live.doors->actors[treasure].state==1&&runtime->doorsUnlocked==0;
    runtime->playerState.keys=1;runtime->playerBody.position.x=tx;runtime->playerBody.position.y=ty;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1))return 54;
    bool openedWithKey=live.doors->actors[treasure].state==2&&runtime->doorsUnlocked==1&&runtime->playerState.keys==0;
    int boss=-1;for(int i=0;i<8;++i)if((live.doors->attached&(1U<<i))&&i!=treasure){boss=i;break;}
    if(boss<0)return 55;
    live.doors->actors[boss].state=1;live.doors->actors[boss].previousState=1;live.doors->actors[boss].targetType=5;
    runtime->bossDefeated=false;runtime->playerBody.position.x=100.0f;runtime->playerBody.position.y=200.0f;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1))return 56;
    bool bossOpen=live.doors->actors[boss].state==2;
    printf("LOCK treasureSlot=%d noKeyClosed=%d keyOpened=%d keysLeft=%d doorsUnlocked=%u bossOpenWithoutDefeat=%d bossDefeated=%d\n",
        treasure,closedNoKey?1:0,openedWithKey?1:0,runtime->playerState.keys,runtime->doorsUnlocked,bossOpen?1:0,runtime->bossDefeated?1:0);
    bool ok=closedNoKey&&openedWithKey&&bossOpen&&!runtime->bossDefeated;
    delete runtime;return ok?0:58;
}
int RunDeathProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1035486403,*runtime);
    if(start.result!=NPS_WORLD_PENDING){printf("DEATH_START result=%d mode=%d page=%d choice=%d\\n",start.result,ui.characterMode,ui.characterPage,ui.characterChoice);return 40;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY)return 41;
    const NativePlacedRoom* target=0;
    for(int i=0;i<runtime->world.layout.count;++i)if(runtime->world.layout.rooms[i].definition==268500997){target=&runtime->world.layout.rooms[i];break;}
    if(!target)return 42;
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime->run.context.stage),runtime->run.context.nativeMode,0};
    NativeRoomSetupContext setup={files.backdrop,runtime->run.floor.plan.configStage,false,0};NativeWorldIssue issue;
    if(runtime->world.Enter(target->gridIndex,target->dimension,files,&issue,&setup)!=NWR_OK)return 43;
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    if(live.enemies.count<=0)return 44;
    runtime->playerBody.position=live.enemies.entries[0]->body.position;
    runtime->playerBody.previousPosition=runtime->playerBody.position;
    unsigned int ticks=0;
    while(ticks<600&&!runtime->playerDead){if(!RunProjectileTicks(session,*runtime,-1,-1,1)){printf("DEATH_STOP prereq=%08x update=%08x phase=%d hp=%d dead=%d\n",runtime->missingNativePrerequisite,runtime->playerUpdate.address,runtime->phase,runtime->playerState.redHearts,runtime->playerDead?1:0);return 45;}++ticks;}
    printf("DEATH seed=1035486403 room=268500997 hits=%u hp=%d/%d soul=%d dead=%d ticks=%u enemies=%d\n",
        runtime->playerHits,runtime->playerState.redHearts,runtime->playerState.maxRedHearts,
        runtime->playerState.soulHearts,runtime->playerDead?1:0,ticks,live.enemies.count);
    NativeActionFrame frame;frame.Clear(runtime->playerCreation.native153c,runtime->playerBody.entityToken,files.frame);
    frame.SupplyConnection(true);frame.SupplyDrop(false);
    for(int action=0;action<12;++action){frame.SupplyHeld(action,false);frame.SupplyPressed(action,false);}
    for(int action=0;action<8;++action)frame.SupplyValue(action,0);
    NativeFrameActions noInput(frame);
    unsigned int dying=0;
    while(runtime->phase==PNR_DYING&&dying<120){session.TickNormal(*runtime,files,&noInput);++dying;}
    const bool gameOver=runtime->phase==PNR_GAME_OVER;const unsigned int hits=runtime->playerHits;
    PortSessionStart restart=session.RestartNormal(1035486403,*runtime,files,&noInput);
    bool restarted=restart.result==NPS_WORLD_PENDING&&runtime->phase==PNR_READY&&!runtime->playerDead&&
        runtime->playerState.redHearts==runtime->playerState.maxRedHearts&&runtime->completePlayerInitializations==1;
    printf("DEATH_FLOW dyingTicks=%u gameOver=%d restartResult=%d restartPhase=%d restartHP=%d/%d\n",
        dying,gameOver?1:0,restart.result,runtime->phase,runtime->playerState.redHearts,runtime->playerState.maxRedHearts);
    bool ok=hits>0&&ticks>0&&gameOver&&restarted;
    delete runtime;return ok?0:46;
}
int RunTransitionProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING)return 31;
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY)return 32;
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];NativeRoomState* room=runtime->world.Current();
    int slot=-1;float x=0,y=0;
    if(live.doors)for(int i=0;i<8;++i)if((live.doors->attached&(1U<<i))&&live.doors->actors[i].state==2&&live.doors->actors[i].targetType==1){
        float px,py;if(room->geometry.Position(live.doors->actors[i].grid,&px,&py)){slot=i;x=px+20.0f;y=py+20.0f;break;}
    }
    if(slot<0)return 33;
    const int from=runtime->world.currentList;runtime->playerBody.position.x=x;runtime->playerBody.position.y=y;
    for(int i=0;i<8&&runtime->roomTransitions==0;++i)if(!RunProjectileTicks(session,*runtime,-1,-1,1)){
        printf("TRANSITION_PENDING next=%08x transitions=%u room=%d phase=%d issue=%d stage=%d type=%u variant=%u\n",
            runtime->missingNativePrerequisite,runtime->roomTransitions,runtime->world.currentList,runtime->phase,
            runtime->entryIssue.result,runtime->entryIssue.construction.stage,runtime->entryIssue.construction.type,
            runtime->entryIssue.construction.variant);return 34;}
    for(int i=0;i<3;++i)if(!RunProjectileTicks(session,*runtime,-1,-1,1))return 36;
    NativeRoomState* entered=runtime->world.Current();NativeWorldResident& resident=runtime->world.resident[runtime->world.currentList];
    const NativePlacedRoom* descriptor=runtime->world.CurrentDescriptor();
    const int definition=descriptor?descriptor->definition:-1;
    printf("TRANSITION seed=1 from=%d slot=%d to=%d definition=%d room=%d enemies=%d transitions=%u player=(%.9g,%.9g)\n",
        from,slot,runtime->world.currentList,definition,entered?1:0,resident.enemies.count,runtime->roomTransitions,
        runtime->playerBody.position.x,runtime->playerBody.position.y);
    bool ok=runtime->roomTransitions==1&&runtime->world.currentList!=from&&entered&&definition>=0&&
        runtime->playerBody.position.x>=entered->space.left&&runtime->playerBody.position.x<=entered->space.right&&
        runtime->playerBody.position.y>=entered->space.top&&runtime->playerBody.position.y<=entered->space.bottom;
    delete runtime;return ok?0:35;
}
int RunEncounterProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1035486403,*runtime);
    if(start.result!=NPS_WORLD_PENDING)return 20;
    if(!AdvanceToBoundary(session,*runtime))return 21;
    if(runtime->phase!=PNR_READY)return 22;
    const NativePlacedRoom* target=0;
    for(int i=0;i<runtime->world.layout.count;++i)if(runtime->world.layout.rooms[i].definition==268500997){target=&runtime->world.layout.rooms[i];break;}
    if(!target)return 23;
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime->run.context.stage),runtime->run.context.nativeMode,0};
    NativeRoomSetupContext setup={files.backdrop,runtime->run.floor.plan.configStage,false,0};NativeWorldIssue issue;
    if(runtime->world.Enter(target->gridIndex,target->dimension,files,&issue,&setup)!=NWR_OK)return 24;
    if(runtime->world.PrepareDoorActors(random,files.frame)!=NDR_OK)return 30;
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];NativeRoomState* room=runtime->world.Current();
    const int spawned=live.enemies.count;if(!room||spawned<=0)return 25;
    const int targetKills=spawned;
    for(int guard=0;guard<12&&live.enemies.count>0;++guard){
        NativeEnemy* enemy=live.enemies.entries[0];if(!enemy)return 26;
        int action=enemy->body.position.x>=runtime->playerBody.position.x?5:4;
        float distance=enemy->body.position.x-runtime->playerBody.position.x;
        if(distance<0.0f)distance=-distance;if(distance<80.0f){
            float x=enemy->body.position.x+(action==5?-100.0f:100.0f);
            if(x<80.0f||x>560.0f)x=enemy->body.position.x;
            runtime->playerBody.position.x=x;runtime->playerBody.position.y=enemy->body.position.y;
            action=enemy->body.position.x>=runtime->playerBody.position.x?5:4;
        }
        if(!RunProjectileTicks(session,*runtime,action,-1,80))return 27;
    }
    for(int i=0;i<10&&live.enemies.count>0;++i)if(!RunProjectileTicks(session,*runtime,-1,-1,20))return 28;
    int attached=0,openDoors=0;
    if(live.doors)for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){++attached;if(live.doors->actors[slot].state==2)++openDoors;}
    if(live.doors)for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot))
        printf("ENCOUNTER_DOOR slot=%d state=%d source=%d target=%d variant=%d\n",slot,live.doors->actors[slot].state,
            live.doors->actors[slot].sourceType,live.doors->actors[slot].targetType,live.doors->actors[slot].variant);
    printf("ENCOUNTER seed=1035486403 room=268500997 enemies=%d kills=%u remaining=%d clear=%d doors=%d open=%d doorsOpened=%u projectiles=%u\n",
        targetKills,runtime->enemyKills,live.enemies.count,room->clear?1:0,attached,openDoors,runtime->doorsOpened,runtime->projectiles.spawned);
    bool ok=spawned==targetKills&&runtime->enemyKills>=(unsigned int)targetKills&&live.enemies.count==0&&room->clear&&runtime->doorsOpened>0;
    delete runtime;return ok?0:29;
}
int RunDescendProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 60;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 61;}
    int grid=-1,dimension=0;
    for(int i=0;i<runtime->world.layout.count;++i){const NativePlacedRoom& p=runtime->world.layout.rooms[i];
        const NativeRoomDef* d=NativeRoom(p.definition);if(d&&d->type==5){grid=p.gridIndex;dimension=p.dimension;break;}}
    if(grid<0){delete runtime;return 62;}
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime->run.context.stage),runtime->run.context.nativeMode,0};
    NativeWorldIssue issue;
    if(runtime->world.Enter(grid,dimension,files,&issue,0)!=NWR_OK){delete runtime;return 63;}
    NativeRoomState* room=runtime->world.Current();if(!room){delete runtime;return 64;}
    room->trapdoorState=2;room->trapdoorTimer=30;
    room->trapdoorX=(room->space.left+room->space.right)*0.5f;
    room->trapdoorY=(room->space.top+room->space.bottom)*0.5f;
    runtime->playerBody.position.x=room->trapdoorX;runtime->playerBody.position.y=room->trapdoorY;
    runtime->playerBody.previousPosition=runtime->playerBody.position;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1)){delete runtime;return 65;}
    if(!runtime->descendRequested){delete runtime;return 66;}
    const int red=runtime->playerState.redHearts,soul=runtime->playerState.soulHearts;
    if(!session.DescendNormal(*runtime,files)){delete runtime;return 67;}
    if(runtime->run.context.stage!=2||!runtime->world.Current()||runtime->phase!=PNR_READY){delete runtime;return 68;}
    if(runtime->playerState.redHearts!=red||runtime->playerState.soulHearts!=soul){delete runtime;return 69;}
    //Continue the maintained descend path through every prepared floor. This
    //exercises Caves..Dark Room generation (including the terminal combined
    //pool and single-boss path) without a rendering device.
    while(runtime->run.context.stage<12){
        files.backdrop=NativeStageBackdrop(runtime->run.context.stage+1);
        if(!session.DescendNormal(*runtime,files)){
            printf("DESCEND_STAGE_FAIL stage=%d prerequisite=%08x\n",runtime->run.context.stage,runtime->missingNativePrerequisite);
            delete runtime;return 70;
        }
        if(!runtime->world.Current()||runtime->phase!=PNR_READY){delete runtime;return 71;}
    }
    if(runtime->run.context.stage!=12){delete runtime;return 72;}
    printf("DESCEND from=1 to=%d room=%d definition=%d hp=%d/%d soul=%d\n",runtime->run.context.stage,
        runtime->world.currentList,runtime->world.CurrentDescriptor()?runtime->world.CurrentDescriptor()->definition:-1,
        runtime->playerState.redHearts,runtime->playerState.maxRedHearts,runtime->playerState.soulHearts);
    delete runtime;return 0;
}
int RunItemProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 80;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 81;}
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    if(!live.room||live.pickups.count>=64){delete runtime;return 82;}
    //006e92d0's room-type table: type2=shop pool1, type24=planetarium pool26.
    const unsigned int shopBefore=runtime->itemPools.pools[1].counter;
    const unsigned int planetBefore=runtime->itemPools.pools[26].counter;
    if(runtime->itemPools.SelectCollectibleForRoom(2,12345)<=0||
       runtime->itemPools.pools[1].counter!=shopBefore+1||
       runtime->itemPools.SelectCollectibleForRoom(24,12345)<=0||
       runtime->itemPools.pools[26].counter!=planetBefore+1){delete runtime;return 91;}
    //Creation owns the contextual identity; contact consumes it without a repick.
    const unsigned int before=runtime->itemPools.pools[0].counter;
    const int itemSubtype=runtime->itemPools.PrepareRewardSubtype(100);
    if(itemSubtype<=0||runtime->itemPools.pools[0].counter!=before+1){delete runtime;return 83;}
    NativePickup* pickup=new NativePickup;
    if(!pickup->Begin(5,100,itemSubtype,0,12345,0,runtime->playerBody.position)){delete pickup;delete runtime;return 83;}
    live.pickups.entries[live.pickups.count++]=pickup;
    const unsigned int collectedBefore=runtime->collectiblesCollected;
    if(!RunProjectileTicks(session,*runtime,-1,-1,2)){delete runtime;return 84;}
    bool owned=false;
    if(runtime->lastCollectible<=0||!runtime->playerState.HasCollectible(runtime->lastCollectible,&owned)||!owned){delete runtime;return 85;}
    if(!pickup->collected||pickup->subtype!=itemSubtype||runtime->collectiblesCollected!=collectedBefore+1||
       runtime->itemPools.pools[0].counter!=before+1){delete runtime;return 86;}
    const int hpBefore=runtime->playerState.maxRedHearts,keysBefore=runtime->playerState.keys;
    if(!runtime->playerState.GrantCollectible(25)){delete runtime;return 87;} //Breakfast
    if(runtime->playerState.maxRedHearts!=std::min(14,hpBefore+2)){delete runtime;return 88;}
    if(!runtime->playerState.GrantCollectible(17)){delete runtime;return 89;} //Skeleton Key
    if(runtime->playerState.keys!=std::min(99,keysBefore+99)){delete runtime;return 90;}
    printf("ITEM pool=0 id=%d collected=%u owned=1 poolCounter=%u effect_hp=%d/%d effect_keys=%d\n",
        runtime->lastCollectible,runtime->collectiblesCollected,runtime->itemPools.pools[0].counter,
        runtime->playerState.redHearts,runtime->playerState.maxRedHearts,runtime->playerState.keys);
    delete runtime;return 0;
}
int RunBombProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 91;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 92;}
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    if(!live.room||live.enemies.count>=64){delete runtime;return 93;}
    const NativePoint at=runtime->playerBody.position;
    NativeEnemy* enemy=new NativeEnemy;
    if(!enemy->Begin(10,0,0,54321,at,0,1)){delete enemy;delete runtime;return 94;}
    live.enemies.entries[live.enemies.count++]=enemy;
    const int bombsBefore=runtime->playerState.bombs;
    if(!runtime->bombs.Place(*runtime)||runtime->playerState.bombs!=bombsBefore-1){delete runtime;return 95;}
    runtime->playerBody.position.x-=200.0f;runtime->playerBody.previousPosition=runtime->playerBody.position;
    if(!RunProjectileTicks(session,*runtime,-1,-1,60)){delete runtime;return 96;}
    if(runtime->bombs.explosions<1||live.enemies.count!=0||runtime->playerHits!=0){delete runtime;return 97;}
    printf("BOMB placed=%u explosions=%u enemyHits=%u bombsLeft=%d enemies=%d\n",
        runtime->bombs.placed,runtime->bombs.explosions,runtime->bombs.enemyHits,
        runtime->playerState.bombs,live.enemies.count);
    delete runtime;return 0;
}unsigned int RunActionPress(PortGameSession& session,PortNormalRuntime& runtime,int action,unsigned int count){
    unsigned int done=0;
    for(unsigned int i=0;i<count;++i){
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime.run.context.stage),runtime.run.context.nativeMode,0};
        NativeActionFrame frame;frame.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame+i);
        frame.SupplyConnection(true);frame.SupplyDrop(false);
        for(int a=0;a<12;++a){frame.SupplyHeld(a,false);frame.SupplyPressed(a,false);}
        for(int a=0;a<8;++a)frame.SupplyValue(a,0);
        frame.SupplyHeld(action,true);frame.SupplyPressed(action,true);
        NativeFrameActions actions(frame);
        if(session.TickNormal(runtime,files,&actions)!=NFR_OK)break;
        ++done;
    }
    return done;
}int RunPocketProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 107;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 108;}
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    if(!live.room||live.pickups.count+2>=64){delete runtime;return 109;}
    //Pill pickup and use.
    NativePickup* pill=new NativePickup;
    if(!pill->Begin(5,70,4,0,555,0,runtime->playerBody.position)){delete pill;delete runtime;return 110;}
    live.pickups.entries[live.pickups.count++]=pill;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1)){delete runtime;return 111;}
    if(!pill->collected||runtime->playerState.heldPill<0){delete runtime;return 112;}
    if(!RunActionPress(session,*runtime,10,1)||runtime->playerState.heldPill!=-1){delete runtime;return 113;}
    //Card pickup and use.
    NativePickup* card=new NativePickup;
    if(!card->Begin(5,300,9,0,556,0,runtime->playerBody.position)){delete card;delete runtime;return 114;}
    live.pickups.entries[live.pickups.count++]=card;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1)){delete runtime;return 115;}
    if(!card->collected||runtime->playerState.heldCard!=9){delete runtime;return 116;}
    const int bombs0=runtime->playerState.bombs,keys0=runtime->playerState.keys,coins0=runtime->playerState.coins;
    if(!RunActionPress(session,*runtime,10,1)||runtime->playerState.heldCard!=0){delete runtime;return 117;}
    if(runtime->playerState.bombs!=std::min(99,bombs0+1)||runtime->playerState.keys!=std::min(99,keys0+1)||
       runtime->playerState.coins!=std::min(99,coins0+1)){delete runtime;return 118;}
    //Direct Health Up pill effect.
    const int hp0=runtime->playerState.maxRedHearts;
    runtime->playerState.heldPill=7;
    if(!RunActionPress(session,*runtime,10,1)){delete runtime;return 119;}
    if(runtime->playerState.heldPill!=-1||runtime->playerState.maxRedHearts!=std::min(14,hp0+2)){delete runtime;return 120;}
    printf("POCKET hp=%d/%d bombs=%d keys=%d coins=%d\n",runtime->playerState.redHearts,runtime->playerState.maxRedHearts,
        runtime->playerState.bombs,runtime->playerState.keys,runtime->playerState.coins);
    delete runtime;return 0;
}int RunActiveProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 121;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 122;}
    runtime->playerState.redHearts=4;
    if(!runtime->playerState.GrantCollectible(45)){delete runtime;return 123;} //Yum Heart
    if(runtime->playerState.activeItems[0]!=45){delete runtime;return 124;}
    const int maxHp=runtime->playerState.maxRedHearts;
    if(!RunActionPress(session,*runtime,9,1)){delete runtime;return 125;}
    if(runtime->playerState.redHearts!=std::min(maxHp,6)){delete runtime;return 126;}
    printf("ACTIVE id=%d hp=%d/%d\n",runtime->playerState.activeItems[0],runtime->playerState.redHearts,runtime->playerState.maxRedHearts);
    delete runtime;return 0;
}int RunActiveBatchProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING||!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 162;}
    NativePlayerState& state=runtime->playerState;
    if(!state.GrantCollectible(34)||!state.GrantCollectible(38)||!state.GrantCollectible(58)||!state.GrantCollectible(37)){delete runtime;return 163;}
    state.activeItems[0]=34;state.activeItems[1]=38;state.activeItems[2]=58;state.activeItems[3]=37;
    const float damage0=state.stats.damage;
    if(!RunActionPress(session,*runtime,9,1)){delete runtime;return 164;}
    if(fabs(state.stats.damage-(damage0+2.0f))>0.001f){delete runtime;return 165;} //Book of Belial
    if(!RunProjectileTicks(session,*runtime,-1,-1,121)){delete runtime;return 166;}
    const unsigned int shots0=runtime->projectiles.spawned;
    state.activeItems[0]=38;
    if(!RunActionPress(session,*runtime,9,1)){delete runtime;return 167;}
    if(runtime->projectiles.spawned<shots0+10){delete runtime;return 168;} //Tammy's Head
    if(!RunProjectileTicks(session,*runtime,-1,-1,121)){delete runtime;return 169;}
    state.activeItems[0]=58;
    if(!RunActionPress(session,*runtime,9,1)){delete runtime;return 170;}
    const bool shadowsOk=runtime->playerInvulnerable>=299;
    if(!shadowsOk){delete runtime;return 171;} //Book of Shadows
    if(!RunProjectileTicks(session,*runtime,-1,-1,121)){delete runtime;return 172;}
    const int bombs0=state.bombs;const unsigned int placed0=runtime->bombs.placed;
    state.activeItems[0]=37;
    if(!RunActionPress(session,*runtime,9,1)){delete runtime;return 173;}
    if(state.bombs!=bombs0||runtime->bombs.placed!=placed0+1){delete runtime;return 174;} //Mr. Boom is free
    if(!RunProjectileTicks(session,*runtime,-1,-1,121)){delete runtime;return 175;}
    if(!state.GrantCollectible(406)){delete runtime;return 176;}
    state.activeItems[0]=406;
    if(!RunActionPress(session,*runtime,9,1)){delete runtime;return 177;}
    if(!(state.permDamageBonus||state.permFireRateBonus||state.permRangeBonus||state.permShotSpeedBonus||state.permSpeedBonus||state.permLuckBonus)){delete runtime;return 178;}
    printf("ACTIVE_BATCH belial=%d tammy=10 shadows=%d mrboom=1 d8=1\n",(int)(state.roomDamageBonus),shadowsOk?1:0);
    delete runtime;return 0;
}int RunBossAwardProbe(NativeMt& random,PortFrontend& ui){
    if(!ui.BindSaveDirectory("build/gameplay/profile/"))return 175;
    ui.characterPage=0;ui.characterChoice=1;ui.characterMode=0;
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING||!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 176;}
    int dingleId=-1;
    if(NativeFindRoomVariant(0,5,1097,&dingleId,NC_NORMAL)!=NR_OK){delete runtime;return 177;}
    const NativeRoomDef* dingle=NativeRoom(dingleId);
    if(!dingle||dingle->type!=5||dingle->subtype!=58){delete runtime;return 177;}
    NativePlacedRoom* descriptor=const_cast<NativePlacedRoom*>(runtime->world.CurrentDescriptor());
    if(!descriptor){delete runtime;return 178;}
    descriptor->definition=dingleId;
    runtime->bossDefeated=true;
    session.AwardBossProgress(*runtime);
    if(runtime->bossAwardIdentity!=58||runtime->bossAwardAchievement!=346||!ui.profile.awards[346]){delete runtime;return 179;}
    if(!ui.profileStorage.primaryWritten){delete runtime;return 180;}
    printf("BOSS_AWARD identity=%d achievement=%d persisted=%d\n",runtime->bossAwardIdentity,
        runtime->bossAwardAchievement,ui.profileStorage.primaryWritten?1:0);
    delete runtime;return 0;
}int RunRerollCacheProbe(NativeMt& random,PortFrontend& ui){
    ui.characterPage=0;ui.characterChoice=1;ui.characterMode=0;
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING||!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY||!runtime->playerState.stats.ready){delete runtime;return 181;}
    NativePlayerState& state=runtime->playerState;
    const float baseDamage=state.stats.damage,baseDelay=state.stats.fireDelay,baseSpeed=state.stats.moveSpeed,baseRange=state.stats.range;
    if(!state.GrantCollectible(407)){delete runtime;return 182;} //Purity
    state.ResetRoomConditionalCache(0); //Red: +4 damage
    if(state.purityAura!=1||fabs(state.stats.damage-(baseDamage+4.0f))>0.001f){delete runtime;return 183;}
    state.ResetRoomConditionalCache(1); //Blue: +2 fire rate
    if(state.purityAura!=2||fabs(state.stats.damage-baseDamage)>0.001f||!(state.stats.fireDelay<baseDelay)){delete runtime;return 184;}
    state.ResetRoomConditionalCache(2); //Yellow: +0.5 speed
    if(state.purityAura!=3||fabs(state.stats.moveSpeed-(baseSpeed+0.5f))>0.001f){delete runtime;return 185;}
    state.ResetRoomConditionalCache(3); //Orange: +3 range
    if(state.purityAura!=4||fabs(state.stats.range-(baseRange+3.0f))>0.001f){delete runtime;return 186;}
    state.Damage(1);
    if(state.purityActive||fabs(state.stats.damage-baseDamage)>0.001f){delete runtime;return 187;} //aura removed on damage
    if(!state.GrantCollectible(240)){delete runtime;return 188;} //Experimental Treatment
    if(!(state.permDamageBonus||state.permFireRateBonus||state.permRangeBonus||state.permShotSpeedBonus||state.permSpeedBonus||state.permLuckBonus)){delete runtime;return 189;}
    const float d8Damage=state.permDamageBonus,d8Range=state.permRangeBonus;
    state.RerollStats(5678);
    if(state.permDamageBonus==d8Damage&&state.permRangeBonus==d8Range){delete runtime;return 190;}
    if(!state.GrantCollectible(1)||!state.GrantCollectible(4)||!state.GrantCollectible(27)||!state.GrantCollectible(258)){delete runtime;return 191;}
    if(!state.ApplyMissingNo(runtime->itemPools,999)){delete runtime;return 192;}
    if(!(state.permDamageBonus||state.permFireRateBonus||state.permRangeBonus||state.permShotSpeedBonus||state.permSpeedBonus||state.permLuckBonus)){delete runtime;return 193;}
    printf("REROLL purity=%d treatment=%d d8=1 missing=1 damage=%.3f range=%.3f\n",
        state.purityAura?1:0,1,state.stats.damage,state.stats.range);
    delete runtime;return 0;
}int RunBossAttackProbe(NativeMt& random,PortFrontend& ui){
    ui.characterPage=0;ui.characterChoice=1;ui.characterMode=0;
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING||!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 194;}
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    const int types[16]={36,62,68,71,262,20,19,67,260,79,28,99,264,916,908,69};
    for(int t=0;t<16;++t){
        for(int i=0;i<live.enemies.count;++i)delete live.enemies.entries[i];
        live.enemies.count=0;if(live.room){live.room->clear=false;}
        runtime->enemyShots.Clear();runtime->playerInvulnerable=1000;
        NativePoint at=runtime->playerBody.position;at.x+=140.0f;at.y+=40.0f;
        NativeEnemy* enemy=new NativeEnemy;
        if(!enemy->Begin(types[t],0,0,700U+(unsigned int)t*37U,at,0,1)){delete enemy;delete runtime;return 195;}
        live.enemies.entries[live.enemies.count++]=enemy;
        if(!RunProjectileTicks(session,*runtime,-1,-1,260)){delete runtime;return 196;}
        if(runtime->enemyShots.spawned==0){delete runtime;return 197;}
    }
    for(int i=0;i<live.enemies.count;++i)delete live.enemies.entries[i];
    live.enemies.count=0;if(live.room)live.room->clear=false;
    NativePoint at=runtime->playerBody.position;at.x+=60.0f;
    NativeEnemy* victim=new NativeEnemy;
    if(!victim->Begin(10,0,0,999U,at,0,1)){delete victim;delete runtime;return 198;}
    live.enemies.entries[live.enemies.count++]=victim;
    const unsigned int fx0=runtime->deathFx.spawned;
    victim->Damage(1000.0f);
    if(!RunProjectileTicks(session,*runtime,-1,-1,1)){delete runtime;return 199;}
    if(runtime->deathFx.spawned<=fx0){delete runtime;return 200;}
    if(!RunProjectileTicks(session,*runtime,-1,-1,35)){delete runtime;return 201;}
    if(!runtime->deathFx.active.empty()){delete runtime;return 202;}
    printf("BOSS_ATTACKS gurdy=1 pin=1 peep=1 fistula=1 megamaw=1 monstro=1 larry=1 duke=1 haunt=1 gemini=1 chub=1 gurdyjr=1 megafatty=1 bumbino=1 plum=1 loki=1 deathfx=1\n");
    delete runtime;return 0;
}int RunAudioProbe(NativeMt& random,PortFrontend& ui){
    ui.characterPage=0;ui.characterChoice=1;ui.characterMode=0;
    NativeAudio audio;
    if(!audio.Initialize("runtime/port/audio"))return 203;
    short pcm[4096*2];
    if(!audio.PlaySfx("resources/sfx/tear fire 4.wav",0.8f))return 204;
    audio.Mix(pcm,1024);
    if(audio.sfxStarted!=1||audio.sfxFrames==0||audio.LastRms()<=0.0001f)return 205;
    if(!audio.PlayMusic("resources/music/diptera sonata(basement).ogg",true,0.7f))return 206;
    audio.Mix(pcm,4096);
    if(audio.musicStarted!=1||audio.musicFrames==0)return 207;
    audio.StopMusic();
    if(!audio.PlayMusicSet("resources/music/diptera sonata intro.ogg",
        "resources/music/diptera sonata(basement).ogg",
        "resources/music/diptera layer intro.ogg",
        "resources/music/diptera layer.ogg",true,0.7f,0.5f))return 226;
    audio.Mix(pcm,4096);
    if(audio.musicIntros==0||audio.musicLayers==0||audio.musicLayerFrames==0)return 227;
    printf("MUSICSET intro=%u layer=%u layer_frames=%u\n",audio.musicIntros,audio.musicLayers,audio.musicLayerFrames);
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING||!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 208;}
    runtime->audio=&audio;
    const unsigned int before=audio.sfxStarted;
    if(!RunProjectileTicks(session,*runtime,5,-1,8)){delete runtime;return 209;}
    const bool gameplaySfx=audio.sfxStarted>before;
    if(!gameplaySfx){delete runtime;return 210;}
    printf("AUDIO device=%d sfx=%u loaded=%u music=%u music_frames=%u sfx_frames=%u rms=%.4f gameplay_sfx=%u\n",
        audio.deviceReady?1:0,audio.sfxStarted,audio.sfxLoaded,audio.musicStarted,
        audio.musicFrames,audio.sfxFrames,audio.LastRms(),audio.sfxStarted-before);
    delete runtime;return 0;
}int RunDoorVisualProbe(NativeMt& random,PortFrontend& ui){
    ui.characterPage=0;ui.characterChoice=1;ui.characterMode=0;
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING||!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 222;}
    if(!RunMovementTicks(session,*runtime,0.0f,0.0f,1)){delete runtime;return 223;}
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    if(!live.doorVisuals||!live.doors){delete runtime;return 224;}
    int opened=0,locked=0;
    for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){
        NativeDoorVisual& visual=live.doorVisuals[slot];
        if(!visual.ready||!visual.main.mainState.clip)continue;
        const char* clip=visual.main.mainState.clip->name;
        if((live.doors->actors[slot].targetType==1||live.doors->actors[slot].targetType==5)&&strcmp(clip,"Opened")==0)++opened;
        if(live.doors->actors[slot].targetType==10&&strcmp(clip,"KeyClosed")==0)++locked;
    }
    if(!opened){delete runtime;return 225;}
    printf("DOORVIS opened=%d locked=%d\n",opened,locked);
    delete runtime;return 0;
}int RunWalkAnimationProbe(NativeMt& random,PortFrontend& ui){
    ui.characterPage=0;ui.characterChoice=1;ui.characterMode=0;
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING||!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 216;}
    NativeSprite& sprite=runtime->playerSprites.sprites[0];
    if(!runtime->playerSprites.bodyReady||!sprite.mainState.clip){delete runtime;return 217;}
    if(!RunMovementTicks(session,*runtime,1.0f,0.0f,12)){delete runtime;return 218;}
    if(!sprite.mainState.clip||strcmp(sprite.mainState.clip->name,"WalkRight")!=0||sprite.mainState.frame<0.5f){delete runtime;return 219;}
    const float movingFrame=sprite.mainState.frame;
    if(!RunMovementTicks(session,*runtime,0.0f,0.0f,120)){delete runtime;return 220;}
    if(sprite.mainState.playing){delete runtime;return 221;}
    printf("WALKANIM clip=WalkRight frame=%.1f stopped=1\n",movingFrame);
    delete runtime;return 0;
}int RunBlackHeartProbe(NativeMt& random,PortFrontend& ui){
    ui.characterPage=0;ui.characterChoice=1;ui.characterMode=0;
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING||!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 211;}
    NativePlayerState& state=runtime->playerState;
    state.maxRedHearts=6;state.redHearts=6;state.soulHearts=0;state.blackHeartMask=0;
    if(!state.GrantCollectible(80)||state.soulHearts!=4||state.blackHeartMask==0){delete runtime;return 212;}
    if(state.Damage(1)||state.soulHearts!=3||state.blackHeartMask==0){delete runtime;return 213;}
    if(state.Damage(3)||state.soulHearts!=0||state.blackHeartMask!=0||state.redHearts!=6){delete runtime;return 214;}
    if(state.Damage(2)||state.redHearts!=4){delete runtime;return 215;}
    printf("BLACKHEART absorbed=4 soul=0 red=4 mask=0\n");
    delete runtime;return 0;
}int RunFamiliarProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 127;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 128;}
    if(!runtime->playerState.GrantCollectible(8)||!runtime->playerState.GrantCollectible(10)||!runtime->playerState.GrantCollectible(117)){delete runtime;return 129;}
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    if(!live.room||live.enemies.count>=64){delete runtime;return 130;}
    NativePoint at=runtime->playerBody.position;at.x+=150.0f;
    NativeEnemy* enemy=new NativeEnemy;
    if(!enemy->Begin(10,0,0,777,at,0,1)){delete enemy;delete runtime;return 131;}
    live.enemies.entries[live.enemies.count++]=enemy;
    if(!RunProjectileTicks(session,*runtime,-1,-1,180)){delete runtime;return 132;}
    if(runtime->familiars.spawned!=3||(runtime->familiars.shots+runtime->familiars.hits)==0){delete runtime;return 133;}
    printf("FAMILIAR spawned=%u shots=%u hits=%u enemies=%d\n",runtime->familiars.spawned,
        runtime->familiars.shots,runtime->familiars.hits,live.enemies.count);
    delete runtime;return 0;
}int RunContinueProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* first=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*first);
    if(start.result!=NPS_WORLD_PENDING){delete first;return 134;}
    if(!AdvanceToBoundary(session,*first)||first->phase!=PNR_READY){delete first;return 135;}
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(first->run.context.stage),first->run.context.nativeMode,0};
    if(!session.DescendNormal(*first,files)||!session.DescendNormal(*first,files)){delete first;return 136;}
    if(first->run.context.stage!=3){delete first;return 137;}
    first->playerState.redHearts=5;first->playerState.keys=42;
    if(!first->playerState.GrantCollectible(25)||!first->playerState.GrantCollectible(8)||
       !first->playerState.GrantCollectible(17)){delete first;return 138;}
    first->playerState.heldCard=9;first->playerState.heldPill=7;
    if(!session.SaveContinue("build/gameplay/continue.dat",*first)){delete first;return 139;}
    const int savedMax=first->playerState.maxRedHearts,savedKeys=first->playerState.keys;
    delete first;
    PortNormalRuntime* second=new PortNormalRuntime;NativeMt local;local.Seed(9001);
    PortGameSession resumed(local,ui);
    if(!resumed.ContinueSavedRun("build/gameplay/continue.dat",*second,files)){delete second;return 140;}
    if(second->run.context.stage!=3||second->phase!=PNR_READY||second->playerState.maxRedHearts!=savedMax||
       second->playerState.keys!=savedKeys||second->playerState.heldCard!=9||second->playerState.heldPill!=7){delete second;return 141;}
    bool owned=false;if(!second->playerState.HasCollectible(25,&owned)||!owned){delete second;return 142;}
    if(!RunProjectileTicks(resumed,*second,-1,-1,2)){delete second;return 143;}
    printf("CONTINUE stage=%d hp=%d/%d keys=%d card=%d pill=%d items=%d\n",second->run.context.stage,
        second->playerState.redHearts,second->playerState.maxRedHearts,second->playerState.keys,
        second->playerState.heldCard,second->playerState.heldPill,(int)second->collectiblesCollected);
    delete second;return 0;
}int RunNativeContinueProbe(NativeMt& random,PortFrontend& ui){
    (void)random;
    PortNormalRuntime* runtime=new PortNormalRuntime;NativeMt local;local.Seed(9100);PortGameSession session(local,ui);
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",1,0,0};
    if(!session.ContinueNativeSave("build/gameplay/profile/gamestate1.dat",*runtime,files)){
        printf("NATIVE_CONTINUE_FAIL phase=%d next=%08x residents=%d\n",runtime->phase,
            runtime->missingNativePrerequisite,runtime->world.residentCount);
        delete runtime;return 144;}
    if(runtime->phase!=PNR_READY||runtime->run.seeds.start!=1||runtime->run.context.stage!=1||runtime->run.player.player!=0){delete runtime;return 145;}
    if(!RunProjectileTicks(session,*runtime,-1,-1,2)){delete runtime;return 146;}
    printf("NATIVE_CONTINUE seed=%u stage=%d hp=%d/%d\n",runtime->run.seeds.start,runtime->run.context.stage,
        runtime->playerState.redHearts,runtime->playerState.maxRedHearts);
    delete runtime;return 0;
}int RunNativeCurrentProbe(NativeMt& random,PortFrontend& ui){
    (void)random;
    ui.characterPage=0;ui.characterChoice=1;
    PortNormalRuntime* first=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*first);
    if(start.result!=NPS_WORLD_PENDING){delete first;return 147;}
    if(!AdvanceToBoundary(session,*first)||first->phase!=PNR_READY){delete first;return 148;}
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(first->run.context.stage),first->run.context.nativeMode,0};
    if(!session.DescendNormal(*first,files)||!session.DescendNormal(*first,files)){delete first;return 149;}
    if(first->run.context.stage!=3){delete first;return 150;}
    if(!first->playerState.GrantCollectible(1)||!first->playerState.GrantCollectible(8)||
       !first->playerState.GrantCollectible(45)||!first->playerState.GrantCollectible(25)){delete first;return 151;}
    first->playerState.maxRedHearts=10;first->playerState.redHearts=5;first->playerState.soulHearts=2;
    first->playerState.keys=42;first->playerState.bombs=3;first->playerState.coins=17;
    first->playerState.heldCard=9;first->playerState.heldPill=7;
    first->playerBody.position.x=333.0f;first->playerBody.position.y=444.0f;
    first->playerBody.previousPosition=first->playerBody.position;
    first->playerState.stats.fireDelay=8.5f;first->playerState.stats.damage=7.25f;
    first->playerState.stats.baseDamage=7.25f;first->playerState.stats.moveSpeed=1.4f;first->playerState.stats.luck=3.0f;
    first->playerState.permDamageBonus=0.75f;first->playerState.permRangeBonus=-2.0f;
    first->playerState.purityAura=2;first->playerState.purityActive=true;
    first->playerState.damageTakenThisFloor=3;first->playerState.crownOfLightDisabled=true;
    const NativeEffectDef* probeEffect=NativePreparedEffect(0,60);
    if(!probeEffect||first->playerState.effects.Add(probeEffect,2,false,0)!=NEF_OK||!first->playerState.effects.SetTimer(0,60,1234)){delete first;return 160;}
    const int poolItem=first->itemPools.SelectCollectible(0);
    const unsigned int poolCounter=first->itemPools.pools[0].counter;
    if(poolItem<=0){delete first;return 152;}
    if(!session.SaveNativeContinue("build/gameplay/profile/current_gamestate1.dat",*first)){
        printf("NATIVE_CURRENT_SAVE_FAIL capture=%08x\n",first->nativeSaveCaptureAddress);delete first;return 153;}
    delete first;
    ui.characterPage=0;ui.characterChoice=3;
    PortNormalRuntime* second=new PortNormalRuntime;NativeMt local;local.Seed(9200);PortGameSession resumed(local,ui);
    NativeSceneFiles resumedFiles={"runtime/game/file_index.bin","runtime/game/",1,0,0};
    if(!resumed.ContinueNativeSave("build/gameplay/profile/current_gamestate1.dat",*second,resumedFiles)){delete second;return 154;}
    if(second->run.context.stage!=3||second->playerState.playerType!=0||second->playerState.maxRedHearts!=10||
       second->playerState.redHearts!=5||second->playerState.soulHearts!=2||second->playerState.keys!=42||
       second->playerState.bombs!=3||second->playerState.coins!=17||second->playerState.activeItems[0]!=45||
       second->playerState.heldCard!=9||second->playerState.heldPill!=7){delete second;return 155;}
    if(fabs(second->playerState.permDamageBonus-0.75f)>0.0001f||fabs(second->playerState.permRangeBonus+2.0f)>0.0001f||
       second->playerState.purityAura!=2||!second->playerState.purityActive||second->playerState.damageTakenThisFloor!=3||
       !second->playerState.crownOfLightDisabled){delete second;return 159;}
    bool owned=false;
    if(!second->playerState.HasCollectible(1,&owned)||!owned||!second->playerState.HasCollectible(25,&owned)||!owned){delete second;return 156;}
    if(second->itemPools.pools[0].counter!=poolCounter){delete second;return 157;}
    if(fabs(second->playerBody.position.x-333.0f)>0.01f||fabs(second->playerBody.position.y-444.0f)>0.01f||
       fabs(second->playerState.stats.fireDelay-8.5f)>0.01f||fabs(second->playerState.stats.damage-7.25f)>0.01f||
       fabs(second->playerState.stats.moveSpeed-1.4f)>0.01f||fabs(second->playerState.stats.luck-3.0f)>0.01f){delete second;return 159;}
    if(second->playerState.effects.Count(0,60)!=2||second->playerState.effects.Timer(0,60)!=1234){delete second;return 161;}
    printf("NATIVE_CURRENT stage=%d hp=%d/%d soul=%d keys=%d bombs=%d coins=%d active=%d card=%d pill=%d poolCounter=%u pos=(%.1f,%.1f) fd=%.2f dmg=%.2f spd=%.2f luck=%.1f effects=%d\n",
        second->run.context.stage,second->playerState.redHearts,second->playerState.maxRedHearts,second->playerState.soulHearts,
        second->playerState.keys,second->playerState.bombs,second->playerState.coins,second->playerState.activeItems[0],
        second->playerState.heldCard,second->playerState.heldPill,second->itemPools.pools[0].counter,
        second->playerBody.position.x,second->playerBody.position.y,second->playerState.stats.fireDelay,
        second->playerState.stats.damage,second->playerState.stats.moveSpeed,second->playerState.stats.luck,
        second->playerState.effects.Count(0,60));printf("NATIVE_CURRENT_EFFECT count=%d timer=%d\n",second->playerState.effects.Count(0,60),second->playerState.effects.Timer(0,60));
    printf("NATIVE_CURRENT_TRIGGER perm_damage=%.2f perm_range=%.2f purity=%d active=%d stacks=%d crown_disabled=%d\n",
        second->playerState.permDamageBonus,second->playerState.permRangeBonus,second->playerState.purityAura,
        second->playerState.purityActive?1:0,second->playerState.damageTakenThisFloor,
        second->playerState.crownOfLightDisabled?1:0);
    NativePlayerEffects clearedEffects;second->playerState.effects.Swap(clearedEffects);
    if(!RunProjectileTicks(resumed,*second,-1,-1,2)){delete second;return 158;}
    printf("NATIVE_CURRENT_TICK pos=(%.1f,%.1f) ticks=2\n",second->playerBody.position.x,second->playerBody.position.y);
    delete second;return 0;
}int RunGreedProbe(NativeMt& random,PortFrontend& ui){
    (void)random;ui.characterPage=0;ui.characterChoice=1;ui.characterMode=2;
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",1,0,0};
    PortSessionStart start=session.StartGreedCharacter(1,*runtime,files);
    if(start.result!=NPS_WORLD_PENDING||runtime->phase!=PNR_READY){printf("GREED_START fail=%d phase=%d prereq=%08x\n",start.result,runtime->phase,runtime->missingNativePrerequisite);delete runtime;return 170;}
    if(!runtime->greedMode||runtime->greedWaveCount<11){delete runtime;return 171;}
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    if(!live.room||live.enemies.count<=0){delete runtime;return 172;}
    printf("GREED start wave=%d/%d room=%d type=%d variant=%d enemies=%d\n",runtime->greedWave+1,runtime->greedWaveCount,
        runtime->world.currentGrid,live.room->type,live.room->variant,live.enemies.count);
    for(int i=0;i<live.enemies.count;++i)delete live.enemies.entries[i];
    live.enemies.count=0;live.room->clear=true;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1)){delete runtime;return 173;}
    printf("GREED tick advanced=%d room=%d\n",runtime->greedWave,runtime->world.currentGrid);
    if(!session.SaveContinue("build/gameplay/greed_continue.dat",*runtime)){delete runtime;return 183;}
    PortNormalRuntime* resumedGreed=new PortNormalRuntime;NativeMt greedRandom;greedRandom.Seed(777);
    PortGameSession resumedSession(greedRandom,ui);NativeSceneFiles resumedFiles={"runtime/game/file_index.bin","runtime/game/",1,0,0};
    if(!resumedSession.ContinueSavedRun("build/gameplay/greed_continue.dat",*resumedGreed,resumedFiles)){delete resumedGreed;delete runtime;return 184;}
    if(!resumedGreed->greedMode||resumedGreed->greedWave!=1||resumedGreed->world.currentGrid!=2){delete resumedGreed;delete runtime;return 185;}
    printf("GREED continue wave=%d room=%d count=%d\n",resumedGreed->greedWave,resumedGreed->world.currentGrid,resumedGreed->greedWaveCount);
    delete resumedGreed;
    if(!session.SaveNativeContinue("build/gameplay/greed_native.dat",*runtime)){printf("GREED native save failed=%08x\n",runtime->nativeSaveCaptureAddress);delete runtime;return 186;}
    NativeSaveSummary greedSummary;
    if(!NativeReadSaveSummary("build/gameplay/greed_native.dat",greedSummary)||!greedSummary.greed||greedSummary.greedWave!=1){delete runtime;return 186;}
    PortFrontend greedyResumeUi;greedyResumeUi.Init(false);
    if(!greedyResumeUi.BindSaveDirectory("build/gameplay/")||greedyResumeUi.music.currentTrack!=0){delete runtime;return 186;}
    PortNormalRuntime* nativeGreed=new PortNormalRuntime;NativeMt nativeRandom;nativeRandom.Seed(888);
    PortGameSession nativeSession(nativeRandom,greedyResumeUi);NativeSceneFiles nativeFiles={"runtime/game/file_index.bin","runtime/game/",1,2,0};
    if(!nativeSession.ContinueNativeSave("build/gameplay/greed_native.dat",*nativeGreed,nativeFiles)){delete nativeGreed;delete runtime;return 187;}
    if(!nativeGreed->greedMode||nativeGreed->greedWave!=1||nativeGreed->world.currentGrid!=2||
       nativeGreed->run.context.nativeMode!=2||nativeGreed->run.player.mode!=2||nativeFiles.nativeMode!=2||
       greedyResumeUi.music.currentTrack==0||greedyResumeUi.music.currentTrack==64||greedyResumeUi.music.currentTrack==95||
       greedyResumeUi.music.native364!=-100){delete nativeGreed;delete runtime;return 188;}
    printf("GREED native continue wave=%d room=%d count=%d mode=%d\n",nativeGreed->greedWave,nativeGreed->world.currentGrid,
        nativeGreed->greedWaveCount,nativeGreed->run.context.nativeMode);
    delete nativeGreed;
    for(int wave=runtime->greedWave+1;wave<runtime->greedWaveCount;++wave){
        NativeRoomState* room=runtime->world.Current();if(!room){delete runtime;return 174;}
        room->clear=true;
        if(!session.AdvanceGreedWave(*runtime,files)){printf("GREED_ADVANCE fail wave=%d prereq=%08x\n",wave,runtime->missingNativePrerequisite);delete runtime;return 175;}
        if(runtime->greedWave!=wave){delete runtime;return 176;}
        NativeRoomState* next=runtime->world.Current();if(!next){delete runtime;return 177;}
    }
    NativeRoomState* last=runtime->world.Current();if(!last){delete runtime;return 178;}
    last->clear=true;
    if(!session.AdvanceGreedWave(*runtime,files)){printf("GREED_BOSS fail=%08x\n",runtime->missingNativePrerequisite);delete runtime;return 179;}
    if(runtime->greedWave!=runtime->greedWaveCount){delete runtime;return 180;}
    NativeWorldResident& bossLive=runtime->world.resident[runtime->world.currentList];
    int bossCount=0;for(int i=0;i<bossLive.enemies.count;++i)if(bossLive.enemies.entries[i]&&bossLive.enemies.entries[i]->type==406)++bossCount;
    if(!runtime->world.Current()||bossCount!=1){delete runtime;return 181;}
    printf("GREED boss room=%d type=%d variant=%d enemies=%d ultraGreed=%d\n",runtime->world.currentGrid,
        runtime->world.Current()->type,runtime->world.Current()->variant,bossLive.enemies.count,bossCount);
    runtime->playerInvulnerable=1000;
    const unsigned int bossShots0=runtime->enemyShots.spawned;
    if(!RunProjectileTicks(session,*runtime,-1,-1,140)){delete runtime;return 189;}
    if(runtime->enemyShots.spawned<=bossShots0){delete runtime;return 190;}
    printf("GREED boss attack shots=%u\n",runtime->enemyShots.spawned-bossShots0);
    runtime->world.Current()->clear=true;
    if(!session.AdvanceGreedWave(*runtime,files)||!runtime->greedComplete){delete runtime;return 182;}
    printf("GREED complete waves=%d next=%d room=%d\n",runtime->greedWaveCount,runtime->greedWave+1,runtime->world.currentGrid);
    ui.characterMode=0;delete runtime;return 0;
}//Authentic generated-floor traversal: BFS over the selected layout's actual
//door graph, clear each room with tears, enter every next room through its
//native door actor, kill the selected boss, use the resulting trapdoor, and
//persist the descended run. Invulnerability only keeps this host probe from
//randomizing the route on contact damage; it does not author rooms, enemies,
//items or door state.
int RunFloorProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){printf("FLOOR_START fail=%d\n",start.result);delete runtime;return 240;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 241;}
    NativeLevelLayout& layout=runtime->world.layout;
    const int count=layout.count,startList=runtime->world.currentList;
    const NativePlacedRoom* initialDescriptor=runtime->world.CurrentDescriptor();
    if(!runtime->minimaps.maps[0].ready||!runtime->minimaps.maps[1].ready||!initialDescriptor||
       runtime->minimaps.maps[0].draws.empty()||runtime->minimaps.maps[1].draws.empty()||
       !(initialDescriptor->displayFlags&5U)){printf("MINIMAP_INITIAL missing\n");delete runtime;return 267;}
    unsigned int discovered=DiscoveredRoomCount(layout);
    float lastSmallX=runtime->minimaps.maps[0].currentMarker.x,lastSmallY=runtime->minimaps.maps[0].currentMarker.y;
    float lastLargeX=runtime->minimaps.maps[1].currentMarker.x,lastLargeY=runtime->minimaps.maps[1].currentMarker.y;
    printf("MINIMAP_INITIAL grid=%d small=%u/%u marker=%.3f,%.3f large=%u/%u marker=%.3f,%.3f discovered=%u refreshed=%u\n",
        runtime->world.currentGrid,(unsigned int)runtime->minimaps.maps[0].draws.size(),
        runtime->minimaps.maps[0].ready?1:0,runtime->minimaps.maps[0].currentMarker.x,runtime->minimaps.maps[0].currentMarker.y,
        (unsigned int)runtime->minimaps.maps[1].draws.size(),runtime->minimaps.maps[1].ready?1:0,
        runtime->minimaps.maps[1].currentMarker.x,runtime->minimaps.maps[1].currentMarker.y,
        discovered,runtime->minimaps.refreshes);
    std::vector<int> prev(count,-1),viaSlot(count,-1),queue;
    std::vector<int> reverseSlots(count,-1);
    prev[startList]=startList;queue.push_back(startList);
    int bossList=-1;
    for(unsigned int q=0;q<queue.size()&&bossList<0;++q){
        const int at=queue[q];const NativePlacedRoom* p=layout.ByList(at);
        if(!p){delete runtime;return 242;}
        const NativeRoomDef* d=NativeRoom(p->definition);
        if(d&&d->type==5){bossList=at;break;}
        for(int slot=0;slot<8;++slot){if(!(p->doors&(1U<<slot)))continue;
            const NativePlacedRoom* target=layout.DoorTarget(at,slot);if(!target)continue;
            const int next=target->listIndex;if(next<0||next>=count||prev[next]>=0)continue;
            prev[next]=at;viaSlot[next]=slot;queue.push_back(next);}
    }
    if(bossList<0){printf("FLOOR_NO_BOSS count=%d\n",count);delete runtime;return 243;}
    std::vector<int> path;bool pathOk=true;
    for(int at=bossList;;at=prev[at]){path.push_back(at);if(at==startList)break;if(at<0||prev[at]<0){pathOk=false;break;}}
    if(!pathOk){delete runtime;return 244;}std::reverse(path.begin(),path.end());
    printf("FLOOR_PATH seed=1 rooms=%d boss=%d route=",(int)path.size(),bossList);
    for(unsigned int i=0;i<path.size();++i)printf("%s%d",i?",":"",layout.rooms[path[i]].gridIndex);printf("\n");
    for(unsigned int i=0;i<path.size();++i){
        const int expected=path[i];
        if(runtime->world.currentList!=expected){printf("FLOOR_ORDER expected=%d actual=%d step=%u\n",expected,runtime->world.currentList,i);delete runtime;return 245;}
        NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
        NativeRoomState* room=runtime->world.Current();const NativePlacedRoom* descriptor=runtime->world.CurrentDescriptor();
        if(!room||!descriptor||!live.doors){printf("FLOOR_ROOM_MISSING step=%u room=%d doors=%d\n",i,room?1:0,live.doors?1:0);delete runtime;return 246;}
        const NativeRoomDef* definition=NativeRoom(descriptor->definition);
        const NativeMinimap& small=runtime->minimaps.maps[0];const NativeMinimap& large=runtime->minimaps.maps[1];
        const unsigned int currentDiscovered=DiscoveredRoomCount(layout);
        const bool movedMaps=fabsf(small.currentMarker.x-lastSmallX)>0.001f||fabsf(small.currentMarker.y-lastSmallY)>0.001f||
            fabsf(large.currentMarker.x-lastLargeX)>0.001f||fabsf(large.currentMarker.y-lastLargeY)>0.001f;
        if(!small.ready||!large.ready||small.draws.empty()||large.draws.empty()||
           !(descriptor->displayFlags&5U)||currentDiscovered<discovered||(i&&!movedMaps)){
            printf("MINIMAP_ROOM_FAIL step=%u ready=%d/%d draws=%u/%u display=%u discovered=%u prior=%u moved=%d\n",
                i,small.ready?1:0,large.ready?1:0,(unsigned int)small.draws.size(),(unsigned int)large.draws.size(),
                descriptor->displayFlags,currentDiscovered,discovered,movedMaps?1:0);delete runtime;return 268;
        }
        discovered=currentDiscovered;
        printf("MINIMAP_ROOM step=%u grid=%d small=%u marker=%.3f,%.3f large=%u marker=%.3f,%.3f discovered=%u visited=%u clear=%u display=%u activations=%u refreshed=%u clearedVisited=%u\n",
            i,runtime->world.currentGrid,(unsigned int)small.draws.size(),small.currentMarker.x,small.currentMarker.y,
            (unsigned int)large.draws.size(),large.currentMarker.x,large.currentMarker.y,discovered,descriptor->visitedCount,
            descriptor->flags&1U,descriptor->displayFlags,live.activations,runtime->minimaps.refreshes,
            ClearedVisitedRoomCount(layout));
        lastSmallX=small.currentMarker.x;lastSmallY=small.currentMarker.y;
        lastLargeX=large.currentMarker.x;lastLargeY=large.currentMarker.y;
        const int entryEnemies=live.enemies.count;const unsigned int kills0=runtime->enemyKills;
        runtime->playerInvulnerable=0x7fffffff;unsigned int combatTicks=0;
        while(live.enemies.count>0&&combatTicks<30000){
            NativeEnemy* enemy=0;for(int e=0;e<live.enemies.count;++e)if(live.enemies.entries[e]&&!live.enemies.entries[e]->dead){enemy=live.enemies.entries[e];break;}
            if(!enemy)break;
            float x=enemy->body.position.x-64.0f,y=enemy->body.position.y;
            NativePoint wanted={x,y},clamped;
            if(room->space.Clamp(wanted,0,0,0,0,&clamped)){x=clamped.x;y=clamped.y;}
            runtime->playerBody.position.x=x;runtime->playerBody.position.y=y;
            runtime->playerBody.previousPosition=runtime->playerBody.position;runtime->playerBody.velocity.x=runtime->playerBody.velocity.y=0;
            const int action=enemy->body.position.x>=x?5:4;
            if(!RunProjectileTicks(session,*runtime,action,-1,8)){printf("FLOOR_COMBAT_STOP step=%u issue=%08x\n",i,runtime->missingNativePrerequisite);delete runtime;return 247;}
            combatTicks+=8;
        }
        if(!RunProjectileTicks(session,*runtime,-1,-1,2)){delete runtime;return 248;}
        const int remaining=live.enemies.count;const unsigned int roomKills=runtime->enemyKills-kills0;
        printf("FLOOR_ROOM step=%u grid=%d def=%d type=%d variant=%u enemies=%d kills=%u remaining=%d clear=%d hp=%d/%d ticks=%u\n",
            i,descriptor->gridIndex,descriptor->definition,definition?definition->type:-1,definition?definition->variant:0,
            entryEnemies,roomKills,remaining,room->clear?1:0,runtime->playerState.redHearts,runtime->playerState.maxRedHearts,combatTicks);
        if(remaining||!room->clear||(entryEnemies&&roomKills==0)){printf("FLOOR_ROOM_FAIL step=%u\n",i);delete runtime;return 249;}
        if(i==1&&path.size()>2){
            const int previous=path[i-1],reverse=reverseSlots[expected];
            if(reverse<0||!(live.doors->attached&(1U<<reverse))){printf("MINIMAP_REENTRY no-back-door reverse=%d\n",reverse);delete runtime;return 269;}
            NativeDoorActor& back=live.doors->actors[reverse];
            if(back.state!=2){printf("MINIMAP_REENTRY back-closed state=%d target=%d\n",back.state,back.targetType);delete runtime;return 270;}
            NativeRoomState* retainedRoom=live.room;NativeDoorSet* retainedDoors=live.doors;bool* retainedGrids=&room->collision.occupied[0];
            const unsigned int retainedActivation=live.activations,refreshesBefore=runtime->minimaps.refreshes,transitionsBefore=runtime->roomTransitions;
            NativeMt backRngBefore=random,backRngExpected=random;
            backRngExpected.Next();
            float bx,by;if(!room->geometry.Position(back.grid,&bx,&by)){delete runtime;return 271;}
            runtime->playerBody.position.x=bx+20.0f;runtime->playerBody.position.y=by+20.0f;
            runtime->playerBody.previousPosition=runtime->playerBody.position;runtime->playerBody.velocity.x=runtime->playerBody.velocity.y=0;
            for(int t=0;t<12&&runtime->world.currentList==expected;++t)if(!RunProjectileTicks(session,*runtime,-1,-1,1)){delete runtime;return 272;}
            NativeWorldResident& home=runtime->world.resident[runtime->world.currentList];
            const NativePlacedRoom* homeDescriptor=runtime->world.CurrentDescriptor();
            if(runtime->world.currentList!=previous||!home.room||!home.doors||!home.perimeterReady||
               runtime->roomTransitions!=transitionsBefore+1||runtime->minimaps.refreshes!=refreshesBefore+1||
               !homeDescriptor||!(homeDescriptor->displayFlags&5U)||memcmp(&random,&backRngExpected,sizeof(random))){
                printf("MINIMAP_REENTRY back-fail current=%d want=%d room=%d doors=%d perimeter=%d mapRefresh=%u transition=%u\n",
                    runtime->world.currentList,previous,home.room?1:0,home.doors?1:0,home.perimeterReady?1:0,
                    runtime->minimaps.refreshes,runtime->roomTransitions);delete runtime;return 273;
            }
            printf("MINIMAP_REENTRY direction=back grid=%d visited=%u clear=%u display=%u activations=%u rngOneDraw=%d small=%.3f,%.3f large=%.3f,%.3f refreshed=%u\n",
                homeDescriptor->gridIndex,homeDescriptor->visitedCount,homeDescriptor->flags&1U,homeDescriptor->displayFlags,
                home.activations,memcmp(&backRngBefore,&random,sizeof(random))?1:0,
                runtime->minimaps.maps[0].currentMarker.x,runtime->minimaps.maps[0].currentMarker.y,
                runtime->minimaps.maps[1].currentMarker.x,runtime->minimaps.maps[1].currentMarker.y,runtime->minimaps.refreshes);
            NativeRoomState* homeRoom=runtime->world.Current();
            runtime->playerBody.position.x=(homeRoom->space.left+homeRoom->space.right)*0.5f;
            runtime->playerBody.position.y=(homeRoom->space.top+homeRoom->space.bottom)*0.5f;
            runtime->playerBody.previousPosition=runtime->playerBody.position;runtime->playerBody.velocity.x=runtime->playerBody.velocity.y=0;
            if(!RunProjectileTicks(session,*runtime,-1,-1,31)){delete runtime;return 274;}
            const int forward=viaSlot[expected];
            if(forward<0||!(home.doors->attached&(1U<<forward))){delete runtime;return 275;}
            NativeDoorActor& forwardDoor=home.doors->actors[forward];
            float fx,fy;if(!homeRoom->geometry.Position(forwardDoor.grid,&fx,&fy)){delete runtime;return 276;}
            runtime->playerBody.position.x=fx+20.0f;runtime->playerBody.position.y=fy+20.0f;
            runtime->playerBody.previousPosition=runtime->playerBody.position;runtime->playerBody.velocity.x=runtime->playerBody.velocity.y=0;
            const unsigned int transitions=runtime->roomTransitions;
            NativeMt forwardRngExpected=random;forwardRngExpected.Next();
            for(int t=0;t<12&&runtime->roomTransitions==transitions;++t)if(!RunProjectileTicks(session,*runtime,-1,-1,1)){delete runtime;return 277;}
            NativeWorldResident& retained=runtime->world.resident[runtime->world.currentList];
            if(runtime->world.currentList!=expected||retained.room!=retainedRoom||retained.doors!=retainedDoors||
               retained.perimeterReady!=1||&runtime->world.Current()->collision.occupied[0]!=retainedGrids||
               retained.activations!=retainedActivation+1||runtime->minimaps.refreshes!=refreshesBefore+2||
               memcmp(&random,&forwardRngExpected,sizeof(random))){
                printf("MINIMAP_REENTRY forward-fail current=%d want=%d room=%d doors=%d perimeter=%d activations=%u/%u refresh=%u\n",
                    runtime->world.currentList,expected,retained.room==retainedRoom?1:0,retained.doors==retainedDoors?1:0,
                    retained.perimeterReady?1:0,retained.activations,retainedActivation,runtime->minimaps.refreshes);delete runtime;return 278;
            }
            printf("MINIMAP_REENTRY direction=forward grid=%d roomRetained=1 doorsRetained=1 perimeterRetained=1 activations=%u rngOneDraw=%d small=%.3f,%.3f large=%.3f,%.3f refreshed=%u\n",
                runtime->world.currentGrid,retained.activations,memcmp(&backRngBefore,&random,sizeof(random))?1:0,
                runtime->minimaps.maps[0].currentMarker.x,runtime->minimaps.maps[0].currentMarker.y,
                runtime->minimaps.maps[1].currentMarker.x,runtime->minimaps.maps[1].currentMarker.y,
                runtime->minimaps.refreshes);
            NativeRoomState* retainedCurrent=runtime->world.Current();
            runtime->playerBody.position.x=(retainedCurrent->space.left+retainedCurrent->space.right)*0.5f;
            runtime->playerBody.position.y=(retainedCurrent->space.top+retainedCurrent->space.bottom)*0.5f;
            runtime->playerBody.previousPosition=runtime->playerBody.position;
            runtime->playerBody.velocity.x=runtime->playerBody.velocity.y=0;
            if(!RunProjectileTicks(session,*runtime,-1,-1,31)){delete runtime;return 284;}
            discovered=DiscoveredRoomCount(layout);lastSmallX=runtime->minimaps.maps[0].currentMarker.x;
            lastSmallY=runtime->minimaps.maps[0].currentMarker.y;lastLargeX=runtime->minimaps.maps[1].currentMarker.x;
            lastLargeY=runtime->minimaps.maps[1].currentMarker.y;
        }
        if(i+1==path.size()){
            printf("FLOOR_BOSS identity=%d award=%d room=%d variant=%u entryEnemies=%d kills=%u\n",
                definition?definition->subtype:-1,runtime->bossAwardIdentity,
                descriptor->definition,definition?definition->variant:0,entryEnemies,roomKills);
            if(!runtime->bossDefeated||!definition||definition->subtype<0){printf("FLOOR_BOSS_FAIL defeated=%d subtype=%d\n",runtime->bossDefeated?1:0,definition?definition->subtype:-1);delete runtime;return 266;}
            if(room->trapdoorState==0){printf("FLOOR_TRAPDOOR missing\n");delete runtime;return 250;}
            for(int t=0;t<40&&room->trapdoorState<2;++t)if(!RunProjectileTicks(session,*runtime,-1,-1,1)){delete runtime;return 251;}
            if(room->trapdoorState!=2){printf("FLOOR_TRAPDOOR state=%d\n",room->trapdoorState);delete runtime;return 252;}
            runtime->playerBody.position.x=room->trapdoorX;runtime->playerBody.position.y=room->trapdoorY;
            runtime->playerBody.previousPosition=runtime->playerBody.position;
            if(!RunProjectileTicks(session,*runtime,-1,-1,1)||!runtime->descendRequested){printf("FLOOR_DESCEND no-contact\n");delete runtime;return 253;}
            const int red=runtime->playerState.redHearts,soul=runtime->playerState.soulHearts;
            const int bombs=runtime->playerState.bombs,keys=runtime->playerState.keys,coins=runtime->playerState.coins;
            const unsigned int kills=runtime->enemyKills;
            NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime->run.context.stage),runtime->run.context.nativeMode,0};
            if(!session.DescendNormal(*runtime,files)){printf("FLOOR_DESCEND fail=%08x\n",runtime->missingNativePrerequisite);delete runtime;return 254;}
            if(runtime->run.context.stage!=2||!runtime->world.Current()||runtime->phase!=PNR_READY){delete runtime;return 255;}
            if(runtime->playerState.redHearts!=red||runtime->playerState.soulHearts!=soul||
               runtime->playerState.bombs!=bombs||runtime->playerState.keys!=keys||runtime->playerState.coins!=coins){delete runtime;return 256;}
            const NativePlacedRoom* nextFloor=runtime->world.CurrentDescriptor();
            printf("FLOOR_TRAPDOOR state=2 descend=1 stage=%d grid=%d hp=%d/%d soul=%d kills=%u\n",
                runtime->run.context.stage,nextFloor?nextFloor->gridIndex:-1,runtime->playerState.redHearts,
                runtime->playerState.maxRedHearts,runtime->playerState.soulHearts,kills);
            if(!nextFloor||!(nextFloor->displayFlags&5U)||!runtime->minimaps.maps[0].ready||
               !runtime->minimaps.maps[1].ready||runtime->minimaps.maps[0].draws.empty()||
               runtime->minimaps.maps[1].draws.empty()||runtime->minimaps.refreshes!=1){
                printf("MINIMAP_FLOOR fail stage=%d display=%u ready=%d/%d draws=%u/%u refreshed=%u\n",
                    runtime->run.context.stage,nextFloor?nextFloor->displayFlags:0,runtime->minimaps.maps[0].ready?1:0,
                    runtime->minimaps.maps[1].ready?1:0,(unsigned int)runtime->minimaps.maps[0].draws.size(),
                    (unsigned int)runtime->minimaps.maps[1].draws.size(),runtime->minimaps.refreshes);
                delete runtime;return 281;
            }
            printf("MINIMAP_FLOOR stage=%d grid=%d refreshed=%u discovered=%u small=%.3f,%.3f large=%.3f,%.3f\n",
                runtime->run.context.stage,runtime->world.currentGrid,runtime->minimaps.refreshes,
                DiscoveredRoomCount(layout),runtime->minimaps.maps[0].currentMarker.x,runtime->minimaps.maps[0].currentMarker.y,
                runtime->minimaps.maps[1].currentMarker.x,runtime->minimaps.maps[1].currentMarker.y);
            //Persistence across the floor boundary carries floor, grid, resources
            //and owned inventory (the continue probe owns the item matrix).
            if(!session.SaveContinue("build/gameplay/floor_continue.dat",*runtime)){delete runtime;return 257;}
            PortNormalRuntime* resumed=new PortNormalRuntime;NativeMt resumedRandom;resumedRandom.Seed(444);
            PortGameSession resumedSession(resumedRandom,ui);NativeSceneFiles resumedFiles={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(2),runtime->run.context.nativeMode,0};
            if(!resumedSession.ContinueSavedRun("build/gameplay/floor_continue.dat",*resumed,resumedFiles)){delete resumed;delete runtime;return 258;}
            if(resumed->run.context.stage!=2||resumed->world.currentGrid!=runtime->world.currentGrid||
               resumed->playerState.redHearts!=red||resumed->playerState.soulHearts!=soul||
               resumed->playerState.bombs!=bombs||resumed->playerState.keys!=keys||resumed->playerState.coins!=coins){delete resumed;delete runtime;return 259;}
            printf("FLOOR_CONTINUE stage=%d grid=%d hp=%d/%d soul=%d bombs=%d keys=%d coins=%d\n",
                resumed->run.context.stage,resumed->world.currentGrid,resumed->playerState.redHearts,resumed->playerState.maxRedHearts,
                resumed->playerState.soulHearts,resumed->playerState.bombs,resumed->playerState.keys,resumed->playerState.coins);
            delete resumed;delete runtime;return 0;
        }
        const int next=path[i+1],slot=viaSlot[next];
        if(slot<0||!(live.doors->attached&(1U<<slot))){printf("FLOOR_DOOR missing step=%u slot=%d\n",i,slot);delete runtime;return 260;}
        int reverse=-1;const NativePlacedRoom* linked=layout.DoorTarget(expected,slot,&reverse);
        if(!linked||linked->listIndex!=next){delete runtime;return 279;}
        reverseSlots[next]=reverse;
        NativeDoorActor& door=live.doors->actors[slot];
        if(door.state!=2){printf("FLOOR_DOOR closed step=%u slot=%d state=%d targetType=%d\n",i,slot,door.state,door.targetType);delete runtime;return 261;}
        printf("FLOOR_CROSS_REQUEST step=%u currentList=%d currentGrid=%d slot=%d targetList=%d storedTarget=%d doorGrid=%d actorTargetGrid=%d\n",
            i,runtime->world.currentList,runtime->world.currentGrid,slot,next,linked->listIndex,door.grid,door.targetGrid);
        float px,py;if(!room->geometry.Position(door.grid,&px,&py)){delete runtime;return 262;}
        runtime->playerBody.position.x=px+20.0f;runtime->playerBody.position.y=py+20.0f;
        runtime->playerBody.previousPosition=runtime->playerBody.position;runtime->playerBody.velocity.x=runtime->playerBody.velocity.y=0;
        const unsigned int transitions=runtime->roomTransitions;
        for(int t=0;t<12&&runtime->roomTransitions==transitions;++t)if(!RunProjectileTicks(session,*runtime,-1,-1,1)){
            printf("FLOOR_TICK_STOP step=%u prereq=%08x phase=%d update=%08x room=%d clear=%d npc=%d/%d\n",i,
                runtime->missingNativePrerequisite,runtime->phase,runtime->playerUpdate.address,runtime->world.currentList,
                room->clear?1:0,room->native12c0,room->native12c4);
            delete runtime;return 263;}
        if(runtime->roomTransitions==transitions||runtime->world.currentList!=next){
            printf("FLOOR_TRANSITION fail step=%u slot=%d transitions=%u want=%d got=%d\n",i,slot,runtime->roomTransitions,next,runtime->world.currentList);
            delete runtime;return 264;}
        const NativePlacedRoom* enteredDescriptor=runtime->world.CurrentDescriptor();
        const NativeMinimap& enteredSmall=runtime->minimaps.maps[0];const NativeMinimap& enteredLarge=runtime->minimaps.maps[1];
        if(!enteredDescriptor||!(enteredDescriptor->displayFlags&5U)||!enteredSmall.ready||!enteredLarge.ready||
           enteredSmall.draws.empty()||enteredLarge.draws.empty()){
            printf("MINIMAP_TRANSITION fail step=%u grid=%d display=%u ready=%d/%d draws=%u/%u\n",i,runtime->world.currentGrid,
                enteredDescriptor?enteredDescriptor->displayFlags:0,enteredSmall.ready?1:0,enteredLarge.ready?1:0,
                (unsigned int)enteredSmall.draws.size(),(unsigned int)enteredLarge.draws.size());delete runtime;return 280;
        }
        printf("MINIMAP_TRANSITION step=%u fromGrid=%d toGrid=%d refreshed=%u discovered=%u small=%.3f,%.3f large=%.3f,%.3f\n",
            i,descriptor->gridIndex,runtime->world.currentGrid,runtime->minimaps.refreshes,
            DiscoveredRoomCount(layout),enteredSmall.currentMarker.x,enteredSmall.currentMarker.y,
            enteredLarge.currentMarker.x,enteredLarge.currentMarker.y);
    }
    delete runtime;return 265;
}
int RunStatProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 103;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY||!runtime->playerState.stats.ready){delete runtime;return 104;}
    const float fd0=runtime->playerState.stats.fireDelay,dmg0=runtime->playerState.stats.damage,spd0=runtime->playerState.stats.moveSpeed;
    if(!runtime->playerState.GrantCollectible(1)||!runtime->playerState.GrantCollectible(4)||
       !runtime->playerState.GrantCollectible(27)||!runtime->playerState.GrantCollectible(46)){delete runtime;return 105;}
    if(!(runtime->playerState.stats.fireDelay<fd0)||!(runtime->playerState.stats.damage>dmg0)||
       !(runtime->playerState.stats.moveSpeed>spd0)||runtime->playerState.stats.luck<1.0f){delete runtime;return 106;}
    printf("STAT fd=%.3f damage=%.3f speed=%.3f luck=%.1f\n",runtime->playerState.stats.fireDelay,
        runtime->playerState.stats.damage,runtime->playerState.stats.moveSpeed,runtime->playerState.stats.luck);
    const float fd1=runtime->playerState.stats.fireDelay,dmg1=runtime->playerState.stats.damage;
    const float spd1=runtime->playerState.stats.moveSpeed,luck1=runtime->playerState.stats.luck;
    delete runtime;
    //The cache operator must be order-independent: the same inventory granted
    //in reverse order owns identical evaluated stats.
    PortNormalRuntime* reverse=new PortNormalRuntime;PortGameSession reverseSession(random,ui);
    PortSessionStart reverseStart=reverseSession.StartNormalCharacter(1,*reverse);
    if(reverseStart.result!=NPS_WORLD_PENDING||!AdvanceToBoundary(reverseSession,*reverse)||reverse->phase!=PNR_READY){delete reverse;return 107;}
    if(!reverse->playerState.GrantCollectible(46)||!reverse->playerState.GrantCollectible(27)||
       !reverse->playerState.GrantCollectible(4)||!reverse->playerState.GrantCollectible(1)){delete reverse;return 108;}
    if(fabs(reverse->playerState.stats.fireDelay-fd1)>0.0001f||fabs(reverse->playerState.stats.damage-dmg1)>0.0001f||
       fabs(reverse->playerState.stats.moveSpeed-spd1)>0.0001f||fabs(reverse->playerState.stats.luck-luck1)>0.0001f){delete reverse;return 109;}
    const int maxBefore=reverse->playerState.maxRedHearts;
    if(!reverse->playerState.GrantCollectible(12)){delete reverse;return 110;}
    if(reverse->playerState.maxRedHearts!=maxBefore+2||reverse->playerState.redHearts!=reverse->playerState.maxRedHearts){delete reverse;return 111;}
    if(fabs(reverse->playerState.stats.damage-6.45f)>0.0001f||fabs(reverse->playerState.stats.moveSpeed-1.6f)>0.0001f){delete reverse;return 112;}
    printf("STAT order=1 resource_hp=%d/%d cache_damage=%.3f cache_speed=%.3f\n",reverse->playerState.redHearts,reverse->playerState.maxRedHearts,
        reverse->playerState.stats.damage,reverse->playerState.stats.moveSpeed);
    delete reverse;return 0;
}int RunConditionalCacheProbe(NativeMt& random,PortFrontend& ui){
    (void)random;ui.characterPage=0;ui.characterChoice=1;ui.characterMode=0;
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING||!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY||!runtime->playerState.stats.ready){delete runtime;return 113;}
    NativePlayerState& state=runtime->playerState;
    const float baseDamage=state.stats.damage,baseShot=state.stats.shotSpeed;
    if(!state.GrantCollectible(415)){delete runtime;return 114;} //Crown of Light
    if(state.stats.damage<baseDamage*1.9f||state.stats.shotSpeed>=baseShot){delete runtime;return 115;}
    if(!state.Damage(1)){}
    if(fabs(state.stats.damage-baseDamage)>0.01f){delete runtime;return 116;} //crown disabled for room
    state.damageTakenThisFloor=0; //isolate the rage tests; crown stays disabled
    if(!state.GrantCollectible(157)||!state.GrantCollectible(695)){delete runtime;return 117;} //Bloody Lust/Gust
    state.redHearts=state.maxRedHearts;state.stats.RecomputeCache(state);
    const float lust0=state.stats.damage,gust0=state.stats.moveSpeed,fd0=state.stats.fireDelay;
    if(!state.Damage(1)){}
    if(fabs(state.stats.damage-(lust0+0.5f))>0.001f||fabs(state.stats.moveSpeed-(gust0+0.07f))>0.001f||!(state.stats.fireDelay<fd0)){delete runtime;return 118;}
    const float lust1=state.stats.damage,gust1=state.stats.moveSpeed;
    if(!state.Damage(1)){}
    if(fabs(state.stats.damage-(lust1+0.7f))>0.001f||fabs(state.stats.moveSpeed-(gust1+0.11f))>0.001f){delete runtime;return 119;}
    state.redHearts=2;if(state.maxRedHearts<2)state.maxRedHearts=2;state.stats.RecomputeCache(state);
    const float range0=state.stats.range,shot0=state.stats.shotSpeed,fd1=state.stats.fireDelay;
    if(!state.GrantCollectible(442)){delete runtime;return 120;} //Dark Prince's Crown
    if(fabs(state.stats.range-(range0+1.5f))>0.001f||fabs(state.stats.shotSpeed-(shot0+0.2f))>0.001f||!(state.stats.fireDelay<fd1)){delete runtime;return 121;}
    state.soulHearts=0;state.blackHeartMask=0;state.boneHearts=0;state.redHearts=state.maxRedHearts;
    if(!state.GrantCollectible(569)){delete runtime;return 122;} //Blood Oath
    state.ResetFloorConditionalCache();
    if(state.redHearts!=1||fabs(state.bloodOathDamage-0.9375f)>0.001f||fabs(state.bloodOathSpeed-0.25f)>0.001f||state.damageTakenThisFloor!=5){delete runtime;return 123;}
    printf("CONDITIONAL crown=1 disabled=1 lust=2 gust=2 dprince=1 blood_oath=1 hp=%d/%d oath_damage=%.3f oath_speed=%.3f damage=%.3f speed=%.3f\n",
        state.redHearts,state.maxRedHearts,state.bloodOathDamage,state.bloodOathSpeed,state.stats.damage,state.stats.moveSpeed);
    delete runtime;return 0;
}int RunHardModeProbe(NativeMt& random,PortFrontend& ui){
    (void)random;(void)ui;
    PortFrontend local;local.Init(false);
    if(!local.BindSaveDirectory("build/gameplay/profile/"))return 98;
    local.profile.difficulty=1;local.characterPage=0;local.characterChoice=1;local.characterMode=1;
    if(!local.music.StartCharacter(0)){printf("HARD music\n");return 99;}
    PortNormalRuntime* runtime=new PortNormalRuntime;NativeMt localGlobal;localGlobal.Seed(777);PortGameSession session(localGlobal,local);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){printf("HARD start=%d\n",start.result);delete runtime;return 100;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){printf("HARD advance phase=%d prereq=%08x\n",runtime->phase,runtime->missingNativePrerequisite);delete runtime;return 101;}
    if(!RunProjectileTicks(session,*runtime,-1,-1,2)){delete runtime;return 102;}
    if(!session.SaveNativeContinue("build/gameplay/hard_gamestate.dat",*runtime)){delete runtime;return 103;}
    PortFrontend resumedUi;resumedUi.Init(false);
    if(!resumedUi.BindSaveDirectory("build/gameplay/")||resumedUi.music.currentTrack!=0){delete runtime;return 103;}
    PortNormalRuntime resumed;NativeMt resumedRandom;resumedRandom.Seed(778);
    NativeSceneFiles resumedFiles={"runtime/game/file_index.bin","runtime/game/",1,2,0};
    if(!PortGameSession(resumedRandom,resumedUi).ContinueNativeSave("build/gameplay/hard_gamestate.dat",resumed,resumedFiles,0)||
       resumed.phase!=PNR_READY||resumed.run.context.nativeMode!=1||resumed.run.player.mode!=1||resumedFiles.nativeMode!=1||
       resumedUi.music.currentTrack==0||resumedUi.music.currentTrack==64||resumedUi.music.currentTrack==95||
       resumedUi.music.native364!=-100){delete runtime;return 104;}
    printf("HARD mode=%d phase=%d player=%d hp=%d/%d slots=%d savedMode=%d\n",runtime->run.player.mode,runtime->phase,
        runtime->playerState.playerType,runtime->playerState.redHearts,runtime->playerState.maxRedHearts,
        (int)runtime->playerState.collectibleCounts.size(),resumed.run.context.nativeMode);
    delete runtime;return 0;
}int RunCharacterMatrixProbe(NativeMt& random,PortFrontend& ui){
    (void)random;(void)ui;
    int ok=0,fail=0;
    for(int page=0;page<2;++page)for(int choice=0;choice<kNativeCharacterCount;++choice){
        const NativeCharacterDef* c=NativeCharacter(page,choice);if(!c)continue;
        const NativePlayerDef* def=NativePlayer(c->player);if(!def){++fail;printf("CHAR_FAIL page=%d choice=%d player=%d missing-def\n",page,choice,c->player);continue;}
        PortFrontend local;local.Init(false);
        if(!local.BindSaveDirectory("build/gameplay/profile/"))return 90;
        memset(&local.profile,0,sizeof(local.profile));
        if(def->achievement>=0&&def->achievement<kNativeAwardCount)local.profile.awards[def->achievement]=true;
        if(def->id==9||def->id==30)local.profile.edenTokens=1;
        local.characterPage=page;local.characterChoice=choice;local.characterMode=0;
        if(!local.music.StartCharacter(page)){++fail;printf("CHAR_FAIL page=%d choice=%d player=%d music\n",page,choice,c->player);continue;}
        PortNormalRuntime* rt=new PortNormalRuntime;NativeMt localGlobal;localGlobal.Seed(1U+(unsigned int)(page*18+choice));PortGameSession session(localGlobal,local);
        PortSessionStart start=session.StartNormalCharacter(1,*rt);
        bool good=start.result==NPS_WORLD_PENDING;
        int startItems=0,startItemsOwned=0;
        if(good){unsigned int steps=AdvanceToBoundary(session,*rt);good=steps&&rt->phase==PNR_READY&&rt->playerState.ready&&rt->playerState.playerType==c->player;
            if(good)good=RunProjectileTicks(session,*rt,-1,-1,2)!=0;}
        if(good)for(int i=0;i<def->itemCount;++i)if(def->items[i]>0){
            bool has=false;++startItems;
            if(rt->playerState.HasCollectible(def->items[i],&has)&&has)++startItemsOwned;
        }
        if(good&&startItemsOwned!=startItems)good=false;
        if(good){++ok;printf("CHAR ok page=%d choice=%d player=%d hp=%d/%d startItems=%d/%d\n",page,choice,c->player,
            rt->playerState.redHearts,rt->playerState.maxRedHearts,startItemsOwned,startItems);}
        else{++fail;printf("CHAR_FAIL page=%d choice=%d player=%d result=%d phase=%d type=%d prereq=%08x\n",page,choice,c->player,
            start.result,rt->phase,rt->playerState.playerType,rt->missingNativePrerequisite);}
        delete rt;
    }
    printf("CHARACTERS ok=%d fail=%d\n",ok,fail);
    return 0;
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
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime->run.context.stage),runtime->run.context.nativeMode,0};
        if(session.AdvanceNormalSetup(*runtime,files)!=NFR_PENDING||room!=runtime->world.Current()||
           memcmp(&before,&random,sizeof(random))||callbackCount!=runtime->playerManager.roomInitCallbacks||!runtime->checkpoints[0].valid){delete runtime;return 12;}
        printf("RESUME residentRetained=1 RNGretained=1 roomCallbacks=%u next=%08x\n",callbackCount,runtime->missingNativePrerequisite);
    }
    if(runtime->phase==PNR_READY){
        const NativeRng rngBeforeTail=runtime->gameRandom;
        //Primary weapon: one diagonal shot whose range expires before the
        //corner, and repeated downward shots that meet the actual bottom wall.
        unsigned int projTicks=RunProjectileTicks(session,*runtime,5,6,1);
        projTicks+=RunProjectileTicks(session,*runtime,-1,-1,160);
        projTicks+=RunProjectileTicks(session,*runtime,7,-1,1);
        projTicks+=RunProjectileTicks(session,*runtime,-1,-1,40);
        printf("PROJECTILES ticks=%u spawned=%u wallHits=%u expired=%u active=%u cooldown=%.9g\n",
            projTicks,runtime->projectiles.spawned,runtime->projectiles.wallHits,runtime->projectiles.expired,
            (unsigned int)runtime->projectiles.active.size(),runtime->projectiles.cooldown);
        if(projTicks!=202||runtime->projectiles.spawned<2||runtime->projectiles.wallHits<1||
           runtime->projectiles.expired<1||!runtime->projectiles.active.empty())return 17;
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
        printf("TICKS ready=%d right=%u start=(%.9g,%.9g) afterRight=(%.9g,%.9g) wall=%u atWall=(%.9g,%.9g) settle=%u settled=(%.9g,%.9g) left=%u afterLeft=(%.9g,%.9g) ticks=%u whole=%u extent=(%.9g,%.9g,%.9g,%.9g)\n",
            runtime->phase==PNR_READY,right,start.x,start.y,afterRight.x,afterRight.y,wall,atWall.x,atWall.y,settle,settled.x,settled.y,left,afterLeft.x,afterLeft.y,
            runtime->completePlayerTicks,runtime->completeWholeGameplayTicks,room.space.left,room.space.top,room.space.right,room.space.bottom);
        if(right<10||afterRight.x<=start.x+1||wall<10||settle<10||fabsf(settled.x-atWall.x)>0.01f||
            left<10||afterLeft.x>=atWall.x-1||runtime->completePlayerTicks<right+wall+settle+left||
            runtime->completeWholeGameplayTicks!=runtime->completePlayerTicks)return 14;
        printf("GAMEPLAY complete=%u playerTicks=%u wholeTicks=%u collisionStop=(%.9g,%.9g) stable=%d\n",
            runtime->completePlayerInitializations,runtime->completePlayerTicks,runtime->completeWholeGameplayTicks,
            settled.x,settled.y,fabsf(settled.x-atWall.x)<=0.01f);
        printf("TAIL_RNG unchanged=%d\n",memcmp(&rngBeforeTail,&runtime->gameRandom,sizeof(NativeRng))==0);
        printf("SIM_STATE clear=%d enemyKills=%u doorsOpened=%u enemies=%d\n",room.clear?1:0,runtime->enemyKills,
            runtime->doorsOpened,runtime->world.resident[runtime->world.currentList].enemies.count);
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
            printf("DOOR slot=%d grid=%d definition=%d type=%d variant=%u\n",slot,p->doorTargets[slot],target?target->definition:-1,d?d->type:-1,d?d->variant:0);
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
    if(!failed){int encounter=RunEncounterProbe(random,ui);if(encounter){printf("ENCOUNTER_RESULT failed=%d\n",encounter);failed=true;}}
    if(!failed){int transition=RunTransitionProbe(random,ui);if(transition){printf("TRANSITION_RESULT failed=%d\n",transition);failed=true;}}
    if(!failed){int locks=RunLockProbe(random,ui);if(locks){printf("LOCK_RESULT failed=%d\n",locks);failed=true;}}
    if(!failed){int descend=RunDescendProbe(random,ui);if(descend){printf("DESCEND_RESULT failed=%d\n",descend);failed=true;}}
    if(!failed){int floor=RunFloorProbe(random,ui);if(floor){printf("FLOOR_RESULT failed=%d\n",floor);failed=true;}}
    if(!failed){int item=RunItemProbe(random,ui);if(item){printf("ITEM_RESULT failed=%d\n",item);failed=true;}}
    if(!failed){int bomb=RunBombProbe(random,ui);if(bomb){printf("BOMB_RESULT failed=%d\n",bomb);failed=true;}}
    if(!failed){int hard=RunHardModeProbe(random,ui);if(hard){printf("HARD_RESULT failed=%d\n",hard);failed=true;}}
    if(!failed){int stat=RunStatProbe(random,ui);if(stat){printf("STAT_RESULT failed=%d\n",stat);failed=true;}}
    if(!failed){int cond=RunConditionalCacheProbe(random,ui);if(cond){printf("CONDITIONAL_RESULT failed=%d\n",cond);failed=true;}}
    if(!failed){int pocket=RunPocketProbe(random,ui);if(pocket){printf("POCKET_RESULT failed=%d\n",pocket);failed=true;}}
    if(!failed){int active=RunActiveProbe(random,ui);if(active){printf("ACTIVE_RESULT failed=%d\n",active);failed=true;}}
    if(!failed){int activeBatch=RunActiveBatchProbe(random,ui);if(activeBatch){printf("ACTIVE_BATCH_RESULT failed=%d\n",activeBatch);failed=true;}}
    if(!failed){int bossAward=RunBossAwardProbe(random,ui);if(bossAward){printf("BOSS_AWARD_RESULT failed=%d\n",bossAward);failed=true;}}
    if(!failed){int reroll=RunRerollCacheProbe(random,ui);if(reroll){printf("REROLL_RESULT failed=%d\n",reroll);failed=true;}}
    if(!failed){int bossAttacks=RunBossAttackProbe(random,ui);if(bossAttacks){printf("BOSS_ATTACKS_RESULT failed=%d\n",bossAttacks);failed=true;}}
    if(!failed){int audioProbe=RunAudioProbe(random,ui);if(audioProbe){printf("AUDIO_RESULT failed=%d\n",audioProbe);failed=true;}}
    if(!failed){int blackHeart=RunBlackHeartProbe(random,ui);if(blackHeart){printf("BLACKHEART_RESULT failed=%d\n",blackHeart);failed=true;}}
    if(!failed){int doorVis=RunDoorVisualProbe(random,ui);if(doorVis){printf("DOORVIS_RESULT failed=%d\n",doorVis);failed=true;}}
    if(!failed){int walkAnim=RunWalkAnimationProbe(random,ui);if(walkAnim){printf("WALKANIM_RESULT failed=%d\n",walkAnim);failed=true;}}
    if(!failed){int familiar=RunFamiliarProbe(random,ui);if(familiar){printf("FAMILIAR_RESULT failed=%d\n",familiar);failed=true;}}
    if(!failed){int cont=RunContinueProbe(random,ui);if(cont){printf("CONTINUE_RESULT failed=%d\n",cont);failed=true;}}
    if(!failed){int nativeCont=RunNativeContinueProbe(random,ui);if(nativeCont){printf("NATIVE_CONTINUE_RESULT failed=%d\n",nativeCont);failed=true;}}
    if(!failed){int nativeCurrent=RunNativeCurrentProbe(random,ui);if(nativeCurrent){printf("NATIVE_CURRENT_RESULT failed=%d\n",nativeCurrent);failed=true;}}
    if(!failed){int greed=RunGreedProbe(random,ui);if(greed){printf("GREED_RESULT failed=%d\n",greed);failed=true;}}
    if(!failed){int chars=RunCharacterMatrixProbe(random,ui);if(chars){printf("CHARACTER_RESULT failed=%d\n",chars);failed=true;}}
    if(!failed){int death=RunDeathProbe(random,ui);if(death){printf("DEATH_RESULT failed=%d\n",death);failed=true;}}
    if(!failed){int pickup=RunGeneratedPickupProbe(random,ui);if(pickup){printf("GENERATED_PICKUP_RESULT failed=%d\n",pickup);failed=true;}}
    if(!failed){int codec=RunNativePickupCodecProbe(random,ui);if(codec){printf("CODEC_FIXTURE_RESULT failed=%d\n",codec);failed=true;}}
    if(!failed){int restart=RunPendingRestartProbe(random,ui);if(restart){printf("PENDING_RESTART_RESULT failed=%d\n",restart);failed=true;}}
    if(!failed){int selectedIntent=RunSelectedRunIntentProbe();if(selectedIntent){printf("SELECTED_RUN_INTENT_RESULT failed=%d\n",selectedIntent);failed=true;}}
    delete runtime;return failed?5:0;
}
