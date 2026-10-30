#include "native_player_state.h"
#include <algorithm>
#include <string.h>
namespace {
void Fold(unsigned int& h,unsigned int n){for(int i=0;i<4;++i)h=(h^((n>>(i*8))&255))*16777619U;}
}
NativePlayerState::NativePlayerState():ready(false),playerType(-1),canShoot(false),maxRedHearts(0),redHearts(0),soulHearts(0),blackHeartMask(0),keys(0),bombs(0),coins(0),resourcesReady(false),runInitializing(false),native1734(false),playerTearsEnabled(false),boneHearts(0),boneMask(0),brokenHearts(0),rottenHearts(0),storedHearts(0),bombReserve(0),healthItemSlots(0),healthItemRemainder(0),changingHealthItems(false),temporaryCollectible(0),transformationFlags(0),native12c8(0),native12ca(0){for(int i=0;i<4;++i)activeItems[i]=0;for(int i=0;i<15;++i)formCounts[i]=0;heldTrinkets[0]=heldTrinkets[1]=temporaryTrinket=native17ec=native17f0=0;trinketsDisabled=ghost=temporaryTrinketGolden=false;}
bool NativePlayerState::ResetFreshNumbers(const NativeRunStart& start,const NativePlayerStartup& startup,NativeEntityBody& body){
    const NativePlayerDef* d=NativePlayer(start.player);
    if(ready||playerType!=-1||!NativeRunStartValid(start)||start.player!=0||!d||!startup.ready||!body.ready||body.type!=1||body.variant!=0||body.subtype!=start.player||
        body.initialSeed!=startup.entityInitSeed||body.entityCollisionClass!=4||startup.banks[NPB_TRINKET].size()<=97||d->black||d->card||d->pill||d->pocketActive)return false;
    NativePlayerState next;next.playerType=start.player;next.runInitializing=true;
    //7099a5 Clear(true) is empty on a genuinely fresh player. No callbacks
    //are skipped. Later nonempty reinitialization needs the actual services.
    if(next.effects.Clear(true)!=NEF_OK)return false;
    Swap(next);
    body.renderDepth=5;body.hitPoints=body.maxHitPoints=1; //70998c,7099e8..fc. 2a4 is NOT entity collision class180.
    return true;
}
bool NativePlayerState::FinishFreshInventory(NativePlayerStartup& startup){
    if(ready||playerType!=0||!startup.ready||startup.banks[NPB_TRINKET].size()<=97||!collectibleCounts.empty()||!trinketCounts.empty())return false;
    NativeRng local=startup.banks[NPB_TRINKET][97];unsigned int value;if(!local.Next(&value))return false;
    native12c8=(unsigned short)(12+value%9);native12ca=0; //709e04..62.
    //77c5d0: normal challenge0's default+6c=1 (4203f8), variant0,
    //player0's inherited canShoot. No item/RNG/effect query is folded away.
    const NativePlayerDef* d=NativePlayer(playerType);if(!d)return false;canShoot=d->canShoot;
    collectibleCounts.assign(startup.banks[NPB_COLLECTIBLE].size(),0);
    trinketCounts.assign(startup.banks[NPB_TRINKET].size(),0);
    //709f6a/74,70a0c4,70b3d0,7099ce,70b477. These are live query
    //owners, independent of the constructor snapshot retained for diagnostics.
    heldTrinkets[0]=heldTrinkets[1]=temporaryTrinket=native17ec=native17f0=0;
    trinketsDisabled=ghost=temporaryTrinketGolden=false;
    native1734=false;playerTearsEnabled=true;
    trinketOverrides.assign(startup.banks[NPB_TRINKET].size(),0);
    //709fb5..709ff4 uses ItemConfig+30 (pilleffect), independently of cards.
    pillEffectCounts.assign(startup.banks[NPB_PILL].size(),0);
    startup.banks[NPB_TRINKET][97]=local;ready=true;return true;
}
bool NativePlayerState::ResetFresh(const NativeRunStart& start,NativePlayerStartup& startup,NativeEntityBody& body){
    return ResetFreshNumbers(start,startup,body)&&FinishFreshInventory(startup);
}
bool NativePlayerState::PrepareStartingResources(const NativeRunStart& start){
    const NativePlayerDef* d=NativePlayer(start.player);
    if(!ready||!NativeRunStartValid(start)||playerType!=start.player||!d||maxRedHearts||redHearts||soulHearts||keys||bombs||coins||!pendingCollectibles.empty())return false;
    //76c1d0's player0 path: metadata health and resource values. The reset
    //starts at zero; ordinary resource helpers clamp to[0,99]. UI dirtiness
    //and other helpers' side effects remain outside this numeric component.
    maxRedHearts=redHearts=d->hp;soulHearts=d->armor;
    keys=std::max(0,std::min(99,d->keys));bombs=std::max(0,std::min(99,d->bombs));coins=std::max(0,std::min(99,d->coins));
    for(int i=0;i<d->itemCount;++i)pendingCollectibles.push_back(d->items[i]);
    if(start.isaacD6)pendingCollectibles.push_back(105); //76cc8e; no fake grant.
    return true;
}
bool NativePlayerState::BeginFresh(const NativeRunStart& start,NativePlayerStartup& startup,NativeEntityBody& body){
    //Retained extraction component: the session uses the individual lifecycle
    //calls in native order and cannot cross the intervening missing services.
    return ResetFresh(start,startup,body)&&PrepareStartingResources(start);
}
bool NativePlayerState::FullHealth() const {
    unsigned int sum=(unsigned int)redHearts+(unsigned int)soulHearts;int total;memcpy(&total,&sum,4);
    return total>=maxRedHearts||effects.Has(0,112)||playerType==39;
}
bool NativePlayerState::LowHealth() const {return (redHearts<2&&soulHearts<1)||(redHearts<1&&soulHearts<3);}
void NativePlayerState::Swap(NativePlayerState& b){
    std::swap(ready,b.ready);std::swap(playerType,b.playerType);std::swap(canShoot,b.canShoot);std::swap(maxRedHearts,b.maxRedHearts);std::swap(redHearts,b.redHearts);
    std::swap(soulHearts,b.soulHearts);std::swap(blackHeartMask,b.blackHeartMask);std::swap(keys,b.keys);std::swap(bombs,b.bombs);std::swap(coins,b.coins);
    std::swap(native12c8,b.native12c8);std::swap(native12ca,b.native12ca);collectibleCounts.swap(b.collectibleCounts);trinketCounts.swap(b.trinketCounts);
    for(int i=0;i<2;++i)std::swap(heldTrinkets[i],b.heldTrinkets[i]);std::swap(temporaryTrinket,b.temporaryTrinket);std::swap(native17ec,b.native17ec);std::swap(native17f0,b.native17f0);
    std::swap(trinketsDisabled,b.trinketsDisabled);std::swap(ghost,b.ghost);std::swap(temporaryTrinketGolden,b.temporaryTrinketGolden);trinketOverrides.swap(b.trinketOverrides);
    std::swap(resourcesReady,b.resourcesReady);std::swap(runInitializing,b.runInitializing);std::swap(boneHearts,b.boneHearts);std::swap(boneMask,b.boneMask);std::swap(brokenHearts,b.brokenHearts);
    std::swap(native1734,b.native1734);std::swap(playerTearsEnabled,b.playerTearsEnabled);
    std::swap(rottenHearts,b.rottenHearts);std::swap(storedHearts,b.storedHearts);std::swap(bombReserve,b.bombReserve);
    std::swap(healthItemSlots,b.healthItemSlots);std::swap(healthItemRemainder,b.healthItemRemainder);std::swap(changingHealthItems,b.changingHealthItems);
    std::swap(temporaryCollectible,b.temporaryCollectible);collectibleOverrides.swap(b.collectibleOverrides);for(int i=0;i<4;++i)std::swap(activeItems[i],b.activeItems[i]);
    std::swap(transformationFlags,b.transformationFlags);for(int i=0;i<15;++i)std::swap(formCounts[i],b.formCounts[i]);pillEffectCounts.swap(b.pillEffectCounts);
    native166c.swap(b.native166c);native1678.swap(b.native1678);pendingCollectibles.swap(b.pendingCollectibles);effects.Swap(b.effects);stats.Swap(b.stats);
}
unsigned int NativePlayerState::StateHash() const {
    unsigned int h=2166136261U;Fold(h,ready);Fold(h,playerType);Fold(h,canShoot);Fold(h,maxRedHearts);Fold(h,redHearts);Fold(h,soulHearts);Fold(h,blackHeartMask);
    Fold(h,keys);Fold(h,bombs);Fold(h,coins);Fold(h,native12c8);Fold(h,native12ca);Fold(h,effects.StateHash());Fold(h,stats.StateHash());
    Fold(h,resourcesReady);Fold(h,runInitializing);Fold(h,boneHearts);Fold(h,boneMask);Fold(h,brokenHearts);Fold(h,rottenHearts);Fold(h,storedHearts);Fold(h,bombReserve);
    Fold(h,native1734);Fold(h,playerTearsEnabled);
    Fold(h,healthItemSlots);Fold(h,healthItemRemainder);Fold(h,changingHealthItems);Fold(h,temporaryCollectible);
    for(int i=0;i<4;++i)Fold(h,activeItems[i]);for(std::map<int,int>::const_iterator p=collectibleOverrides.begin();p!=collectibleOverrides.end();++p){Fold(h,p->first);Fold(h,p->second);}
    Fold(h,transformationFlags);for(int i=0;i<15;++i)Fold(h,formCounts[i]);Fold(h,(unsigned int)pillEffectCounts.size());for(unsigned int i=0;i<pillEffectCounts.size();++i)Fold(h,pillEffectCounts[i]);
    Fold(h,(unsigned int)collectibleCounts.size());for(unsigned int i=0;i<collectibleCounts.size();++i)Fold(h,collectibleCounts[i]);
    Fold(h,(unsigned int)trinketCounts.size());for(unsigned int i=0;i<trinketCounts.size();++i)Fold(h,trinketCounts[i]);
    for(int i=0;i<2;++i)Fold(h,heldTrinkets[i]);Fold(h,temporaryTrinket);Fold(h,native17ec);Fold(h,native17f0);Fold(h,trinketsDisabled);Fold(h,ghost);Fold(h,temporaryTrinketGolden);
    Fold(h,(unsigned int)trinketOverrides.size());for(unsigned int i=0;i<trinketOverrides.size();++i)Fold(h,trinketOverrides[i]);
    Fold(h,(unsigned int)native166c.size());for(unsigned int i=0;i<native166c.size();++i)Fold(h,native166c[i]);
    Fold(h,(unsigned int)native1678.size());for(unsigned int i=0;i<native1678.size();++i)Fold(h,native1678[i]);
    Fold(h,(unsigned int)pendingCollectibles.size());for(unsigned int i=0;i<pendingCollectibles.size();++i)Fold(h,pendingCollectibles[i]);return h;
}
