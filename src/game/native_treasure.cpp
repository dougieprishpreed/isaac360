#include "native_treasure.h"
#include <string.h>
NativeTreasureResult NativePlaceTreasure(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,
        NativeLevelLayout& layout,const NativeTreasureContext& c,NativeTreasureReport* output) {
    if(!output || !stream.state || stream.index!=35 || c.stage<1 || c.stage>13 || c.stageType<0 || c.stageType>5 ||
        c.placement.dimension<0 || c.placement.dimension>2 || c.placement.nativeMode<0 || c.placement.nativeMode>3 ||
        layout.count<0 || layout.count>507) return NTR_BAD_REQUEST;
    if(c.placement.nativeMode==2 || c.placement.nativeMode==3) return NTR_UNSUPPORTED_CONTEXT;
    NativeTreasureReport r;memset(&r,0,sizeof(r));r.definition=r.topologyIndex=r.layoutIndex=-1;r.entrySeed=stream.state;
    if(!(c.stage<7 || (c.stage<9 && c.held110) ||
        (c.stage==10 && ((c.stageType==0 && c.held161) || (c.stageType==1 && c.held155))))) r.skipped=NTS_STAGE;
    else if(c.configuredType2) r.skipped=NTS_CONFIGURED;
    else if(c.counter1c312c>=3) r.skipped=NTS_COUNTER;
    if(r.skipped!=NTS_NONE) { *output=r;return NTR_OK; }
    r.boundA=1+((c.profileDb || c.profileOverride)?1:0)+((c.profileDd || c.profileOverride)?1:0);
    r.boundB=1+((c.profileDc || c.profileOverride)?1:0)+((c.profileDe || c.profileOverride)?1:0);
    stream.Next(&r.subtypeSeed);++r.layoutDraws;NativeRng local;local.Set(r.subtypeSeed,19);
    unsigned int n;local.Next(&n);++r.localDraws;r.subtype=n%r.boundA;
    local.Next(&n);++r.localDraws;r.subtype+=n%r.boundB;
    local.Next(&n);++r.localDraws;
    if(!(n&1) || c.placement.nativeMode!=1) r.subtype=r.boundA+r.boundB-2;
    local.Next(&n);++r.localDraws;r.localState=local.state;r.rareByte=n&255;
    if(!r.rareByte) r.subtype=11;
    else if(r.rareByte==1 && r.subtype>=2) r.subtype=10;
    if(c.eligiblePlayerTrinket33) r.subtype+=100;
    stream.Next(&r.placementSeed);++r.layoutDraws;
    NativeRoomQuery q={0,2,13,0,0xffffffffU,1,10,0,r.subtype};
    r.selectionResult=pool.SelectOrdinary(r.placementSeed,q,true,&r.definition,&r.selectorDraws);
    //Native dereferences a null selection. Keep consumed owner/local state and
    //return an explicit bounded error; never invent data or silently repick.
    if(r.selectionResult!=NR_OK) { *output=r;return r.selectionResult==NR_NO_MATCH?NTR_NO_ROOM:NTR_BAD_POOL; }
    const NativeRoomDef* data=NativeRoom(r.definition);
    r.geometryResult=graph.PickEndRoom(data->shape,data->doors,&r.topologyIndex);
    if(r.geometryResult!=NSP_OK) { *output=r;return r.geometryResult==NSP_UNRESOLVED_DOOR?NTR_UNRESOLVED_GEOMETRY:NTR_GEOMETRY_FAILED; }
    r.placementResult=layout.Place(graph.rooms[r.topologyIndex],r.definition,r.placementSeed,c.placement,&r.layoutIndex);
    *output=r;return r.placementResult==NP_OK?NTR_OK:NTR_DESCRIPTOR_FAILED;
}
