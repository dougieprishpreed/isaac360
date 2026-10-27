#include "native_player_state.h"
#include <algorithm>
#include <string.h>
namespace {
void Fold(unsigned int& h,unsigned int n){for(int i=0;i<4;++i)h=(h^((n>>(i*8))&255))*16777619U;}
}
NativePlayerState::NativePlayerState():ready(false),playerType(-1),canShoot(false),maxRedHearts(0),redHearts(0),soulHearts(0),blackHeartMask(0),keys(0),bombs(0),coins(0),native12c8(0),native12ca(0){}
bool NativePlayerState::ResetFreshNumbers(const NativeRunStart& start,const NativePlayerStartup& startup,NativeEntityBody& body){
    const NativePlayerDef* d=NativePlayer(start.player);
    if(!NativeRunStartValid(start)||start.player!=0||!d||!startup.ready||!body.ready||body.type!=1||body.variant!=0||body.subtype!=start.player||
        body.initialSeed!=startup.entityInitSeed||body.entityCollisionClass!=0||startup.banks[NPB_TRINKET].size()<=97||d->black||d->card||d->pill||d->pocketActive)return false;
    NativePlayerState next;next.playerType=start.player;
    //7099a5 Clear(true) is empty on a genuinely fresh player. No callbacks
    //are skipped. Later nonempty reinitialization needs the actual services.
    if(next.effects.Clear(true)!=NEF_OK)return false;
    Swap(next);
    body.entityCollisionClass=5;body.hitPoints=body.maxHitPoints=1; //70998c,7099e8..fc.
    return true;
}
bool NativePlayerState::FinishFreshInventory(NativePlayerStartup& startup){
    if(ready||playerType!=0||!startup.ready||startup.banks[NPB_TRINKET].size()<=97||!collectibleCounts.empty()||!trinketCounts.empty()||!cardCounts.empty())return false;
    NativeRng local=startup.banks[NPB_TRINKET][97];unsigned int value;if(!local.Next(&value))return false;
    native12c8=(unsigned short)(12+value%9);native12ca=0; //709e04..62.
    //77c5d0: normal challenge0's default+6c=1 (4203f8), variant0,
    //player0's inherited canShoot. No item/RNG/effect query is folded away.
    const NativePlayerDef* d=NativePlayer(playerType);if(!d)return false;canShoot=d->canShoot;
    collectibleCounts.assign(startup.banks[NPB_COLLECTIBLE].size(),0);
    trinketCounts.assign(startup.banks[NPB_TRINKET].size(),0);
    cardCounts.assign(startup.banks[NPB_CARD].size(),0);
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
    cardCounts.swap(b.cardCounts);pendingCollectibles.swap(b.pendingCollectibles);effects.Swap(b.effects);
}
unsigned int NativePlayerState::StateHash() const {
    unsigned int h=2166136261U;Fold(h,ready);Fold(h,playerType);Fold(h,canShoot);Fold(h,maxRedHearts);Fold(h,redHearts);Fold(h,soulHearts);Fold(h,blackHeartMask);
    Fold(h,keys);Fold(h,bombs);Fold(h,coins);Fold(h,native12c8);Fold(h,native12ca);Fold(h,effects.StateHash());
    Fold(h,(unsigned int)collectibleCounts.size());for(unsigned int i=0;i<collectibleCounts.size();++i)Fold(h,collectibleCounts[i]);
    Fold(h,(unsigned int)trinketCounts.size());for(unsigned int i=0;i<trinketCounts.size();++i)Fold(h,trinketCounts[i]);
    Fold(h,(unsigned int)cardCounts.size());for(unsigned int i=0;i<cardCounts.size();++i)Fold(h,cardCounts[i]);
    Fold(h,(unsigned int)pendingCollectibles.size());for(unsigned int i=0;i<pendingCollectibles.size();++i)Fold(h,pendingCollectibles[i]);return h;
}
