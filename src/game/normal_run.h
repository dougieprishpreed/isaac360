#ifndef GENERIC_NORMAL_RUN_H
#define GENERIC_NORMAL_RUN_H
#include "native_boss_lifecycle.h"
#include "native_post_boss.h"
#include "native_treasure.h"
#include "native_shop.h"
#include "native_player.h"
#include "native_profile_room.h"
#include "native_mid_special.h"
#include "native_secret.h"
#include "native_ordinary.h"
#include "native_offgrid.h"
#include "native_player_state.h"
enum NormalRunPhase { NRF_IDLE,NRF_NEED_FLOOR_CONTEXT,NRF_TOPOLOGY,NRF_BOSS,NRF_TYPE8,NRF_TYPE2_SHOP,NRF_TYPE4_TREASURE,NRF_OPTIONAL,NRF_PROFILE_ROOM,NRF_TYPE10,NRF_MINIBOSS,NRF_CHALLENGE_ROOM,NRF_ARCADE_TYPE20,NRF_PLANETARIUM,NRF_BEDROOM,NRF_SECRET,NRF_TYPE29,NRF_ORDINARY,NRF_OFFGRID,NRF_LEVEL_FINISH,NRF_NEED_ROOM_ENTRY,NRF_BLOCKED };
enum NormalRunResult { NFR_OK,NFR_PENDING,NFR_BAD_REQUEST,NFR_MISSING_DATA,NFR_NATIVE_RETRY,NFR_NATIVE_UNRESOLVED,NFR_ATTEMPT_GUARD };
// Staged normal-run caller. Missing higher caller inputs suspend before their RNG-consuming
// call; rejected native geometry retains the actual consumed streams/weights.
struct NormalRunFlow {
    NormalRunPhase phase;NormalRunResult result;
    NativeSeeds seeds;NativeRunStart player;NativeProfile profile;
    NativeFloorContext context;NativeFloorGenerator floor;NativeTopology topology;
    NativeRoomPool pool;NativeLevelLayout layout;NativeBossIdentityManager bosses;
    NativeBossSequence bossSequence;NativeBossEligibility bossEligibility;
    int firstPlayerKeys,firstPlayerCoins;bool fullHealthPredicate,minibossPlaced;
    int firstPlayerRedHearts,firstPlayerSoulHearts;bool firstPlayerStateOwned;
    unsigned int runFlagsLow,runFlagsHigh;
    unsigned int retainedRoomSeed; //006efeb4; secret and type29 local owners.
    NativeMidSpecialReport midReport;
    NativeSecretReport secretReport;
    NativeOrdinaryReport ordinaryReport;
    NativeOffgridReport offgridReport;
    unsigned int levelSeed; //006fa6c4; one layout advance after door finalization.
    int currentGrid,previousGrid; //006faa68/006faa6e initial start descriptor.
    NativePlacementContext placement;int lastDefinition,lastCode,steps,rejections;
    bool Begin(unsigned int seed,const NativeRunStart& player,const NativeProfile& profile);
    //Snapshot the actual pre-floor player numeric owner. Generation may retry
    //without resetting these values from XML. No partially initialized tick.
    bool SupplyPlayerState(const NativePlayerState&);
    // First ordinary chapter006fb850: stage1 seed bits, profile86/342. This
    // reads its owner without consuming it. Effective curses remain external.
    bool InitialStageType(int* stageType) const;
    NormalRunResult SupplyInitialFloor(unsigned int effectiveCurses);
    // Clean ordinary first stage before native profile byte65/award33: its
    // curse denominator is zero. Later-profile curse services remain pending.
    NormalRunResult StartInitialFloor();
    // Explicit post-curse owner is required for nonzero/late-profile fixtures.
    // A clean first stage before award33 has a proven undrawn setup clone.
    NormalRunResult SupplyFloorContext(const NativeFloorContext& context,const NativeRng* evaluatedCurseOwner=0);
    NormalRunResult Step();
private:
    NormalRunResult Block(NormalRunResult why,int code);
    NormalRunResult Reject(int code);
};
#endif
