#ifndef GENERIC_NATIVE_FLOOR_H
#define GENERIC_NATIVE_FLOOR_H
#include "native_topology.h"
#include "native_rooms.h"
struct NativeFloorContext {
    int stage,stageType,nativeMode,challenge;
    unsigned int effectiveCurses,gameFlags,runCounter;
    bool held599,reservationProfileGate;
    int catalog;
};
struct NativeFloorPlan {
    NativeLevelSeeds streams;
    NativeRng topologyStream;
    NativeTopologyConfig options;
    int target,minimumDeadEnds,minDifficulty,maxDifficulty,configStage;
    int poolCount,shapeHistogram[13],layoutDraws,secondaryDraws,setupDraws;
    bool widenedPool;
    int catalog;
};
enum NativeFloorResult { NF_OK,NF_BAD_REQUEST,NF_UNSUPPORTED_MODE,NF_MISSING_STAGE,NF_EMPTY_POOL,
    NF_RETRY,NF_CONTENT_READY,NF_ATTEMPT_LIMIT,NF_BAD_PHASE,NF_INVALID_TOPOLOGY };
enum NativeFloorPhase { NF_IDLE,NF_GENERATING,NF_WAIT_CONTENT,NF_COMPLETE,NF_FAILED };
// Ordinary-mode006f6340 setup and retry lifecycle. Effective curses/profile/
// inventory context is explicit; this does not invent unlocks or script state.
NativeFloorResult NativePrepareFloor(const NativeLevelSeeds& streams,const NativeFloorContext& context,NativeFloorPlan* output);
struct NativeFloorGenerator {
    NativeFloorPlan plan;
    NativeFloorPhase phase;
    int attempts,target,contentFailures,sizeGrowths;
    NativeFloorResult Begin(const NativeLevelSeeds& streams,const NativeFloorContext& context);
    NativeFloorResult Step(NativeTopology* topology);
    // Pass the graph after special placement so its extra topology draws also
    // survive rejection/acceptance. Caller layout/secondary streams stay owned.
    bool RejectContent(const NativeTopology* topology=0);
    bool AcceptContent(const NativeTopology* topology=0);
};
#endif
