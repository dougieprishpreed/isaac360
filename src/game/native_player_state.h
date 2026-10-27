#ifndef GENERIC_NATIVE_PLAYER_STATE_H
#define GENERIC_NATIVE_PLAYER_STATE_H
#include "native_player.h"
#include "native_player_startup.h"
#include "native_entity_body.h"
#include "native_player_effects.h"
//Owned numeric/inventory portion of fresh ordinary player0 Init. This does
//not complete appearance/Init, apply starting collectibles, register/place the entity,
//evaluate stats or authorize a tick. Pending grants are explicit.
class NativePlayerState {
public:
    bool ready;int playerType;bool canShoot;
    int maxRedHearts,redHearts,soulHearts,blackHeartMask; //1288,128c,1294,1298.
    int keys,bombs,coins; //12a4,12ac,12b0.
    unsigned short native12c8;unsigned int native12ca;
    std::vector<int> collectibleCounts,cardCounts,pendingCollectibles;
    std::vector<unsigned char> trinketCounts;
    NativePlayerEffects effects;
    NativePlayerState();
    //7099a2..709ff6 only. Starting resources belong to the later76c1d0
    //call, after appearance setup; never project them into a live session.
    bool ResetFresh(const NativeRunStart&,NativePlayerStartup&,NativeEntityBody&);
    bool ResetFreshNumbers(const NativeRunStart&,const NativePlayerStartup&,NativeEntityBody&);
    bool FinishFreshInventory(NativePlayerStartup&);
    bool PrepareStartingResources(const NativeRunStart&);
    bool BeginFresh(const NativeRunStart&,NativePlayerStartup&,NativeEntityBody&);
    bool FullHealth() const; //Per ordinary player in00908b40 /0078bb50.
    bool LowHealth() const;
    void Swap(NativePlayerState&);
    unsigned int StateHash() const;
private:
    NativePlayerState(const NativePlayerState&);
    NativePlayerState& operator=(const NativePlayerState&);
};
#endif
