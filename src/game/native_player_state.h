#ifndef GENERIC_NATIVE_PLAYER_STATE_H
#define GENERIC_NATIVE_PLAYER_STATE_H
#include "native_player.h"
#include "native_player_startup.h"
#include "native_entity_body.h"
#include "native_player_effects.h"
#include "native_player_stats.h"
#include <map>
struct NativePlayerResourceUi {
    unsigned int dirty,state;
    NativePlayerResourceUi():dirty(0),state(0){}
    void Changed(){if(dirty!=2){dirty=1;state=2;}}
};
//Owned numeric/inventory portion of fresh ordinary player0 Init. This does
//not complete appearance/Init, apply starting collectibles, register/place the entity,
//evaluate stats or authorize a tick. Pending grants are explicit.
class NativePlayerState {
public:
    bool ready;int playerType;bool canShoot;
    int maxRedHearts,redHearts,soulHearts,blackHeartMask; //1288,128c,1294,1298.
    int keys,bombs,coins; //12a4,12ac,12b0.
    bool resourcesReady,runInitializing; //Shared New Run lifecycle's native243378.
    bool native1734,playerTearsEnabled; //709ed1=false;709ad7's1335=true.
    int boneHearts,boneMask,brokenHearts,rottenHearts,storedHearts,bombReserve;
    short healthItemSlots,healthItemRemainder;bool changingHealthItems;
    int activeItems[4],temporaryCollectible;
    unsigned int transformationFlags;
    int formCounts[15];
    std::vector<int> pillEffectCounts;
    std::map<int,int> collectibleOverrides;
    unsigned short native12c8;unsigned int native12ca;
    std::vector<int> collectibleCounts,pendingCollectibles;
    std::vector<int> native166c,native1678; //Empty variable-length histories, not98 card counters.
    std::vector<unsigned char> trinketCounts;
    int heldTrinkets[2],temporaryTrinket,native17ec,native17f0;
    bool trinketsDisabled,ghost,temporaryTrinketGolden;
    std::vector<unsigned char> trinketOverrides;
    NativePlayerEffects effects;
    NativePlayerStats stats;
    NativePlayerState();
    //7099a2..709ff6 only. Starting resources belong to the later76c1d0
    //call, after appearance setup; never project them into a live session.
    bool ResetFresh(const NativeRunStart&,NativePlayerStartup&,NativeEntityBody&);
    bool ResetFreshNumbers(const NativeRunStart&,const NativePlayerStartup&,NativeEntityBody&);
    bool FinishFreshInventory(NativePlayerStartup&);
    bool PrepareStartingResources(const NativeRunStart&);
    //Actual76c1d0 ordinary first-player branch, including zero-amount helper
    //calls and resource/UI effects. Starting grants belong to76c990 later.
    bool InitializeStartingResources(const NativeRunStart&,NativePlayerResourceUi&);
    bool HasCollectible(int id,bool* result) const;
    bool TrinketMultiplier(int id,int* result) const;
    bool HeartLimit(bool ignoreBroken,int* result) const;
    bool BeginFresh(const NativeRunStart&,NativePlayerStartup&,NativeEntityBody&);
    bool FullHealth() const; //Per ordinary player in00908b40 /0078bb50.
    bool LowHealth() const;
    void Swap(NativePlayerState&);
    unsigned int StateHash() const;
private:
    bool AddMaxHearts(int amount);
    bool AddSoulHearts(int amount);
    bool AddBlackHearts(int amount);
    bool AddBrokenHearts(int amount);
    bool ReconcileHealthItems();
    bool ReconcileBoneHearts();
    void TrimBlackHearts();
    void TrimStoredHearts();
    NativePlayerState(const NativePlayerState&);
    NativePlayerState& operator=(const NativePlayerState&);
};
#endif
