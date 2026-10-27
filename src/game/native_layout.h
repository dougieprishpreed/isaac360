#ifndef GENERIC_NATIVE_LAYOUT_H
#define GENERIC_NATIVE_LAYOUT_H
#include "native_topology.h"
#include "native_rooms.h"
static const int kNativePlacedRoomLimit=507;
struct NativePlacementContext {
    int dimension; // Explicit 0..2; caller resolves the native current dimension.
    int roomConfigStage; // Result of native006ee930, not a seed-array index.
    int nativeMode,specialGridIndex;
    bool held146;
};
struct NativePlacedRoom {
    int gridIndex,safeGridIndex,listIndex,dimension,definition;
    unsigned int doors,flags,placementSeed,nativeB4;
    unsigned int displayFlags; //Descriptor+3c, distinct from unresolved+B4.
    unsigned int visitedCount; //Descriptor+40. Increment is later in Room::Init.
    NativeRoomSeeds seeds;
    int doorTargets[8],doorTargetSlots[8]; //006f3530 potential grid cells / reverse slots.
};
enum NativePlacementResult { NP_OK,NP_BAD_REQUEST,NP_CAPACITY,NP_BAD_FOOTPRINT };
// Common bounded placement/lookup state from006eeea0/006f6100. This is descriptor
// metadata and grid ownership, not a live scene, native save import or full run.
struct NativeLevelLayout {
    int count,currentDimension;
    int owners[3][kNativeTopologyCells];
    NativePlacedRoom rooms[kNativePlacedRoomLimit];
    NativePlacedRoom offGrid[18]; //Native fixed negative slots; not grid/list count.
    void Reset();
    NativePlacementResult Place(const NativeTopologyRoom& topology,int definition,unsigned int placementSeed,
        const NativePlacementContext& context,int* index=0);
    const NativePlacedRoom* ByCell(int gridIndex,int dimension=-1) const;
    const NativePlacedRoom* ByList(int index) const;
    NativePlacedRoom* Special(int gridIndex);
    //006f3ad0: all grid descriptors, then18 fixed negative slots. Does not
    //draw RNG. Potential cells and enabled reciprocal doors remain distinct.
    bool FinalizeDoors();
    const NativePlacedRoom* DoorTarget(int listIndex,int slot,int* reverseSlot=0) const;
};
#endif
