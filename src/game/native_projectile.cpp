#include "native_projectile.h"
#include "game_session.h"
#include "native_player_input.h"
#include <math.h>
#include <string.h>
namespace {
float Length(NativePoint p){return (float)sqrt((double)(p.x*p.x+p.y*p.y));}
}
NativeProjectileSystem::NativeProjectileSystem(){Reset();}
void NativeProjectileSystem::Reset(){active.clear();spawned=wallHits=expired=enemyKills=0;cooldown=0;}
void NativeProjectileSystem::Swap(NativeProjectileSystem& b){
    active.swap(b.active);unsigned int n=spawned;spawned=b.spawned;b.spawned=n;
    n=wallHits;wallHits=b.wallHits;b.wallHits=n;n=expired;expired=b.expired;b.expired=n;
    n=enemyKills;enemyKills=b.enemyKills;b.enemyKills=n;
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
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    //Advance existing tears and pop them on the real room collision owner,
    //enemy body contact or the evaluated range.
    for(unsigned int i=0;i<active.size();){
        NativeProjectile& p=active[i];
        p.position.x+=p.velocity.x;p.position.y+=p.velocity.y;
        p.travelled+=Length(p.velocity);
        bool hitEnemy=false;
        for(int e=0;e<live.enemies.count;++e){NativeEnemy* enemy=live.enemies.entries[e];
            if(!enemy||!enemy->ready||enemy->dead)continue;
            float dx=p.position.x-enemy->body.position.x,dy=p.position.y-enemy->body.position.y;
            float radius=enemy->body.collision.radius+4.0f;
            if(dx*dx+dy*dy<=radius*radius){
                enemy->Damage(p.damage);hitEnemy=true;
                if(enemy->dead){++enemyKills;++runtime.enemyKills;
                    if(NativeBossEntityType(enemy->type,enemy->body.variant)){
                        runtime.bossDefeated=true;
                        if(room->trapdoorState==0){room->trapdoorState=1;room->trapdoorTimer=0;
                            room->trapdoorX=enemy->body.position.x;room->trapdoorY=enemy->body.position.y;}
                    }}
                break;
            }
        }
        if(hitEnemy){active.erase(active.begin()+i);continue;}
        int collision=0;
        if(!room->collision.AtPosition(room->geometry,p.position,room->space.zeroCollision,&collision)){*next=0x61f6d0;return false;}
        if(collision!=0&&collision!=5){++wallHits;active.erase(active.begin()+i);continue;}
        if(p.travelled>=p.range){++expired;active.erase(active.begin()+i);continue;}
        ++i;
    }
    //Remove dead actors from the resident list, then expose the clear owner.
    for(int e=0;e<live.enemies.count;){
        NativeEnemy* enemy=live.enemies.entries[e];
        if(enemy&&enemy->dead){delete enemy;for(int j=e+1;j<live.enemies.count;++j)live.enemies.entries[j-1]=live.enemies.entries[j];--live.enemies.count;live.enemies.entries[live.enemies.count]=0;continue;}
        ++e;
    }
    if(live.enemies.count==0)room->clear=true;
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

NativeEnemyShotSystem::NativeEnemyShotSystem(){spawned=wallHits=0;}
void NativeEnemyShotSystem::Clear(){active.clear();spawned=wallHits=0;}
void NativeEnemyShotSystem::Fire(const NativePoint& position,float vx,float vy,float damage,float radius,int life,unsigned int owner){
    NativeEnemyShot shot;shot.position=position;shot.velocity.x=vx;shot.velocity.y=vy;
    shot.damage=damage;shot.radius=radius;shot.life=life;shot.owner=owner;
    active.push_back(shot);++spawned;
}
void NativeEnemyShotSystem::Swap(NativeEnemyShotSystem& b){
    active.swap(b.active);unsigned int n=spawned;spawned=b.spawned;b.spawned=n;n=wallHits;wallHits=b.wallHits;b.wallHits=n;
}
bool NativeEnemyShotSystem::Step(PortNormalRuntime& runtime){
    NativeRoomState* room=runtime.world.Current();NativeEntityBody& body=runtime.playerBody;
    if(!room||!body.ready)return false;
    for(unsigned int i=0;i<active.size();){
        NativeEnemyShot& shot=active[i];
        shot.position.x+=shot.velocity.x;shot.position.y+=shot.velocity.y;
        if(shot.life>0)--shot.life;
        int collision=0;
        if(!room->collision.AtPosition(room->geometry,shot.position,room->space.zeroCollision,&collision))return false;
        if(collision!=0&&collision!=5){++wallHits;active.erase(active.begin()+i);continue;}
        float dx=shot.position.x-body.position.x,dy=shot.position.y-body.position.y;
        float radius=shot.radius+body.collision.radius;
        if(dx*dx+dy*dy<=radius*radius){
            if(!runtime.playerDead){
                if(runtime.playerInvulnerable<=0){
                    bool dead=runtime.playerState.Damage((int)shot.damage);
                    ++runtime.playerHits;runtime.playerInvulnerable=30;
                    if(dead){runtime.playerDead=true;runtime.playerBody.removed=true;runtime.phase=PNR_DYING;runtime.playerDeathTicks=0;}
                }
            }
            active.erase(active.begin()+i);continue;
        }
        if(shot.life<=0){active.erase(active.begin()+i);continue;}
        ++i;
    }
    return true;
}

NativeBombSystem::NativeBombSystem(){placed=explosions=enemyHits=rockHits=0;}
void NativeBombSystem::Clear(){active.clear();placed=explosions=enemyHits=rockHits=0;}
bool NativeBombSystem::Place(PortNormalRuntime& runtime){
    if(runtime.playerDead||!runtime.playerBody.ready||runtime.playerState.bombs<=0)return false;
    --runtime.playerState.bombs;
    NativeBomb bomb;bomb.position=runtime.playerBody.position;bomb.timer=45;bomb.radius=90.0f;
    active.push_back(bomb);++placed;return true;
}
bool NativeBombSystem::Step(PortNormalRuntime& runtime){
    NativeRoomState* room=runtime.world.Current();
    if(!room||runtime.world.currentList<0||runtime.world.currentList>=kNativeWorldRoomLimit)return false;
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    for(unsigned int i=0;i<active.size();){
        NativeBomb& bomb=active[i];
        if(--bomb.timer>0){++i;continue;}
        const float radius=bomb.radius;
        for(int e=0;e<live.enemies.count;++e){
            NativeEnemy* enemy=live.enemies.entries[e];if(!enemy||!enemy->ready||enemy->dead)continue;
            float dx=enemy->body.position.x-bomb.position.x,dy=enemy->body.position.y-bomb.position.y;
            if(dx*dx+dy*dy>radius*radius)continue;
            const float px=enemy->body.position.x,py=enemy->body.position.y;
            enemy->Damage(100.0f);++enemyHits;
            if(enemy->dead){
                ++runtime.enemyKills;
                if(NativeBossEntityType(enemy->type,enemy->body.variant)){
                    runtime.bossDefeated=true;
                    if(room->trapdoorState==0){room->trapdoorState=1;room->trapdoorTimer=0;room->trapdoorX=px;room->trapdoorY=py;}
                }
            }
        }
        for(int g=0;g<room->staticGridCount;){
            const NativeStaticGrid& grid=room->staticGrids[g];
            const bool destructible=grid.type==1000||grid.type==1001||grid.type==1002||grid.type==1500||grid.type==1400;
            bool destroyed=false;
            if(destructible){
                float gx=0,gy=0;
                if(room->geometry.Position(grid.grid,&gx,&gy)){
                    float dx=gx+20.0f-bomb.position.x,dy=gy+20.0f-bomb.position.y;
                    if(dx*dx+dy*dy<=radius*radius){
                        const int cell=room->geometry.PositionIndex(gx+20.0f,gy+20.0f);
                        if(cell>=0){room->collision.occupied[cell]=0;room->collision.types[cell]=0;}
                        room->staticGrids[g]=room->staticGrids[--room->staticGridCount];
                        ++rockHits;destroyed=true;
                    }
                }
            }
            if(!destroyed)++g;
        }
        if(!runtime.playerDead&&runtime.playerInvulnerable<=0){
            float dx=runtime.playerBody.position.x-bomb.position.x,dy=runtime.playerBody.position.y-bomb.position.y;
            if(dx*dx+dy*dy<=radius*radius*0.5625f){
                if(runtime.playerState.Damage(2)){
                    runtime.playerDead=true;runtime.playerBody.removed=true;runtime.phase=PNR_DYING;runtime.playerDeathTicks=0;
                }
                ++runtime.playerHits;runtime.playerInvulnerable=30;
            }
        }
        for(int e=0;e<live.enemies.count;){
            NativeEnemy* enemy=live.enemies.entries[e];
            if(enemy&&enemy->dead){
                delete enemy;
                for(int j=e+1;j<live.enemies.count;++j)live.enemies.entries[j-1]=live.enemies.entries[j];
                --live.enemies.count;live.enemies.entries[live.enemies.count]=0;continue;
            }
            ++e;
        }
        if(live.enemies.count==0)room->clear=true;
        ++explosions;
        active.erase(active.begin()+i);
    }
    return true;
}
void NativeBombSystem::Swap(NativeBombSystem& b){
    active.swap(b.active);
    unsigned int n=placed;placed=b.placed;b.placed=n;
    n=explosions;explosions=b.explosions;b.explosions=n;
    n=enemyHits;enemyHits=b.enemyHits;b.enemyHits=n;
    n=rockHits;rockHits=b.rockHits;b.rockHits=n;
}
