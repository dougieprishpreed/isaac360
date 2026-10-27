#include "native_rooms.h"
#include "native_rng.h"
#include "normal_room_data.h"
#include <string.h>
typedef char NormalRoomCapacityCheck[(sizeof(kNormalRoomDefs)/sizeof(kNormalRoomDefs[0])<=kNativeRoomCapacity)?1:-1];
int NativeRoomCount(int catalog){return catalog==NC_NORMAL?sizeof(kNormalRoomDefs)/sizeof(kNormalRoomDefs[0]):0;}
int NativeRoomId(int catalog,int ordinal){return ordinal>=0&&ordinal<NativeRoomCount(catalog)?kNormalDefinitionBase+kNormalRoomDefs[ordinal].stage*65536+kNormalRoomDefs[ordinal].ordinal:-1;}
int NativeRoomCatalog(int id){return id>=kNormalDefinitionBase&&id<kNormalDefinitionBase+0x1000000?NC_NORMAL:-1;}
int NativeRoomPoolIndex(int id){if(NativeRoomCatalog(id)!=NC_NORMAL)return -1;int value=id-kNormalDefinitionBase,stage=value/65536,ordinal=value&65535;const NormalStageRange& r=kNormalStageRanges[stage];return r.source>=0&&ordinal<r.count?r.first+ordinal:-1;}
const NativeRoomDef* NativeRoom(int id){int i=NativeRoomPoolIndex(id);return i<0?0:&kNormalRoomDefs[i];}
const NativeRoomSourceDef* NativeRoomSourceBinding(int source){int n=source-kNormalSourceBase;return n>=0&&n<(int)(sizeof(kNormalRoomSources)/sizeof(kNormalRoomSources[0]))?&kNormalRoomSources[n]:0;}
const char* NativeRoomSource(int source){const NativeRoomSourceDef* s=NativeRoomSourceBinding(source);return s?s->resource:0;}
unsigned int NativeCatalogFingerprint(int catalog){return catalog==NC_NORMAL?kNormalCatalogFingerprint:0;}
const NativeRoomDef* NativeFindRoom(const char* resource,int ordinal,int catalog){if(!resource||ordinal<0)return 0;for(int i=0;i<NativeRoomCount(catalog);++i){const NativeRoomDef* r=NativeRoom(NativeRoomId(catalog,i));if(r->ordinal==ordinal&&!strcmp(NativeRoomSource(r->source),resource))return r;}return 0;}
int NativeStageSource(int stage,int catalog){return catalog==NC_NORMAL&&stage>=0&&stage<256?kNormalStageRanges[stage].source:-1;}
NativeRoomResult NativeFindRoomVariant(int stage,int type,unsigned int variant,int* selected,int catalog) {
    if(!selected || NativeStageSource(stage,catalog)<0) return NR_BAD_REQUEST;
    for(int i=0;i<NativeRoomCount(catalog);++i) {int id=NativeRoomId(catalog,i);const NativeRoomDef* r=NativeRoom(id);if(r->stage==stage&&r->type==type&&r->variant==variant) {
        *selected=id; return NR_OK;
    }
    }
    return NR_NO_MATCH;
}
bool NativeRoomConfigStage(int stage,int type,int selectionMode,int nativeMode,int* output) {
    if(!output || stage<1 || stage>13 || type<0 || type>5 || selectionMode< -1 || selectionMode>1) return false;
    bool alternate=selectionMode==1 || (selectionMode==-1 && (nativeMode==2 || nativeMode==3));
    int result;
    if(alternate) {
        if(stage==7) result=25;
        else if(stage==6) result=24;
        else if(stage==5) result=14;
        else if(type==4) result=stage*2+25;
        else if(type==5) result=stage*2+26;
        else result=type+stage*3-2;
    } else if(stage==13) result=35;
    else if(stage>8) {
        if(stage==9) result=type==4?36:13;
        else if(stage==12) result=26;
        else result=type+(stage-3)*2;
    } else if(type==4) result=((stage-1)&~1)+27;
    else if(type==5) result=((stage-1)>>1)*2+28;
    else result=type+1+((stage-1)>>1)*3;
    *output=result; return true;
}
namespace {
bool Match(const NativeRoomDef& room,const NativeRoomQuery& q,int source) {
    return room.source==source && room.stage==q.stage && room.type==q.type && (q.shape==13 || room.shape==q.shape) &&
        room.variant>=q.minVariant && room.variant<=q.maxVariant && room.difficulty>=q.minDifficulty &&
        room.difficulty<=q.maxDifficulty && (q.subtype==-1 || room.subtype==(unsigned int)q.subtype);
}
float Add(float a,float b) { volatile float result=a+b; return result; }
float Mul(float a,float b) { volatile float result=a*b; return result; }
}
NativeRoomResult NativeRoomHistogram(const NativeRoomQuery& q,int* count,int shapes[13],int catalog) {
    int source=NativeStageSource(q.stage,catalog);
    if(!count || !shapes || source<0 || q.minVariant>q.maxVariant || q.minDifficulty>q.maxDifficulty ||
       q.subtype< -1 || q.requiredDoors>255 || q.shape<1 || q.shape>13) return NR_BAD_REQUEST;
    int n=0,hist[13]={0};
    for(int i=0;i<NativeRoomCount(catalog);++i) {
        const NativeRoomDef& room=*NativeRoom(NativeRoomId(catalog,i)); if(!Match(room,q,source)) continue;
        if(q.requiredDoors && room.doors==0xffffffffU) return NR_UNKNOWN_DOORS;
        if((q.requiredDoors&room.doors)!=q.requiredDoors) continue;
        if(room.shape<0 || room.shape>12) return NR_BAD_REQUEST;
        ++n; ++hist[room.shape];
    }
    *count=n; memcpy(shapes,hist,sizeof(hist)); return NR_OK;
}
void NativeRoomPool::Reset(int value) {catalog=value;memset(weights,0,sizeof(weights));for(int i=0;i<NativeRoomCount(catalog);++i)weights[i]=NativeRoom(NativeRoomId(catalog,i))->weight;}
float NativeRoomPool::Weight(int definition) const {int i=NativeRoomPoolIndex(definition);return i>=0&&NativeRoomCatalog(definition)==catalog?weights[i]:-1;}
NativeRoomResult NativeRoomPool::SelectOrdinary(unsigned int seed,const NativeRoomQuery& q,bool deplete,int* selected,int* draws) {
    if(draws) *draws=0;
    int source=NativeStageSource(q.stage,catalog);
    if(!selected || !seed || source<0 || q.minVariant>q.maxVariant || q.minDifficulty>q.maxDifficulty || q.subtype< -1 || q.requiredDoors>255 || q.shape<1 || q.shape>13) return NR_BAD_REQUEST;
    float total=0,exact=0; int exactCount=0;
    for(int i=0;i<NativeRoomCount(catalog);++i) {
        const NativeRoomDef& room=*NativeRoom(NativeRoomId(catalog,i)); if(!Match(room,q,source)) continue;
        if(q.requiredDoors && room.doors==0xffffffffU) return NR_UNKNOWN_DOORS;
        if((q.requiredDoors&room.doors)!=q.requiredDoors) continue;
        if(!(weights[i]>=0 && weights[i]<=1000000)) return NR_BAD_REQUEST;
        total=Add(total,weights[i]);
        if(q.requiredDoors && room.doors==q.requiredDoors) { exact=Add(exact,weights[i]); ++exactCount; }
    }
    if(total<=0) return NR_NO_MATCH;
    NativeRng rng; rng.Set(seed,7); float point; rng.Float(&point); if(draws) ++*draws;
    bool prefer=false;
    if(exactCount) { float roll; rng.Float(&roll); if(draws) ++*draws; volatile float ratio=exact/total; prefer=roll<Mul(ratio,9.99f); }
    float limit=Mul(point,prefer?exact:total),sum=0;
    for(int i=0;i<NativeRoomCount(catalog);++i) {
        const NativeRoomDef& room=*NativeRoom(NativeRoomId(catalog,i));
        if(!Match(room,q,source) || (room.doors&q.requiredDoors)!=q.requiredDoors || (prefer && room.doors!=q.requiredDoors)) continue;
        sum=Add(sum,weights[i]);
        if(limit<sum) {
            if(deplete) { float weight=Mul(weights[i],0.1f); weights[i]=weight<0.0000001f?0.0000001f:weight; }
            *selected=NativeRoomId(catalog,i); return NR_OK;
        }
    }
    return NR_NO_MATCH;
}
