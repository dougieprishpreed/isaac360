#ifndef GENERIC_NATIVE_PROFILE_ROOM_H
#define GENERIC_NATIVE_PROFILE_ROOM_H
#include "native_optional.h"
struct NativeProfileRoom12Context {
    NativePlacementContext placement;
    int stage;
    bool profileDb,profileDc,profileDd,profileDe,profileOverride;
    bool configuredType12,seedFlag8;
};
enum NativeProfileRoom12Result { NP12_OK,NP12_BAD_REQUEST,NP12_UNSUPPORTED_CONTEXT,NP12_NO_ROOM,NP12_BAD_POOL,
    NP12_UNRESOLVED_GEOMETRY,NP12_BAD_GEOMETRY,NP12_DESCRIPTOR_FAILED };
struct NativeProfileRoom12Report {
    int highest,subtype,definition,topologyIndex,layoutIndex,layoutDraws,selectorDraws;
    unsigned int entrySeed,selectionSeed,subtypeState,roll20,roll4,placementSeed;
    bool install;
    NativeOptionalAction action;
    NativeRoomResult selectionResult;
    NativeSpecialResult geometryResult;
    NativePlacementResult placementResult;
};
//006f0c16..006f0e81: both selection seed and bounded subtype consume the SAME
//layout owner. Query uses the earlier seed and NO weight decrease. Geometry is
//kept when requeued; install uses the final gate-roll state.
NativeProfileRoom12Result NativePlaceProfileRoom12(NativeTopology& topology,NativeRoomPool& pool,NativeRng& layoutStream,
    NativeLevelLayout& layout,const NativeProfileRoom12Context& context,NativeProfileRoom12Report* output);
#endif
