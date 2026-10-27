#ifndef GENERIC_NATIVE_ORDINARY_H
#define GENERIC_NATIVE_ORDINARY_H
#include "native_layout.h"
struct NativeOrdinaryContext {
    int stage,stageType,configStage,startGridIndex,minDifficulty,maxDifficulty;
    unsigned int effectiveCurses;
    NativePlacementContext placement;
};
struct NativeOrdinaryEntry {
    int topologyIndex,layoutIndex,definition,minDifficulty;
    unsigned int placementSeed;
};
struct NativeOrdinaryReport {
    int orderCount,placed,startLookups,relaxed,layoutDraws,selectorDraws,tailBurns,failedTopologyIndex;
    NativeRoomResult roomResult;
    NativePlacementResult placementResult;
    NativeOrdinaryEntry entries[kNativeTopologyCells];
};
enum NativeOrdinaryResult { NOP_OK,NOP_BAD_REQUEST,NOP_UNSUPPORTED_CONTEXT,NOP_MISSING_STAGE,NOP_NO_ROOM,NOP_PLACEMENT_FAILED };
//006f28b8..006f2a9d only: after special placement, before later substitutions.
// A failure retains consumed RNG/depletion and already placed descriptors. The
// outer floor retry owns descriptor reset; it must not rewind those streams.
NativeOrdinaryResult NativePlaceOrdinary(const NativeTopology& topology,NativeRoomPool& pool,
    NativeRng& layoutStream,NativeLevelLayout& layout,const NativeOrdinaryContext& context,NativeOrdinaryReport* report);
#endif
