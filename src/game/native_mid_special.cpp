#include "native_mid_special.h"
#include <string.h>
namespace {
bool Valid(const NativeTopology& g,const NativeRoomPool& p,const NativeRng& s,const NativeLevelLayout& l,const NativeMidSpecialContext& c,NativeMidSpecialReport* out) {
    return out&&s.state&&s.index==35&&c.stage>=1&&c.stage<=13&&c.stageType>=0&&c.stageType<=5&&
        c.placement.nativeMode>=0&&c.placement.nativeMode<=3&&c.placement.dimension>=0&&c.placement.dimension<=2&&
        g.count>=1&&g.count<=169&&g.deadEndCount>=0&&g.otherCount>=0&&g.deadEndCount+g.otherCount<=g.count&&
        l.count>=0&&l.count<=507&&NativeStageSource(0,p.catalog)>=0;
}
NativeMidSpecialReport Report(unsigned int seed) {
    NativeMidSpecialReport r;memset(&r,0,sizeof(r));r.definition=r.topologyIndex=r.layoutIndex=-1;r.subtype=-1;
    r.entrySeed=seed;r.selectionResult=NR_NO_MATCH;r.geometryResult=NSP_NO_MATCH;r.action=NOA_NO_GEOMETRY;return r;
}
unsigned int Draw(NativeRng& stream,NativeMidSpecialReport& r) {unsigned int n=0;stream.Next(&n);++r.layoutDraws;return n;}
int EffectiveStage(const NativeMidSpecialContext& c) {return c.stage+((c.stageType==4||c.stageType==5)?1:0);}
NativeRoomResult Select(NativeRoomPool& p,NativeMidSpecialReport& r,int low,int high) {
    NativeRoomQuery q={0,r.type,13,0,0xffffffffU,low,high,0,r.subtype};int draws=0;
    r.selectionResult=p.SelectOrdinary(r.selectionSeed,q,true,&r.definition,&draws);r.selectorDraws+=draws;++r.selectionCalls;return r.selectionResult;
}
NativeMidSpecialResult Geometry(NativeTopology& graph,NativeMidSpecialReport& r) {
    if(r.definition<0)return NMS_OK;
    const NativeRoomDef* d=NativeRoom(r.definition);if(!d)return NMS_BAD_POOL;
    r.geometryResult=graph.PickEndRoom(d->shape,d->doors,&r.topologyIndex);
    return r.geometryResult==NSP_UNRESOLVED_DOOR?NMS_UNRESOLVED_GEOMETRY:r.geometryResult==NSP_BAD_REQUEST?NMS_BAD_GEOMETRY:NMS_OK;
}
NativeMidSpecialResult Install(NativeTopology& graph,NativeLevelLayout& layout,const NativeMidSpecialContext& c,NativeMidSpecialReport& r) {
    if(r.geometryResult!=NSP_OK)return NMS_OK;
    if(!r.install) {
        if(graph.deadEndCount>=kNativeTopologyCells)return NMS_BAD_GEOMETRY;
        graph.deadEnds[graph.deadEndCount++]=graph.rooms[r.topologyIndex].index;r.action=NOA_REQUEUED;return NMS_OK;
    }
    r.placementResult=layout.Place(graph.rooms[r.topologyIndex],r.definition,r.placementSeed,c.placement,&r.layoutIndex);
    if(r.placementResult!=NP_OK)return NMS_DESCRIPTOR_FAILED;r.action=NOA_PLACED;return NMS_OK;
}
NativeMidSpecialResult SelectionError(NativeRoomResult r) {return r==NR_NO_MATCH?NMS_NO_ROOM:NMS_BAD_POOL;}
}
NativeMidSpecialResult NativePlaceMiniboss(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,NativeLevelLayout& layout,
        const NativeMidSpecialContext& c,NativeMidSpecialReport* output) {
    if(!Valid(graph,pool,stream,layout,c,output))return NMS_BAD_REQUEST;
    if(c.placement.nativeMode>1)return NMS_UNSUPPORTED_CONTEXT;
    NativeMidSpecialReport r=Report(stream.state);r.type=6;if(c.stage>=11){r.action=NOA_STAGE_SKIP;*output=r;return NMS_OK;}
    int stage=EffectiveStage(c);
    if(stage>2) {
        r.superBound=(c.profile48||c.profileOverride)?30:80;
        if(c.profile65||c.profileOverride)r.superBound=10;
        if(c.profileC9OrCA)r.superBound=5;
    }
    if(stage>=3&&Draw(stream,r)%10==0&&!c.seedEffect32) {
        Draw(stream,r);r.subtype=14; //The additional owner advance is not selection.
    } else {
        if(r.superBound)r.superVariant=Draw(stream,r)%(unsigned int)r.superBound==0;
        const int base[]={2,3,1,0,5,6};int candidates[6],count=0;
        for(int i=0;i<6;++i)if(!(c.disabledSins&(1U<<(9+i))))candidates[count++]=base[i]+(r.superVariant?7:0);
        if(count)r.subtype=candidates[Draw(stream,r)%(unsigned int)count];
    }
    if(r.subtype>=0) {
        int difficulty=c.stage<7?1:10;r.selectionSeed=Draw(stream,r);
        NativeRoomResult selected=Select(pool,r,difficulty,difficulty);
        if(selected==NR_NO_MATCH) {r.fallback=true;r.selectionSeed=Draw(stream,r);selected=Select(pool,r,1,10);}
        if(selected!=NR_OK&&selected!=NR_NO_MATCH){*output=r;return NMS_BAD_POOL;}
    }
    NativeMidSpecialResult result=Geometry(graph,r);if(result!=NMS_OK){*output=r;return result;}
    if(!c.configuredType6) {
        r.gateRoll=Draw(stream,r);r.install=!(r.gateRoll&3);
        if(!r.install){r.secondGateRoll=Draw(stream,r);r.install=r.secondGateRoll%3==0&&stage==1;}
    }
    r.placementSeed=stream.state;result=Install(graph,layout,c,r);
    if(result==NMS_OK&&r.action==NOA_PLACED) {
        const int subtypes[]={2,3,1,0,5,6};for(int i=0;i<6;++i)if(r.subtype==subtypes[i]||r.subtype==subtypes[i]+7)r.flagsLowOr=0x200U<<i;
        if(r.subtype==14)r.flagsHighOr=1;
    }
    *output=r;return result;
}
NativeMidSpecialResult NativePlaceChallengeRoom(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,NativeLevelLayout& layout,
        const NativeMidSpecialContext& c,NativeMidSpecialReport* output) {
    if(!Valid(graph,pool,stream,layout,c,output))return NMS_BAD_REQUEST;
    if(c.placement.nativeMode>1)return NMS_UNSUPPORTED_CONTEXT;
    NativeMidSpecialReport r=Report(stream.state);r.type=11;if(c.stage>=11){r.action=NOA_STAGE_SKIP;*output=r;return NMS_OK;}r.subtype=c.levelChallengeSubtype?1:0;r.selectionSeed=Draw(stream,r);
    NativeRoomResult selected=Select(pool,r,1,10);if(selected!=NR_OK){*output=r;return SelectionError(selected);}
    NativeMidSpecialResult result=Geometry(graph,r);if(result!=NMS_OK){*output=r;return result;}
    if(!c.configuredType11) {
        r.gateRoll=Draw(stream,r);int stage=EffectiveStage(c);
        r.install=(!(r.gateRoll&1)||stage>=3)&&c.fullHealthPredicate&&stage>=2;
    }
    r.placementSeed=stream.state;result=Install(graph,layout,c,r);*output=r;return result;
}
NativeMidSpecialResult NativePlaceArcadeOrType20(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,NativeLevelLayout& layout,
        const NativeMidSpecialContext& c,NativeMidSpecialReport* output) {
    if(!Valid(graph,pool,stream,layout,c,output))return NMS_BAD_REQUEST;
    if(c.placement.nativeMode>1)return NMS_UNSUPPORTED_CONTEXT;
    NativeMidSpecialReport r=Report(stream.state);if(c.stage>=11){r.action=NOA_STAGE_SKIP;*output=r;return NMS_OK;}r.gateRoll=Draw(stream,r);bool type20Requested=r.gateRoll%10==0;
    if(!type20Requested){r.secondGateRoll=Draw(stream,r);type20Requested=r.secondGateRoll%3==0&&c.firstPlayerKeys>1;}
    type20Requested=type20Requested&&!c.configuredType20;
    r.type=type20Requested&&!c.eligiblePlayer2?20:9;r.subtype=r.type==20?-1:c.eligiblePlayer2?1:0;r.selectionSeed=Draw(stream,r);
    NativeRoomResult selected=Select(pool,r,1,10);
    if(selected==NR_NO_MATCH&&r.type==9&&r.subtype==1){r.fallback=true;r.subtype=0;r.selectionSeed=Draw(stream,r);selected=Select(pool,r,1,10);}
    if(selected!=NR_OK){*output=r;return SelectionError(selected);}
    NativeMidSpecialResult result=Geometry(graph,r);if(result!=NMS_OK){*output=r;return result;}
    bool configured=r.type==9?c.configuredType9:c.configuredType20;
    int stage=EffectiveStage(c);bool resourceGate=r.type==9?(c.firstPlayerCoins>=5||c.eligiblePlayer2):c.firstPlayerKeys>1;
    r.install=!configured&&resourceGate&&(stage==2||stage==4||stage==6||stage==8||(c.eligiblePlayer2&&stage<11));
    r.placementSeed=stream.state;result=Install(graph,layout,c,r);*output=r;return result;
}
NativeMidSpecialResult NativePlacePlanetarium(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,NativeLevelLayout& layout,
        const NativeMidSpecialContext& c,NativeMidSpecialReport* output) {
    if(!Valid(graph,pool,stream,layout,c,output)||!(c.planetariumChance>=0&&c.planetariumChance<=1))return NMS_BAD_REQUEST;
    if(c.placement.nativeMode>1)return NMS_UNSUPPORTED_CONTEXT;
    NativeMidSpecialReport r=Report(stream.state);r.type=24;r.selectionSeed=Draw(stream,r);
    NativeRoomResult selected=Select(pool,r,1,10);if(selected!=NR_OK){*output=r;return SelectionError(selected);}
    NativeMidSpecialResult result=Geometry(graph,r);if(result!=NMS_OK){*output=r;return result;}
    if(!c.configuredType24&&!c.configuredType4&&r.geometryResult==NSP_OK) {
        stream.Float(&r.chanceRoll);++r.layoutDraws;r.gateRoll=stream.state;
        r.install=r.chanceRoll<c.planetariumChance&&(c.profile1da||c.profileOverride);
    }
    r.placementSeed=stream.state;result=Install(graph,layout,c,r);*output=r;return result;
}
NativeMidSpecialResult NativePlaceBedroom(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,NativeLevelLayout& layout,
        const NativeMidSpecialContext& c,NativeMidSpecialReport* output) {
    if(!Valid(graph,pool,stream,layout,c,output))return NMS_BAD_REQUEST;
    if(c.placement.nativeMode>1)return NMS_UNSUPPORTED_CONTEXT;
    NativeMidSpecialReport r=Report(stream.state);
    if(c.stage>=7){r.action=NOA_STAGE_SKIP;*output=r;return NMS_OK;}
    bool predicate=!c.configuredType19&&c.eligibleRoom19;
    r.type=((!(Draw(stream,r)&1)||!predicate)&&!c.configuredType18)?18:19;
    r.selectionSeed=Draw(stream,r);
    NativeRoomResult selected=Select(pool,r,1,10);if(selected!=NR_OK){*output=r;return SelectionError(selected);}
    NativeMidSpecialResult result=Geometry(graph,r);if(result!=NMS_OK){*output=r;return result;}
    r.gateRoll=Draw(stream,r);r.install=r.gateRoll%50==0;
    if(!r.install){r.secondGateRoll=Draw(stream,r);r.install=r.secondGateRoll%5==0&&c.lowHealthPlayer0;}
    r.placementSeed=stream.state;result=Install(graph,layout,c,r);*output=r;return result;
}
