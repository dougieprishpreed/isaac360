#ifndef GENERIC_GAME_SESSION_H
#define GENERIC_GAME_SESSION_H
#include "frontend.h"
#include "normal_run.h"
#include "native_world.h"
#include "native_player_input.h"
#include "native_player_motion.h"
#include "native_player_startup.h"
#include "native_entity_body.h"
#include "native_sprite.h"
#include "native_player_creation.h"
struct PortSessionStart {NativePlayerStartResult result;int choice,player;};
enum PortNormalPhase { PNR_IDLE,PNR_GENERATING,PNR_NEED_CURSES,PNR_ROOM_COMPONENTS,PNR_NEED_NATIVE_INIT,PNR_FACTORY_GAP,PNR_SOURCE_ERROR,PNR_ERROR };
//One owned creation/generation/world lifetime for both adapters. Normal start
//holds at the reached pre-generation appearance service. Neither component
//readiness nor a separately extracted floor can enable a partial player tick.
struct PortNormalRuntime {
    NormalRunFlow run;
    NativeWorld world;
    NativePlayerStartup playerStartup;
    NativeEntityBody playerBody;
    NativePlayerSprites playerSprites;
    NativePlayerState playerState;
    NativePlayerCreation playerCreation;
    PortNormalPhase phase;
    NativeWorldIssue entryIssue;
    unsigned int missingNativePrerequisite;
    unsigned int completePlayerInitializations,completePlayerTicks;
    PortNormalRuntime();
private:
    PortNormalRuntime(const PortNormalRuntime&);
    PortNormalRuntime& operator=(const PortNormalRuntime&);
};
struct PortGameSession {
    NativeMt& globalRandom;PortFrontend& frontend;
    PortGameSession(NativeMt& r,PortFrontend& f):globalRandom(r),frontend(f){}
    PortSessionStart StartNormalCharacter(unsigned int seed,NormalRunFlow&);
    NormalRunResult AdvanceNormalSetup(NormalRunFlow&);
    PortSessionStart StartNormalCharacter(unsigned int seed,PortNormalRuntime&);
    NormalRunResult AdvanceNormalSetup(PortNormalRuntime&,const NativeSceneFiles&);
    NativeWorldResult PrepareNormalWorld(const NormalRunFlow&,NativeWorld&,const NativeSceneFiles&,NativeWorldIssue* issue=0);
    //Component boundary only: real evaluated entity/action inputs are required.
    //Does not initialize a player, advance simulation or enable normal Play.
    NativePlayerInputResult EvaluateNormalInput(const NormalRunFlow&,const NativeWorld&,const NativePlayerInputContext&,NativeActionSource&,bool shooting,NativePoint*);
    NativePlayerVelocityResult EvaluateNormalVelocity(const NormalRunFlow&,const NativeWorld&,const NativePlayerVelocityContext&,NativeActionSource&,NativeMovementEffects&,NativePlayerVelocityState*,NativePlayerVelocityIssue* issue=0);
private:
    PortSessionStart BeginNormalCharacter(unsigned int,NormalRunFlow&);
    PortGameSession& operator=(const PortGameSession&);
};
#endif
