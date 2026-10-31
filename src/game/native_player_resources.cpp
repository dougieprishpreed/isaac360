#include "native_player_state.h"
#include "native_sprite.h"
#include "native_player_resource_data.h"
#include <algorithm>

const NativeSpriteSheet* NativePreparedPlayerTearsSheet(){return &kNativePlayerTearsSheet;}

namespace {
int Clamp(int n,int low,int high){return std::max(low,std::min(high,n));}
bool EffectCollectible(int id){
    for(unsigned int i=0;i<sizeof(kNativeEffectCollectibles)/sizeof(kNativeEffectCollectibles[0]);++i)if(kNativeEffectCollectibles[i]==id)return true;
    return false;
}
}
bool NativePlayerState::HasCollectible(int id,bool* result) const{
    if(!result||!ready||id<0||(unsigned int)id>=collectibleCounts.size())return false;
    //7226e6..7228c2. A special identity does not itself imply an entitlement.
    //On this ordinary owner the periodic trinkets are genuinely absent; their
    //positive branches require the floor/frame RNG context before proceeding.
    if((id==327||id==328)&&trinketCounts.size()>21&&trinketCounts[21])return false;
    if(id==9&&trinketCounts.size()>93&&trinketCounts[93])return false;
    if(id==313&&effects.Has(0,112))return false; //Needs profile award286.
    //78aaf0 only entitles types3/12; both are excluded by this owner's type0.
    //The four transformation entitlements use1f10, reset to0 in fresh Init.
    if((id==305&&(transformationFlags&0x10))||(id==257&&(transformationFlags&8))||
       (id==231&&(transformationFlags&0x20))||(id==182&&(transformationFlags&0x40))){*result=true;return true;}
    if(id==118&&effects.Has(1,id)){*result=true;return true;}
    //Fresh ordinary variant0 has no ghost, parent/twin inventory forwarding,
    //or alternate-form tag filter. The actual owned tree/slots/counts follow
    //72292e..722a08, including the recursive191/pocket predicates.
    std::map<int,int>::const_iterator p=collectibleOverrides.find(id);
    if(p!=collectibleOverrides.end()&&p->second>0){*result=true;return true;}
    if(temporaryCollectible&&temporaryCollectible==id){*result=true;return true;}
    if(EffectCollectible(id)&&effects.Has(1,id)){*result=true;return true;}
    for(int i=0;i<4;++i)if(activeItems[i]==id){*result=true;return true;}
    if(id!=191){bool box=false;if(!HasCollectible(191,&box))return false;
        if(box){return false;} //Its copied pocket identity is not initialized on this path.
    }
    if(effects.Has(1,488))return false; //Needs its real copied pocket identity.
    *result=collectibleCounts[id]>0;return true;
}
bool NativePlayerState::GrantCollectible(int id){
    if(!ready||id<=0||(unsigned int)id>=collectibleCounts.size())return false;
    ++collectibleCounts[id];
    //Prepared non-stat item effects with exact resource/heart semantics. Stat-
    //cache items remain owned but their native cache arithmetic is separate.
    switch(id){
    case 15: //<3
    case 22: //Lunch
    case 23: //Dinner
    case 24: //Dessert
    case 25: //Breakfast
    case 26: //Rotten Meat
        if(AddMaxHearts(2)){redHearts=std::min(maxRedHearts,redHearts+2);}
        break;
    case 16: //Raw Liver
        if(AddMaxHearts(4)){redHearts=std::min(maxRedHearts,redHearts+4);}
        break;
    case 17: keys=std::min(99,keys+99);break; //Skeleton Key
    case 18: coins=std::min(99,coins+99);break; //A Dollar
    case 19: bombs=std::min(99,bombs+10);break; //Boom!
    default:break;
    }
    //Prepared stat-cache deltas for the common original items. These adjust the
    //live evaluated stats after grant; the full native cache operator remains
    //a separate port item.
    if(stats.ready){
        switch(id){
        case 1:{ //The Sad Onion: +0.7 tears.
            float tears=30.0f/(stats.fireDelay+1.0f)-1.0f;
            tears+=0.7f;stats.fireDelay=30.0f/(tears+1.0f)-1.0f;break;}
        case 4: //Cricket's Head: damage x1.5.
            stats.damage*=1.5f;stats.baseDamage*=1.5f;break;
        case 12: //Magic Mushroom: damage x1.5 and +0.3 speed.
            stats.damage*=1.5f;stats.baseDamage*=1.5f;
            stats.moveSpeed=std::min(2.0f,stats.moveSpeed+0.3f);break;
        case 27: //Wooden Spoon: +0.3 speed.
        case 28: //The Belt: +0.3 speed.
            stats.moveSpeed=std::min(2.0f,stats.moveSpeed+0.3f);break;
        case 46: stats.luck+=1.0f;break; //Lucky Foot
        default:break;
        }
        stats.dirty=0xffffU;
    }
    return true;
}
bool NativePlayerState::HeartLimit(bool ignoreBroken,int* result) const{
    if(!result)return false;
    bool birthright=false;if(!HasCollectible(619,&birthright))return false;
    //77b3fd..77ba5d: ordinary variant0/type0 capacity, then broken-heart loss.
    int limit=24+(birthright?12:0);*result=ignoreBroken?limit:std::max(0,limit-2*brokenHearts);return true;
}
void NativePlayerState::TrimBlackHearts(){
    //70cce0. Retain only the native low ceil(soul/2) bits; no removal effect
    //is reached in the initial zero-mask state.
    int n=(soulHearts+1)/2;
    if(n<32)blackHeartMask=(int)((unsigned int)blackHeartMask&((1U<<n)-1));
}
void NativePlayerState::TrimStoredHearts(){
    //77af60's positive stored-heart removal branch is not reached initially.
    int capacity=(std::min(maxRedHearts,redHearts)+1)/2+(soulHearts+1)/2+boneHearts;
    storedHearts=std::min(storedHearts,capacity);
}
bool NativePlayerState::ReconcileBoneHearts(){
    int capacity;if(!HeartLimit(false,&capacity))return false;
    boneHearts=std::min(boneHearts,capacity/2-(maxRedHearts+soulHearts+1)/2);
    if(boneHearts<=0){boneHearts=boneMask=0;return true;}
    //No initial bone-heart metadata for this character. Later nonempty masks
    //require77b331's original slot reordering before this method can finish.
    return false;
}
bool NativePlayerState::ReconcileHealthItems(){
    //77f35e tests243378 BEFORE the health-item guard, queries or mutation.
    if(runInitializing)return true;
    //77f340: the reentry flag is scoped to the complete helper, not a cached
    //query answer. Ordinary health item501 is absent on this fresh path.
    if(changingHealthItems)return true;
    changingHealthItems=true;
    int previous=healthItemSlots;
    healthItemSlots=(short)std::min((int)healthItemSlots,maxRedHearts/2+(int)healthItemRemainder);
    bool item=false;bool ok=HasCollectible(501,&item);
    if(!ok||item||previous!=healthItemSlots||healthItemSlots||healthItemRemainder){changingHealthItems=false;return false;}
    changingHealthItems=false;return true;
}
bool NativePlayerState::AddSoulHearts(int amount){
    //70c810. Positive gains are the black/starting-heart path used by the
    //prepared character metadata (70c810's normalization below).
    if(amount<0)return false;
    int capacity;if(!HeartLimit(false,&capacity))return false;
    soulHearts=Clamp(soulHearts+amount,0,capacity-maxRedHearts-2*boneHearts);
    if(soulHearts>=8)return false; //Reached award service is not silently skipped.
    TrimBlackHearts();TrimStoredHearts();return ReconcileBoneHearts();
}
bool NativePlayerState::AddBlackHearts(int amount){
    //70cae4..70cc09: native odd-half accounting, before AddSoulHearts.
    int original=soulHearts,rounded=original+(original&1),odd=amount&1,adjust=0;
    if((original&1)&&odd){adjust=-1;odd=0;}
    int capacity;if(!HeartLimit(false,&capacity))return false;
    if(rounded+maxRedHearts+2*boneHearts>=capacity-1){
        odd=0;for(int slot=rounded-1,count=0;slot>0&&count<amount;slot-=2){
            unsigned int bit=1U<<((slot-1)/2);
            if(!(slot<=soulHearts&&(slot&1)&&((unsigned int)blackHeartMask&bit))){blackHeartMask|=1U<<(slot/2);count+=2;}
        }
    }else for(int slot=(rounded+adjust)/2;slot<(rounded+odd+amount)/2;++slot)blackHeartMask|=1U<<slot;
    if(!AddSoulHearts(amount))return false;
    TrimStoredHearts();return ReconcileBoneHearts();
}
bool NativePlayerState::AddMaxHearts(int amount){
    //70c526..70c5f4, health class0. Negative gains have their own removal
    //service and are not part of76c1d0's reached zero-amount call.
    if(amount<0)return false;
    int capacity;if(!HeartLimit(false,&capacity))return false;
    if(maxRedHearts+amount>capacity)return false;
    maxRedHearts=std::max(0,std::min(capacity,maxRedHearts+amount));
    soulHearts=std::min(soulHearts,std::max(0,capacity-maxRedHearts-2*boneHearts));
    redHearts=std::min(redHearts,maxRedHearts+2*boneHearts);
    if(maxRedHearts>=14)return false;
    TrimBlackHearts();if(!ReconcileHealthItems())return false;
    //779df0: trim rotten count, retain at least its two half-hearts.
    rottenHearts=std::min(rottenHearts,(redHearts+1)/2);redHearts=std::max(redHearts,2*rottenHearts);
    TrimStoredHearts();return ReconcileBoneHearts();
}
bool NativePlayerState::AddBrokenHearts(int amount){
    int capacity;if(!HeartLimit(true,&capacity))return false;
    brokenHearts=Clamp(brokenHearts+amount,0,capacity/2);
    if(!AddMaxHearts(0))return false;
    bool birthright=false;if(!HasCollectible(694,&birthright)||birthright)return false;
    return true;
}
bool NativePlayerState::InitializeStartingResources(const NativeRunStart& start,NativePlayerResourceUi& ui){
    const NativePlayerDef* p=NativePlayer(start.player);
    if(!ready||resourcesReady||!NativeRunStartValid(start)||!p||maxRedHearts||redHearts||soulHearts||keys||bombs||coins||
        p->hp>=14)return false;
    //76c33c: physical health precedes even the zero metadata helper calls.
    maxRedHearts=redHearts=p->hp;soulHearts=p->armor;
    if(!AddBlackHearts(p->black)||!AddBrokenHearts(0))return false;
    //76c524 has this first player in its manager before virtual Init. The
    //ordinary broadcast copies to that same owner; no registration is skipped.
    bombs=Clamp(bombs+p->bombs,0,99);bombReserve=std::min(bombReserve,bombs);ui.Changed();
    keys=Clamp(keys+p->keys,0,99);ui.Changed();
    bool deepPockets=false;if(!HasCollectible(416,&deepPockets))return false;
    coins=Clamp(coins+p->coins,0,deepPockets?999:99);
    bool greed=false;if(!HasCollectible(109,&greed)||greed||!ReconcileHealthItems())return false;
    //Starting collectibles/cards/pills/pocket items require their native grant
    //and stat-effect branches; numeric character resources start here.
    ui.Changed();resourcesReady=true;return true;
}
