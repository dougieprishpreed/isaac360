#include "native_minimap.h"
#include "native_world.h"
#include "normal_run.h"
#include "native_player_state.h"
#include "native_persistence.h"
#include "native_minimap_data.h"
#include <math.h>
#include <algorithm>
#include <string.h>
namespace {
float Add(float a,float b){volatile float f=a+b;return f;}
float Mul(float a,float b){volatile float f=a*b;return f;}
bool Absent(const NativePlayerState& p,int item){bool held;return p.HasCollectible(item,&held)&&!held;}
bool Secret(int type){return type==7||type==8;}
bool Draw(NativeMinimap& map,NativeSprite& sprite,const char* animation,int frame,int grid,float x,float y){
    if(sprite.SetFrame(animation,frame)!=NSPR_OK)return false;
    NativeMinimapDraw d={sprite.asset,animation,frame,grid,{x,y}};map.draws.push_back(d);return true;
}
const char* Icon(const NativePlacedRoom& p,const NativeRoomDef& d){
    if(!(p.displayFlags&4)||d.type<1||d.type>29)return "";
    if(d.type==4&&(p.flags&2048))return kMinimapTreasureAlternate;
    if(d.type==11)return d.subtype==1?kMinimapChallengeBoss:kMinimapChallengeNormal;
    return kMinimapRoomIcons[d.type-1];
}
const char* SavedPickupIcon(unsigned int variant){
    //8d0fc5's prepared type5 classes. Each variant maps to its original
    //minimap_icons clip; this is presentation identity, not a generic item
    //substitute. Child/extra type5 payloads remain rejected by the room
    //persistence owner before this selection.
    if(variant==10)return "IconHeart";
    if(variant==20)return "IconCoin";
    if(variant==30)return "IconKey";
    if(variant==40)return "IconBomb";
    if(variant>=50&&variant<=58)return "IconChest";
    if(variant==60)return "IconChest";
    if(variant==69)return "IconChest";
    if(variant==70)return "IconPill";
    if(variant==90)return "IconBattery";
    if(variant==100)return "IconItem";
    if(variant==300)return "IconCard";
    if(variant==350)return "IconTrinket";
    if(variant==360)return "IconChest";
    if(variant==380)return "IconItem";
    return 0;
}
}
const NativeSpriteAsset* NativePreparedMinimapSprite(unsigned int i){return i<3?kMinimapAssets[i]:0;}
NativeMinimap::NativeMinimap():columns(0),rows(0),firstColumn(0),firstRow(0),iconLimit(0),textureWidth(0),textureHeight(0),cellWidth(0),cellHeight(0),padding(0),border(0),ready(false){
    minimum.x=minimum.y=maximum.x=maximum.y=currentMarker.x=currentMarker.y=0;
}
bool NativeMinimaps::Prepare(){
    if(ready)return true;
    for(int i=0;i<2;++i){NativeMinimap& m=maps[i];
        if(m.sprite.Load(kMinimapAssets[i]->path)!=NSPR_OK)return false;
        //8d21e6..232a, relative to each child+18/+178.
        m.cellWidth=i?16.0f:7.0f;m.cellHeight=i?14.0f:6.0f;m.padding=i?4.0f:2.0f;m.border=1;
        m.columns=m.rows=i?13:7;m.iconLimit=i?4:1;
        m.textureWidth=(int)(m.cellWidth+m.border)*m.columns+2*(int)m.padding+(int)m.border;
        m.textureHeight=(int)(m.cellHeight+m.border)*m.rows+2*(int)m.padding+(int)m.border;
    }
    if(icons.Load(kMinimapAssets[2]->path)!=NSPR_OK)return false;ready=true;return true;
}
void NativeMinimaps::Reset(){
    for(int i=0;i<2;++i){maps[i].sprite.Clear();maps[i].draws.clear();maps[i].ready=false;}
    icons.Clear();ready=discoveryPrepared=entryPrepared=false;refreshes=lastEntrySerial=lastError=0;
}
bool NativeMinimaps::EnterInitial(const NormalRunFlow& run,NativeWorld& world,const NativePlayerState& player,unsigned int* next){
    if(next)*next=0x6f4e20;
    const NativePlacedRoom* current=world.CurrentDescriptor();const NativeRoomDef* d=current?NativeRoom(current->definition):0;
    if(!next||!d||run.context.stage!=1||run.context.stageType>2||run.context.challenge||world.layout.currentDimension||
       world.currentGrid<0||world.currentGrid!=world.previousGrid||d->type!=1||!Prepare())return false;
    if(entryPrepared&&lastEntrySerial==world.entrySerial){*next=0;return true;}
    if(entryPrepared)return false;
    if(!EnterWorld(run,world,player,next)){lastError=*next;return false;}
    discoveryPrepared=entryPrepared=true;*next=0;return true;
}
bool NativeMinimaps::EnterLinked(const NormalRunFlow& run,NativeWorld& world,const NativePlayerState& player,unsigned int* next){
    if(next)*next=0x6f4e20;
    if(!next||!Prepare()||world.currentList<0||!world.CurrentDescriptor())return false;
    if(lastEntrySerial==world.entrySerial){*next=0;return true;}
    if(!EnterWorld(run,world,player,next)){lastError=*next;return false;}
    *next=0;return true;
}
bool NativeMinimaps::DiscoverTarget(NativeWorld& world,const NativePlacedRoom& target,const NativePlacedRoom& source,
    int slot,const NativePlayerState& player,unsigned int* next){
    NativePlacedRoom* mutableTarget=world.layout.ByList(target.listIndex);
    const NativeRoomDef* targetData=NativeRoom(target.definition);
    const NativeRoomDef* sourceData=NativeRoom(source.definition);
    if(!mutableTarget||!targetData||!sourceData)return false;
    const bool targetSecret=Secret(targetData->type);
    const bool sourceSecret=Secret(sourceData->type);
    //6f4b20 called from6f4e80 passes current? target, current, slot.
    if(targetSecret||sourceSecret){
        if(!Absent(player,76)){*next=0x6f4bd3;return false;}
        const NativeWorldResident& sourceLive=world.resident[source.listIndex];
        const NativeDoorActor* actor=sourceLive.doors&&slot>=0&&slot<8?&sourceLive.doors->actors[slot]:0;
        if(!actor||!actor->native279){
            if(!Absent(player,76)){*next=0x6f4ce8;return false;}
            if(!Absent(player,91)){*next=0x6f4d07;return false;}
            //4d0e goes straight to4dff when neither item is held.
        }else{
            if(!Absent(player,91)){*next=0x6f4d25;return false;}
            mutableTarget->displayFlags|=3;
            if(!Absent(player,91)){*next=0x6f4dba;return false;}
        }
    }else{
        //4d1e: The Map's held branch and its one-level recursive reveal are a
        //separate native dependency; the ordinary absent branch is complete.
        if(!Absent(player,91)){*next=0x6f4d25;return false;}
        mutableTarget->displayFlags|=(targetData->type==6||targetData->type==18||targetData->type==19)?3U:5U;
        if(!Absent(player,91)){*next=0x6f4dba;return false;}
    }
    if(target.flags&1024)mutableTarget->displayFlags|=5;
    return true;
}
bool NativeMinimaps::EnterWorld(const NormalRunFlow& run,NativeWorld& world,const NativePlayerState& player,unsigned int* next){
    (void)run;
    const NativePlacedRoom* current=world.CurrentDescriptor();
    NativePlacedRoom* mutableCurrent=world.CurrentDescriptor();
    const NativeWorldResident& live=world.resident[world.currentList];
    if(!current||!mutableCurrent||!live.doors)return false;
    mutableCurrent->displayFlags|=5;
    //6f4e80 iterates actual door actors/targets. Preserve descriptor identity
    //and the secret76/dual91 gates before rebuilding either map draw list.
    for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){
        const NativePlacedRoom* target=world.layout.DoorTarget(world.currentList,slot);
        if(!target){*next=0x6f6100;return false;}
        if(!DiscoverTarget(world,*target,*current,slot,player,next))return false;
    }
    for(int i=0;i<2;++i)if(!Build(i,world,next))return false;
    ++refreshes;lastEntrySerial=world.entrySerial;*next=0;return true;
}
bool NativeMinimaps::Build(int index,const NativeWorld& world,unsigned int* next){
    *next=0x8cffb0;NativeMinimap& m=maps[index];const NativePlacedRoom* current=world.CurrentDescriptor();
    m.draws.clear();m.columns=std::min(13,m.columns);m.rows=std::min(13,m.rows);
    m.firstColumn=std::max(0,std::min(13-m.columns,world.currentGrid%13-m.columns/2));
    m.firstRow=std::max(0,std::min(13-m.rows,world.currentGrid/13-m.rows/2));
    m.minimum.x=(float)m.textureWidth;m.minimum.y=(float)m.textureHeight;m.maximum.x=m.maximum.y=0;
    const float sx=Add(m.cellWidth,m.border),sy=Add(m.cellHeight,m.border);
    //First pass includes every visible occupied cell, including the other
    //cells of large rooms. This is not a deduplicated descriptor traversal.
    for(int y=m.firstRow;y<m.firstRow+m.rows;++y)for(int x=m.firstColumn;x<m.firstColumn+m.columns;++x){
        int grid=y*13+x;const NativePlacedRoom* p=world.layout.ByCell(grid,world.layout.currentDimension);if(!p||p->definition<0||!p->displayFlags)continue;
        float px=Mul((float)(x-m.firstColumn),sx),py=Mul((float)(y-m.firstRow),sy);
        m.minimum.x=std::min(m.minimum.x,px);m.minimum.y=std::min(m.minimum.y,py);
        m.maximum.x=std::max(m.maximum.x,Add(Add(px,m.cellWidth),Mul(Add(m.padding,m.border),2)));
        m.maximum.y=std::max(m.maximum.y,Add(Add(py,m.cellHeight),Mul(Add(m.padding,m.border),2)));
        if(!Draw(m,m.sprite,kMinimapOutline,0,grid,px,py))return false;
    }
    //The following two passes extend one cell above/left. Native93..99
    //compares safe-grid identity; large-room glyphs are emitted at the origin.
    const int firstX=std::max(0,m.firstColumn-1),firstY=std::max(0,m.firstRow-1);
    for(int pass=0;pass<2;++pass)for(int y=firstY;y<m.firstRow+m.rows;++y)for(int x=firstX;x<m.firstColumn+m.columns;++x){
        const int grid=y*13+x;const NativePlacedRoom* p=world.layout.ByCell(grid,world.layout.currentDimension);const NativeRoomDef* d=p?NativeRoom(p->definition):0;
        if(!d)continue;if(p->gridIndex!=grid&&!(d->shape==9&&p->gridIndex+1==grid))continue;
        float px=Mul((float)(x-m.firstColumn),sx),py=Mul((float)(y-m.firstRow),sy);
        if(d->shape==9)px=Add(px,-sx);
        if(pass==0){const char* animation=0;
            if(p->safeGridIndex==current->safeGridIndex){animation=kMinimapCurrent;
                const float w=d->shape==6||d->shape>=8?2.0f:1.0f,h=d->shape==4||d->shape>=8?2.0f:1.0f;
                m.currentMarker.x=Add(Add(px,Mul(Mul(w,m.cellWidth),0.5f)),Add(m.padding,m.border));
                m.currentMarker.y=Add(Add(py,Mul(Mul(h,m.cellHeight),0.5f)),Add(m.padding,m.border));
            }else if((p->flags&1)&&(p->displayFlags&1))animation=kMinimapVisited;
            else if((p->displayFlags&1)&&!Secret(d->type)&&d->type!=29)animation=kMinimapUnvisited;
            if(animation&&!Draw(m,m.sprite,animation,d->shape-1,grid,px,py))return false;
        }else{
            //8d0fc5 scans the saved entity vector for pickup identities before
            //the room-type icon. The reached key30 and bomb40 classes map to
            //their original minimap clips. Ambient1000 records have no map
            //marker. Type6 pickup classes and grid18 records still stop at
            //their exact unsupported native branches.
            const NativeRoomPersistent* saved=world.resident[p->listIndex].persistent;
            if(saved&&saved->captured){
                int savedIcons=0;
                for(unsigned int i=0;i<saved->entities.size();++i){unsigned int type=0,variant=0;
                    if(!saved->entities[i].Get(0,4,&type)||!saved->entities[i].Get(4,4,&variant)){*next=0x8d0fc5;return false;}
                    if(type==5){const char* pickupIcon=SavedPickupIcon(variant);if(!pickupIcon){*next=0x8d13c9;return false;}
                        if(savedIcons<m.iconLimit){float ix=Add(Add(m.padding,m.border),Add(Mul(m.cellWidth,0.5f),(float)savedIcons*2.0f));
                            float iy=Add(Add(m.padding,m.border),Mul(m.cellHeight,0.5f));
                            if(!Draw(m,icons,pickupIcon,0,grid,(float)floor(Add(Add(px,ix),-2.0f)),
                                (float)floor(Add(Add(py,iy),-2.0f))))return false;++savedIcons;}
                    }else if(type==6){*next=0x8d1463;return false;}
                }
                for(unsigned int i=0;i<saved->grids.size();++i){unsigned int type;
                    if(!saved->grids[i].Get(0,4,&type)||type==18){*next=0x8d1561;return false;}}
            }
            const char* icon=Icon(*p,*d);if(!*icon)continue;
            //One room-type icon, hence icon count<=1 on this actual path.
            float dx=Add(m.padding,m.border),dy=dx;
            if(d->shape==6||d->shape==8)dx=Add(dx,Mul(m.cellWidth,0.5f));
            if(d->shape==4||d->shape==8)dy=Add(dy,Mul(m.cellHeight,0.5f));
            dx=Add(dx,Mul(m.cellWidth,0.5f));dy=Add(dy,Mul(m.cellHeight,0.5f));
            if(!Draw(m,icons,icon,0,grid,(float)floor(Add(Add(px,dx),-2.0f)),(float)floor(Add(Add(py,dy),-2.0f))))return false;
        }
    }
    m.ready=true;return true;
}
