#include "native_secret.h"
#include <string.h>
namespace {
void Add(unsigned char* blocked,int x,int y){NativeCell p={x,y};int at=NativeCellIndex(p);if(at>=0)blocked[at]=1;}
bool Valid(const NativeTopology& g,const NativeRoomPool& p,const NativeLevelLayout& l,unsigned int seed,const NativeSecretContext& c,NativeSecretReport* out) {
    return out&&seed&&g.count>0&&g.count<=169&&l.count>=0&&l.count<=507&&c.stage>=1&&c.stage<=13&&
        c.startGridIndex>=0&&c.startGridIndex<169&&c.placement.nativeMode>=0&&c.placement.nativeMode<=1&&
        c.placement.dimension>=0&&c.placement.dimension<=2&&NativeStageSource(c.placement.roomConfigStage,p.catalog)>=0;
}
NativeSecretReport Report(unsigned int seed){NativeSecretReport r;memset(&r,0,sizeof(r));r.entrySeed=r.localState=seed;
    for(int i=0;i<3;++i)r.entries[i].definition=r.entries[i].topologyIndex=r.entries[i].layoutIndex=-1;return r;}
NativeRoomResult Select(NativeRoomPool& p,int stage,int type,NativeSecretEntry& e) {
    NativeRoomQuery q={stage,type,13,0,0xffffffffU,1,10,0,-1};int draws=0;
    NativeRoomResult result=p.SelectOrdinary(e.seed,q,true,&e.definition,&draws);e.selectorDraws+=draws;++e.selectionCalls;
    if(result==NR_NO_MATCH){q.stage=0;e.fallback=true;result=p.SelectOrdinary(e.seed,q,true,&e.definition,&draws);e.selectorDraws+=draws;++e.selectionCalls;}
    return result;
}
}
bool NativeSecretBlockedCells(const NativeLevelLayout& layout,const NativeSecretContext& c,unsigned char* blocked) {
    if(!blocked||layout.count<0||layout.count>507||c.placement.dimension<0||c.placement.dimension>2||c.startGridIndex<0||c.startGridIndex>=169)return false;
    memset(blocked,0,169);
    if(c.stage==11){const int offsets[]={-1,1,-13,13};for(int k=0;k<4;++k){int at=c.startGridIndex+offsets[k];if(at>=0&&at<169)blocked[at]=1;}}
    for(int i=0;i<layout.count;++i) {
        const NativePlacedRoom& r=layout.rooms[i];if(r.gridIndex<0)continue;
        const NativeRoomDef* d=NativeRoom(r.definition);if(!d)return false;
        int x=r.gridIndex%13,y=r.gridIndex/13,w=d->width/13,h=d->height/7;
        int cells[8][2]={{x-1,y},{x,y-1},{x+w,y},{x,y+h},{x-1,y+1},{x+1,y-1},{x+w,y+1},{x+1,y+h}};
        for(int slot=0;slot<8;++slot) {
            if((d->shape==1&&slot>=4)||(d->shape==4&&(slot==5||slot==7))||(d->shape==6&&(slot==4||slot==6)))continue;
            if(!(d->doors&(1U<<slot))||d->type==5||d->type==8||d->type==7)Add(blocked,cells[slot][0],cells[slot][1]);
        }
    }
    return true;
}
NativeSecretResult NativePlaceSecretRooms(NativeTopology& graph,NativeRoomPool& pool,NativeLevelLayout& layout,
        unsigned int seed,const NativeSecretContext& c,NativeSecretReport* output) {
    if(!Valid(graph,pool,layout,seed,c,output))return NSR_BAD_REQUEST;
    NativeSecretReport r=Report(seed);if(c.configuredType7){*output=r;return NSR_OK;}
    r.requested=(c.held102?2:1)+(c.held589?1:0);NativeRng local;local.Set(seed,1);
    for(int i=0;i<r.requested;++i) {
        NativeSecretEntry& e=r.entries[i];e.seed=local.state;
        NativeRoomResult selected=Select(pool,c.placement.roomConfigStage,7,e);
        if(selected!=NR_OK){*output=r;return selected==NR_NO_MATCH?NSR_MISSING_ROOM:NSR_BAD_DATA;}
        unsigned char blocked[169];if(!NativeSecretBlockedCells(layout,c,blocked)){*output=r;return NSR_BAD_DATA;}
        e.topologyIndex=graph.PickSecretRoom(blocked);
        if(e.topologyIndex>=0) {
            if(layout.Place(graph.rooms[e.topologyIndex],e.definition,e.seed,c.placement,&e.layoutIndex)!=NP_OK){*output=r;return NSR_DESCRIPTOR_FAILED;}
            ++r.placed;
        }
        local.Next(&r.localState); //Even a failed geometric search advances it.
    }
    *output=r;return NSR_OK;
}
NativeSecretResult NativePlaceType29(NativeTopology& graph,NativeRoomPool& pool,NativeLevelLayout& layout,
        unsigned int seed,const NativeSecretContext& c,NativeSecretReport* output) {
    if(!Valid(graph,pool,layout,seed,c,output))return NSR_BAD_REQUEST;
    NativeSecretReport r=Report(seed);if(c.configuredType29){*output=r;return NSR_OK;}
    r.requested=1;NativeSecretEntry& e=r.entries[0];NativeRng local;local.Set(seed,71);local.Next(&e.seed);r.localState=local.state;
    NativeRoomResult selected=Select(pool,c.placement.roomConfigStage,29,e);
    if(selected==NR_NO_MATCH){*output=r;return NSR_OK;} //Nullable in006f2861.
    if(selected!=NR_OK){*output=r;return NSR_BAD_DATA;}
    e.topologyIndex=graph.PickDistantRoom();
    if(e.topologyIndex>=0) {
        if(layout.Place(graph.rooms[e.topologyIndex],e.definition,e.seed,c.placement,&e.layoutIndex)!=NP_OK){*output=r;return NSR_DESCRIPTOR_FAILED;}
        layout.rooms[e.layoutIndex].nativeB4=99;++r.placed;
    }
    *output=r;return NSR_OK;
}
