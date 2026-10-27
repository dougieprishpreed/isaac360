#include "native_shop.h"
#include <string.h>
NativeShopResult NativePlaceShops(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,
        NativeLevelLayout& layout,const NativeShopContext& c,NativeShopReport* output) {
    if(!output || !stream.state || stream.index!=35 || c.stage<1 || c.stage>13 || c.stageType<0 || c.stageType>5 ||
        c.trinket82Multiplier<1 || c.trinket82Multiplier>2 || c.placement.dimension<0 || c.placement.dimension>2 ||
        c.placement.nativeMode<0 || c.placement.nativeMode>3 || layout.count<0 || layout.count>507) return NSH_BAD_REQUEST;
    if(c.placement.nativeMode==2 || c.placement.nativeMode==3 || NativeStageSource(c.placement.roomConfigStage,pool.catalog)<0) return NSH_UNSUPPORTED_CONTEXT;
    NativeShopReport r;memset(&r,0,sizeof(r));r.entrySeed=stream.state;
    for(int i=0;i<2;++i) r.entries[i].definition=r.entries[i].topologyIndex=r.entries[i].layoutIndex=-1;
    r.stageGate=c.stage<7 || (c.stage<9 && c.held111) ||
        (c.stage==10 && ((c.stageType==0 && c.held161) || (c.stageType==1 && c.held155)));
    if(r.stageGate && !c.configuredType4) r.requested=(c.curses&2)?2:1;
    else {
        //00703560: effective byte88 plus alternate early-stage predicate. The
        //run flag only suppresses stages1..6. No guessed challenge service.
        bool extra=(c.stageType==4 || c.stageType==5) && !(c.stage<=6 && (c.runFlags&0x10000)) &&
            (c.stage==2 || (c.stage==1 && (c.curses&2)));
        if(c.extraConfig88 && extra) {r.extraBranch=true;r.requested=1;}
    }
    for(int i=0;i<r.requested;++i) {
        NativeShopEntry& e=r.entries[i];
        if(!r.extraBranch) {
            stream.Next(&e.firstRoll);++e.layoutDraws;e.special=e.firstRoll%100==0;
            if(!e.special) {
                stream.Next(&e.secondRoll);++e.layoutDraws;
                e.special=(e.secondRoll%(100/c.trinket82Multiplier)<15 && c.held82) || c.challenge19;
            }
            e.subtype=e.special?(c.eligiblePlayerTrinket112?3:1):(c.held112?2:0);
        }
        stream.Next(&e.placementSeed);++e.layoutDraws;r.layoutDraws+=e.layoutDraws;
        NativeRoomQuery q={c.placement.roomConfigStage,4,13,0,0xffffffffU,1,10,0,e.subtype};int draws=0;
        e.primaryResult=pool.SelectOrdinary(e.placementSeed,q,true,&e.definition,&draws);e.selectorDraws=draws;e.selectionResult=e.primaryResult;
        if(e.primaryResult==NR_NO_MATCH) {
            e.fallback=true;q.stage=0;e.selectionResult=pool.SelectOrdinary(e.placementSeed,q,true,&e.definition,&draws);e.selectorDraws+=draws;
        }
        r.selectorDraws+=e.selectorDraws;
        //Native dereferences both-missing data. Bound that failure without a
        //substitute. Invalid weights/pools are a port error, not a fallback.
        if(e.selectionResult!=NR_OK) { *output=r;return e.selectionResult==NR_NO_MATCH?NSH_NO_ROOM:NSH_BAD_POOL; }
        const NativeRoomDef* data=NativeRoom(e.definition);e.geometryResult=graph.PickEndRoom(data->shape,data->doors,&e.topologyIndex);
        if(e.geometryResult!=NSP_OK) { *output=r;return e.geometryResult==NSP_UNRESOLVED_DOOR?NSH_UNRESOLVED_GEOMETRY:NSH_GEOMETRY_FAILED; }
        e.placementResult=layout.Place(graph.rooms[e.topologyIndex],e.definition,e.placementSeed,c.placement,&e.layoutIndex);
        if(e.placementResult!=NP_OK) { *output=r;return NSH_DESCRIPTOR_FAILED; }
        ++r.placed;if(!i) r.firstPlacedState=stream.state;
    }
    if(r.placed && !r.extraBranch) { stream.Set(r.firstPlacedState,35);r.restored=true; }
    *output=r;return NSH_OK;
}
