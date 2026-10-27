#include "native_ordinary.h"
#include <string.h>
NativeOrdinaryResult NativePlaceOrdinary(const NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,
        NativeLevelLayout& layout,const NativeOrdinaryContext& c,NativeOrdinaryReport* report) {
    if(!report || !stream.state || stream.index!=35 || c.stage<1 || c.stage>13 || c.stageType<0 || c.stageType>5 ||
        c.startGridIndex<0 || c.startGridIndex>=169 || c.minDifficulty<0 || c.minDifficulty>c.maxDifficulty ||
        c.placement.dimension<0 || c.placement.dimension>2 || c.placement.nativeMode<0 || c.placement.nativeMode>3 ||
        layout.count<0 || layout.count>507 || graph.count<1 || graph.count>169) return NOP_BAD_REQUEST;
    // These native selector paths need distinct pools/branches, not an ordinary
    // substitute. No actor-support filtering or repicks are permitted here.
    if(c.stage==12 || c.placement.nativeMode==2 || c.placement.nativeMode==3 || (c.effectiveCurses&0x80)) return NOP_UNSUPPORTED_CONTEXT;
    if(NativeStageSource(c.configStage,pool.catalog)<0) return NOP_MISSING_STAGE;
    int order[kNativeTopologyCells]; int n=graph.PlacementOrder(order,kNativeTopologyCells);
    if(n<0) return NOP_BAD_REQUEST;
    bool seen[kNativeTopologyCells]={false};
    for(int i=0;i<n;++i) {
        int id=order[i]; if(id<0 || id>=graph.count || seen[id]) return NOP_BAD_REQUEST; seen[id]=true;
        const NativeTopologyRoom& node=graph.rooms[id];
        if(NativeCellIndex(node.origin)<0 || node.shape<1 || node.shape>12 || node.doors>255 || node.depth<0) return NOP_BAD_REQUEST;
    }
    memset(report,0,sizeof(*report)); report->orderCount=n; report->failedTopologyIndex=-1;
    for(int i=0;i<n;++i) {
        int id=order[i],definition=-1,low=c.minDifficulty; const NativeTopologyRoom& node=graph.rooms[id];
        if(low>1 && !(stream.state&7)) { low=1; ++report->relaxed; }
        NativeRoomResult selected;
        if(NativeCellIndex(node.origin)==c.startGridIndex && node.depth==0) {
            int stage=c.stage==11 && c.stageType<2?16+c.stageType:0;
            unsigned int variant=stage?0:2; ++report->startLookups;
            selected=NativeFindRoomVariant(stage,1,variant,&definition,pool.catalog);
        } else {
            unsigned int seed; stream.Next(&seed); ++report->layoutDraws;
            NativeRoomQuery q={c.configStage,1,node.shape,c.stage==11?1U:0U,0xffffffffU,low,c.maxDifficulty,node.doors,0};
            int draws=0; selected=pool.SelectOrdinary(seed,q,true,&definition,&draws); report->selectorDraws+=draws;
        }
        report->roomResult=selected;
        if(selected!=NR_OK) { report->failedTopologyIndex=id; return NOP_NO_ROOM; }
        int index=-1;
        report->placementResult=layout.Place(node,definition,stream.state,c.placement,&index);
        if(report->placementResult!=NP_OK) { report->failedTopologyIndex=id; return NOP_PLACEMENT_FAILED; }
        NativeOrdinaryEntry& entry=report->entries[report->placed++];
        entry.topologyIndex=id; entry.layoutIndex=index; entry.definition=definition; entry.minDifficulty=low; entry.placementSeed=stream.state;
    }
    // Native total topology size, not current leaf count or placed count. Special
    // room selections were removed from the two ordinary-placement lists.
    for(int i=n;i<graph.count;++i) { unsigned int ignored; stream.Next(&ignored); ++report->layoutDraws; ++report->tailBurns; }
    return NOP_OK;
}
