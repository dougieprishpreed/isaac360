#include "native_projectile.h"
#include "game_session.h"
#include "native_player_input.h"
#include <math.h>
#include <string.h>
namespace {
float Length(NativePoint p){return (float)sqrt((double)(p.x*p.x+p.y*p.y));}
}
NativeProjectileSystem::NativeProjectileSystem(){Reset();}
void NativeProjectileSystem::Reset(){active.clear();spawned=wallHits=expired=0;cooldown=0;}
void NativeProjectileSystem::Swap(NativeProjectileSystem& b){
    active.swap(b.active);unsigned int n=spawned;spawned=b.spawned;b.spawned=n;
    n=wallHits;wallHits=b.wallHits;b.wallHits=n;n=expired;expired=b.expired;b.expired=n;
    float c=cooldown;cooldown=b.cooldown;b.cooldown=c;
}
bool NativeProjectileSystem::Step(PortNormalRuntime& runtime,NativeActionSource* actions,unsigned int frame,unsigned int* next){
    (void)frame;
    if(next)*next=0x61f6d0;
    NativeRoomState* room=runtime.world.Current();NativeEntityBody& body=runtime.playerBody;
    NativePlayerState& state=runtime.playerState;if(!room||!body.ready||!state.stats.ready)return false;
    if(cooldown>0)cooldown-=1.0f;
    //Evaluate the real shot vector through the maintained native input service.
    NativePoint shot={0,0};bool fire=false;
    if(actions){
        NativePlayerInputContext c;memset(&c,0,sizeof(c));
        c.playerType=state.playerType;c.controller=runtime.playerCreation.native153c;c.playerToken=body.entityToken;
        c.controlsEnabled=true;c.stage=runtime.run.context.stage;c.stageType=runtime.run.context.stageType;
        c.dimension=runtime.world.layout.currentDimension;
        NativePlayerInputResult r=NativeShootingInput(c,*actions,&shot);
        if(r==NPI_BAD_REQUEST||r==NPI_BAD_VALUE)return false;
        if(r==NPI_OK)fire=state.canShoot&&state.stats.weapons[1]&&(shot.x!=0.0f||shot.y!=0.0f);
    }
    //Advance existing tears and pop them on the real room collision owner or
    //when the evaluated range is exhausted.
    for(unsigned int i=0;i<active.size();){
        NativeProjectile& p=active[i];
        p.position.x+=p.velocity.x;p.position.y+=p.velocity.y;
        p.travelled+=Length(p.velocity);
        int collision=0;
        if(!room->collision.AtPosition(room->geometry,p.position,room->space.zeroCollision,&collision)){*next=0x61f6d0;return false;}
        if(collision!=0&&collision!=5){++wallHits;active.erase(active.begin()+i);continue;}
        if(p.travelled>=p.range){++expired;active.erase(active.begin()+i);continue;}
        ++i;
    }
    if(fire&&cooldown<=0.0f){
        float length=Length(shot);if(length<=0.0f)return false;
        NativeProjectile p;p.position=body.position;
        float speed=state.stats.shotSpeed*10.0f;
        p.velocity.x=shot.x/length*speed;p.velocity.y=shot.y/length*speed;
        p.damage=state.stats.damage;p.travelled=0;p.range=state.stats.range;p.owner=body.entityToken;
        active.push_back(p);++spawned;
        float delay=state.stats.fireDelay;if(delay<1.0f)delay=1.0f;cooldown=delay;
    }
    if(next)*next=0;return true;
}
