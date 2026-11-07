#include "native_world.h"
#include "native_room_effects.h"
#include "native_room_view.h"
#include "native_door_resources.h"
#include "native_player_state.h"
#include "native_persistence.h"
#include "native_room_update.h"
#include "normal_run.h"
#include <string.h>
namespace {
NativeWorldResult Result(NativeWorldIssue* issue,NativeWorldResult result,const NativePlacedRoom* p=0) {
    if(issue){issue->result=result;if(p){issue->listIndex=p->gridIndex<0?507-p->gridIndex-1:p->listIndex;
        issue->gridIndex=p->gridIndex;issue->dimension=p->dimension;issue->definition=p->definition;}}
    return result;
}
bool ApplySavedPickups(const std::vector<NativeSaveSummary::Pickup>& saved,NativePickupList& live,int cells){
    if(live.count<0||live.count>64)return false;
    const int originalCount=live.count;
    std::vector<bool> used(originalCount?originalCount:0,false);
    std::vector<NativePickup*> result;
    std::vector<NativePickup*> created;
    result.reserve(saved.size());
    for(unsigned int s=0;s<saved.size();++s){
        const NativeSaveSummary::Pickup& value=saved[s];
        if(value.type!=5||value.grid<0||value.grid>=cells)return false;
        int found=-1;
        for(int i=0;i<originalCount;++i){
            const NativePickup* pickup=live.entries[i];
            if(!pickup||used[i]||pickup->type!=value.type||pickup->variant!=value.variant||
               pickup->subtype!=value.subtype||pickup->grid!=value.grid||
               pickup->initialSeed!=value.initialSeed)continue;
            if(found>=0)return false;
            found=i;
        }
        NativePickup* pickup=0;
        if(found>=0){used[found]=true;pickup=live.entries[found];}
        else{
            //A cleared-room reward is created at runtime, so the fresh
            //descriptor has no preplanned actor to match. Reconstruct the
            //saved type5 identity through the same construction owner.
            NativePoint at;at.x=value.x;at.y=value.y;
            pickup=new NativePickup;
            if(!pickup->Begin(value.type,value.variant,value.subtype,value.grid,value.initialSeed,0,at)){
                for(unsigned int i=0;i<created.size();++i)delete created[i];
                delete pickup;return false;
            }
            created.push_back(pickup);
        }
        result.push_back(pickup);
    }
    //Validate every saved identity before changing any resident state.
    for(unsigned int s=0;s<saved.size();++s){
        const NativeSaveSummary::Pickup& value=saved[s];
        NativePickup* pickup=result[s];
        pickup->type=value.type;pickup->variant=value.variant;pickup->subtype=value.subtype;
        pickup->grid=value.grid;pickup->currentSeed=value.currentSeed;pickup->initialSeed=value.initialSeed;
        pickup->position.x=value.x;pickup->position.y=value.y;
        pickup->native46c=value.native46c;pickup->native478=value.native478;pickup->native480=value.native480;
        pickup->native484=value.native484;pickup->native27c=value.native27c;
        pickup->native474=value.native474;pickup->native476=value.native476;
        pickup->native4cc=value.native4cc;pickup->native470=value.native470;
        pickup->native34=value.native34;pickup->native38=value.native38;
        pickup->native360=value.native360;pickup->native4e0=value.native4e0;
        pickup->native47c=value.native47c;pickup->nativeExtraCount=value.nativeExtraCount;
        pickup->nativeChildCount=value.nativeChildCount;pickup->nativeD8=value.nativeD8;
        pickup->collected=false;
    }
    //Consumed preplanned actors are absent from the saved list; live entries
    //not named by the save are removed. Created rewards replace the list in
    //native saved order.
    for(int i=0;i<originalCount;++i)if(!used[i]){delete live.entries[i];live.entries[i]=0;}
    for(int i=0;i<64;++i)live.entries[i]=0;
    for(unsigned int i=0;i<result.size();++i)live.entries[i]=result[i];
    live.count=(int)result.size();return true;
}
bool ApplySavedGrids(const NativeSaveSummary::SavedRoom& saved,NativeRoomState& room,unsigned int frame,unsigned int* nextAddress){
    if((int)saved.grids.size()!=kNativeRoomGridLimit||saved.grid<0||saved.grid>=kNativeTopologyCells)return false;
    room.staticGridCount=0;room.decorationCount=0;room.collision.Clear();
    for(int cell=0;cell<kNativeRoomGridLimit;++cell){
        const NativeSaveSummary::Grid& value=saved.grids[cell];
        if(cell>=room.geometry.cells){
            if(value.type||value.variant||value.state||value.count||value.seed||value.auxiliary||value.tail){
                if(nextAddress)*nextAddress=0x7a1130;return false;
            }
            continue;
        }
        if(!value.type){
            if(value.variant||value.state||value.count||value.seed||value.auxiliary||value.tail){
                if(nextAddress)*nextAddress=0x7a1130;return false;
            }
            continue;
        }
        if(value.type==16)continue; //Door actors are reconstructed with their owner.
        if(value.type==1){
            //8df570's Init/derived-animation work is not yet reconstructed for
            //a saved decoration. Stopping preserves the record rather than
            //inventing animation0.
            if(nextAddress)*nextAddress=0x8df570;return false;
        }
        const int raw=NativeRawGridTypeForVariant(value.type,value.variant);
        if(!raw){
            if(nextAddress)*nextAddress=0x7a1166;
            return false;
        }
        if(room.staticGridCount>=kNativeRoomGridLimit){if(nextAddress)*nextAddress=0x7a1166;return false;}
        NativeStaticGrid& grid=room.staticGrids[room.staticGridCount++];
        if(!NativeRestoreSerializedGrid(cell,value.type,value.variant,value.state,value.count,value.seed,
            value.tail,value.auxiliary,frame,&grid)){if(nextAddress)*nextAddress=0x7a1166;return false;}
        const bool wall=value.type==15;const bool poop=value.type==14;
        if(value.type==6&&(value.variant!=0||value.state!=0)){if(nextAddress)*nextAddress=0x7af16a;return false;}
        if(!room.collision.Set(cell,true,grid.collisionClass,
            wall?1000:((value.type==21||value.type==24)?1000:((value.type==6||poop)?1500:0)),wall?15:raw)){
            if(nextAddress)*nextAddress=0x7a1166;return false;
        }
    }
    return true;
}
}
NativeWorld::NativeWorld(){memset(resident,0,sizeof(resident));Reset();}
NativeWorld::~NativeWorld(){Reset();}
void NativeWorld::Reset() {
    for(int i=0;i<kNativeWorldRoomLimit;++i){resident[i].enemies.Clear();resident[i].pickups.Clear();delete resident[i].room;delete resident[i].doors;delete resident[i].effects;delete resident[i].view;delete[] resident[i].doorVisuals;delete resident[i].persistent;delete resident[i].update;memset(&resident[i],0,sizeof(resident[i]));}
    layout.Reset();currentList=previousList=currentGrid=previousGrid=-1;residentCount=0;entrySerial=0;
    restoredRooms.clear();
}
bool NativeWorld::Bind(const NativeLevelLayout& selected) {
    if(selected.count<1||selected.count>507||selected.currentDimension<0||selected.currentDimension>2)return false;
    int owners[3][169];for(int d=0;d<3;++d)for(int i=0;i<169;++i)owners[d][i]=-1;
    for(int i=0;i<selected.count;++i) {
        const NativePlacedRoom& p=selected.rooms[i];const NativeRoomDef* d=NativeRoom(p.definition);
        if(!d||(NativeRoomCatalog(p.definition)!=NC_NORMAL&&NativeRoomCatalog(p.definition)!=NC_GREED)||p.listIndex!=i||p.dimension<0||p.dimension>2||p.gridIndex<0||p.gridIndex>=169||!p.placementSeed)return false;
        NativeCell offsets[4];int n=NativeShapeFootprint(d->shape,offsets);if(!n)return false;
        for(int k=0;k<n;++k){NativeCell at={p.gridIndex%13+offsets[k].x,p.gridIndex/13+offsets[k].y};int cell=NativeCellIndex(at);
            if(cell<0||owners[p.dimension][cell]>=0||selected.owners[p.dimension][cell]!=i)return false;owners[p.dimension][cell]=i;}
    }
    for(int d=0;d<3;++d)for(int cell=0;cell<169;++cell){int owner=selected.owners[d][cell];
        if(owner!=owners[d][cell])return false;}
    for(int i=0;i<18;++i){const NativePlacedRoom& p=selected.offGrid[i];if(p.gridIndex!=-i-1||
        (p.definition>=0&&((NativeRoomCatalog(p.definition)!=NC_NORMAL&&NativeRoomCatalog(p.definition)!=NC_GREED)||!NativeRoom(p.definition))))return false;}
    NativeLevelLayout* copy=new NativeLevelLayout(selected);Reset();layout=*copy;delete copy;return true;
}
void NativeWorld::Swap(NativeWorld& other) {
    NativeLevelLayout* copy=new NativeLevelLayout(layout);layout=other.layout;other.layout=*copy;delete copy;
    for(int i=0;i<kNativeWorldRoomLimit;++i){NativeWorldResident r=resident[i];resident[i]=other.resident[i];other.resident[i]=r;}
    int n=currentList;currentList=other.currentList;other.currentList=n;n=previousList;previousList=other.previousList;other.previousList=n;
    n=currentGrid;currentGrid=other.currentGrid;other.currentGrid=n;n=previousGrid;previousGrid=other.previousGrid;other.previousGrid=n;
    n=residentCount;residentCount=other.residentCount;other.residentCount=n;unsigned int serial=entrySerial;entrySerial=other.entrySerial;other.entrySerial=serial;
    restoredRooms.swap(other.restoredRooms);
}
NativeWorldResult NativeWorld::Start(const NativeLevelLayout& selected,int grid,int dimension,const NativeSceneFiles& files,NativeWorldIssue* issue,
    const NativeRoomSetupContext* setup,const std::vector<NativeSaveSummary::SavedRoom>* restored) {
    NativeWorld* next=new NativeWorld;if(!next->Bind(selected)){delete next;return Result(issue,NWR_BAD_REQUEST);}
    if(restored&&!restored->empty()){
        NativeSaveSummary summary;summary.savedRooms=*restored;
        if(!next->InstallSavedRooms(summary,issue)){delete next;return Result(issue,NWR_BAD_REQUEST);}
    }
    NativeWorldResult result=next->Enter(grid,dimension,files,issue,setup);
    if(result==NWR_OK){next->previousGrid=next->currentGrid;next->previousList=next->currentList;Swap(*next);}delete next;return result;
}
NativeWorldResult NativeWorld::Enter(int cell,int dimension,const NativeSceneFiles& files,NativeWorldIssue* issue,const NativeRoomSetupContext* setup) {
    if(issue){memset(issue,0,sizeof(*issue));issue->listIndex=issue->gridIndex=issue->dimension=issue->definition=-1;}
    if(dimension<0||dimension>2||files.nativeMode<0||files.nativeMode>3)return Result(issue,NWR_BAD_REQUEST);
    int waterMode;if(setup&&(setup->backdrop!=files.backdrop||setup->configStage<0||!NativeBackdropWaterPitsMode(setup->backdrop,&waterMode)))return Result(issue,NWR_BAD_REQUEST);
    const NativePlacedRoom* p=layout.ByCell(cell,dimension);if(!p||p->definition<0)return Result(issue,NWR_NO_ROOM);
    int id=p->gridIndex<0?507-p->gridIndex-1:p->listIndex;
    if(id<0||id>=kNativeWorldRoomLimit)return Result(issue,NWR_BAD_REQUEST,p);
    const NativeSaveSummary::SavedRoom* restored=RestoredRoom(id);
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
        resident[id].enemies.Clear();resident[id].pickups.Clear();
        source=opened?NativeSceneBuildRoom(record,p->seeds,files.backdrop,files.nativeMode,stage,room,&detail,&markers,files.frame,
            &resident[id].enemies,&resident[id].pickups,restored!=0):NSC_SOURCE_ERROR;
        if(source==NSC_OK&&setup&&!NativeSetupRoom(record.type,p->flags,p->visitedCount,p->seeds,markers,*setup,&setupState))source=NSC_BAD_REQUEST;
        delete[] raw;if(issue){issue->source=source;if(opened)issue->construction=detail;}
        if(source!=NSC_OK){resident[id].enemies.Clear();resident[id].pickups.Clear();delete room;return Result(issue,source==NSC_SOURCE_ERROR?NWR_SOURCE_ERROR:
            (source==NSC_UNSUPPORTED_CONTENT||source==NSC_UNSUPPORTED_SHAPE||source==NSC_UNSUPPORTED_EXTENT)?NWR_CONSTRUCTION_GAP:NWR_BAD_REQUEST,p);}
        if(restored){
            unsigned int restoreAddress=0;
            if(!ApplySavedGrids(*restored,*room,files.frame,&restoreAddress)||
               !ApplySavedPickups(restored->pickups,resident[id].pickups,room->geometry.cells)){
                if(issue){issue->result=NWR_BAD_REQUEST;issue->source=source;issue->construction=detail;issue->nativeNext=restoreAddress;}
                resident[id].enemies.Clear();resident[id].pickups.Clear();delete room;
                return Result(issue,NWR_BAD_REQUEST,p);
            }
            room->clear=(restored->flags&1U)!=0;
            room->rewardResolved=room->clear;room->rewardVariant=room->rewardSubtype=0;
            //A cleared boss room's type9000 cell is already open in the saved
            //state; the save's clear bit is the ownership record.
            if(room->clear&&(room->type==5||room->type==17)&&room->trapdoorCell>=0){
                room->trapdoorState=2;room->trapdoorTimer=30;
            }
            for(unsigned int i=0;i<restored->entities.size();++i){
                const NativeSaveSummary::Entity& entity=restored->entities[i];
                const unsigned int type=entity.fields[0x00],variant=entity.fields[0x04],subtype=entity.fields[0x08];
                if(type==5)continue;
                if(type==1000&&subtype==0&&(variant==33||variant==68))continue;
                if(issue){issue->result=NWR_CONSTRUCTION_GAP;issue->source=source;issue->construction=detail;
                    issue->nativeNext=type==1000?0x79d0b0:0x79daf0;}
                resident[id].enemies.Clear();resident[id].pickups.Clear();delete room;
                return Result(issue,NWR_CONSTRUCTION_GAP,p);
            }
            if(resident[id].enemies.count){
                //Saved entity records outside the reached type-5 path are not
                //reconstructed yet. Do not retain regenerated combatants from
                //a room whose saved entity snapshot was empty.
                if(issue){issue->result=NWR_CONSTRUCTION_GAP;issue->source=source;
                    issue->construction=detail;}
                resident[id].enemies.Clear();resident[id].pickups.Clear();delete room;
                return Result(issue,NWR_CONSTRUCTION_GAP,p);
            }
        }
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
    const NativeSaveSummary::SavedRoom* restored=RestoredRoom(currentList);
    if(restored){
        r.restoreAddress=0x7a1130;
        NativeDoorSet* doors=new NativeDoorSet;memset(doors,0,sizeof(*doors));
        for(int slot=0;slot<8;++slot)if(!NativeDoorGrid(room->shape,room->geometry,slot,&doors->potentialGrid[slot])){
            delete doors;return NDR_BAD_REQUEST;
        }
        bool savedDoorSlot[8]={false,false,false,false,false,false,false,false};
        for(int cell=0;cell<kNativeRoomGridLimit;++cell){
            if(restored->grids[cell].type!=16)continue;
            bool matched=false;
            for(int slot=0;slot<8;++slot)if((descriptor->doors&(1U<<slot))&&doors->potentialGrid[slot]==cell){
                if(savedDoorSlot[slot]){delete doors;r.restoreAddress=0x7a1130;return NDR_BAD_REQUEST;}
                savedDoorSlot[slot]=true;matched=true;break;
            }
            if(!matched||cell>=room->geometry.cells){delete doors;r.restoreAddress=0x7a1130;return NDR_BAD_REQUEST;}
        }
        for(int slot=0;slot<8;++slot)if(descriptor->doors&(1U<<slot)){
            const int cell=doors->potentialGrid[slot];
            const NativeSaveSummary::Grid* source=cell>=0&&cell<(int)restored->grids.size()?&restored->grids[cell]:0;
            if(!source||source->type!=16||!savedDoorSlot[slot]){delete doors;r.restoreAddress=0x7a1130;return NDR_BAD_REQUEST;}
            NativeDoorActor saved;memset(&saved,0,sizeof(saved));
            saved.grid=cell;saved.type=16;saved.variant=source->variant;saved.state=source->state;
            saved.auxiliary=source->auxiliary; saved.tail=source->tail;
            saved.initializations=source->count;saved.seed=source->seed;
            if(NativeCreateDoor(slot,cell,frame,owner,doors,&saved)!=NDR_OK){
                delete doors;r.restoreAddress=0x7a1130;return NDR_BAD_REQUEST;
            }
            doors->actors[slot].x=(float)(cell%room->geometry.stride)*40.0f+40.0f;
            doors->actors[slot].y=(float)(cell/room->geometry.stride)*40.0f+120.0f;
            if(!room->collision.Set(cell,true,0,1000,16)){delete doors;r.restoreAddress=0x7a1130;return NDR_BAD_REQUEST;}
        }
        r.doors=doors;r.restoreAddress=0;return NDR_OK;
    }
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
NativeDoorResult NativeWorld::PrepareDoorVisuals(int stage,int stageType,int nativeMode,int challenge,const NativePlayerState& player) {
    NativeRoomState* room=Current();const NativePlacedRoom* p=CurrentDescriptor();
    if(!room||!p||currentList<0||currentList>=kNativeWorldRoomLimit||!resident[currentList].doors)return NDR_BAD_REQUEST;
    NativeWorldResident& live=resident[currentList];
    live.doorFailureSlot=-1;live.doorFailureKind=0;
    if(!live.doorVisuals)live.doorVisuals=new NativeDoorVisual[8];
    for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){
        NativeDoorVisual& v=live.doorVisuals[slot];if(v.ready)continue;
        const NativePlacedRoom* target=layout.DoorTarget(currentList,slot);
        const NativeRoomDef* d=target?NativeRoom(target->definition):0;if(!d)return NDR_MISSING_TARGET;
        NativeDoorBinding c={0};c.sourceType=room->type;c.targetType=d->type;
        c.sourceVariant=room->variant;c.targetVariant=d->variant;c.sourceFlags=p->flags;c.targetFlags=target->flags;
        c.stage=stage;c.stageType=stageType;c.nativeMode=nativeMode;
        c.currentGrid=currentGrid;c.targetGrid=p->doorTargets[slot];c.dimension=layout.currentDimension;
        c.treasureGrid=-1;c.generatedRoomCount=layout.count;c.challenge=challenge;c.sourceData=c.targetData=true;
        NativeDoorResult selected=SelectDoorVariant(slot,c);
        if(selected!=NDR_OK){
            live.doorFailureSlot=slot;live.doorFailureKind=selected;
            live.doorFailureVariant=live.doors->actors[slot].variant;
            live.doorFailureAux=live.doors->actors[slot].auxiliary;
            ++live.doorVisualGaps;continue;
        }
        if(!v.BindFresh(live.doors->actors[slot],live.backdrop,p->flags,target->flags,player)){
            live.doorFailureSlot=slot;live.doorFailureKind=live.doors->actors[slot].targetType;
            live.doorFailureVariant=live.doors->actors[slot].variant;live.doorFailureAux=live.doors->actors[slot].auxiliary;
            //The selected target rules remain owned by the actor. Missing
            //presentation art is an explicit visual gap, not a reason to
            //leave every later door unselected.
            ++live.doorVisualGaps;continue;
        }
    }
    return NDR_OK;
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
NativePlacedRoom* NativeWorld::CurrentDescriptor() {
    if(currentList<0||currentList>=kNativeWorldRoomLimit)return 0;
    return layout.ByList(currentList);
}
bool NativeWorld::InstallSavedRooms(const NativeSaveSummary& summary,NativeWorldIssue* issue){
    std::vector<NativeSaveSummary::SavedRoom> next;
    std::vector<int> seen(kNativeWorldRoomLimit,-1);
    for(unsigned int i=0;i<summary.savedRooms.size();++i){
        const NativeSaveSummary::SavedRoom& saved=summary.savedRooms[i];
        //The serialized visit owner is the authority that distinguishes an
        //unmodified descriptor record from a captured live room. A matching
        //current descriptor with visit0 is still fresh and must regenerate.
        if(!saved.visitedCount)continue;
        if(saved.grids.size()!=kNativeRoomGridLimit||saved.listIndex<0||saved.listIndex>=kNativeWorldRoomLimit){
            if(issue)issue->nativeNext=0x7a1130;Result(issue,NWR_BAD_REQUEST);return false;
        }
        NativePlacedRoom* descriptor=layout.ByList(saved.listIndex);
        const NativeRoomDef* definition=descriptor?NativeRoom(descriptor->definition):0;
        if(!descriptor||!definition||descriptor->gridIndex!=saved.grid||descriptor->safeGridIndex!=saved.safeGrid||
           descriptor->listIndex!=saved.listIndex||descriptor->dimension!=saved.dimension||
           definition->stage!=saved.definitionStage||definition->type!=saved.definitionType||
           (int)definition->variant!=saved.definitionVariant||seen[saved.listIndex]>=0){
            if(issue)issue->nativeNext=0x7a1130;Result(issue,NWR_BAD_REQUEST,descriptor);return false;
        }
        for(int cell=0;cell<kNativeRoomGridLimit;++cell){
            const int type=saved.grids[cell].type;
            if(type&&type!=1&&type!=16&&!NativeRawGridTypeForVariant(type,saved.grids[cell].variant)){
                if(issue)issue->nativeNext=0x7a1166;Result(issue,NWR_BAD_REQUEST,descriptor);return false;
            }
        }
        seen[saved.listIndex]=(int)next.size();
        next.push_back(saved);
    }
    //No world metadata is changed until every visited serialized descriptor
    //and every reached grid factory ID has been validated.
    for(unsigned int i=0;i<next.size();++i){
        NativePlacedRoom* descriptor=layout.ByList(next[i].listIndex);
        descriptor->visitedCount=next[i].visitedCount;
        descriptor->displayFlags=next[i].displayFlags;
        descriptor->flags=next[i].flags;
        descriptor->native48=next[i].native48;
        descriptor->clearEventCount=next[i].clearEventCount;
    }
    restoredRooms=next;return true;
}
const NativeSaveSummary::SavedRoom* NativeWorld::RestoredRoom(int listIndex) const {
    for(unsigned int i=0;i<restoredRooms.size();++i)
        if(restoredRooms[i].listIndex==listIndex)return &restoredRooms[i];
    return 0;
}
bool NativeWorld::CurrentRoomRestored() const {return currentList>=0&&RestoredRoom(currentList)!=0;}
NativeWorldResult NativeWorld::RecordActivation(NormalRunFlow& run) {
    NativeRoomState* room=Current();NativePlacedRoom* descriptor=CurrentDescriptor();
    if(!room||!descriptor||room->type>=30)return NWR_BAD_REQUEST;
    //7a3b8a: the visit count and type statistic are unconditional on a
    //successfully initialized room. The clear bit remains conditional.
    ++descriptor->visitedCount;++run.roomTypeVisits[room->type];
    if(room->clear)descriptor->flags|=1U;
    resident[currentList].setup.flags=descriptor->flags;
    return NWR_OK;
}
void NativeWorld::SyncCurrentRoomClear() {
    NativeRoomState* room=Current();NativePlacedRoom* descriptor=CurrentDescriptor();
    if(room&&descriptor&&room->clear)descriptor->flags|=1U;
}
const NativeWorldResident* NativeWorld::At(int i) const {return i>=0&&i<kNativeWorldRoomLimit?resident+i:0;}
