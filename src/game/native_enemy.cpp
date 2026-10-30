#include "native_enemy.h"
#include "native_room_state.h"
#include <math.h>
#include <string.h>
namespace {
float Length(NativePoint p){return (float)sqrt((double)(p.x*p.x+p.y*p.y));}
}
NativeEnemy::NativeEnemy():ready(false),dead(false),seed(0){}
bool NativeEnemy::Begin(int variant,unsigned int seed,const NativePoint& position,unsigned int frame,int stage){
    if(variant<0||variant>1||!seed)return false;
    NativeEntityBody next;
    if(next.BeginFresh(10,variant,0,seed,frame,stage,&position)!=NEB_OK)return false;
    body.Swap(next);this->seed=seed;dead=false;ready=true;return true;
}
void NativeEnemy::Update(const NativeRoomState& room,const NativeEntityBody& player){
    if(!ready||dead||body.removed||!player.ready)return;
    NativePoint delta={player.position.x-body.position.x,player.position.y-body.position.y};
    float distance=Length(delta);
    if(distance>0.0f){
        float speed=distance>20.0f?0.65f:0.0f;
        body.velocity.x=delta.x/distance*speed;body.velocity.y=delta.y/distance*speed;
        NativePoint next={body.position.x+body.velocity.x,body.position.y+body.velocity.y};
        int collision=0;
        if(room.collision.AtPosition(room.geometry,next,room.space.zeroCollision,&collision)&&(collision==0||collision==5))body.position=next;
    }
    NativePoint clamped;if(room.space.Clamp(body.position,2,2,2,2,&clamped))body.position=clamped;
    body.time+=1.0f;
}
void NativeEnemy::Damage(float amount){
    if(!ready||dead||amount<=0.0f)return;
    body.hitPoints-=amount;
    if(body.hitPoints<=0.0f){body.hitPoints=0.0f;body.removed=true;dead=true;}
}
void NativeEnemy::Swap(NativeEnemy& b){body.Swap(b.body);bool r=ready;ready=b.ready;b.ready=r;r=dead;dead=b.dead;b.dead=r;unsigned int s=seed;seed=b.seed;b.seed=s;}
void NativeEnemyList::Clear(){for(int i=0;i<count;++i)delete entries[i];for(int i=0;i<64;++i)entries[i]=0;count=0;}
