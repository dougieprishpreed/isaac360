#ifndef GENERIC_NATIVE_TREASURE_H
#define GENERIC_NATIVE_TREASURE_H
// Historical component name. Native type2 is SHOP, including upgrade/rare
// subtypes; see normal raw names "Shop L1" etc. Do not swap caller order.
#include "native_layout.h"
struct NativeTreasureContext {
    NativePlacementContext placement;
    int stage,stageType,counter1c312c;
    bool configuredType2,held110,held161,held155;
    // Effective profile bytes and override predicate, not an unlock service.
    bool profileDb,profileDc,profileDd,profileDe,profileOverride;
    // Any player with native +2c==0 and +1304==33. Not a general held test.
    bool eligiblePlayerTrinket33;
};
enum NativeTreasureSkip { NTS_NONE,NTS_STAGE,NTS_CONFIGURED,NTS_COUNTER };
enum NativeTreasureResult { NTR_OK,NTR_BAD_REQUEST,NTR_UNSUPPORTED_CONTEXT,NTR_NO_ROOM,NTR_BAD_POOL,
    NTR_GEOMETRY_FAILED,NTR_UNRESOLVED_GEOMETRY,NTR_DESCRIPTOR_FAILED };
struct NativeTreasureReport {
    NativeTreasureSkip skipped;
    int boundA,boundB,subtype,layoutDraws,localDraws,selectorDraws;
    unsigned int entrySeed,subtypeSeed,localState,placementSeed,rareByte;
    int definition,topologyIndex,layoutIndex;
    NativeRoomResult selectionResult;
    NativeSpecialResult geometryResult;
    NativePlacementResult placementResult;
};
//006efebc..006f0339. Enter AFTER NativePlacePostBossType8's unconditional
//following draw. The incoming state is already drawn; do not burn it again.
//A selected treasure uses two MORE owner draws and four local3/3/26 draws.
NativeTreasureResult NativePlaceTreasure(NativeTopology& topology,NativeRoomPool& pool,NativeRng& layoutStream,
    NativeLevelLayout& layout,const NativeTreasureContext& context,NativeTreasureReport* output);
#endif
