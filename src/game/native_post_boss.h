#ifndef GENERIC_NATIVE_POST_BOSS_H
#define GENERIC_NATIVE_POST_BOSS_H
#include "native_layout.h"
struct NativePostBossContext {
    NativePlacementContext placement;
    bool configuredType8,extraType8; //Effective config-tree/held589 predicates.
};
struct NativePostBossEntry {
    int definition,topologyIndex,layoutIndex;
    unsigned int placementSeed;
    NativeRoomResult selectionResult;
    NativeSpecialResult geometryResult;
    NativePlacementResult placementResult;
};
struct NativePostBossReport {
    int requested,placed,layoutDraws,localDraws,selectorDraws;
    unsigned int initialSeed,localState,nextSelectionSeed;
    bool optionalSecondFailure;
    NativePostBossEntry entries[2];
};
enum NativePostBossResult { NPB_OK,NPB_BAD_REQUEST,NPB_UNSUPPORTED_CONTEXT,NPB_NO_ROOM,NPB_BAD_POOL,
    NPB_FIRST_GEOMETRY_FAILED,NPB_UNRESOLVED_GEOMETRY,NPB_DESCRIPTOR_FAILED };
//006efce4..006efebc: optional one/two type8 end-room placements, followed by
//the UNCONDITIONAL layout draw for the next (treasure) selection branch. That
//branch must consume nextSelectionSeed, not draw its initial seed again.
NativePostBossResult NativePlacePostBossType8(NativeTopology& topology,NativeRoomPool& pool,NativeRng& layoutStream,
    NativeLevelLayout& layout,const NativePostBossContext& context,NativePostBossReport* output);
#endif
