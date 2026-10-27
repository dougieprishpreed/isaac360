#ifndef GENERIC_NATIVE_SHOP_H
#define GENERIC_NATIVE_SHOP_H
// Historical component name. This caller selects type4 TREASURE rooms after
// the type2 shop caller.
#include "native_layout.h"
struct NativeShopContext {
    NativePlacementContext placement;
    int stage,stageType,trinket82Multiplier; //00908a70 result, clamped1..2.
    unsigned int curses,runFlags;
    bool configuredType4,held111,held161,held155;
    bool held82,held112,eligiblePlayerTrinket112,challenge19,extraConfig88;
};
struct NativeShopEntry {
    int subtype,definition,topologyIndex,layoutIndex,layoutDraws,selectorDraws;
    unsigned int firstRoll,secondRoll,placementSeed;
    bool special,fallback;
    NativeRoomResult primaryResult,selectionResult;
    NativeSpecialResult geometryResult;
    NativePlacementResult placementResult;
};
struct NativeShopReport {
    int requested,placed,layoutDraws,selectorDraws;
    unsigned int entrySeed,firstPlacedState;
    bool stageGate,extraBranch,restored;
    NativeShopEntry entries[2];
};
enum NativeShopResult { NSH_OK,NSH_BAD_REQUEST,NSH_UNSUPPORTED_CONTEXT,NSH_NO_ROOM,NSH_BAD_POOL,
    NSH_GEOMETRY_FAILED,NSH_UNRESOLVED_GEOMETRY,NSH_DESCRIPTOR_FAILED };
//006f0339..006f085b, including007d4f90 same-seed config-stage->stage0
//fallback. Effective profile/inventory/config predicates remain caller-owned.
NativeShopResult NativePlaceShops(NativeTopology& topology,NativeRoomPool& pool,NativeRng& layoutStream,
    NativeLevelLayout& layout,const NativeShopContext& context,NativeShopReport* output);
#endif
