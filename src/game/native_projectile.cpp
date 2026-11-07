#include "native_projectile.h"
#include "game_session.h"
#include "native_player_input.h"
#include "native_audio.h"
#include "native_pickup.h"
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
            if(!enemy||!enemy->participates||!enemy->ready||enemy->dead)continue;
            float dx=p.position.x-enemy->body.position.x,dy=p.position.y-enemy->body.position.y;
            float radius=enemy->body.collision.radius+4.0f;
            if(dx*dx+dy*dy<=radius*radius){
                enemy->Damage(p.damage);hitEnemy=true;
                if(runtime.audio)runtime.audio->PlaySfx("resources/sfx/v2/meat impact.wav",0.45f);
                if(enemy->dead){++enemyKills;++runtime.enemyKills;
                    if(runtime.audio)runtime.audio->PlaySfx("resources/sfx/death burst small.wav",0.55f);
                    if(NativeBossEntityType(enemy->type,enemy->body.variant))
                        runtime.bossDefeated=true;
                }
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
    if(fire&&cooldown<=0.0f){
        float length=Length(shot);if(length<=0.0f)return false;
        NativeProjectile p;p.position=body.position;
        float speed=state.stats.shotSpeed*10.0f;
        p.velocity.x=shot.x/length*speed;p.velocity.y=shot.y/length*speed;
        p.damage=state.stats.damage;p.travelled=0;p.range=state.stats.range;p.owner=body.entityToken;
        active.push_back(p);++spawned;
        if(runtime.audio)runtime.audio->PlaySfx("resources/sfx/tear fire 4.wav",0.42f);
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
                    if(runtime.audio)runtime.audio->PlaySfx("resources/sfx/player/hurt grunt 1.wav",0.7f);
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
            enemy->Damage(100.0f);++enemyHits;
            if(enemy->dead){
                ++runtime.enemyKills;
                if(NativeBossEntityType(enemy->type,enemy->body.variant))
                    runtime.bossDefeated=true;
            }
        }
        for(int g=0;g<room->staticGridCount;){
            NativeStaticGrid& grid=room->staticGrids[g];
            const bool poop=grid.type==1490||grid.type==1494||grid.type==1495||
                grid.type==1496||grid.type==1497||grid.type==1500;
            const bool destructible=grid.type==1000||grid.type==1001||grid.type==1002||
                poop||grid.type==1400;
            bool handled=false;bool kept=false;
            if(destructible){
                float gx=0,gy=0;
                if(room->geometry.Position(grid.grid,&gx,&gy)){
                    float dx=gx+20.0f-bomb.position.x,dy=gy+20.0f-bomb.position.y;
                    if(dx*dx+dy*dy<=radius*radius){
                        //The static-grid owner already carries the native cell
                        //that constructed the collision. Round-tripping its
                        //world center through PositionIndex at a cell boundary
                        //can clear a different cell.
                        const int cell=grid.grid;
                        if(poop){
                            //Retail GRIDDUMP: destruction sets state1000 and
                            //collision class0; the room path-cost update then
                            //decays the stored 1500 to0. The grid record stays
                            //for the destruction animation/save state.
                            if(grid.state!=1000){
                                grid.state=1000;grid.collisionClass=0;
                                room->collision.classes[cell]=0;
                                //Retail GRIDDUMP golden poop (variant3) dropped
                                //six coin pickups at its cell. Rainbow poop
                                //(variant4) dropped no pickup; the decoded heal
                                //owner restores the red-heart container.
                                if(grid.variant==3&&live.pickups.count<=58){
                                    float px,py;NativePoint at;
                                    if(room->geometry.Position(grid.grid,&px,&py)){
                                        at.x=px+20.0f;at.y=py+20.0f;
                                        unsigned int local=grid.seed?grid.seed:0x16a9de81U;
                                        for(int coin=0;coin<6;++coin){
                                            local=NativeShift(local,5,9,7);
                                            NativePickup* pickup=new NativePickup;
                                            if(!pickup->Begin(5,20,0,grid.grid,local?local:1U,runtime.tickFrame,at)){
                                                delete pickup;break;
                                            }
                                            live.pickups.entries[live.pickups.count++]=pickup;
                                        }
                                    }
                                }else if(grid.variant==4){
                                    runtime.playerState.redHearts=runtime.playerState.maxRedHearts;
                                }
                                ++rockHits;
                            }
                            kept=true;
                        }else{
                            room->collision.occupied[cell]=0;room->collision.classes[cell]=0;
                            room->collision.types[cell]=0;room->collision.native76c[cell]=0;
                            room->staticGrids[g]=room->staticGrids[--room->staticGridCount];
                            ++rockHits;
                        }
                        handled=true;
                    }
                }
            }
            if(!handled||kept)++g;
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
        ++explosions;
        if(runtime.audio)runtime.audio->PlaySfx("resources/sfx/explosion_strong1.wav",0.65f);
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

NativeFamiliarSystem::NativeFamiliarSystem(){spawned=shots=hits=0;}
void NativeFamiliarSystem::Clear(){active.clear();spawned=shots=hits=0;}
bool NativeFamiliarSystem::Sync(PortNormalRuntime& runtime){
    if(runtime.playerDead||!runtime.playerBody.ready)return true;
    const int familiarItems[]={8,10,117}; //Brother Bobby, Halo of Flies, Dead Bird.
    for(int k=0;k<3;++k){
        bool held=false;
        if(!runtime.playerState.HasCollectible(familiarItems[k],&held))return false;
        if(!held)continue;
        bool present=false;
        for(unsigned int i=0;i<active.size();++i)if(active[i].item==familiarItems[k]){present=true;break;}
        if(present)continue;
        NativeFamiliar f;f.item=familiarItems[k];f.position=runtime.playerBody.position;
        f.shootCooldown=30;f.contactCooldown=0;f.time=0;
        active.push_back(f);++spawned;
    }
    return true;
}
bool NativeFamiliarSystem::Step(PortNormalRuntime& runtime){
    NativeRoomState* room=runtime.world.Current();
    if(!room||runtime.world.currentList<0||runtime.world.currentList>=kNativeWorldRoomLimit)return false;
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    for(unsigned int i=0;i<active.size();++i){
        NativeFamiliar& f=active[i];f.time+=1.0f;
        NativeEnemy* nearest=0;float best=1.0e9f;
        for(int e=0;e<live.enemies.count;++e){NativeEnemy* enemy=live.enemies.entries[e];
            if(!enemy||!enemy->participates||!enemy->ready||enemy->dead)continue;
            float dx=enemy->body.position.x-f.position.x,dy=enemy->body.position.y-f.position.y;
            float d=dx*dx+dy*dy;if(d<best){best=d;nearest=enemy;}}
        if(f.item==8){
            const float ox=(i%2)?-34.0f:34.0f,oy=-30.0f;
            f.position.x+=(runtime.playerBody.position.x+ox-f.position.x)*0.15f;
            f.position.y+=(runtime.playerBody.position.y+oy-f.position.y)*0.15f;
            if(f.shootCooldown>0)--f.shootCooldown;
            if(f.shootCooldown<=0&&nearest){
                float dx=nearest->body.position.x-f.position.x,dy=nearest->body.position.y-f.position.y;
                float d=(float)sqrt((double)(dx*dx+dy*dy));
                if(d>0.0f&&d<500.0f){
                    NativeProjectile p;p.position=f.position;
                    p.velocity.x=dx/d*8.0f;p.velocity.y=dy/d*8.0f;
                    p.damage=3.5f;p.travelled=0;p.range=runtime.playerState.stats.range;
                    p.owner=runtime.playerBody.entityToken;
                    runtime.projectiles.active.push_back(p);++runtime.projectiles.spawned;
                    ++shots;f.shootCooldown=45;
                }
            }
        }else if(f.item==10){
            const float angle=f.time*0.10f+(i%2?3.14159265f:0.0f);
            f.position.x=runtime.playerBody.position.x+(float)cos((double)angle)*52.0f;
            f.position.y=runtime.playerBody.position.y+(float)sin((double)angle)*52.0f;
            if(f.contactCooldown>0)--f.contactCooldown;
            if(nearest&&f.contactCooldown<=0){
                float dx=nearest->body.position.x-f.position.x,dy=nearest->body.position.y-f.position.y;
                if(dx*dx+dy*dy<22.0f*22.0f){
                    nearest->Damage(5.0f);++hits;f.contactCooldown=30;
                    if(nearest->dead){++runtime.enemyKills;
                        if(NativeBossEntityType(nearest->type,nearest->body.variant))
                            runtime.bossDefeated=true;
                    }
                }
            }
        }else if(f.item==117){
            if(nearest){
                float dx=nearest->body.position.x-f.position.x,dy=nearest->body.position.y-f.position.y;
                float d=(float)sqrt((double)(dx*dx+dy*dy));
                if(d>0.0f){f.position.x+=dx/d*1.4f;f.position.y+=dy/d*1.4f;}
                if(f.contactCooldown>0)--f.contactCooldown;
                if(d<24.0f&&f.contactCooldown<=0){
                    nearest->Damage(5.0f);++hits;f.contactCooldown=30;
                    if(nearest->dead){
                        ++runtime.enemyKills;
                        if(NativeBossEntityType(nearest->type,nearest->body.variant))
                            runtime.bossDefeated=true;
                    }
                }
            }else{
                const float ox=(i%2)?-34.0f:34.0f,oy=-30.0f;
                f.position.x+=(runtime.playerBody.position.x+ox-f.position.x)*0.15f;
                f.position.y+=(runtime.playerBody.position.y+oy-f.position.y)*0.15f;
            }
        }
        NativePoint clamped;if(room->space.Clamp(f.position,4,4,4,4,&clamped))f.position=clamped;
    }
    return true;
}
void NativeFamiliarSystem::Swap(NativeFamiliarSystem& b){
    active.swap(b.active);unsigned int n=spawned;spawned=b.spawned;b.spawned=n;
    n=shots;shots=b.shots;b.shots=n;n=hits;hits=b.hits;b.hits=n;
}
