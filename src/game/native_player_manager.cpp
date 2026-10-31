#include "native_player_manager.h"
#include "native_sprite.h"
#include "native_player_resource_data.h"
#include <algorithm>

NativePlayerManager::NativePlayerManager():begun(false),initialized(false),worldRegistered(false),seed(0),entityToken(0),nextEntityToken(0),postInitCalls(0),controller(-1),selectedBabyCount(0),hudRefreshes(0),cacheCallbacks(0),roomInitCallbacks(0),levelInitCallbacks(0),roomCacheDirty(false){
    playerTearsSheet=0;
    for(int i=0;i<5;++i)selectedBabies[i]=-1;
    for(int i=0;i<4;++i)for(int j=0;j<2;++j){slotIdentity[i][j]=-1;slotTimer[i][j]=0;}
    for(int i=0;i<8;++i){hudPlayerTokens[i]=-1;hudDirty[i]=false;}
}
bool NativePlayerManager::PrepareInitialRoomEntry(const NativePlayerState& state,const NativeEntityBody& body,unsigned int* next){
    if(next)*next=0x905d80;
    if(!next||!initialized||members.size()!=1||!worldRegistered||entityToken!=body.entityToken||!body.native168||!state.resourcesReady)return false;
    entryPlayerOffsets.clear();NativePoint delta={0,0};entryPlayerOffsets.push_back(delta);
    //The actual first player is the primary reference.905e85's overlap
    //randomization and906151's secondary-position addition are never reached.
    //905f8d->758280 returns at759b12 before queries while1734 is false.
    if(state.native1734){*next=0x7582c3;return false;}
    if(!state.playerTearsEnabled){*next=0x90604b;return false;}
    //906041->90a5f0 loads the real preload.xml group "player tears".
    //Retain the resolved immutable PNG binding for both graphics adapters.
    playerTearsSheet=NativePreparedPlayerTearsSheet();
    if(!playerTearsSheet||!playerTearsSheet->path||!playerTearsSheet->width||!playerTearsSheet->height){*next=0x90a665;return false;}
    //No type3 familiar or type10..999 actor exists on the admitted starting
    //room path.9062af now calls the real checkpoint service; it is not an
    //entity-registration commit and is not skipped during New Run.
    for(unsigned int i=0;i<worldTypes.size();++i)if(worldTypes[i]!=1&&worldTypes[i]!=1000){*next=0x90622f;return false;}
    *next=0x9062b1;return true;
}
bool NativePlayerManager::RefreshFreshHud(const NativeEntityBody& body,int inputController){
    if(!initialized||members.size()!=1||!body.native16a||inputController<0)return false;
    //8f2c80 releases previous HUD references, then904f40 maps the genuine
    //primary input owner to the first free manager slot. No twin/parent exists.
    for(int i=0;i<8;++i)if(hudPlayerTokens[i]>=0){hudPlayerTokens[i]=-1;hudDirty[i]=true;}
    int slot=-1;
    for(int i=0;i<4;++i)if(slotIdentity[i][0]==inputController){slot=i;break;}
    if(slot<0)for(int i=0;i<4;++i)if(slotIdentity[i][0]<0){slot=i;slotIdentity[i][0]=inputController;break;}
    if(slot<0)return false;
    controller=inputController;hudPlayerTokens[slot]=(int)entityToken;hudDirty[slot]=true;
    ++hudRefreshes;return true;
}
bool NativePlayerManager::EmptyCacheCallback(unsigned int flag){
    if(!initialized||!postInitCallbacks.empty()||!flag)return false;
    //7f07e0's original callback registry was constructed empty with the other
    //native lists. This still records each ordered cache-category dispatch.
    ++cacheCallbacks;return true;
}
bool NativePlayerManager::EmptyRoomInitCallback(){
    if(!initialized||!postInitCallbacks.empty())return false;++roomInitCallbacks;return true;
}
bool NativePlayerManager::EmptyLevelInitCallback(){
    if(!initialized||!postInitCallbacks.empty())return false;++levelInitCallbacks;return true;
}
bool NativePlayerManager::ReconcileNoFamiliars(int variant,int count,const NativePlayerStartup& startup){
    if(!initialized||!startup.ready||variant<0||count!=0||worldMembers.size()!=worldTypes.size())return false;
    for(unsigned int i=0;i<worldTypes.size();++i)if(worldTypes[i]==3)return false;
    //716660:417810(type3,variant) visits the actual registered types. Both
    //removal and gain loops are empty when no familiar exists;
    //the supplied collectible/trinket RNG is consequently never advanced.
    return true;
}
bool NativePlayerManager::BeginFresh(unsigned int playerSeed){
    if(begun||!playerSeed)return false;seed=playerSeed;begun=true;return true;
}
bool NativePlayerManager::RegisterFirst(){
    if(!begun||!members.empty())return false;members.push_back(0);return true;
}
bool NativePlayerManager::InitializeResources(const NativeRunStart& start,NativePlayerState& state){
    if(members.size()!=1||members[0]!=0||worldRegistered)return false;
    return state.InitializeStartingResources(start,resourceUi);
}
bool NativePlayerManager::PostPlayerInit(const NativePlayerState& state){
    if(!state.resourcesReady||members.size()!=1||!postInitCallbacks.empty())return false;
    //7f0c5b returns before a VM call when the original constructed list is
    //empty. No callback result or pre-generation76c990 call is manufactured.
    ++postInitCalls;return true;
}
bool NativePlayerManager::RegisterWorld(NativeEntityBody& body){
    if(!postInitCalls||worldRegistered||!body.ready||body.type!=1||body.variant!=0)return false;
    //904a2a occurs after virtual Init returns, before registry insertion.
    body.position.x=320;body.position.y=400;
    if(!RegisterEntity(body))return false;entityToken=body.entityToken;worldRegistered=true;return true;
}
bool NativePlayerManager::RegisterEntity(NativeEntityBody& body){
    if(!body.ready||body.native168||worldMembers.size()!=worldTypes.size())return false;
    body.entityToken=nextEntityToken++;worldMembers.push_back(body.entityToken);worldTypes.push_back(body.type);
    if(!(body.flagsLow&8U))updateMembers.push_back(body.entityToken);
    //414bc0 inserts immediately into the native actor/type and render lists.
    //414c5c writes168;169/16a retain the base Init values.
    body.native168=true;return true;
}
bool NativePlayerManager::FinishSetup(const NativeProfile& profile){
    if(initialized||!worldRegistered||members.size()!=1)return false;
    std::vector<int> eligible;
    for(unsigned int i=0;i<sizeof(kNativeManagerBabies)/sizeof(kNativeManagerBabies[0]);++i){
        const NativeManagerBabyDef& b=kNativeManagerBabies[i];
        if(b.achievement<0||b.achievement==0||profile.Award(b.achievement))eligible.push_back(b.id);
    }
    NativeRng local;if(!local.Set(seed,27))return false;
    for(int i=(int)eligible.size()-1;i>0;--i){unsigned int value;if(!local.Next(&value))return false;std::swap(eligible[i],eligible[value%(i+1)]);}
    babyCandidates.swap(eligible);selectedBabyCount=std::min(5,(int)babyCandidates.size());
    for(int i=0;i<selectedBabyCount;++i)selectedBabies[i]=babyCandidates[i];
    characterCandidates.clear();characterCandidates.push_back(-1);
    for(unsigned int i=0;i<sizeof(kNativeManagerCharacterOrder)/sizeof(kNativeManagerCharacterOrder[0]);++i){
        int id=kNativeManagerCharacterOrder[i];const NativePlayerDef* p=NativePlayer(id);if(!p)return false;
        if(p->achievement>=638)continue;
        if(p->achievement>0&&!profile.Award(p->achievement))continue;
        if((id==9||id==30)&&!profile.edenTokens)continue;
        characterCandidates.push_back(id);
    }
    //908199 only visits IDs >=41. The current inherited players.xml has41
    //entries; no fallback characters or fabricated unlocks are appended.
    if(NativePlayer(41))return false;
    initialized=true;return true;
}
void NativePlayerManager::Swap(NativePlayerManager& b){
    std::swap(begun,b.begun);std::swap(initialized,b.initialized);std::swap(worldRegistered,b.worldRegistered);
    std::swap(seed,b.seed);std::swap(entityToken,b.entityToken);std::swap(nextEntityToken,b.nextEntityToken);std::swap(postInitCalls,b.postInitCalls);
    members.swap(b.members);worldMembers.swap(b.worldMembers);worldTypes.swap(b.worldTypes);updateMembers.swap(b.updateMembers);babyCandidates.swap(b.babyCandidates);characterCandidates.swap(b.characterCandidates);
    std::swap(controller,b.controller);for(int i=0;i<8;++i){std::swap(hudPlayerTokens[i],b.hudPlayerTokens[i]);std::swap(hudDirty[i],b.hudDirty[i]);}
    std::swap(selectedBabyCount,b.selectedBabyCount);for(int i=0;i<5;++i)std::swap(selectedBabies[i],b.selectedBabies[i]);
    std::swap(hudRefreshes,b.hudRefreshes);std::swap(cacheCallbacks,b.cacheCallbacks);std::swap(roomCacheDirty,b.roomCacheDirty);
    std::swap(roomInitCallbacks,b.roomInitCallbacks);
    std::swap(levelInitCallbacks,b.levelInitCallbacks);
    entryPlayerOffsets.swap(b.entryPlayerOffsets);std::swap(playerTearsSheet,b.playerTearsSheet);
    for(int i=0;i<4;++i)for(int j=0;j<2;++j){std::swap(slotIdentity[i][j],b.slotIdentity[i][j]);std::swap(slotTimer[i][j],b.slotTimer[i][j]);}
    std::swap(resourceUi,b.resourceUi);
}
