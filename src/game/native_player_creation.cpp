#include "native_player_creation.h"
#include "native_player_creation_data.h"
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
NativePlayerCreation::NativePlayerCreation():constructed(false),costumesReset(false),prefixPrepared(false),native1540(-1),native1544(-1),constructorFields(false),costumeCapacity(0){
    //Slots are not defined until711d20. Keep their storage out of the hash
    //and out of consumers until that service has actually run.
}
NativeSpriteResult NativePlayerCreation::Construct(const char* property){
    NativePlayerCreation next;
    for(int i=0;i<2;++i){NativeSpriteResult r=next.previews[i].Construct(property);if(r!=NSPR_OK)return r;}
    next.constructed=true;Swap(next);return NSPR_OK;
}
bool NativePlayerCreation::PreparePrefix(const NativeRunStart& start,unsigned int seed,NativePlayerStartup& startup,NativeEntityBody& body,NativePlayerSprites& sprites,NativePlayerState& state){
    const NativeEntityBodyProperties* p=NativePreparedPlayerBody(0);
    if(constructed||prefixPrepared||!NativeRunStartValid(start)||!p||!seed)return false;
    if(Construct(p->animationPath)!=NSPR_OK||!AssignFirstPlayer())return false;
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
    if(sprites.BeginFresh(p->animationPath)!=NSPR_OK||!ResetFreshCostumes())return false;
    prefixPrepared=true;return true; //70a994 ->76ea50 is still required.
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
    unsigned int h=2166136261U;Fold(h,constructed);Fold(h,prefixPrepared);Fold(h,native1540);Fold(h,native1544);Fold(h,costumesReset);Fold(h,costumeCapacity);
    for(int i=0;i<2;++i)Fold(h,previews[i].StateHash());Fold(h,cache.StateHash());Fold(h,constructorFields.StateHash());Fold(h,weapon.StateHash());
    if(costumesReset)for(int i=0;i<15;++i){const NativePlayerCostumeSlot& s=costumeSlots[i];Fold(h,s.native0);Fold(h,s.native4);Fold(h,s.native8);Fold(h,s.nativeC);}return h;
}
