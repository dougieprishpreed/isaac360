#ifndef GENERIC_NATIVE_OPTIONAL_H
#define GENERIC_NATIVE_OPTIONAL_H
#include "native_layout.h"
struct NativeOptionalContext {
    NativePlacementContext placement;
    int stage,firstPlayer12a4; //Signed native field; meaning remains caller-owned.
    bool configuredType21,configuredType13,hasFirstPlayer;
    bool predicate908b40; //Effective player-manager00908b40(0), not general health logic.
};
enum NativeOptionalAction { NOA_STAGE_SKIP,NOA_PLACED,NOA_REQUEUED,NOA_NO_GEOMETRY };
enum NativeOptionalResult { NOC_OK,NOC_BAD_REQUEST,NOC_UNSUPPORTED_CONTEXT,NOC_NO_PLAYER,NOC_NO_ROOM,NOC_BAD_POOL,
    NOC_UNRESOLVED_GEOMETRY,NOC_BAD_GEOMETRY,NOC_DESCRIPTOR_FAILED };
struct NativeOptionalReport {
    int type,definition,topologyIndex,layoutIndex,layoutDraws,selectorDraws;
    unsigned int entrySeed,roll50,roll5,selectionSeed,roll7,roll4,placementSeed;
    bool requestedType21,install;
    NativeOptionalAction action;
    NativeRoomResult selectionResult;
    NativeSpecialResult geometryResult;
    NativePlacementResult placementResult;
};
//006f085b..006f0c16: choose type21/type13, resize an end room, then either
//install with the FINAL gate-roll seed or append its index to the leaf-list tail.
//A rejected installation still retains selected weight depletion/resized geometry.
NativeOptionalResult NativePlaceOptionalRoom(NativeTopology& topology,NativeRoomPool& pool,NativeRng& layoutStream,
    NativeLevelLayout& layout,const NativeOptionalContext& context,NativeOptionalReport* output);
struct NativeType10Context {
    NativePlacementContext placement;
    int stage;
    bool configuredType10,held599;
};
struct NativeType10Entry {
    int definition,topologyIndex,layoutIndex;
    unsigned int selectionSeed,placementSeed;
    NativeOptionalAction action;
    NativeRoomResult selectionResult;
    NativeSpecialResult geometryResult;
    NativePlacementResult placementResult;
};
struct NativeType10Report {
    int requested,placed,requeued,layoutDraws,localDraws,selectorDraws;
    unsigned int entrySeed,initialSeed,localState;
    NativeType10Entry entries[2];
};
//006f0e81..006f10a6: ONE owner draw then local11/7/12, one/two type10
//candidates. Configured nodes requeue; others consume one/two more local
//draws and install if geometry exists, without a second probability gate.
NativeOptionalResult NativePlaceType10Rooms(NativeTopology& topology,NativeRoomPool& pool,NativeRng& layoutStream,
    NativeLevelLayout& layout,const NativeType10Context& context,NativeType10Report* output);
#endif
