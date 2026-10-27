#ifndef GENERIC_NATIVE_TOPOLOGY_H
#define GENERIC_NATIVE_TOPOLOGY_H
#include "native_rng.h"
#include "native_geometry.h"
static const int kNativeTopologyCells=169;
static const int kNativeCandidateCapacity=112; // Eight doors * (one + thirteen).
struct NativeTopologyRoom {
    NativeCell origin,connection;
    int shape,index,direction,parentSlot,depth;
    unsigned int doors;
    int neighbours[8],neighbourCount;
    bool deadEnd;
    bool nativeFlag44; // Separate native byte+44, not the leaf byte+3c.
};
struct NativeTopologyConfig {
    unsigned int shapes;
    bool reserveAboveStart,loopA,loopB;
    unsigned char reserved[kNativeTopologyCells];
};
enum NativeTopologyResult { NT_OK,NT_BAD_REQUEST,NT_EXHAUSTED,NT_TOO_FEW_DEAD_ENDS,NT_INVALID_CONNECTION };
enum NativeSpecialResult { NSP_OK,NSP_BAD_REQUEST,NSP_NO_MATCH,NSP_UNRESOLVED_DOOR };
struct NativeTopology {
    NativeRng rng;
    NativeTopologyConfig config;
    int occupied[kNativeTopologyCells];
    NativeTopologyRoom rooms[kNativeTopologyCells];
    int count,loops,deadEndCount,otherCount,repairAttempts;
    int deadEnds[kNativeTopologyCells],others[kNativeTopologyCells];
    unsigned int draws;
    int bossPending,bossPlacements; // Native wrapper+388/+38c, reset per attempt.
    // One native growth attempt. extraRooms EXCLUDES the starting room.
    // Exhaustion preserves the partial graph and consumed stream for its caller;
    // retry policy belongs to the higher-level floor-placement routine.
    NativeTopologyResult Generate(const NativeRng& stream,const NativeTopologyConfig& options,
        const NativeTopologyRoom& start,int extraRooms,int minimumDeadEnds=0);
    // Full008fa440 wrapper, including code beyond the incorrectly noreturn
    // logger: up to five GrowOne/Refresh repairs, then the depth sort. A short
    // initial growth is diagnostic; native caller acceptance uses leaf count.
    NativeTopologyResult GenerateWrapped(const NativeRng& stream,const NativeTopologyConfig& options,
        const NativeTopologyRoom& start,int extraRooms,int minimumDeadEnds);
    bool CanPlace(NativeCell origin,int shape) const;
    bool DoorsCompatible(NativeCell origin,int shape) const;
    int CountNeighbours(NativeCell cell) const;
    int Candidates(int index,bool force,NativeTopologyRoom* output);
    void Shuffle(NativeTopologyRoom* candidates,int size);
    NativeTopologyResult Refresh();
    // 008fb870 exports nonleaf list first, then the current leaf list. Capacity
    // failure leaves output unchanged; sorting/removal belongs to prior calls.
    int PlacementOrder(int* indices,int capacity) const;
    // 008fa680: shuffle existing room indices, force candidate probabilities,
    // and append the first shape-1 end room. Returns its index, or -1 exhausted.
    int AddDeadEnd();
    // 008fb970: source-room and candidate order, forced shape probabilities,
    // first placement with fewer than two neighbouring cells; no shuffle.
    int GrowOne();
    //008fd990: ordered deterministic resize; final thin5/7 candidate reads
    // a native stack cookie as a door slot and is explicitly unsupported.
    NativeSpecialResult TryResizeEndRoom(int index,int shape,unsigned int doors,int* candidate=0);
    NativeSpecialResult PickEndRoom(int shape,unsigned int doors,int* index);
    //008fb4e0/008fb6a0: depth/straight-run selection and optional cached second
    // boss when config.loopA is set. Lists and partial door refresh are retained.
    NativeSpecialResult PickBossRoom(int shape,unsigned int doors,bool forceNew,int* index);
    NativeSpecialResult ReserveRoom(int index); //008fe000 footprint plus cross.
    //008fa9b0: candidate scan and tied-score sampling use this topology's RNG.
    //These additions refresh adjacent doors without changing placement lists.
    int PickSecretRoom(const unsigned char blocked[kNativeTopologyCells]);
    //008fae80: isolated type29 room, with distance-two compatibility checks.
    int PickDistantRoom();
private:
    unsigned int Draw();
    int Place(const NativeTopologyRoom& room);
    void SortDeadEnds();
    void RefreshRoom(int index);
    int AddIsolated(int cell,bool distant);
    NativeSpecialResult PrepareBossRoom(int shape,unsigned int doors);
};
NativeTopologyRoom NativeTopologyStart(int x,int y,int shape=1);
#endif
