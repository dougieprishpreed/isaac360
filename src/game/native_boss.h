#ifndef GENERIC_NATIVE_BOSS_H
#define GENERIC_NATIVE_BOSS_H
#include "native_layout.h"
struct NativeBossEligibility {
    // Explicit native profile bytes54,55,56,88,19e,19f, in that order.
    bool subtype20,subtype21,subtype19,subtype42,subtypes57to66,subtypes67to72;
    bool profileOverride; // Effective backend2/run-counter/flag bypass.
};
struct NativeBossRequest {
    int identity,nativeMode,effectiveConfigStage;
    unsigned int seed,runFlags,effectiveCurses;
    NativeBossEligibility eligibility;
};
struct NativeBossReport {
    int definition,attempts,selectorDraws,fallbacks;
    unsigned int localState;
    bool retryLimit;
    NativeRoomResult roomResult;
};
enum NativeBossResult { NB_OK,NB_BAD_REQUEST,NB_UNSUPPORTED_CONTEXT,NB_MISSING_STAGE,NB_NO_ROOM,NB_BAD_POOL };
bool NativeBossSubtypeAllowed(unsigned int subtype,const NativeBossEligibility& eligibility);
//006ef3d0: caller identity and seed are explicit. Local5/15/17 stream and every
// rejected selection/depletion are retained; this is not boss identity picking.
NativeBossResult NativeSelectBossRoom(NativeRoomPool& pool,const NativeBossRequest& request,NativeBossReport* output);
struct NativeBossPlacementReport {
    NativeBossReport selection;
    NativeBossResult selectionResult;
    int topologyIndex,layoutIndex;
    unsigned int placementSeed;
    NativeSpecialResult geometryResult;
    NativePlacementResult placementResult;
};
enum NativeBossPlacementResult { NBP_OK,NBP_BAD_REQUEST,NBP_SELECTION_FAILED,NBP_GEOMETRY_FAILED,NBP_DESCRIPTOR_FAILED };
//Ordinary caller boundary after identity selection: one layout draw, metadata,
//actual selected geometry, descriptor. Earlier identity RNG/cache services and
//higher curse/stage/profile sequencing remain separate; no stream is rewound.
NativeBossPlacementResult NativePlaceSelectedBoss(NativeTopology& topology,NativeRoomPool& pool,NativeRng& layoutStream,
    NativeLevelLayout& layout,const NativeBossRequest& context,const NativePlacementContext& placement,bool forceNew,NativeBossPlacementReport* output);
enum NativeBossSequencePhase { NBS_IDLE,NBS_FIRST_IDENTITY,NBS_SECOND_IDENTITY,NBS_COMPLETE,NBS_FAILED };
enum NativeBossSequenceResult { NBS_OK,NBS_NEED_IDENTITY,NBS_BAD_REQUEST,NBS_UNSUPPORTED_CONTEXT,NBS_PLACEMENT_FAILED,NBS_RESERVATION_FAILED };
//Normal006ef6b0 prefix, with identity selection explicitly requested between
//placements. Stage12 multi-boss and the stage11 profile shortcut are separate.
struct NativeBossSequence {
    NativeBossSequencePhase phase;
    NativeBossRequest context;
    NativePlacementContext placement;
    int stage,firstLayoutIndex,lastLayoutIndex,lastTopologyIndex;
    unsigned int restorationSeed;
    NativeBossSequenceResult Begin(int stage,bool stage11Shortcut,const NativeBossRequest& context,const NativePlacementContext& placement);
    int IdentityStage() const;
    NativeBossSequenceResult PlaceIdentity(int identity,NativeTopology& topology,NativeRoomPool& pool,NativeRng& layoutStream,
        NativeLevelLayout& layout,NativeBossPlacementReport* output);
};
#endif
