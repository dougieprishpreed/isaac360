#include "native_boss.h"
#include <string.h>
bool NativeBossSubtypeAllowed(unsigned int subtype,const NativeBossEligibility& e) {
    if(e.profileOverride) return true;
    if(subtype==20) return e.subtype20;
    if(subtype==21) return e.subtype21;
    if(subtype==19) return e.subtype19;
    if(subtype==42) return e.subtype42;
    if(subtype>=57 && subtype<=66) return e.subtypes57to66;
    if(subtype>=67 && subtype<=72) return e.subtypes67to72;
    return true;
}
NativeBossResult NativeSelectBossRoom(NativeRoomPool& pool,const NativeBossRequest& c,NativeBossReport* output) {
    if(!output || !c.seed || c.nativeMode<0 || c.nativeMode>3) return NB_BAD_REQUEST;
    if(c.nativeMode==2 || c.nativeMode==3 || (c.effectiveCurses&0x80)) return NB_UNSUPPORTED_CONTEXT;
    bool special=c.identity==89 && (c.runFlags&0x8000)!=0;
    if(special && NativeStageSource(c.effectiveConfigStage,pool.catalog)<0) return NB_MISSING_STAGE;
    NativeBossReport r; memset(&r,0,sizeof(r)); r.definition=-1; r.localState=c.seed;
    // Unsigned negation preserves x86 INT_MIN semantics without signed overflow.
    unsigned int magnitude=0U-(unsigned int)c.identity;
    for(int attempt=0;attempt<=50;++attempt) {
        r.localState=NativeShift(r.localState,5,15,17); ++r.attempts;
        bool ranged=c.identity<0;
        NativeRoomQuery q={special?c.effectiveConfigStage:0,5,13,ranged?magnitude:0,ranged?magnitude+49:0xffffffffU,
            1,10,0,ranged?-1:c.identity};
        int selected=-1,draws=0;
        r.roomResult=pool.SelectOrdinary(r.localState,q,true,&selected,&draws); r.selectorDraws+=draws;
        if(special && r.roomResult==NR_NO_MATCH) {
            ++r.fallbacks;
            //007d4f90 retries with the SAME seed, not another owner/local draw.
            if(NativeStageSource(31,pool.catalog)<0) { *output=r; return NB_MISSING_STAGE; }
            q.stage=31; r.roomResult=pool.SelectOrdinary(r.localState,q,true,&selected,&draws); r.selectorDraws+=draws;
        }
        r.definition=selected;
        if(r.roomResult!=NR_OK) { *output=r; return r.roomResult==NR_NO_MATCH?NB_NO_ROOM:NB_BAD_POOL; }
        if(special) { *output=r; return NB_OK; }
        const NativeRoomDef* data=NativeRoom(selected);
        bool reject=ranged?!NativeBossSubtypeAllowed(data->subtype,c.eligibility):
            (data->type==5 && data->variant>=3700 && data->variant<=3849);
        // Native tests the old50 counter after selection: attempt51 returns
        // even a still-rejected room. It neither filters it out nor draws52.
        if(!reject || attempt==50) { r.retryLimit=reject; *output=r; return NB_OK; }
    }
    return NB_BAD_POOL; // Bounded loop returns above on its final selection.
}
NativeBossPlacementResult NativePlaceSelectedBoss(NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,
        NativeLevelLayout& layout,const NativeBossRequest& context,const NativePlacementContext& placement,bool forceNew,NativeBossPlacementReport* output) {
    if(!output || !stream.state || stream.index!=35 || placement.dimension<0 || placement.dimension>2 ||
        placement.nativeMode!=context.nativeMode || context.nativeMode<0 || context.nativeMode>3 || layout.count<0 || layout.count>507) return NBP_BAD_REQUEST;
    NativeBossPlacementReport r; memset(&r,0,sizeof(r)); r.topologyIndex=r.layoutIndex=-1;
    r.selection.definition=-1;
    // This draw belongs to the caller even if subsequent selection/placement
    // fails. A higher retry clears descriptors but retains consumed state.
    stream.Next(&r.placementSeed); NativeBossRequest request=context; request.seed=r.placementSeed;
    r.selectionResult=NativeSelectBossRoom(pool,request,&r.selection);
    if(r.selectionResult!=NB_OK) { *output=r; return NBP_SELECTION_FAILED; }
    const NativeRoomDef* data=NativeRoom(r.selection.definition);
    r.geometryResult=graph.PickBossRoom(data->shape,data->doors,forceNew,&r.topologyIndex);
    if(r.geometryResult!=NSP_OK) { *output=r; return NBP_GEOMETRY_FAILED; }
    r.placementResult=layout.Place(graph.rooms[r.topologyIndex],r.selection.definition,r.placementSeed,placement,&r.layoutIndex);
    *output=r; return r.placementResult==NP_OK?NBP_OK:NBP_DESCRIPTOR_FAILED;
}
NativeBossSequenceResult NativeBossSequence::Begin(int nextStage,bool stage11Shortcut,const NativeBossRequest& c,const NativePlacementContext& pc) {
    if(nextStage<1 || nextStage>13 || c.nativeMode<0 || c.nativeMode>3 || pc.nativeMode!=c.nativeMode || pc.dimension<0 || pc.dimension>2) return NBS_BAD_REQUEST;
    //The maintained terminal-floor path uses one boss identity for stage12;
    //the original multi-boss sequence is not yet prepared.
    if((nextStage==11 && stage11Shortcut) || c.nativeMode==2 || c.nativeMode==3 || (c.effectiveCurses&0x80)) return NBS_UNSUPPORTED_CONTEXT;
    context=c; placement=pc; stage=nextStage; phase=NBS_FIRST_IDENTITY;
    firstLayoutIndex=lastLayoutIndex=lastTopologyIndex=-1; restorationSeed=0; return NBS_OK;
}
int NativeBossSequence::IdentityStage() const { return phase==NBS_FIRST_IDENTITY?stage:phase==NBS_SECOND_IDENTITY?stage+1:-1; }
NativeBossSequenceResult NativeBossSequence::PlaceIdentity(int identity,NativeTopology& graph,NativeRoomPool& pool,NativeRng& stream,
        NativeLevelLayout& layout,NativeBossPlacementReport* output) {
    if(!output || (phase!=NBS_FIRST_IDENTITY && phase!=NBS_SECOND_IDENTITY) || !stream.state || stream.index!=35 || layout.count<0 || layout.count>507) return NBS_BAD_REQUEST;
    bool second=phase==NBS_SECOND_IDENTITY;
    NativeBossRequest request=context; request.identity=identity;
    NativeBossPlacementResult result=NativePlaceSelectedBoss(graph,pool,stream,layout,request,placement,false,output);
    if(result!=NBP_OK) { phase=NBS_FAILED; return NBS_PLACEMENT_FAILED; }
    lastLayoutIndex=output->layoutIndex; lastTopologyIndex=output->topologyIndex;
    if(!second) firstLayoutIndex=lastLayoutIndex;
    if(!second && (context.effectiveCurses&2)) {
        //Captured BEFORE requesting the stage+1 identity. Do not pick both IDs
        //up front: the identity manager has its own evolving state/cache.
        restorationSeed=stream.state; phase=NBS_SECOND_IDENTITY; return NBS_NEED_IDENTITY;
    }
    if(second) stream.Set(restorationSeed,35); //Success only; failed second keeps consumed state.
    NativeSpecialResult reserved=graph.ReserveRoom(lastTopologyIndex);
    if(reserved!=NSP_OK) { phase=NBS_FAILED; return NBS_RESERVATION_FAILED; }
    phase=NBS_COMPLETE; return NBS_OK;
}
