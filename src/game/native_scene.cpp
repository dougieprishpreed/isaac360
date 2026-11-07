#include "native_scene.h"
#include "native_decoration_data.h"
#include "../content/game_content.h"
#include "../content/port_data.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
namespace {
enum Factory { FACTORY_MISSING,FACTORY_MARKER,FACTORY_DECORATION,FACTORY_WALL,FACTORY_ENEMY,FACTORY_ROCK,FACTORY_PIT,FACTORY_PICKUP,FACTORY_TRAPDOOR };
Factory SelectedFactory(const RoomRecord& record,const NativeSpawnChoice& choice,int backdrop) {
    const RoomRecordOption& e=choice.entry;
    if(e.type==970)return FACTORY_MARKER;
    //Grid type9000 variant0 is the room-record trapdoor owner. Its cell and
    //constructed state are preserved; opening remains gated by the boss clear.
    if(e.type==9000&&e.variant==0&&e.subtype==0)return FACTORY_TRAPDOOR;
    //Inherited ordinary actor records with maintained physical factories.
    //Enemy subtype is the champion/colour selector; the physical body remains
    //owned by type/variant and is stored with the live actor.
    if(NativePreparedEnemyBody(e.type,e.variant))return FACTORY_ENEMY;
    //Ordinary rock/block grids: solid static obstacles without destruction yet.
    //Ordinary rocks, poop variants, pillars and teleporters are static grids.
    //Raw room records normalize to the saved GridEntityType:
    //1490/1495/1496/1494/1497/1500 -> class14 poop variants1/2/3/4/5/0,
    //1901 -> class24 pillar, 5000/5001 -> class21 teleporter variants0/1.
    if((e.type==1000||e.type==1001||e.type==1002||e.type==1008||e.type==1010||
        e.type==1490||e.type==1494||e.type==1495||e.type==1496||e.type==1497||
        e.type==1500||e.type==1400||e.type==1901||e.type==5000||e.type==5001)&&e.subtype==0)return FACTORY_ROCK;
    //Room grid markers and later stage grid families are accepted as
    //construction records. Their original art and collision rules remain
    //separate services.
    if((e.type==3000||e.type==1300||e.type==1900||e.type==1930||e.type==1931||e.type==1940||e.type==1410||e.type==4500||e.type==4000)&&e.subtype==0)return FACTORY_PIT;
    //0079c1b3 maps1999 to grid15. Ordinary wall Init has no asset/MT calls;
    //room16's graphics path remains an explicit dependency.
    if(e.type==1999&&record.type!=16)return FACTORY_WALL;
    //Type0 variant10/20/30 decorations are owned by the stage backdrop's
    //props animation set. Native mode does not select a different props file;
    //backdrops without a props set remain explicit unsupported content.
    if(e.type==0&&(e.variant==10||e.variant==20||e.variant==30)&&e.subtype==0&&
       NativeDecorationAnimationCount(backdrop))return FACTORY_DECORATION;
    //Room-spawn type5 owns pickup identity/position. Variant0 is the ordinary
    //random-pickup request; the native reward selector remains a later service.
    if(e.type==5)return FACTORY_PICKUP;
    return FACTORY_MISSING;
}
}
NativeSceneResult NativeSceneInspectPlan(const RoomRecord& record,const NativeSpawnPlan& plan,int backdrop,int mode,NativeSceneDependencies* output) {
    if(!output||record.format!=RRF_STB1||plan.count<0||plan.count>kNativeRoomGridLimit||plan.count!=(int)record.stackCount||backdrop<0||mode<0||mode>3)return NSC_BAD_REQUEST;
    NativeSceneDependencies next;memset(&next,0,sizeof(next));
    for(int i=0;i<plan.count;++i){const NativeSpawnChoice& c=plan.choices[i];if(!c.present)continue;
        if(c.grid<0||c.grid>=kNativeRoomGridLimit||!c.seed)return NSC_BAD_REQUEST;
        if(SelectedFactory(record,c,backdrop)!=FACTORY_MISSING){++next.supported;continue;}
        NativeSceneDependency& d=next.missing[next.count++];d.choice=i;d.grid=c.grid;d.seed=c.seed;
        d.type=c.entry.type;d.variant=c.entry.variant;d.subtype=c.entry.subtype;
    }
    *output=next;return NSC_OK;
}
NativeSceneResult NativeSceneReadRecord(const NativeSceneFiles& files,int definition,unsigned char** bytes,unsigned int* length,unsigned int* recordHash) {
    const NativeRoomDef* d=NativeRoom(definition);
    if(!d || !NativeRoomSourceBinding(d->source) || !files.indexPath || !files.objectRoot || !bytes || !length || !recordHash) return NSC_BAD_REQUEST;
    //One content index per data root, loaded once (src/content/port_data.h); the
    //room file read last stays cached, since consecutive records share a file.
    const GpcResourceIndex* index=PortDataContentIndex(files.indexPath);if(!index) return NSC_SOURCE_ERROR;
    GpcEntry e;
    const NativeRoomSourceDef* binding=NativeRoomSourceBinding(d->source);
    GpcResult found=index->FindOriginal(binding->request,&e);
    if(found==GPC_OK&&binding&&(e.package!=binding->package||memcmp(e.sha256,binding->sha256,32)))return NSC_SOURCE_ERROR;
    if(found!=GPC_OK || e.size>1024*1024 || d->offset<8 || d->offset>e.size || d->bytes>e.size-d->offset) return NSC_SOURCE_ERROR;
    std::string path=std::string(files.objectRoot)+e.objectPath;
    static std::string cachedPath;static std::vector<unsigned char> cached;
    if(path!=cachedPath){
        cachedPath.clear();
        if(!PortDataRead(path.c_str(),cached,1024*1024)) return NSC_SOURCE_ERROR;
        cachedPath=path;
    }
    bool ok=cached.size()>=8 && cached.size()==e.size && !memcmp(&cached[0],"STB1",4);
    if(!ok) return NSC_SOURCE_ERROR;
    unsigned char* raw=new unsigned char[d->bytes];
    memcpy(raw,&cached[d->offset],d->bytes);
    unsigned int hash=2166136261U;for(unsigned int i=0;i<d->bytes;++i) hash=(hash^raw[i])*16777619U;
    *bytes=raw;*length=d->bytes;*recordHash=hash?hash:1;return NSC_OK;
}
NativeSceneResult NativeSceneBuildRoom(const RoomRecord& record,const NativeRoomSeeds& seeds,int backdrop,int nativeMode,int configStage,
    NativeRoomState* output,NativeConstructionIssue* issue,NativeRoomMarkers* markers,unsigned int frame,NativeEnemyList* enemies,
    NativePickupList* pickups,bool restoreStaticGrids) {
    NativeConstructionIssue detail={NCS_HEADER,-1,0,0,0,NRS_BAD_DATA};if(issue)*issue=detail;
    if(!output||backdrop<0||nativeMode<0||nativeMode>3||configStage<0||!seeds.spawn||!seeds.award)return NSC_BAD_REQUEST;
    if(record.format!=RRF_STB1){detail.stage=NCS_FORMAT;if(issue)*issue=detail;return NSC_UNSUPPORTED_CONTENT;}
    NativeRoomState room;memset(&room,0,sizeof(room));room.type=record.type;room.variant=record.variant;room.shape=record.shape;
    if(!room.geometry.Init(record.shape,record.width,record.height))return NSC_UNSUPPORTED_SHAPE;
    if(!room.space.Init(record.shape,room.geometry,configStage,record.type))return NSC_BAD_REQUEST;
    room.collision.Clear(); //007a10cf..10dd clears all448 native+76c entries.
    room.native6f60=-1;room.native11ec=0;
    room.native12c0=0;room.native12c4=0;room.native7130=-1;
    room.rewardResolved=false;room.rewardVariant=room.rewardSubtype=0;
    room.bossDead=false;room.list9Count=0;room.trapdoorCell=-1;
    room.native1210=0; //Host storage; gameplay caller writes7a2cc3 before reading.
    room.particlesEnabled=false;
    for(int i=0;i<8;++i)room.native7628[i]=0;
    bool seen[8]={false};
    for(unsigned int i=0;i<record.doorCount;++i){RoomRecordDoor door;if(!record.Door(i,&door))return NSC_SOURCE_ERROR;int slot=-1;
        for(int j=0;j<8;++j){NativeCell at;if(NativeShapeStbDoor(record.shape,j,&at)&&at.x==door.x&&at.y==door.y)slot=j;}
        if(slot<0||seen[slot])return NSC_SOURCE_ERROR;seen[slot]=true;room.doors[slot]=door.existsByte!=0;}
    NativeSpawnPlan* plan=new NativeSpawnPlan;detail.stage=NCS_PLAN;detail.plan=NativePlanRoomSpawns(record,seeds,plan);if(issue)*issue=detail;
    if(detail.plan!=NRS_OK){delete plan;return NSC_SOURCE_ERROR;}
    for(int i=0;i<plan->count;++i){const NativeSpawnChoice& choice=plan->choices[i];if(!choice.present)continue;
        const RoomRecordOption& entry=choice.entry;
        //0079b7b8:970 has only the preliminary setup actions, no main actor.
        Factory factory=SelectedFactory(record,choice,backdrop);
        if(factory==FACTORY_MARKER)continue;
        detail.stage=NCS_ENTRY;detail.choice=i;detail.type=entry.type;detail.variant=entry.variant;detail.subtype=entry.subtype;if(issue)*issue=detail;
        if(factory==FACTORY_ENEMY){
            //A restored descriptor's saved entity vector is authoritative.
            //Regenerating combatants would substitute actors into a cleared
            //room; unsupported saved non-pickup entities stop explicitly in
            //NativeWorld::Enter instead.
            if(restoreStaticGrids)continue;
            if(!enemies||enemies->count>=64){delete plan;return NSC_UNSUPPORTED_CONTENT;}
            float px,py;NativePoint position;
            if(!room.geometry.Position(choice.grid,&px,&py)){delete plan;return NSC_UNSUPPORTED_EXTENT;}
            position.x=px+20.0f;position.y=py+20.0f;
            NativeEnemy* enemy=new NativeEnemy;
            if(!enemy->Begin((int)entry.type,(int)entry.variant,(int)entry.subtype,choice.seed,position,frame,configStage)){
                delete enemy;delete plan;return NSC_BAD_REQUEST;
            }
            enemies->entries[enemies->count++]=enemy;
            //00414bc0 increments the container's +a8 for an active main NPC.
            //Service NPCs (machines, beggars, shopkeepers) keep their physical
            //body but do not own the hostile clear encounter.
            if(enemy->participates){++room.native12c0;room.encounter=true;}
            continue;
        }
        if(factory==FACTORY_TRAPDOOR){
            //The reached room state owns one boss-exit cell. Additional
            //type9000 records remain selected content but are not collapsed
            //into that single owner; do not reject the whole room.
            if(room.trapdoorCell>=0)continue;
            float px,py;
            if(!room.geometry.Position(choice.grid,&px,&py)){delete plan;return NSC_UNSUPPORTED_EXTENT;}
            room.trapdoorCell=choice.grid;room.trapdoorX=px+20.0f;room.trapdoorY=py+20.0f;
            room.trapdoorState=0;room.trapdoorTimer=0;
            continue;
        }
        if(factory==FACTORY_PICKUP){
            //The saved type5 vector is authoritative on restore; runtime
            //rewards have no preplanned scene actor at all.
            if(restoreStaticGrids)continue;
            //Preserve the selected spawn identity and cell. Pickups do not
            //block movement or make the room an encounter. Collection,
            //reward selection and the complete variant table are separate.
            if(pickups&&pickups->count<64){
                float px,py;NativePoint position;
                if(!room.geometry.Position(choice.grid,&px,&py)){delete plan;return NSC_UNSUPPORTED_EXTENT;}
                position.x=px+20.0f;position.y=py+20.0f;
                int variant=(int)entry.variant;
                if(variant==0)variant=NativePickupVariant(record.type,choice.seed);
                NativePickup* pickup=new NativePickup;
                if(!pickup->Begin((int)entry.type,variant,(int)entry.subtype,choice.grid,choice.seed,frame,position)){
                    delete pickup;delete plan;return NSC_BAD_REQUEST;}
                pickups->entries[pickups->count++]=pickup;
            }
            continue;
        }
        //A restored descriptor supplies grids as authoritative saved records.
        //Do not instantiate the raw STB grid and erase it after the fact.
        if(restoreStaticGrids&&(factory==FACTORY_ROCK||factory==FACTORY_WALL||factory==FACTORY_DECORATION))continue;
        if(factory==FACTORY_ROCK){
            if(room.collision.occupied[choice.grid])continue;
            NativeStaticGrid& rock=room.staticGrids[room.staticGridCount];
            rock.grid=choice.grid;rock.type=(int)entry.type;
            //Retail GRIDDUMP: class21 teleporters and class24 pillars spawn
            //in state1; poop and ordinary rocks spawn in state0.
            const bool teleporter=entry.type==5000||entry.type==5001;
            const bool pillar=entry.type==1901;
            rock.state=(teleporter||pillar)?1:0;
            if(pillar){
                //6d3632..3682: variant = xorshift(descriptorSeed + gridCell)
                //modulo the pillar animation count (2). This is the same
                //local5/9/7 owner used by room decorations.
                unsigned int local=seeds.spawn+(unsigned int)choice.grid;
                local=NativeShift(local,5,9,7);
                rock.variant=(int)(local%2U);
            }else{
                rock.variant=NativeSerializedGridVariant((int)entry.type,entry.variant);
            }
            rock.nativeType=NativeSerializedGridType((int)entry.type);
            if(!rock.nativeType){delete plan;return NSC_UNSUPPORTED_CONTENT;}
            //006c5720 runs virtual Init and then006c573d increments native+10.
            //This named value is the post-Init count serialized by90c7a7; it is
            //not the separate +1c initialized byte.
            const bool poop=rock.nativeType==14;
            rock.initializations=1;rock.collisionClass=(rock.nativeType==6||poop)?3:(rock.nativeType==21?0:4);
            rock.seed=choice.seed;rock.native18=0;
            //006c5670 initializes common+native20 to16a9de81. The reached
            //ordinary type4 Init6d8a40 and class14 poop Init6ced40 do not
            //replace that tail, so fresh rocks must not agree with a guessed
            //zero.
            rock.native20=(rock.nativeType==4||rock.nativeType==6||poop||rock.nativeType==21||rock.nativeType==24)?0x16a9de81U:0;
            rock.initializedFrame=frame;rock.initialized=true;
            //Retail GRIDDUMP: teleporter PATH=1000; poop starts at1500 and
            //decays through the room's path-cost update; other new grids 0.
            ++room.staticGridCount;room.collision.Set(choice.grid,true,rock.collisionClass,
                rock.nativeType==21?1000:((rock.nativeType==6||poop)?1500:0),(int)entry.type);continue;
        }
        if(factory==FACTORY_WALL){
            //0079b508 refuses a grid already occupied by an earlier stack.
            //Decoration props also occupy native+24 even though noncolliding.
            if(room.collision.occupied[choice.grid])continue;
            NativeStaticGrid& wall=room.staticGrids[room.staticGridCount];
            if(!NativeSelectedWall(record.type,choice.grid,entry.variant,entry.subtype,choice.seed,frame,&wall)){delete plan;return NSC_BAD_REQUEST;}
            ++room.staticGridCount;room.collision.Set(choice.grid,true,wall.collisionClass,1000,15);continue;
        }
        if(factory==FACTORY_PIT){
            //Pit grids are accepted without a drawn/colliding static object
            //until their original pit graphics and fall rule are implemented.
            continue;
        }
        if(factory==FACTORY_DECORATION){
            if(room.collision.occupied[choice.grid])continue;
            NativeDecoration& prop=room.decorations[room.decorationCount];int x,y;
            if(room.geometry.Tile(choice.grid,&x,&y)&&NativeRoomDecoration(seeds.spawn,room.geometry,choice.grid,backdrop,&prop.seed,&prop.animation)){
                prop.cell=choice.grid;prop.variant=entry.variant;prop.initialSeed=choice.seed;
                ++room.decorationCount;room.collision.Set(choice.grid,true,0,0,1);continue;}
            bool extent=choice.grid>=room.geometry.cells;delete plan;return extent?NSC_UNSUPPORTED_EXTENT:NSC_BAD_REQUEST;
        }
        //Keep the selected identity. A native actor/grid factory is required;
        //approximate movement, HP, rewards or item effects are not substitutes.
        delete plan;return NSC_UNSUPPORTED_CONTENT;
    }
    if(markers)*markers=plan->markers;room.clear=!room.encounter;delete plan;detail.stage=NCS_READY;if(issue)*issue=detail;*output=room;return NSC_OK;
}
