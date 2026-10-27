#include "native_optional.h"
#include <string.h>
NativeOptionalResult NativePlaceOptionalRoom(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,
        NativeLevelLayout& layout,const NativeOptionalContext& c,NativeOptionalReport* output) {
    if(!output || !stream.state || stream.index!=35 || c.stage<1 || c.stage>13 ||
        c.placement.dimension<0 || c.placement.dimension>2 || c.placement.nativeMode<0 || c.placement.nativeMode>3 ||
        layout.count<0 || layout.count>507 || graph.count<1 || graph.count>169 || graph.deadEndCount<0 || graph.otherCount<0 ||
        graph.deadEndCount+graph.otherCount>graph.count) return NOC_BAD_REQUEST;
    if(c.placement.nativeMode==2 || c.placement.nativeMode==3) return NOC_UNSUPPORTED_CONTEXT;
    NativeOptionalReport r;memset(&r,0,sizeof(r));r.definition=r.topologyIndex=r.layoutIndex=-1;r.entrySeed=stream.state;
    if(c.stage>=11) { *output=r;return NOC_OK; }
    stream.Next(&r.roll50);++r.layoutDraws;r.requestedType21=r.roll50%50==0;
    if(!r.requestedType21) {
        stream.Next(&r.roll5);++r.layoutDraws;
        if(r.roll5%5==0) {
            //Native logs then dereferences its first player. A missing effective
            //caller value is a bounded port error after those two consumed draws.
            if(!c.hasFirstPlayer) { *output=r;return NOC_NO_PLAYER; }
            r.requestedType21=c.firstPlayer12a4>1;
        }
    }
    r.type=r.requestedType21 && !c.configuredType21?21:13;
    stream.Next(&r.selectionSeed);++r.layoutDraws;
    NativeRoomQuery q={0,r.type,13,0,0xffffffffU,1,10,0,-1};
    r.selectionResult=pool.SelectOrdinary(r.selectionSeed,q,true,&r.definition,&r.selectorDraws);
    if(r.selectionResult!=NR_OK) { *output=r;return r.selectionResult==NR_NO_MATCH?NOC_NO_ROOM:NOC_BAD_POOL; }
    const NativeRoomDef* data=NativeRoom(r.definition);r.geometryResult=graph.PickEndRoom(data->shape,data->doors,&r.topologyIndex);
    if(r.geometryResult==NSP_UNRESOLVED_DOOR || r.geometryResult==NSP_BAD_REQUEST) {
        *output=r;return r.geometryResult==NSP_UNRESOLVED_DOOR?NOC_UNRESOLVED_GEOMETRY:NOC_BAD_GEOMETRY;
    }
    //The type13 config gate governs installation even when type21 was selected.
    //Native executes these rolls even when no geometry was found.
    if(!c.configuredType13) {
        stream.Next(&r.roll7);++r.layoutDraws;r.install=r.roll7%7==0;
        if(!r.install) {stream.Next(&r.roll4);++r.layoutDraws;r.install=!(r.roll4&3) && c.predicate908b40;}
    }
    if(r.geometryResult!=NSP_OK) r.action=NOA_NO_GEOMETRY;
    else if(r.install) {
        r.placementSeed=stream.state;r.placementResult=layout.Place(graph.rooms[r.topologyIndex],r.definition,r.placementSeed,c.placement,&r.layoutIndex);
        if(r.placementResult!=NP_OK) { *output=r;return NOC_DESCRIPTOR_FAILED; }
        r.action=NOA_PLACED;
    } else {
        //Native wrapper+370/+374 is the LEAF list. Append the resized room's
        //native index to its tail; do not put it in the nonleaf list or refresh.
        graph.deadEnds[graph.deadEndCount++]=graph.rooms[r.topologyIndex].index;r.action=NOA_REQUEUED;
    }
    *output=r;return NOC_OK;
}
NativeOptionalResult NativePlaceType10Rooms(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,
        NativeLevelLayout& layout,const NativeType10Context& c,NativeType10Report* output) {
    if(!output || !stream.state || stream.index!=35 || c.stage<1 || c.stage>13 ||
        c.placement.dimension<0 || c.placement.dimension>2 || c.placement.nativeMode<0 || c.placement.nativeMode>3 ||
        layout.count<0 || layout.count>507 || graph.count<1 || graph.count>169 || graph.deadEndCount<0 || graph.otherCount<0 ||
        graph.deadEndCount+graph.otherCount>graph.count) return NOC_BAD_REQUEST;
    if(c.placement.nativeMode==2 || c.placement.nativeMode==3) return NOC_UNSUPPORTED_CONTEXT;
    NativeType10Report r;memset(&r,0,sizeof(r));r.entrySeed=stream.state;
    for(int i=0;i<2;++i) r.entries[i].definition=r.entries[i].topologyIndex=r.entries[i].layoutIndex=-1;
    if(c.stage>=11) { *output=r;return NOC_OK; }
    stream.Next(&r.initialSeed);++r.layoutDraws;NativeRng local;local.Set(r.initialSeed,66);r.localState=local.state;r.requested=c.held599?2:1;
    for(int i=0;i<r.requested;++i) {
        NativeType10Entry& e=r.entries[i];local.Next(&e.selectionSeed);++r.localDraws;r.localState=local.state;
        NativeRoomQuery q={0,10,13,0,0xffffffffU,1,10,0,-1};int draws=0;
        e.selectionResult=pool.SelectOrdinary(e.selectionSeed,q,true,&e.definition,&draws);r.selectorDraws+=draws;
        if(e.selectionResult!=NR_OK) { *output=r;return e.selectionResult==NR_NO_MATCH?NOC_NO_ROOM:NOC_BAD_POOL; }
        const NativeRoomDef* data=NativeRoom(e.definition);e.geometryResult=graph.PickEndRoom(data->shape,data->doors,&e.topologyIndex);
        if(e.geometryResult==NSP_UNRESOLVED_DOOR || e.geometryResult==NSP_BAD_REQUEST) {
            *output=r;return e.geometryResult==NSP_UNRESOLVED_DOOR?NOC_UNRESOLVED_GEOMETRY:NOC_BAD_GEOMETRY;
        }
        if(!c.configuredType10) {
            unsigned int n;local.Next(&n);++r.localDraws;
            if(n&1) {local.Next(&n);++r.localDraws;}r.localState=local.state;
            if(e.geometryResult==NSP_OK) {
                e.placementSeed=local.state;e.placementResult=layout.Place(graph.rooms[e.topologyIndex],e.definition,e.placementSeed,c.placement,&e.layoutIndex);
                if(e.placementResult!=NP_OK) { *output=r;return NOC_DESCRIPTOR_FAILED; }e.action=NOA_PLACED;++r.placed;
            } else e.action=NOA_NO_GEOMETRY;
        } else if(e.geometryResult==NSP_OK) {
            graph.deadEnds[graph.deadEndCount++]=graph.rooms[e.topologyIndex].index;e.action=NOA_REQUEUED;++r.requeued;
        } else e.action=NOA_NO_GEOMETRY;
    }
    *output=r;return NOC_OK;
}
