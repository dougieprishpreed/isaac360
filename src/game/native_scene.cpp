#include "native_scene.h"
#include "native_decoration_data.h"
#include "../content/game_content.h"
#include <stdio.h>
#include <string.h>
namespace {
enum Factory { FACTORY_MISSING,FACTORY_MARKER,FACTORY_DECORATION,FACTORY_WALL,FACTORY_ENEMY,FACTORY_ROCK,FACTORY_PICKUP };
Factory SelectedFactory(const RoomRecord& record,const NativeSpawnChoice& choice,int backdrop,int mode) {
    const RoomRecordOption& e=choice.entry;
    if(e.type==970)return FACTORY_MARKER;
    //Inherited ordinary actor records with maintained physical factories.
    if(e.subtype==0&&((e.type==10&&(e.variant==0||e.variant==1))||(e.type==13&&e.variant==0)||
        (e.type==14&&e.variant==0)||(e.type==208&&e.variant==0)||
        (e.type==226&&(e.variant==0||e.variant==1))||
        (e.type==284&&e.variant==0)))return FACTORY_ENEMY;
    //Ordinary rock/block grids: solid static obstacles without destruction yet.
    if((e.type==1000||e.type==1002)&&e.variant==0&&e.subtype==0)return FACTORY_ROCK;
    //1500 poop and1400 fireplace are static grids, not entity actors.
    if(e.type==1500||e.type==1400)return FACTORY_ROCK;
    //0079c1b3 maps1999 to grid15. Ordinary wall Init has no asset/MT calls;
    //room16's graphics path remains an explicit dependency.
    if(e.type==1999&&record.type!=16)return FACTORY_WALL;
    if(e.type==0&&(e.variant==10||e.variant==20||e.variant==30)&&e.subtype==0&&backdrop==1&&mode==0)return FACTORY_DECORATION;
    //Room-spawn type5 owns pickup identity/position. Variant0 is the ordinary
    //random-pickup request; the native reward selector remains a later service.
    if(e.type==5)return FACTORY_PICKUP;
    return FACTORY_MISSING;
}
}
NativeSceneResult NativeSceneInspectPlan(const RoomRecord& record,const NativeSpawnPlan& plan,int backdrop,int mode,NativeSceneDependencies* output) {
    if(!output||record.format!=RRF_STB1||plan.count<0||plan.count>kNativeRoomGridLimit||plan.count!=(int)record.stackCount||backdrop<0||mode<0||mode>1)return NSC_BAD_REQUEST;
    NativeSceneDependencies next;memset(&next,0,sizeof(next));
    for(int i=0;i<plan.count;++i){const NativeSpawnChoice& c=plan.choices[i];if(!c.present)continue;
        if(c.grid<0||c.grid>=kNativeRoomGridLimit||!c.seed)return NSC_BAD_REQUEST;
        if(SelectedFactory(record,c,backdrop,mode)!=FACTORY_MISSING){++next.supported;continue;}
        NativeSceneDependency& d=next.missing[next.count++];d.choice=i;d.grid=c.grid;d.seed=c.seed;
        d.type=c.entry.type;d.variant=c.entry.variant;d.subtype=c.entry.subtype;
    }
    *output=next;return NSC_OK;
}
NativeSceneResult NativeSceneReadRecord(const NativeSceneFiles& files,int definition,unsigned char** bytes,unsigned int* length,unsigned int* recordHash) {
    const NativeRoomDef* d=NativeRoom(definition);
    if(!d || !NativeRoomSourceBinding(d->source) || !files.indexPath || !files.objectRoot || !bytes || !length || !recordHash) return NSC_BAD_REQUEST;
    FILE* index=fopen(files.indexPath,"rb");if(!index) return NSC_SOURCE_ERROR;
    GpcEntry e;
    const NativeRoomSourceDef* binding=NativeRoomSourceBinding(d->source);
    GpcResult found=GpcFindOriginalResource(index,binding->request,&e);fclose(index);
    if(found==GPC_OK&&binding&&(e.package!=binding->package||memcmp(e.sha256,binding->sha256,32)))return NSC_SOURCE_ERROR;
    if(found!=GPC_OK || e.size>1024*1024 || d->offset<8 || d->offset>e.size || d->bytes>e.size-d->offset) return NSC_SOURCE_ERROR;
    char path[768];if(strlen(files.objectRoot)+strlen(e.objectPath)+1>=sizeof(path)) return NSC_SOURCE_ERROR;
    sprintf(path,"%s%s",files.objectRoot,e.objectPath);
    if(strchr(files.objectRoot,'\\')) for(char* p=path;*p;++p) if(*p=='/') *p='\\';
    FILE* f=fopen(path,"rb");if(!f) return NSC_SOURCE_ERROR;
    unsigned char magic[4];fseek(f,0,SEEK_END);long size=ftell(f);fseek(f,0,SEEK_SET);
    bool ok=size>=8 && (unsigned long)size==e.size && fread(magic,1,4,f)==4 && !memcmp(magic,"STB1",4);
    unsigned char* raw=new unsigned char[d->bytes];
    if(ok) ok=fseek(f,d->offset,SEEK_SET)==0 && fread(raw,1,d->bytes,f)==d->bytes;fclose(f);
    if(!ok) {delete[] raw;return NSC_SOURCE_ERROR;}
    unsigned int hash=2166136261U;for(unsigned int i=0;i<d->bytes;++i) hash=(hash^raw[i])*16777619U;
    *bytes=raw;*length=d->bytes;*recordHash=hash?hash:1;return NSC_OK;
}
NativeSceneResult NativeSceneBuildRoom(const RoomRecord& record,const NativeRoomSeeds& seeds,int backdrop,int nativeMode,int configStage,
    NativeRoomState* output,NativeConstructionIssue* issue,NativeRoomMarkers* markers,unsigned int frame,NativeEnemyList* enemies,NativePickupList* pickups) {
    NativeConstructionIssue detail={NCS_HEADER,-1,0,0,0,NRS_BAD_DATA};if(issue)*issue=detail;
    if(!output||backdrop<0||nativeMode<0||nativeMode>1||configStage<0||!seeds.spawn||!seeds.award)return NSC_BAD_REQUEST;
    if(record.format!=RRF_STB1){detail.stage=NCS_FORMAT;if(issue)*issue=detail;return NSC_UNSUPPORTED_CONTENT;}
    NativeRoomState room;memset(&room,0,sizeof(room));room.type=record.type;room.variant=record.variant;room.shape=record.shape;
    if(!room.geometry.Init(record.shape,record.width,record.height))return NSC_UNSUPPORTED_SHAPE;
    if(!room.space.Init(record.shape,room.geometry,configStage,record.type))return NSC_BAD_REQUEST;
    room.collision.Clear(); //007a10cf..10dd clears all448 native+76c entries.
    room.native6f60=-1;room.native11ec=0;
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
        Factory factory=SelectedFactory(record,choice,backdrop,nativeMode);
        if(factory==FACTORY_MARKER)continue;
        detail.stage=NCS_ENTRY;detail.choice=i;detail.type=entry.type;detail.variant=entry.variant;detail.subtype=entry.subtype;if(issue)*issue=detail;
        if(factory==FACTORY_ENEMY){
            if(!enemies||enemies->count>=64){delete plan;return NSC_UNSUPPORTED_CONTENT;}
            float px,py;NativePoint position;
            if(!room.geometry.Position(choice.grid,&px,&py)){delete plan;return NSC_UNSUPPORTED_EXTENT;}
            position.x=px+20.0f;position.y=py+20.0f;
            NativeEnemy* enemy=new NativeEnemy;
            if(!enemy->Begin((int)entry.type,(int)entry.variant,choice.seed,position,frame,configStage)){
                delete enemy;delete plan;return NSC_BAD_REQUEST;
            }
            enemies->entries[enemies->count++]=enemy;room.encounter=true;continue;
        }
        if(factory==FACTORY_PICKUP){
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
        if(factory==FACTORY_ROCK){
            if(room.collision.occupied[choice.grid])continue;
            NativeStaticGrid& rock=room.staticGrids[room.staticGridCount];
            rock.grid=choice.grid;rock.type=(int)entry.type;rock.variant=(int)entry.variant;rock.state=0;
            rock.initializations=0;rock.collisionClass=4;rock.seed=choice.seed;rock.native18=0;rock.native20=0;
            rock.initializedFrame=frame;rock.initialized=true;
            ++room.staticGridCount;room.collision.Set(choice.grid,true,4,0,(int)entry.type);continue;
        }
        if(factory==FACTORY_WALL){
            //0079b508 refuses a grid already occupied by an earlier stack.
            //Decoration props also occupy native+24 even though noncolliding.
            if(room.collision.occupied[choice.grid])continue;
            NativeStaticGrid& wall=room.staticGrids[room.staticGridCount];
            if(!NativeSelectedWall(record.type,choice.grid,entry.variant,entry.subtype,choice.seed,frame,&wall)){delete plan;return NSC_BAD_REQUEST;}
            ++room.staticGridCount;room.collision.Set(choice.grid,true,wall.collisionClass,1000,15);continue;
        }
        if(factory==FACTORY_DECORATION){
            if(room.collision.occupied[choice.grid])continue;
            NativeDecoration& prop=room.decorations[room.decorationCount];int x,y;
            if(room.geometry.Tile(choice.grid,&x,&y)&&NativeBasementDecoration(seeds.spawn,room.geometry,choice.grid,&prop.seed,&prop.animation)){
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
