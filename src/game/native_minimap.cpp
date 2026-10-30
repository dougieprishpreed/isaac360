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
    icons.Clear();ready=discoveryPrepared=entryPrepared=false;refreshes=0;
}
bool NativeMinimaps::EnterInitial(const NormalRunFlow& run,NativeWorld& world,const NativePlayerState& player,unsigned int* next){
    if(next)*next=0x6f4e20;
    const NativePlacedRoom* current=world.CurrentDescriptor();const NativeRoomDef* d=current?NativeRoom(current->definition):0;
    if(!next||entryPrepared||!d||run.context.stage!=1||run.context.stageType>2||run.context.challenge||world.layout.currentDimension||
       world.currentGrid<0||world.currentGrid!=world.previousGrid||d->type!=1||!Prepare())return false;
    if(!discoveryPrepared){
        world.layout.rooms[world.currentList].displayFlags|=5;
        //6f4e80 iterates actual door actors/targets. In6f4b20 the ordinary
        //current room takes4b62 for non-secret neighbors, then4d14. Secret
        //neighbors query76 followed by91; do not reverse these predicates.
        const NativeWorldResident& live=world.resident[world.currentList];
        for(int slot=0;slot<8;++slot)if(live.doors&&(live.doors->attached&(1U<<slot))){
            const NativePlacedRoom* target=world.layout.DoorTarget(world.currentList,slot);
            const NativeRoomDef* td=target?NativeRoom(target->definition):0;if(!td)return false;
            if(Secret(td->type)){
                if(!Absent(player,76)){*next=0x6f4bd3;return false;}
                if(!live.doors->actors[slot].native279){
                    if(!Absent(player,76)){*next=0x6f4ce8;return false;}
                    if(!Absent(player,91)){*next=0x6f4d07;return false;}
                    //4d0e goes straight to4dff when neither item is held.
                }else{
                    if(!Absent(player,91)){*next=0x6f4d25;return false;}
                    world.layout.rooms[target->listIndex].displayFlags|=3;
                    if(!Absent(player,91)){*next=0x6f4dba;return false;}
                }
            }else{
                if(!Absent(player,91)){*next=0x6f4d25;return false;}
                NativePlacedRoom& t=world.layout.rooms[target->listIndex];
                t.displayFlags|=(td->type==6||td->type==18||td->type==19)?3U:5U;
                if(!Absent(player,91)){*next=0x6f4dba;return false;}
            }
            if(target->flags&1024)world.layout.rooms[target->listIndex].displayFlags|=5;
        }
        discoveryPrepared=true;
    }
    for(int i=0;i<2;++i)if(!maps[i].ready&&!BuildInitial(i,world,next))return false;
    ++refreshes;entryPrepared=true;*next=0;return true;
}
bool NativeMinimaps::BuildInitial(int index,const NativeWorld& world,unsigned int* next){
    *next=0x8cffb0;NativeMinimap& m=maps[index];const NativePlacedRoom* current=world.CurrentDescriptor();
    m.draws.clear();m.columns=std::min(13,m.columns);m.rows=std::min(13,m.rows);
    m.firstColumn=std::max(0,std::min(13-m.columns,world.currentGrid%13-m.columns/2));
    m.firstRow=std::max(0,std::min(13-m.rows,world.currentGrid/13-m.rows/2));
    m.minimum.x=(float)m.textureWidth;m.minimum.y=(float)m.textureHeight;m.maximum.x=m.maximum.y=0;
    const float sx=Add(m.cellWidth,m.border),sy=Add(m.cellHeight,m.border);
    //First pass includes every visible occupied cell, including the other
    //cells of large rooms. This is not a deduplicated descriptor traversal.
    for(int y=m.firstRow;y<m.firstRow+m.rows;++y)for(int x=m.firstColumn;x<m.firstColumn+m.columns;++x){
        int grid=y*13+x;const NativePlacedRoom* p=world.layout.ByCell(grid,0);if(!p||p->definition<0||!p->displayFlags)continue;
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
        const int grid=y*13+x;const NativePlacedRoom* p=world.layout.ByCell(grid,0);const NativeRoomDef* d=p?NativeRoom(p->definition):0;
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
            //All saved actors on the first entry are retained1000/21/33/68
            //ambients, not the5/6 minimap item factories. Only grid18 adds a
            //marker; inspect the live checkpoint rather than assuming empty.
            const NativeRoomPersistent* saved=world.resident[p->listIndex].persistent;
            if(saved&&saved->captured){
                for(unsigned int i=0;i<saved->entities.size();++i){unsigned int type;
                    if(!saved->entities[i].Get(0,4,&type)||type==5||type==6){*next=0x8d0fc5;return false;}}
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
