#ifndef GENERIC_NATIVE_WORLD_H
#define GENERIC_NATIVE_WORLD_H
#include "native_scene.h"
#include "native_doors.h"
#include "native_enemy.h"
#include "native_pickup.h"
#include "native_save_reader.h"
static const int kNativeWorldRoomLimit=kNativePlacedRoomLimit+18;
struct NormalRunFlow;
class NativePlayerState;
class NativePlayerManager;
class NativeRoomEffects;
class NativeEntityBody;
class NativePlayerCreation;
class NativeRoomFx;
struct NativeRoomView;
struct NativeDoorVisual;
struct NativeRoomPersistent;
class NativeRoomUpdate;
enum NativeWorldResult { NWR_OK,NWR_BAD_REQUEST,NWR_NO_ROOM,NWR_NO_DOOR,NWR_SOURCE_ERROR,NWR_CONSTRUCTION_GAP };
struct NativeWorldIssue {
    NativeWorldResult result;int listIndex,gridIndex,dimension,definition;
    NativeSceneResult source;NativeConstructionIssue construction;
    unsigned int nativeNext;
};
struct NativeWorldResident {
    NativeRoomState* room;unsigned int hash,activations;
    NativeDoorSet* doors;
    NativeRoomEffects* effects;
    NativeRoomView* view;
    NativeDoorVisual* doorVisuals;
    NativeRoomPersistent* persistent; //Descriptor68/74 mutable persistence owners.
    NativeRoomUpdate* update;
    NativeEnemyList enemies;
    NativePickupList pickups;
    unsigned int doorSelectionMask,doorPendingOpenMask;
    int backdrop,nativeMode,configStage;
    bool setupKnown;
    bool perimeterReady;
    unsigned int roomInitAddress;
    int tintedRockRoom,tintedRockGrid;
    //Diagnostic only: which door visual binding stopped construction.
    int doorFailureSlot,doorFailureKind,doorFailureVariant,doorFailureAux;
    int doorVisualGaps;
    NativeRng roomInitRandom;
    bool gridPostConstructed;
    bool savedActorsRestored;
    unsigned int restoreAddress;
    int ambientCounts[3],ambientGroup,ambientIndex;
    NativeRoomSetupState setup;
};
//Full descriptor ownership with lazy live-room storage. A selected room is
//constructed only at entry; unvisited rooms remain selected, never discarded.
//No constructor failure changes generation, descriptors or the current room.
class NativeWorld {
public:
    NativeLevelLayout layout;
    int currentList,previousList,residentCount; //Fixed negative slots map507..524.
    int currentGrid,previousGrid; //Actual entered cells, including occupied L cells.
    unsigned int entrySerial;
    NativeWorldResident resident[kNativeWorldRoomLimit];
    std::vector<NativeSaveSummary::SavedRoom> restoredRooms;
    NativeWorld();
    ~NativeWorld();
    void Reset();
    //Atomic level replacement. No raw reads, room construction or RNG draws.
    bool Bind(const NativeLevelLayout& selected);
    NativeWorldResult Start(const NativeLevelLayout&,int startGrid,int dimension,const NativeSceneFiles&,NativeWorldIssue* issue=0,const NativeRoomSetupContext* setup=0,
        const std::vector<NativeSaveSummary::SavedRoom>* restored=0);
    void Swap(NativeWorld&);
    NativeWorldResult Enter(int gridIndex,int dimension,const NativeSceneFiles&,NativeWorldIssue* issue=0,const NativeRoomSetupContext* setup=0);
    //Descriptor link traversal only. Interactive lock/payment/blast/secret-door
    //rules belong to the door actor; this does not bypass them in gameplay.
    NativeWorldResult EnterLinked(int nativeSlot,const NativeSceneFiles&,NativeWorldIssue* issue=0);
    //Explicit native factory boundary; caller supplies the current global MT
    //owner after all preceding Init work. Does not assume a run-seed bootstrap.
    NativeDoorResult PrepareDoorActors(NativeMt&,unsigned int frame);
    //Presentation binding for the current resident after a transition or
    //reentry. Unsupported visual targets keep their actor/collision state and
    //are counted as explicit gaps; they do not fail the resident.
    NativeDoorResult PrepareDoorVisuals(int stage,int stageType,int nativeMode,int challenge,const NativePlayerState&);
    NativeWorldResult PreparePerimeter(NativeMt&,unsigned int frame);
    NativeWorldResult ContinueInitialRoom(NormalRunFlow&,const NativePlayerState&,const NativeEntityBody&,const NativePlayerCreation&,NativePlayerManager&,const NativeRoomFx&,NativeMt&,unsigned int* nextAddress);
    bool CaptureInitialRoom(unsigned int* nextAddress); //79d610, before6fca60.
    //Pre-asset006cb870 selection only, using the exact linked descriptors.
    NativeDoorResult SelectDoorVariant(int slot,const NativeDoorBinding&);
    NativeRoomState* Current();
    const NativeRoomState* Current() const;
    const NativePlacedRoom* CurrentDescriptor() const;
    NativePlacedRoom* CurrentDescriptor();
    //Room::Init's reached descriptor bookkeeping at7a3b8a. Call after the
    //resident is prepared; this does not infer clear from a bare entry.
    NativeWorldResult RecordActivation(NormalRunFlow&);
    void SyncCurrentRoomClear();
    //Install a native current-floor room set before constructing its first
    //resident. Matching uses saved descriptor identity, not a current-cell
    //alias; all visited-room identities are validated before mutation.
    bool InstallSavedRooms(const NativeSaveSummary&,NativeWorldIssue*);
    const NativeSaveSummary::SavedRoom* RestoredRoom(int listIndex) const;
    bool CurrentRoomRestored() const;
    const NativeWorldResident* At(int listIndex) const;
private:
    NativeWorld(const NativeWorld&);
    NativeWorld& operator=(const NativeWorld&);
};
#endif
