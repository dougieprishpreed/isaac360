#include "native_world.h"
#include "native_room_effects.h"
#include "native_room_view.h"
#include "native_door_resources.h"
#include "native_persistence.h"
#include "native_room_update.h"
#include <string.h>
namespace {
NativeWorldResult Result(NativeWorldIssue* issue,NativeWorldResult result,const NativePlacedRoom* p=0) {
    if(issue){issue->result=result;if(p){issue->listIndex=p->gridIndex<0?507-p->gridIndex-1:p->listIndex;
        issue->gridIndex=p->gridIndex;issue->dimension=p->dimension;issue->definition=p->definition;}}
    return result;
}
}
NativeWorld::NativeWorld(){memset(resident,0,sizeof(resident));Reset();}
NativeWorld::~NativeWorld(){Reset();}
void NativeWorld::Reset() {
    for(int i=0;i<kNativeWorldRoomLimit;++i){resident[i].enemies.Clear();delete resident[i].room;delete resident[i].doors;delete resident[i].effects;delete resident[i].view;delete[] resident[i].doorVisuals;delete resident[i].persistent;delete resident[i].update;memset(&resident[i],0,sizeof(resident[i]));}
    layout.Reset();currentList=previousList=currentGrid=previousGrid=-1;residentCount=0;entrySerial=0;
}
bool NativeWorld::Bind(const NativeLevelLayout& selected) {
    if(selected.count<1||selected.count>507||selected.currentDimension<0||selected.currentDimension>2)return false;
    int owners[3][169];for(int d=0;d<3;++d)for(int i=0;i<169;++i)owners[d][i]=-1;
    for(int i=0;i<selected.count;++i) {
        const NativePlacedRoom& p=selected.rooms[i];const NativeRoomDef* d=NativeRoom(p.definition);
        if(!d||NativeRoomCatalog(p.definition)!=NC_NORMAL||p.listIndex!=i||p.dimension<0||p.dimension>2||p.gridIndex<0||p.gridIndex>=169||!p.placementSeed)return false;
        NativeCell offsets[4];int n=NativeShapeFootprint(d->shape,offsets);if(!n)return false;
        for(int k=0;k<n;++k){NativeCell at={p.gridIndex%13+offsets[k].x,p.gridIndex/13+offsets[k].y};int cell=NativeCellIndex(at);
            if(cell<0||owners[p.dimension][cell]>=0||selected.owners[p.dimension][cell]!=i)return false;owners[p.dimension][cell]=i;}
    }
    for(int d=0;d<3;++d)for(int cell=0;cell<169;++cell){int owner=selected.owners[d][cell];
        if(owner!=owners[d][cell])return false;}
    for(int i=0;i<18;++i){const NativePlacedRoom& p=selected.offGrid[i];if(p.gridIndex!=-i-1||
        (p.definition>=0&&(NativeRoomCatalog(p.definition)!=NC_NORMAL||!NativeRoom(p.definition))))return false;}
    NativeLevelLayout* copy=new NativeLevelLayout(selected);Reset();layout=*copy;delete copy;return true;
}
void NativeWorld::Swap(NativeWorld& other) {
    NativeLevelLayout* copy=new NativeLevelLayout(layout);layout=other.layout;other.layout=*copy;delete copy;
    for(int i=0;i<kNativeWorldRoomLimit;++i){NativeWorldResident r=resident[i];resident[i]=other.resident[i];other.resident[i]=r;}
    int n=currentList;currentList=other.currentList;other.currentList=n;n=previousList;previousList=other.previousList;other.previousList=n;
    n=currentGrid;currentGrid=other.currentGrid;other.currentGrid=n;n=previousGrid;previousGrid=other.previousGrid;other.previousGrid=n;
    n=residentCount;residentCount=other.residentCount;other.residentCount=n;unsigned int serial=entrySerial;entrySerial=other.entrySerial;other.entrySerial=serial;
}
NativeWorldResult NativeWorld::Start(const NativeLevelLayout& selected,int grid,int dimension,const NativeSceneFiles& files,NativeWorldIssue* issue,const NativeRoomSetupContext* setup) {
    NativeWorld* next=new NativeWorld;if(!next->Bind(selected)){delete next;return Result(issue,NWR_BAD_REQUEST);}
    NativeWorldResult result=next->Enter(grid,dimension,files,issue,setup);
    if(result==NWR_OK){next->previousGrid=next->currentGrid;next->previousList=next->currentList;Swap(*next);}delete next;return result;
}
NativeWorldResult NativeWorld::Enter(int cell,int dimension,const NativeSceneFiles& files,NativeWorldIssue* issue,const NativeRoomSetupContext* setup) {
    if(issue){memset(issue,0,sizeof(*issue));issue->listIndex=issue->gridIndex=issue->dimension=issue->definition=-1;}
    if(dimension<0||dimension>2||files.nativeMode<0||files.nativeMode>1)return Result(issue,NWR_BAD_REQUEST);
    int waterMode;if(setup&&(setup->backdrop!=files.backdrop||setup->configStage<0||!NativeBackdropWaterPitsMode(setup->backdrop,&waterMode)))return Result(issue,NWR_BAD_REQUEST);
    const NativePlacedRoom* p=layout.ByCell(cell,dimension);if(!p||p->definition<0)return Result(issue,NWR_NO_ROOM);
    int id=p->gridIndex<0?507-p->gridIndex-1:p->listIndex;
    if(id<0||id>=kNativeWorldRoomLimit)return Result(issue,NWR_BAD_REQUEST,p);
    if(resident[id].room&&(resident[id].backdrop!=files.backdrop||resident[id].nativeMode!=files.nativeMode))return Result(issue,NWR_BAD_REQUEST,p);
    if(resident[id].room&&setup&&!resident[id].setupKnown)return Result(issue,NWR_BAD_REQUEST,p);
    if(resident[id].room&&setup&&resident[id].configStage!=setup->configStage)return Result(issue,NWR_BAD_REQUEST,p);
    if(!resident[id].room) {
        unsigned char* raw=0;unsigned int length=0,hash=0;
        NativeSceneResult source=NativeSceneReadRecord(files,p->definition,&raw,&length,&hash);if(issue)issue->source=source;
        if(source!=NSC_OK)return Result(issue,NWR_SOURCE_ERROR,p);
        RoomRecord record;NativeRoomState* room=new NativeRoomState;NativeConstructionIssue detail={NCS_HEADER,-1,0,0,0,NRS_BAD_DATA};
        bool opened=record.Open(raw,length,RRF_STB1)==RRR_OK;
        NativeRoomMarkers markers;NativeRoomSetupState setupState={0};
        int stage=setup?setup->configStage:NativeRoom(p->definition)->stage;
        resident[id].enemies.Clear();
        source=opened?NativeSceneBuildRoom(record,p->seeds,files.backdrop,files.nativeMode,stage,room,&detail,&markers,files.frame,&resident[id].enemies):NSC_SOURCE_ERROR;
        if(source==NSC_OK&&setup&&!NativeSetupRoom(record.type,p->flags,p->visitedCount,p->seeds,markers,*setup,&setupState))source=NSC_BAD_REQUEST;
        delete[] raw;if(issue){issue->source=source;if(opened)issue->construction=detail;}
        if(source!=NSC_OK){resident[id].enemies.Clear();delete room;return Result(issue,source==NSC_SOURCE_ERROR?NWR_SOURCE_ERROR:
            (source==NSC_UNSUPPORTED_CONTENT||source==NSC_UNSUPPORTED_SHAPE||source==NSC_UNSUPPORTED_EXTENT)?NWR_CONSTRUCTION_GAP:NWR_BAD_REQUEST,p);}
        //Room header's potential doors become this descriptor's enabled doors.
        //The door actor still owns open/locked/secret/payment semantics.
        for(int side=0;side<8;++side)room->doors[side]=(p->doors&(1U<<side))!=0;
        resident[id].room=room;resident[id].hash=hash;resident[id].backdrop=files.backdrop;resident[id].nativeMode=files.nativeMode;resident[id].configStage=stage;++residentCount;
        //The room owns its effect cache even when its ambient spawn count is
        //zero. It is constructed with no player owner, independently of RNG.
        resident[id].effects=new NativeRoomEffects;
        resident[id].update=new NativeRoomUpdate(files.frame);
        if(setup){resident[id].setup=setupState;resident[id].setupKnown=true;
            NativePlacedRoom& descriptor=id<507?layout.rooms[id]:layout.offGrid[id-507];descriptor.flags=setupState.flags;}
    }
    previousList=currentList;currentList=id;previousGrid=currentGrid;currentGrid=cell;layout.currentDimension=dimension;
    ++entrySerial;++resident[id].activations;return Result(issue,NWR_OK,p);
}
NativeWorldResult NativeWorld::EnterLinked(int slot,const NativeSceneFiles& files,NativeWorldIssue* issue) {
    if(currentList<0||currentList>=layout.count||slot<0||slot>=8)return Result(issue,NWR_BAD_REQUEST);
    const NativePlacedRoom* p=layout.DoorTarget(currentList,slot);
    if(!p)return Result(issue,NWR_NO_DOOR);
    return Enter(layout.rooms[currentList].doorTargets[slot],p->dimension,files,issue);
}
NativeRoomState* NativeWorld::Current(){return currentList>=0&&currentList<kNativeWorldRoomLimit?resident[currentList].room:0;}
const NativeRoomState* NativeWorld::Current() const{return currentList>=0&&currentList<kNativeWorldRoomLimit?resident[currentList].room:0;}
NativeDoorResult NativeWorld::PrepareDoorActors(NativeMt& owner,unsigned int frame) {
    NativeRoomState* room=Current();const NativePlacedRoom* descriptor=CurrentDescriptor();
    if(!room||!descriptor)return NDR_BAD_REQUEST;
    NativeWorldResident& r=resident[currentList];if(r.doors)return NDR_ALREADY_ATTACHED;
    //Existing attached actors survive reentry. Restoring a visited room with
    //no grid objects still needs the original serialization/Init path.
    if(descriptor->visitedCount)return NDR_BAD_REQUEST;
    NativeDoorSet* doors=new NativeDoorSet;
    if(!NativeInitDoorSet(room->shape,room->geometry,descriptor->doors,frame,owner,doors)){delete doors;return NDR_BAD_REQUEST;}
    //0079c7b0 sets+3c to0 before006cbe50 writes native+76c=1000.
    //Later asset/animation/interaction services still own final door collision.
    for(int slot=0;slot<8;++slot)if(doors->attached&(1U<<slot)){
        int grid=doors->actors[slot].grid;
        //79c93a..94b destroys a non-door occupant before replacing +24.
        //Remove its maintained owner as well as replacing collision data.
        for(int i=0;i<room->staticGridCount;++i)if(room->staticGrids[i].grid==grid){
            for(int j=i+1;j<room->staticGridCount;++j)room->staticGrids[j-1]=room->staticGrids[j];--room->staticGridCount;break;}
        for(int i=0;i<room->decorationCount;++i)if(room->decorations[i].cell==grid){
            for(int j=i+1;j<room->decorationCount;++j)room->decorations[j-1]=room->decorations[j];--room->decorationCount;break;}
        room->collision.Set(grid,true,0,1000,16);
    }
    r.doors=doors;return NDR_OK;
}
NativeDoorResult NativeWorld::SelectDoorVariant(int slot,const NativeDoorBinding& c) {
    const NativePlacedRoom* source=CurrentDescriptor();
    if(!source||slot<0||slot>7||!resident[currentList].doors||!(resident[currentList].doors->attached&(1U<<slot)))return NDR_BAD_REQUEST;
    const NativePlacedRoom* target=layout.ByCell(source->doorTargets[slot],layout.currentDimension);
    const NativeRoomDef* from=NativeRoom(source->definition);const NativeRoomDef* to=target?NativeRoom(target->definition):0;
    if(!from||!to)return NDR_MISSING_TARGET;
    if(!c.sourceData||!c.targetData||c.sourceType!=from->type||c.targetType!=to->type||c.sourceVariant!=(int)from->variant||c.targetVariant!=(int)to->variant||
        c.sourceFlags!=source->flags||c.targetFlags!=target->flags||c.currentGrid!=currentGrid||c.targetGrid!=source->doorTargets[slot]||
        c.dimension!=layout.currentDimension||c.nativeMode!=resident[currentList].nativeMode)return NDR_BAD_REQUEST;
    NativeWorldResident& r=resident[currentList];NativeDoorActor& actor=r.doors->actors[slot];NativeDoorSelection selected;
    if(!NativeSelectDoor(c,actor.variant,actor.auxiliary,&selected))return NDR_BAD_REQUEST;
    actor.sourceType=selected.sourceType;actor.targetType=selected.targetType;actor.variant=selected.variant;actor.auxiliary=selected.auxiliary;actor.targetGrid=c.targetGrid;
    r.doorSelectionMask|=1U<<slot;if(selected.openAfterAssetSetup)r.doorPendingOpenMask|=1U<<slot;else r.doorPendingOpenMask&=~(1U<<slot);return NDR_OK;
}
const NativePlacedRoom* NativeWorld::CurrentDescriptor() const {
    if(currentList<0||currentList>=kNativeWorldRoomLimit)return 0;
    return currentList<507?layout.ByList(currentList):layout.ByCell(-(currentList-507)-1);
}
const NativeWorldResident* NativeWorld::At(int i) const {return i>=0&&i<kNativeWorldRoomLimit?resident+i:0;}
