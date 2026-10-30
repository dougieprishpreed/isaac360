#ifndef GENERIC_NATIVE_PLAYER_MANAGER_H
#define GENERIC_NATIVE_PLAYER_MANAGER_H
#include "native_player_state.h"
#include <vector>
struct NativeSpriteSheet;
class NativePlayerCreation;
struct NormalRunFlow;
class NativeWorld;
const NativeSpriteSheet* NativePreparedPlayerTearsSheet();

//904960: stable player identities precede virtual Init; world registration is
//separate. A handle survives the temporary-to-session ownership swap.
class NativePlayerManager {
public:
    bool begun,initialized,worldRegistered;
    unsigned int seed,entityToken,nextEntityToken,postInitCalls;
    std::vector<unsigned int> members,worldMembers;
    std::vector<int> worldTypes;
    std::vector<unsigned int> updateMembers; //414d24: only actors without flag8 at insertion.
    int controller,hudPlayerTokens[8];bool hudDirty[8];
    std::vector<int> babyCandidates,characterCandidates;
    int selectedBabies[5],selectedBabyCount;
    int slotIdentity[4][2],slotTimer[4][2];
    NativePlayerResourceUi resourceUi;
    unsigned int hudRefreshes,cacheCallbacks;
    unsigned int roomInitCallbacks;
    unsigned int levelInitCallbacks;
    bool roomCacheDirty;
    std::vector<NativePoint> entryPlayerOffsets; //905dc7..905f45, primary-relative.
    const NativeSpriteSheet* playerTearsSheet;
    bool PrepareInitialRoomEntry(const NativePlayerState&,const NativeEntityBody&,unsigned int* next);
    bool PlaceInitial(const NormalRunFlow&,const NativeWorld&,NativePlayerCreation&,const NativePlayerState&,NativeEntityBody&,unsigned int* next);
    bool RefreshFreshHud(const NativeEntityBody&,int controller);
    bool RegisterEntity(NativeEntityBody&);
    bool EmptyCacheCallback(unsigned int flag);
    bool EmptyRoomInitCallback(); //7f2d50, original registry empty.
    bool EmptyLevelInitCallback(); //7f2f30, original registry empty.
    bool ReconcileNoFamiliars(int variant,int count,const NativePlayerStartup&);
    NativePlayerManager();
    bool BeginFresh(unsigned int playerSeed);
    bool RegisterFirst();
    bool InitializeResources(const NativeRunStart&,NativePlayerState&);
    bool PostPlayerInit(const NativePlayerState&);
    bool RegisterWorld(NativeEntityBody&);
    bool FinishSetup(const NativeProfile&);
    void Swap(NativePlayerManager&);
private:
    std::vector<int> postInitCallbacks; //Native72 lists start empty; mods deferred.
    NativePlayerManager(const NativePlayerManager&);
    NativePlayerManager& operator=(const NativePlayerManager&);
};
#endif
