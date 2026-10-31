#ifndef GENERIC_NATIVE_PLAYER_STATE_H
#define GENERIC_NATIVE_PLAYER_STATE_H
#include "native_player.h"
#include "native_player_startup.h"
#include "native_entity_body.h"
#include "native_player_effects.h"
#include "native_player_stats.h"
#include <map>
class NativeItemPools;
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
    //Conditional cache owners for Bloody Lust/Bloody Gust (shared hit count)
    //and Crown of Light's per-room disable. Reset by floor/room transitions.
    int damageTakenThisFloor;bool crownOfLightDisabled;
    //Blood Oath's floor-owned drained-health bonuses. The effect persists even
    //if the item is removed, until the next floor start.
    float bloodOathDamage,bloodOathSpeed;
    //Current-room active-item buffs (Book of Belial, The Nail, Razor Blade).
    float roomDamageBonus,roomSpeedBonus;
    //Permanent stat changes from Experimental Treatment/D8/Missing No, plus
    //Purity's per-room aura (1 damage,2 fire rate,3 speed,4 range).
    float permDamageBonus,permFireRateBonus,permRangeBonus,permShotSpeedBonus,permSpeedBonus,permLuckBonus;
    int purityAura;bool purityActive;
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
    int heldCard,heldPill; //Pocket slots: card id / pill effect id,0 empty.
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
    //Maintained pickup grant: records ownership in the native collectible
    //counter slice. Per-item stat/effect branches remain separate services.
    bool GrantCollectible(int id);
    //Reached ordinary contact damage: soul hearts absorb before red hearts.
    //Returns true when the player has no remaining heart health.
    bool Damage(int amount);
    bool TrinketMultiplier(int id,int* result) const;
    bool HeartLimit(bool ignoreBroken,int* result) const;
    bool BeginFresh(const NativeRunStart&,NativePlayerStartup&,NativeEntityBody&);
    bool FullHealth() const; //Per ordinary player in00908b40 /0078bb50.
    //Trigger/reroll cache owners.
    void ApplyExperimentalTreatment(unsigned int sample);
    void RerollStats(unsigned int sample); //D8-style permanent stat reroll.
    bool ApplyMissingNo(NativeItemPools&,unsigned int sample); //Passive reroll + stats.
    //Conditional cache room/floor boundaries. The rage counter is per floor;
    //Crown of Light's deactivation and Purity's aura are per room.
    void ResetRoomConditionalCache(unsigned int sample=0);
    void ResetFloorConditionalCache(unsigned int sample=0);
    bool AddHeartContainers(int halfHearts){if(!AddMaxHearts(halfHearts))return false;redHearts+=halfHearts;if(redHearts>maxRedHearts)redHearts=maxRedHearts;return true;}
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
