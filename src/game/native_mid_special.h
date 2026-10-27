#ifndef GENERIC_NATIVE_MID_SPECIAL_H
#define GENERIC_NATIVE_MID_SPECIAL_H
#include "native_optional.h"
// Effective native caller services, independent of constructor support.
struct NativeMidSpecialContext {
    NativePlacementContext placement;
    int stage,stageType,firstPlayerKeys,firstPlayerCoins;
    unsigned int disabledSins; // seed effects9..14, NOT gameplay support flags.
    bool seedEffect32,profile48,profile65,profileOverride,profileC9OrCA;
    bool configuredType6,configuredType11,configuredType9,configuredType20;
    bool levelChallengeSubtype,fullHealthPredicate,eligiblePlayer2;
    bool configuredType24,configuredType4,profile1da;
    float planetariumChance; //Effective00702220; the first clean player0 stage is0.01.
    bool configuredType18,configuredType19,eligibleRoom19,lowHealthPlayer0;
};
enum NativeMidSpecialResult { NMS_OK,NMS_BAD_REQUEST,NMS_UNSUPPORTED_CONTEXT,NMS_NO_ROOM,NMS_BAD_POOL,
    NMS_UNRESOLVED_GEOMETRY,NMS_BAD_GEOMETRY,NMS_DESCRIPTOR_FAILED };
struct NativeMidSpecialReport {
    int type,subtype,definition,topologyIndex,layoutIndex,layoutDraws,selectorDraws,selectionCalls;
    unsigned int entrySeed,selectionSeed,placementSeed,gateRoll,secondGateRoll,flagsLowOr,flagsHighOr;
    int superBound;
    float chanceRoll;
    bool superVariant,fallback,install;
    NativeOptionalAction action;
    NativeRoomResult selectionResult;
    NativeSpecialResult geometryResult;
    NativePlacementResult placementResult;
};
//006f10a6..006f1893: subtype choice, exact-difficulty/fresh-seed fallback,
//optional installation/requeue and the native sin visitation flag effects.
NativeMidSpecialResult NativePlaceMiniboss(NativeTopology&,NativeRoomPool&,NativeRng&,NativeLevelLayout&,
    const NativeMidSpecialContext&,NativeMidSpecialReport*);
//006f1893..006f1a73: type11 selection always occurs, even when stage1 cannot
//install it. One additional owner gate draw is retained when unconfigured.
NativeMidSpecialResult NativePlaceChallengeRoom(NativeTopology&,NativeRoomPool&,NativeRng&,NativeLevelLayout&,
    const NativeMidSpecialContext&,NativeMidSpecialReport*);
//006f1a73..006f1e67: type20 or type9; eligible player2 is the effective result
//of009098b0(2), not merely the presence of player type2.
NativeMidSpecialResult NativePlaceArcadeOrType20(NativeTopology&,NativeRoomPool&,NativeRng&,NativeLevelLayout&,
    const NativeMidSpecialContext&,NativeMidSpecialReport*);
//006f1e67..006f2022: select and resize type24 before the chance/unlock gates.
//A locked room still consumes the floating draw when unconfigured geometry
//exists, then is requeued. No layout draw occurs when geometry is absent.
NativeMidSpecialResult NativePlacePlanetarium(NativeTopology&,NativeRoomPool&,NativeRng&,NativeLevelLayout&,
    const NativeMidSpecialContext&,NativeMidSpecialReport*);
//006f2022..006f23c8. Configured rooms affect selection, not installation.
NativeMidSpecialResult NativePlaceBedroom(NativeTopology&,NativeRoomPool&,NativeRng&,NativeLevelLayout&,
    const NativeMidSpecialContext&,NativeMidSpecialReport*);
#endif
