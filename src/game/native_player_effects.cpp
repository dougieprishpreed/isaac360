#include "native_player_effects.h"
#include "native_player_effect_data.h"
#include <string.h>
#include <algorithm>
namespace {
int Signed(unsigned int n){int result;memcpy(&result,&n,4);return result;}
int Add32(int a,int b){return Signed((unsigned int)a+(unsigned int)b);}
bool Match(const NativeEffectDef& d,int type,int id){return d.id==id&&(d.type==type||(type==1&&(d.type==3||d.type==4)));}
void Fold(unsigned int& h,unsigned int n){for(int i=0;i<4;++i)h=(h^((n>>(i*8))&255))*16777619U;}
}
const NativeEffectDef* NativePreparedEffect(int type,int id){
    for(unsigned int i=0;i<sizeof(kNativeEffectDefs)/sizeof(kNativeEffectDefs[0]);++i)
        if(kNativeEffectDefs[i].type==type&&kNativeEffectDefs[i].id==id)return kNativeEffectDefs+i;
    return 0;
}
NativePlayerEffects::NativePlayerEffects():flags(0),changed(false),disabled(false),playerOwner(true),inCallback(false){}
bool NativePlayerEffects::Has(int type,int id) const {
    if(!disabled)for(unsigned int i=0;i<entries.size();++i)if(Match(*entries[i].definition,type,id))return true;
    return false;
}
int NativePlayerEffects::Count(int type,int id) const {
    if(!disabled)for(unsigned int i=0;i<entries.size();++i)if(Match(*entries[i].definition,type,id))return entries[i].count;
    return 0;
}
int NativePlayerEffects::Timer(int type,int id) const {
    if(!disabled)for(unsigned int i=0;i<entries.size();++i)if(Match(*entries[i].definition,type,id))return entries[i].timer;
    return 0;
}
bool NativePlayerEffects::SetTimer(int type,int id,int timer){
    if(inCallback||disabled)return false;
    for(unsigned int i=0;i<entries.size();++i)if(Match(*entries[i].definition,type,id)){entries[i].timer=timer;return true;}
    return false;
}
NativeEffectResult NativePlayerEffects::Add(const NativeEffectDef* d,int count,bool costume,NativeEffectCallbacks* callbacks){
    if(inCallback)return NEF_CALLBACK_REENTRY;
    if(disabled||!count)return NEF_OK;
    if(!d)return NEF_BAD_REQUEST;
    for(unsigned int i=0;i<entries.size();++i)if(entries[i].definition==d){
        entries[i].count=Add32(entries[i].count,count);entries[i].timer=Add32(entries[i].timer,d->duration);
        flags|=d->cacheFlags;changed=true;return NEF_OK;
    }
    if(playerOwner&&costume&&!callbacks)return NEF_NEED_CALLBACKS;
    NativeEffectEntry e={d,count,d->duration};entries.push_back(e);
    if(playerOwner&&costume){inCallback=true;callbacks->AddCostume(*this,*d);inCallback=false;}
    flags|=d->cacheFlags;changed=true;return NEF_OK;
}
NativeEffectResult NativePlayerEffects::Remove(const NativeEffectDef* d,unsigned int count,NativeEffectCallbacks* callbacks){
    if(inCallback)return NEF_CALLBACK_REENTRY;
    if(disabled)return NEF_OK;
    if(!d)return NEF_BAD_REQUEST;
    for(unsigned int i=0;i<entries.size();++i)if(entries[i].definition==d){
        if(!callbacks)return NEF_NEED_CALLBACKS;
        unsigned int removed=std::min(count,(unsigned int)entries[i].count);
        entries[i].count=Signed((unsigned int)entries[i].count-removed);
        if(entries[i].count<=0)entries.erase(entries.begin()+i);
        inCallback=true;callbacks->Remove(*this,*d,playerOwner?removed:0,playerOwner);inCallback=false;
        flags|=d->cacheFlags;changed=true;break;
    }
    return NEF_OK;
}
NativeEffectResult NativePlayerEffects::Clear(bool all,NativeEffectCallbacks* callbacks){
    if(inCallback)return NEF_CALLBACK_REENTRY;
    if(disabled)return NEF_OK;
    if(!callbacks)for(unsigned int i=0;i<entries.size();++i)if(all||!entries[i].definition->persistent)return NEF_NEED_CALLBACKS;
    for(unsigned int i=0;i<entries.size();){
        NativeEffectEntry e=entries[i];if(!all&&e.definition->persistent){++i;continue;}
        flags|=e.definition->cacheFlags;entries.erase(entries.begin()+i);
        inCallback=true;callbacks->Remove(*this,*e.definition,playerOwner?(unsigned int)e.count:0,playerOwner);inCallback=false;changed=true;
    }
    return NEF_OK;
}
NativeEffectResult NativePlayerEffects::Update(NativeEffectCallbacks* callbacks){
    if(inCallback)return NEF_CALLBACK_REENTRY;
    if(disabled)return NEF_OK;
    if(!callbacks)for(unsigned int i=0;i<entries.size();++i)if(entries[i].timer==1)return NEF_NEED_CALLBACKS;
    flags=0;
    for(unsigned int i=0;i<entries.size();){
        if(entries[i].timer<=0||--entries[i].timer){++i;continue;}
        NativeEffectEntry e=entries[i];flags|=e.definition->cacheFlags;entries.erase(entries.begin()+i);
        inCallback=true;callbacks->Remove(*this,*e.definition,playerOwner?(unsigned int)e.count:0,playerOwner);inCallback=false;changed=true;
    }
    return NEF_OK;
}
void NativePlayerEffects::Swap(NativePlayerEffects& b){
    std::swap(flags,b.flags);std::swap(changed,b.changed);std::swap(disabled,b.disabled);std::swap(playerOwner,b.playerOwner);entries.swap(b.entries);
}
unsigned int NativePlayerEffects::StateHash() const {
    unsigned int h=2166136261U;Fold(h,flags);Fold(h,changed);Fold(h,disabled);Fold(h,playerOwner);Fold(h,(unsigned int)entries.size());
    for(unsigned int i=0;i<entries.size();++i){const NativeEffectEntry& e=entries[i];Fold(h,e.definition->type);Fold(h,e.definition->id);Fold(h,e.count);Fold(h,e.timer);}
    return h;
}
