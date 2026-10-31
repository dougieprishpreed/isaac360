#include "native_player_creation.h"
#include "native_player_creation_data.h"
#include "native_player_appearance_data.h"
#include "native_player_manager.h"
#include <algorithm>
#include <string.h>
namespace {
void Fold(unsigned int& h,unsigned int n){for(int i=0;i<4;++i)h=(h^((n>>(8*i))&255))*16777619U;}
void ScalarHash(unsigned int& h,float n){unsigned int b;memcpy(&b,&n,4);Fold(h,b);}
}
NativePlayerDefaultWeapon::NativePlayerDefaultWeapon():ready(false),playerToken(0),native14(0),native28(0),native30(0),native3c(0),native40(0),native44(0),native8(0),nativeC(10),native10(0){
    native18.x=native18.y=native20.x=native20.y=native34.x=native34.y=0;
}
bool NativePlayerDefaultWeapon::BeginFresh(unsigned int owner){
    if(ready)return false;
    //Fresh Reset948c10 writes3c/40/44, then92cfbb/c1 binds owner and mode1.
    native3c=native40=native44=0;playerToken=owner;native28=1;ready=true;return true;
}
unsigned int NativePlayerDefaultWeapon::StateHash() const{
    unsigned int h=2166136261U;Fold(h,ready);if(!ready)return h;
    Fold(h,playerToken);Fold(h,native14);Fold(h,native28);Fold(h,native30);Fold(h,native3c);Fold(h,native40);Fold(h,native44);
    ScalarHash(h,native8);ScalarHash(h,nativeC);ScalarHash(h,native10);ScalarHash(h,native18.x);ScalarHash(h,native18.y);
    ScalarHash(h,native20.x);ScalarHash(h,native20.y);ScalarHash(h,native34.x);ScalarHash(h,native34.y);return h;
}
NativePlayerPreview::NativePlayerPreview():selected(-1),native120(0){
    //7e0713..3f: rgba1/1/1/.5, offsets.5/.5/.5, tail1/0/0/0.
    for(int i=0;i<4;++i)nativeColorTail[i]=i==0?1.0f:0;
}
NativeSpriteResult NativePlayerPreview::Construct(const char* property){
    NativePlayerPreview next;NativeSpriteResult r=next.sprite.Load(property);
    if(r!=NSPR_OK)return r;
    for(int i=0;i<7;++i)next.sprite.color[i]=i<3?1.0f:0.5f;
    for(int i=0;i<4;++i)next.sprite.color[i+7]=next.nativeColorTail[i];
    Swap(next);return NSPR_OK;
}
bool NativePlayerPreview::Empty() const{return sequence.empty()&&selected==-1;}
void NativePlayerPreview::Swap(NativePlayerPreview& b){
    sprite.Swap(b.sprite);sequence.swap(b.sequence);std::swap(selected,b.selected);std::swap(native120,b.native120);
    for(int i=0;i<4;++i)std::swap(nativeColorTail[i],b.nativeColorTail[i]);
}
unsigned int NativePlayerPreview::StateHash() const{
    unsigned int h=2166136261U;Fold(h,sprite.StateHash());Fold(h,selected);Fold(h,native120);
    for(int i=0;i<4;++i)ScalarHash(h,nativeColorTail[i]);Fold(h,(unsigned int)sequence.size());
    for(unsigned int i=0;i<sequence.size();++i)Fold(h,sequence[i]);return h;
}
NativePlayerCreationCache::NativePlayerCreationCache(bool snapshot){
    //Explicit 7087dc..708e79 stores only. Container descriptors are logical
    //empty descriptors, never copied host addresses or persisted native ABI.
    const NativeCreationWrite* writes=snapshot?kNativeCreationCacheWrites:kNativePlayerConstructorWrites;
    unsigned int count=snapshot?sizeof(kNativeCreationCacheWrites)/sizeof(kNativeCreationCacheWrites[0]):sizeof(kNativePlayerConstructorWrites)/sizeof(kNativePlayerConstructorWrites[0]);
    for(unsigned int i=0;i<count;++i){
        const NativeCreationWrite& w=writes[i];
        for(unsigned int byte=0;byte<w.bytes;++byte)initializedBytes[w.offset+byte]=(unsigned char)(w.value>>(8*byte));
    }
}
bool NativePlayerCreationCache::Scalar(unsigned int offset,unsigned int* value) const{
    if(!value)return false;unsigned int n=0;
    for(unsigned int byte=0;byte<4;++byte){std::map<unsigned int,unsigned char>::const_iterator p=initializedBytes.find(offset+byte);
        if(p==initializedBytes.end())return false;n|=(unsigned int)p->second<<(8*byte);}
    *value=n;return true;
}
bool NativePlayerCreationCache::Byte(unsigned int offset,unsigned char* value) const{
    if(!value)return false;std::map<unsigned int,unsigned char>::const_iterator p=initializedBytes.find(offset);
    if(p==initializedBytes.end())return false;*value=p->second;return true;
}
void NativePlayerCreationCache::AssignScalar(unsigned int offset,unsigned int value){
    AssignBytes(offset,value,4);
}
void NativePlayerCreationCache::AssignBytes(unsigned int offset,unsigned int value,unsigned int count){
    for(unsigned int byte=0;byte<count&&byte<4;++byte)initializedBytes[offset+byte]=(unsigned char)(value>>(8*byte));
}
void NativePlayerCreationCache::ApplyFreshInit(unsigned int begin,unsigned int end){
    for(unsigned int i=0;i<sizeof(kNativePlayerInitFixedWrites)/sizeof(kNativePlayerInitFixedWrites[0]);++i){
        const NativeCreationWrite& w=kNativePlayerInitFixedWrites[i];if(w.address<begin||w.address>=end)continue;
        for(unsigned int byte=0;byte<w.bytes;++byte)initializedBytes[w.offset+byte]=(unsigned char)(w.value>>(8*byte));
    }
}
void NativePlayerCreationCache::Swap(NativePlayerCreationCache& b){initializedBytes.swap(b.initializedBytes);}
unsigned int NativePlayerCreationCache::StateHash() const{
    unsigned int h=2166136261U;for(std::map<unsigned int,unsigned char>::const_iterator p=initializedBytes.begin();p!=initializedBytes.end();++p){Fold(h,p->first);Fold(h,p->second);}return h;
}
NativePlayerCreation::NativePlayerCreation():constructed(false),costumesReset(false),prefixPrepared(false),appearancePrepared(false),resetTailPrepared(false),postGenerationPrepared(false),initialSaveNormalized(false),native1540(-1),native1544(-1),native153c(-1),controllerAssigned(false),native17a4(-1),native17a8(-1),constructorFields(false),costumeCapacity(0),native1fac(0),native1fb0Owned(false){
    //Slots are not defined until711d20. Keep their storage out of the hash
    //and out of consumers until that service has actually run.
}
bool NativePlayerCreation::FinishPostGeneration(const NativeRunStart& start,NativePlayerStartup& startup,NativeEntityBody& body,NativePlayerSprites& sprites,NativePlayerState& state,NativePlayerManager& manager,unsigned int* next){
    if(next)*next=0x76c990;
    const NativePlayerDef* p=NativePlayer(start.player);
    if(!prefixPrepared||postGenerationPrepared||!NativeRunStartValid(start)||!p||!manager.initialized||!state.resourcesReady)return false;
    //76c9d6's early alternate branch is an actual owned effect predicate.
    if(state.effects.Has(1,667)){if(next)*next=0x76c9e5;return false;}
    //Character-specific cards, pills, pocket actives and starting items need
    //their native grant/effect branches; numeric resources are already owned.
    //The fresh no-item stat evaluation remains the maintained start path.
    //76cc8e appends105 only for the genuine award29 obligation established
    //by the profile owner. Keep that grant pending until its full713120 path.
    if(start.isaacD6){
        if(state.pendingCollectibles.empty())state.pendingCollectibles.push_back(105);
        if(next)*next=0x713120;return false;
    }
    if(!state.stats.EvaluateFresh(state,*this,manager,startup,body,sprites,next))return false;
    //76e42c ->70d380(-1) broadcasts every shared resource to the sole first
    //member. Registration set168=true, so70d090 reaches the real109
    //predicate and77f340; the latter returns at77f365 because243378 is
    //still true.783310/7835a0 reject player0 by their18/36 type checks.
    bool moneyPower;unsigned int sharedA,sharedB;
    if(manager.members.size()!=1||!body.native168||!body.native16a||!state.runInitializing||
       !state.HasCollectible(109,&moneyPower)||moneyPower||state.coins>=55||state.bombs<state.bombReserve||
       !constructorFields.Scalar(0x1cb8,&sharedA)||sharedA||!constructorFields.Scalar(0x1cbc,&sharedB)||sharedB)return false;
    //Type0 excludes twin/form creation, and normal challenge0 excludes31/43.
    //76e8de marks this post-generation initialization, after all prior calls.
    state.native1734=true;postGenerationPrepared=true;if(next)*next=0;return true;
}
NativeSpriteResult NativePlayerCreation::Construct(const char* property){
    NativePlayerCreation next;
    for(int i=0;i<2;++i){NativeSpriteResult r=next.previews[i].Construct(property);if(r!=NSPR_OK)return r;}
    next.constructed=true;Swap(next);return NSPR_OK;
}
bool NativePlayerCreation::PreparePrefix(const NativeRunStart& start,unsigned int seed,NativePlayerStartup& startup,NativeEntityBody& body,NativePlayerSprites& sprites,NativePlayerState& state,NativePlayerManager& manager,int controller){
    const NativeEntityBodyProperties* p=NativePreparedPlayerBody(0);
    if(constructed||prefixPrepared||!NativeRunStartValid(start)||!p||!seed||controller<0)return false;
    if(!manager.BeginFresh(seed)||Construct(p->animationPath)!=NSPR_OK||!AssignFirstPlayer()||!manager.RegisterFirst())return false;
    //904a27 virtual Init709330. First-player manager identity precedes the
    //indexed banks, the independent fifth draw, and base physical Init.
    if(!startup.BeginFresh(seed)||body.BeginFresh(1,0,start.player,startup.entityInitSeed,0,1)!=NEB_OK)return false;
    //7099ad Clear(true), reset health/body, then the present default weapon
    //factory. Its Init neither queries items nor advances any RNG owner.
    if(!state.ResetFreshNumbers(start,startup,body))return false;
    constructorFields.ApplyFreshInit(0x7099a2,0x709afc);
    if(!weapon.BeginFresh((unsigned int)native1540))return false;
    constructorFields.ApplyFreshInit(0x709b16,0x709e04);
    //709e04: consume trinket97 once.77c5d0 uses real normal player metadata
    //and the original default challenge0 can-shoot flag, before inventory reset.
    if(!state.FinishFreshInventory(startup))return false;
    constructorFields.ApplyFreshInit(0x709e69,0x70a344);
    if(sprites.PrepareAuxiliary()!=NSPR_OK)return false;
    native153c=controller;controllerAssigned=true; //70a845: actual primary input owner, before body load.
    if(sprites.PrepareBody(p->animationPath)!=NSPR_OK||!ResetFreshCostumes())return false;
    if(!PrepareFreshAppearance(body,sprites,state)||!FinishFreshReset(startup,state))return false;
    if(!manager.InitializeResources(start,state)||!manager.PostPlayerInit(state))return false;
    prefixPrepared=true;return true;
}
bool NativePlayerCreation::FinishFreshReset(const NativePlayerStartup& startup,NativePlayerState& state){
    if(!appearancePrepared||resetTailPrepared||!startup.ready||native1fb0Owned)return false;
    constructorFields.ApplyFreshInit(0x70a9cd,0x70aab1);
    //Native variant0 jumps directly to70aed1. The variant1 RNG/effect
    //factories at70aabb..70aeca are not reached by the constructed body.
    constructorFields.ApplyFreshInit(0x70aed1,0x70af0f);
    state.native166c.clear(); //70af0f.
    state.native1678.clear(); //70af1b.
    constructorFields.ApplyFreshInit(0x70af1c,0x70af89);
    //78cdf0(0) replaces the constructor's null reference with an owned null
    //token wrapper. There is no previous live reference/deletion callback.
    native1fac=0;native1fb0Owned=true;
    constructorFields.ApplyFreshInit(0x70af8e,0x70b154);
    native1e04.assign(startup.banks[NPB_CARD].size(),false); //ItemConfig+24.
    native1e14.assign(15,false);
    constructorFields.ApplyFreshInit(0x70b1db,0x70b2d0);
    //Empty constructor-owned1ebc tree and1c8c sequence need no removal
    //callbacks. The two indexed arrays follow their original config extents.
    native1ec4.assign(startup.banks[NPB_COLLECTIBLE].size(),0);
    native1ed0.assign(startup.banks[NPB_TRINKET].size(),0);
    constructorFields.ApplyFreshInit(0x70b380,0x70b47b);
    resetTailPrepared=true;return true;
}
bool NativePlayerCreation::PrepareFreshAppearance(const NativeEntityBody& body,NativePlayerSprites& sprites,const NativePlayerState& state){
    unsigned char ghost=1;
    if(!constructed||!costumesReset||appearancePrepared||!body.ready||body.variant!=0||!state.ready||
        !constructorFields.Byte(0x1f80,&ghost)||ghost||!costumeRecords.empty()||!sprites.bodyReady)return false;
    NativeSprite& sprite=sprites.sprites[0];
    //76eafc: reset selectors, then ordinary challenge0 skips challenge30's
    //replacement.76ec20 writes all15 layer paths, not a shared sheet object.
    native17a4=native17a8=-1;
    for(int i=0;i<15&&(unsigned int)i<sprite.layerSheets.size();++i)
        if(sprite.ReplaceLayerSheet(i,kNativeAppearanceSheets[0].path)!=NSPR_OK)return false;
    //76ec7a/447ae0/70ef90: exact name, original asset path, no costume/color
    //suffix on this player. Restore ghost after replacing the other layers.
    int ghostLayer=sprite.LayerOrdinal("ghost");
    if(ghostLayer>=0){const NativeSpriteSheet* original=sprite.OriginalLayerSheet(ghostLayer);
        if(!original||sprite.ReplaceLayerSheet(ghostLayer,original->path)!=NSPR_OK)return false;}
    if(sprite.RefreshSheets()!=NSPR_OK)return false;
    //76edae ->70f9f0: no live costume records, no type12/24 head mask,
    //skinColor=-1 already equals both selectors. Empty slots have ALL three
    //indices reset to-1 here, unlike711d20's third index0.
    for(int i=0;i<15;++i){NativePlayerCostumeSlot& s=costumeSlots[i];s.native0=s.native4=s.native8=-1;s.nativeC=(i>=1&&i<=3)||i==11;}
    //Normal seed effects54 and challenge19/42 are absent; can-shoot permits
    //the native blindfold branch to fall through. No appearance RNG draws.
    playerName=kNativeFreshPlayerName;appearancePrepared=true;return true;
}
bool NativePlayerCreation::AssignFirstPlayer(){
    if(!constructed||native1540!=-1||native1544!=-1)return false;
    native1540=native1544=0;return true;
}
bool NativePlayerCreation::ResetFreshCostumes(){
    if(!constructed||native1540!=0||native1544!=0||costumesReset)return false;
    //Fresh707b40 has no costume records. Reserve is capacity, not32 live
    //costumes; no removal callback is suppressed by clearing an empty vector.
    costumeRecords.reserve(32);costumeCapacity=32;
    for(int i=0;i<15;++i){NativePlayerCostumeSlot& s=costumeSlots[i];s.native0=s.native4=-1;s.native8=0;s.nativeC=(i>=1&&i<=3)||i==11;}
    costumesReset=true;return true;
}
void NativePlayerCreation::Swap(NativePlayerCreation& b){
    std::swap(constructed,b.constructed);std::swap(prefixPrepared,b.prefixPrepared);std::swap(native1540,b.native1540);std::swap(native1544,b.native1544);
    std::swap(native153c,b.native153c);std::swap(controllerAssigned,b.controllerAssigned);
    std::swap(appearancePrepared,b.appearancePrepared);std::swap(native17a4,b.native17a4);std::swap(native17a8,b.native17a8);playerName.swap(b.playerName);
    std::swap(resetTailPrepared,b.resetTailPrepared);std::swap(native1fac,b.native1fac);std::swap(native1fb0Owned,b.native1fb0Owned);
    std::swap(postGenerationPrepared,b.postGenerationPrepared);
    std::swap(initialSaveNormalized,b.initialSaveNormalized);
    native1e04.swap(b.native1e04);native1e14.swap(b.native1e14);native1ec4.swap(b.native1ec4);native1ed0.swap(b.native1ed0);
    costumeRecords.swap(b.costumeRecords);
    std::swap(weapon,b.weapon);
    for(int i=0;i<2;++i)previews[i].Swap(b.previews[i]);cache.Swap(b.cache);constructorFields.Swap(b.constructorFields);
    //Never read undefined slot storage during a successful or failed start.
    for(int i=0;i<15;++i){
        if(costumesReset&&b.costumesReset)std::swap(costumeSlots[i],b.costumeSlots[i]);
        else if(b.costumesReset)costumeSlots[i]=b.costumeSlots[i];
        else if(costumesReset)b.costumeSlots[i]=costumeSlots[i];
    }
    std::swap(costumesReset,b.costumesReset);std::swap(costumeCapacity,b.costumeCapacity);
}
unsigned int NativePlayerCreation::StateHash() const{
    unsigned int h=2166136261U;Fold(h,constructed);Fold(h,prefixPrepared);Fold(h,appearancePrepared);Fold(h,native1540);Fold(h,native1544);Fold(h,costumesReset);Fold(h,costumeCapacity);
    Fold(h,controllerAssigned);if(controllerAssigned)Fold(h,native153c);
    if(appearancePrepared){Fold(h,native17a4);Fold(h,native17a8);for(unsigned int i=0;i<playerName.size();++i)Fold(h,(unsigned char)playerName[i]);}
    Fold(h,resetTailPrepared);Fold(h,native1fac);Fold(h,native1fb0Owned);
    Fold(h,postGenerationPrepared);
    Fold(h,initialSaveNormalized);
    Fold(h,(unsigned int)native1e04.size());for(unsigned int i=0;i<native1e04.size();++i)Fold(h,native1e04[i]);
    Fold(h,(unsigned int)native1e14.size());for(unsigned int i=0;i<native1e14.size();++i)Fold(h,native1e14[i]);
    Fold(h,(unsigned int)native1ec4.size());for(unsigned int i=0;i<native1ec4.size();++i)Fold(h,native1ec4[i]);
    Fold(h,(unsigned int)native1ed0.size());for(unsigned int i=0;i<native1ed0.size();++i)Fold(h,native1ed0[i]);
    for(int i=0;i<2;++i)Fold(h,previews[i].StateHash());Fold(h,cache.StateHash());Fold(h,constructorFields.StateHash());Fold(h,weapon.StateHash());
    if(costumesReset)for(int i=0;i<15;++i){const NativePlayerCostumeSlot& s=costumeSlots[i];Fold(h,s.native0);Fold(h,s.native4);Fold(h,s.native8);Fold(h,s.nativeC);}return h;
}
