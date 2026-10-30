#include "native_layout.h"
#include <string.h>
void NativeLevelLayout::Reset() {
    memset(this,0,sizeof(*this));
    //006aee68: ctor sentinel, distinct from type29's later write of99.
    for(int i=0;i<kNativePlacedRoomLimit;++i){NativePlacedRoom& r=rooms[i];
        r.nativeB4=0xffffffffU;r.gridIndex=r.safeGridIndex=r.definition=-1;r.listIndex=i;}
    for(int d=0;d<3;++d) for(int i=0;i<kNativeTopologyCells;++i) owners[d][i]=-1;
    for(int i=0;i<18;++i){offGrid[i].nativeB4=0xffffffffU;offGrid[i].gridIndex=offGrid[i].safeGridIndex=-i-1;offGrid[i].listIndex=507+i;offGrid[i].definition=-1;
        for(int slot=0;slot<8;++slot)offGrid[i].doorTargets[slot]=offGrid[i].doorTargetSlots[slot]=-1;}
}
NativePlacementResult NativeLevelLayout::Place(const NativeTopologyRoom& node,int definition,unsigned int seed,
        const NativePlacementContext& context,int* index) {
    const NativeRoomDef* data=NativeRoom(definition);
    if(!data || !seed || context.dimension<0 || context.dimension>2 || node.doors>255 || count<0) return NP_BAD_REQUEST;
    if(count>=kNativePlacedRoomLimit) return NP_CAPACITY;
    NativeCell offsets[4]; int cells[4],size=NativeShapeFootprint(data->shape,offsets);
    if(!size || NativeCellIndex(node.origin)<0) return NP_BAD_FOOTPRINT;
    for(int i=0;i<size;++i) {
        NativeCell cell={node.origin.x+offsets[i].x,node.origin.y+offsets[i].y}; cells[i]=NativeCellIndex(cell);
        if(cells[i]<0) return NP_BAD_FOOTPRINT;
    }
    NativePlacedRoom result; memset(&result,0,sizeof(result));
    result.nativeB4=0xffffffffU;
    for(int slot=0;slot<8;++slot)result.doorTargets[slot]=result.doorTargetSlots[slot]=-1;
    result.gridIndex=NativeCellIndex(node.origin);
    result.safeGridIndex=result.gridIndex+(data->shape==9?1:0);
    result.listIndex=count; result.dimension=context.dimension; result.definition=definition;
    result.doors=node.doors; result.placementSeed=seed;
    if(!NativeInitRoomSeeds(seed,&result.seeds)) return NP_BAD_REQUEST;
    if(data->type==4 && context.held146 &&
        ((context.nativeMode!=2 && context.nativeMode!=3) || result.gridIndex==context.specialGridIndex)) result.flags|=0x800;
    // Descriptor base is Level+slot*b8+14. Thus the saved load at +6c is
    // descriptor+58 (SPAWN seed), not decoration/auxiliary. The local draw does
    // not modify that stored seed or the caller's floor RNG stream.
    if(data->type==1 && context.roomConfigStage==33 && NativeShift(result.seeds.spawn,1,21,20)%5==0) result.flags|=0x1000;
    rooms[count]=result;
    // Native placement writes every selected-data footprint cell directly.
    // Occupancy/resize eligibility is the caller's responsibility; overwrites
    // remain observable rather than silently changing native placement order.
    for(int i=0;i<size;++i) owners[context.dimension][cells[i]]=count;
    if(index) *index=count;
    ++count; return NP_OK;
}
const NativePlacedRoom* NativeLevelLayout::ByCell(int gridIndex,int dimension) const {
    if(dimension<0) dimension=currentDimension;
    if(dimension>=0&&dimension<=2&&gridIndex>=-18&&gridIndex<0)return &offGrid[-gridIndex-1];
    if(gridIndex<0 || gridIndex>=kNativeTopologyCells || dimension<0 || dimension>2) return 0;
    return ByList(owners[dimension][gridIndex]);
}
const NativePlacedRoom* NativeLevelLayout::ByList(int index) const { return index>=0 && index<count && index<kNativePlacedRoomLimit?&rooms[index]:0; }
NativePlacedRoom* NativeLevelLayout::Special(int gridIndex) {return gridIndex>=-18&&gridIndex<0?&offGrid[-gridIndex-1]:0;}
