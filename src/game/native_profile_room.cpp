#include "native_profile_room.h"
#include <string.h>
NativeProfileRoom12Result NativePlaceProfileRoom12(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,
        NativeLevelLayout& layout,const NativeProfileRoom12Context& c,NativeProfileRoom12Report* output) {
    if(!output || !stream.state || stream.index!=35 || c.stage<1 || c.stage>13 ||
        c.placement.dimension<0 || c.placement.dimension>2 || c.placement.nativeMode<0 || c.placement.nativeMode>3 ||
        layout.count<0 || layout.count>507 || graph.count<1 || graph.count>169 || graph.deadEndCount<0 || graph.otherCount<0 ||
        graph.deadEndCount+graph.otherCount>graph.count) return NP12_BAD_REQUEST;
    if(c.placement.nativeMode==2 || c.placement.nativeMode==3) return NP12_UNSUPPORTED_CONTEXT;
    NativeProfileRoom12Report r;memset(&r,0,sizeof(r));r.definition=r.topologyIndex=r.layoutIndex=-1;r.entrySeed=stream.state;
    if(c.stage>=11) { *output=r;return NP12_OK; }
    if(c.profileDb || c.profileOverride) r.highest=1;
    if(c.profileDc || c.profileOverride) r.highest=2;
    if(c.profileDd || c.profileOverride) r.highest=3;
    if(c.profileDe || c.profileOverride) r.highest=4;
    stream.Next(&r.selectionSeed);++r.layoutDraws;unsigned int subtype;stream.Int(r.highest+1,&subtype);++r.layoutDraws;
    r.subtype=subtype;r.subtypeState=stream.state;
    NativeRoomQuery q={0,12,13,0,0xffffffffU,1,10,0,r.subtype};
    r.selectionResult=pool.SelectOrdinary(r.selectionSeed,q,false,&r.definition,&r.selectorDraws);
    if(r.selectionResult!=NR_OK) { *output=r;return r.selectionResult==NR_NO_MATCH?NP12_NO_ROOM:NP12_BAD_POOL; }
    const NativeRoomDef* data=NativeRoom(r.definition);r.geometryResult=graph.PickEndRoom(data->shape,data->doors,&r.topologyIndex);
    if(r.geometryResult==NSP_UNRESOLVED_DOOR || r.geometryResult==NSP_BAD_REQUEST) {
        *output=r;return r.geometryResult==NSP_UNRESOLVED_DOOR?NP12_UNRESOLVED_GEOMETRY:NP12_BAD_GEOMETRY;
    }
    if(!c.configuredType12) {
        stream.Next(&r.roll20);++r.layoutDraws;r.install=r.roll20%20==0;
        if(!r.install) {stream.Next(&r.roll4);++r.layoutDraws;r.install=!(r.roll4&3) && c.seedFlag8;}
    }
    if(r.geometryResult!=NSP_OK) r.action=NOA_NO_GEOMETRY;
    else if(r.install) {
        r.placementSeed=stream.state;r.placementResult=layout.Place(graph.rooms[r.topologyIndex],r.definition,r.placementSeed,c.placement,&r.layoutIndex);
        if(r.placementResult!=NP_OK) { *output=r;return NP12_DESCRIPTOR_FAILED; }r.action=NOA_PLACED;
    } else {graph.deadEnds[graph.deadEndCount++]=graph.rooms[r.topologyIndex].index;r.action=NOA_REQUEUED;}
    *output=r;return NP12_OK;
}
