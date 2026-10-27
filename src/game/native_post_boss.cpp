#include "native_post_boss.h"
#include <string.h>
NativePostBossResult NativePlacePostBossType8(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,
        NativeLevelLayout& layout,const NativePostBossContext& c,NativePostBossReport* output) {
    if(!output || !stream.state || stream.index!=35 || c.placement.dimension<0 || c.placement.dimension>2 ||
        c.placement.nativeMode<0 || c.placement.nativeMode>3 || layout.count<0 || layout.count>507) return NPB_BAD_REQUEST;
    if(c.placement.nativeMode==2 || c.placement.nativeMode==3) return NPB_UNSUPPORTED_CONTEXT;
    NativePostBossReport r; memset(&r,0,sizeof(r));
    for(int i=0;i<2;++i) r.entries[i].definition=r.entries[i].topologyIndex=r.entries[i].layoutIndex=-1;
    if(!c.configuredType8) {
        stream.Next(&r.initialSeed); ++r.layoutDraws; NativeRng local;local.Set(r.initialSeed,9);r.localState=local.state;
        r.requested=c.extraType8?2:1;
        for(int i=0;i<r.requested;++i) {
            NativePostBossEntry& e=r.entries[i];local.Next(&e.placementSeed);++r.localDraws;r.localState=local.state;
            NativeRoomQuery q={0,8,13,0,0xffffffffU,1,10,0,-1};int draws=0;
            e.selectionResult=pool.SelectOrdinary(e.placementSeed,q,true,&e.definition,&draws);r.selectorDraws+=draws;
            //Native immediately dereferences the result. Missing data is an
            //explicit bounded port failure, never a substitute or support repick.
            if(e.selectionResult!=NR_OK) { *output=r;return e.selectionResult==NR_NO_MATCH?NPB_NO_ROOM:NPB_BAD_POOL; }
            const NativeRoomDef* data=NativeRoom(e.definition);
            e.geometryResult=graph.PickEndRoom(data->shape,data->doors,&e.topologyIndex);
            if(e.geometryResult!=NSP_OK) {
                if(e.geometryResult==NSP_UNRESOLVED_DOOR) { *output=r;return NPB_UNRESOLVED_GEOMETRY; }
                if(!i) { *output=r;return NPB_FIRST_GEOMETRY_FAILED; }
                r.optionalSecondFailure=true;break; //Native accepts the first room.
            }
            e.placementResult=layout.Place(graph.rooms[e.topologyIndex],e.definition,e.placementSeed,c.placement,&e.layoutIndex);
            if(e.placementResult!=NP_OK) { *output=r;return NPB_DESCRIPTOR_FAILED; }
            ++r.placed;
        }
    }
    stream.Next(&r.nextSelectionSeed);++r.layoutDraws;*output=r;return NPB_OK;
}
