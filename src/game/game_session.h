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
#include "native_player_manager.h"
#include "native_room_fx.h"
#include "native_item_pools.h"
#include "native_nightmares.h"
#include "native_persistence.h"
#include "native_minimap.h"
#include "native_save_writer.h"
#include "native_pause_ui.h"
#include "native_player_update.h"
struct PortSessionStart {NativePlayerStartResult result;int choice,player;};
enum PortNormalPhase { PNR_IDLE,PNR_GENERATING,PNR_NEED_CURSES,PNR_ROOM_COMPONENTS,PNR_NEED_NATIVE_INIT,PNR_READY,PNR_FACTORY_GAP,PNR_SOURCE_ERROR,PNR_ERROR };
//One owned creation/generation/world lifetime for both adapters. Normal start
//holds inside selected-room entry at the native checkpoint service. Neither
//component readiness nor a separately extracted floor enables a partial tick.
struct PortNormalRuntime {
    NormalRunFlow run;
    NativeRng gameRandom;
    NativeItemPools itemPools;
    NativeSoundState sounds;
    NativeNightmares nightmares;
    NativeRunHistory history;
    NativeRunCheckpoint checkpoints[2];
    NativeGamePersistent initialSave;
    NativeSaveWriter initialSaveFile;
    bool initialSaveFileWritten;
    NativeWorld world;
    NativePlayerStartup playerStartup;
    NativeEntityBody playerBody;
    NativePlayerSprites playerSprites;
    NativePlayerState playerState;
    NativePlayerCreation playerCreation;
    NativePlayerManager playerManager;
    NativePlayerUpdate playerUpdate;
    NativeRoomFx roomFx;
    NativeMinimaps minimaps;
    NativePauseUi pauseUi;
    PortNormalPhase phase;
    NativeWorldIssue entryIssue;
    unsigned int missingNativePrerequisite;
    unsigned int completePlayerInitializations,completePlayerTicks;
    unsigned int tickFrame;
    bool roomEntryPrefixPrepared;
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
    NormalRunResult AdvanceNormalSetup(PortNormalRuntime&,const NativeSceneFiles&,NativeActionSource* actions=0);
    NormalRunResult TickNormal(PortNormalRuntime&,const NativeSceneFiles&,NativeActionSource* actions);
    NativeWorldResult PrepareNormalWorld(const NormalRunFlow&,NativeWorld&,const NativeSceneFiles&,NativeWorldIssue* issue=0);
    //Component boundary only: real evaluated entity/action inputs are required.
    //Does not initialize a player, advance simulation or enable normal Play.
    NativePlayerInputResult EvaluateNormalInput(const NormalRunFlow&,const NativeWorld&,const NativePlayerInputContext&,NativeActionSource&,bool shooting,NativePoint*);
    NativePlayerVelocityResult EvaluateNormalVelocity(const NormalRunFlow&,const NativeWorld&,const NativePlayerVelocityContext&,NativeActionSource&,NativeMovementEffects&,NativePlayerVelocityState*,NativePlayerVelocityIssue* issue=0);
private:
    PortSessionStart BeginNormalCharacter(unsigned int,NormalRunFlow&);
    NativeWorldResult ContinueInitialGeneration(PortNormalRuntime&,NativeActionSource*);
    PortGameSession& operator=(const PortGameSession&);
};
#endif
