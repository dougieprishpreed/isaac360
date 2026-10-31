#include "native_enemy.h"
#include "native_projectile.h"
#include "native_room_state.h"
#include <math.h>
#include <string.h>
namespace {
float Length(NativePoint p){return (float)sqrt((double)(p.x*p.x+p.y*p.y));}
//Advance with the room collision owner, sliding along one axis when the
//diagonal destination is blocked. Ordinary floor/solid classes are unchanged
//from the previous actor update.
bool Passable(const NativeRoomState& room,float x,float y){
    int collision=0;NativePoint at={x,y};
    return room.collision.AtPosition(room.geometry,at,room.space.zeroCollision,&collision)&&(collision==0||collision==5);
}
void MoveBody(const NativeRoomState& room,NativeEntityBody& body,float vx,float vy){
    body.velocity.x=vx;body.velocity.y=vy;
    if(Passable(room,body.position.x+vx,body.position.y+vy)){
        body.position.x+=vx;body.position.y+=vy;return;
    }
    if(Passable(room,body.position.x+vx,body.position.y)){body.position.x+=vx;return;}
    if(Passable(room,body.position.x,body.position.y+vy)){body.position.y+=vy;}
}
}
NativeEnemy::NativeEnemy():ready(false),dead(false),type(0),seed(0){}
bool NativeEnemy::Begin(int type,int variant,unsigned int seed,const NativePoint& position,unsigned int frame,int stage){
    if(!NativePreparedEnemyBody(type,variant)||!seed)return false;
    NativeEntityBody next;
    if(next.BeginFresh(type,variant,0,seed,frame,stage,&position)!=NEB_OK)return false;
    body.Swap(next);this->type=type;this->seed=seed;dead=false;ready=true;return true;
}
void NativeEnemy::Update(const NativeRoomState& room,const NativeEntityBody& player,NativeEnemyShotSystem* shots){
    if(!ready||dead||body.removed||!player.ready)return;
    NativePoint delta={player.position.x-body.position.x,player.position.y-body.position.y};
    const float distance=Length(delta);
    const float dx=distance>0.0f?delta.x/distance:0.0f;
    const float dy=distance>0.0f?delta.y/distance:0.0f;
    const int phase=(int)body.time;
    const unsigned int salt=seed&63U;
    float vx=0.0f,vy=0.0f;
    if(type==13||type==18||type==25||type==61||type==240||type==242||type==248||type==38||type==90||type==91||type==92||type==93||type==212||type==219||type==254||(type==260&&body.variant==10)||type==213||type==223||type==246||type==258||type==288||type==83||type==868||type==58||type==60||(type==229&&body.variant==1)||type==286||type==859){
        //Fly: fast erratic drift with a per-entity phase offset.
        const float wobble=sinf(phase*0.45f+(float)salt*0.19f);
        const float sway=cosf(phase*0.37f+(float)salt*0.11f);
        vx=(dx*0.75f+wobble*0.85f)*0.85f;
        vy=(dy*0.75f+sway*0.85f)*0.85f;
    }else if(type==14){
        //Pooter: hold a firing distance and shoot at the player on its own
        //cadence. The shot owner advances in the room tick.
        const float want=120.0f;
        const float approach=distance<want-24.0f?-1.0f:(distance>want+24.0f?1.0f:0.0f);
        vx=dx*approach*0.40f;vy=dy*approach*0.40f;
        if(shots&&distance>8.0f&&distance<440.0f&&((phase+(int)salt)%90)==0){
            NativePoint origin={body.position.x+dx*20.0f,body.position.y+dy*20.0f};
            shots->Fire(origin,dx*3.4f,dy*3.4f,1.0f,7.0f,240,body.entityToken);
        }
    }else if(type==208){
        //Fatty: slow walk, with a periodic lunge toward the player.
        const int cycle=phase%150;
        if(cycle==0&&distance>0.0f){vx=dx*2.4f;vy=dy*2.4f;}
        else if(cycle<18){vx=body.velocity.x*0.92f;vy=body.velocity.y*0.92f;}
        else{vx=dx*0.30f;vy=dy*0.30f;}
    }else if(type==226||type==237||type==21||type==23||type==41||type==881){
        //Skinny/Rotty: charge windows separated by short recovery.
        const int cycle=phase%70;const float speed=body.variant==1?1.55f:1.10f;
        if(cycle>=16&&cycle<46){vx=dx*speed;vy=dy*speed;}
        else{vx=dx*0.22f;vy=dy*0.22f;}
    }else if(type==217||type==29||type==34){
        //Dip: short hop then a slow approach.
        const int cycle=phase%90;
        if(cycle<12){vx=dx*1.6f;vy=dy*1.6f;}
        else if(cycle<30){vx=body.velocity.x*0.90f;vy=body.velocity.y*0.90f;}
        else{vx=dx*0.25f;vy=dy*0.25f;}
    }else if(type==256){
        //Dart Fly: committed fast dart toward the player.
        vx=dx*1.05f;vy=dy*1.05f;
    }else if(type==20){
        //Monstro: slow walk with a committed jump and a five-tear spread.
        const int cycle=phase%180;
        if(cycle>=70&&cycle<92){vx=dx*2.2f;vy=dy*2.2f;}
        else{vx=dx*0.38f;vy=dy*0.38f;}
        if(shots&&distance<540.0f&&cycle==130){
            const float px=-dy,py=dx;
            for(int k=-2;k<=2;++k){
                NativePoint origin={body.position.x+dx*28.0f+px*(float)k*12.0f,body.position.y+dy*28.0f+py*(float)k*12.0f};
                shots->Fire(origin,dx*3.0f+px*(float)k*0.55f,dy*3.0f+py*(float)k*0.55f,1.0f,8.0f,240,body.entityToken);
            }
        }
    }else if(type==19){
        //Larry Jr.: committed charges separated by recovery.
        const int cycle=phase%90;
        if(cycle<42){vx=dx*0.95f;vy=dy*0.95f;}
        else{vx=dx*0.25f;vy=dy*0.25f;}
        if(shots&&distance<520.0f&&cycle==62)
            shots->Fire(body.position,dx*3.2f,dy*3.2f,1.0f,8.0f,240,body.entityToken);
    }else if(type==67||type==260||type==79||type==908||type==28||type==36||type==62||type==68||type==71||type==99||type==262||type==264||type==916||type==72||type==43||type==45||type==263||type==265||type==900||type==75||type==76||type==78||type==266||type==270||type==74||type==81){
        //Duke/Haunt: floating boss with a periodic spread volley.
        const int cycle=phase%150;
        vx=dx*0.32f;vy=dy*0.32f;
        if(shots&&distance<540.0f&&cycle==90){
            const float px=-dy,py=dx;
            for(int k=-1;k<=2;++k){
                NativePoint origin={body.position.x+dx*24.0f+px*(float)k*12.0f,body.position.y+dy*24.0f+py*(float)k*12.0f};
                shots->Fire(origin,dx*2.9f+px*(float)k*0.7f,dy*2.9f+py*(float)k*0.7f,1.0f,8.0f,240,body.entityToken);
            }
        }
    }else if(type==261){
        //Dingle/Dangle: slide toward the player, then spit a three-tear
        //spread before recovering.
        const int cycle=phase%150;
        if(cycle<20){vx=dx*0.18f;vy=dy*0.18f;}
        else if(cycle<85){const float speed=body.variant==1?0.85f:0.95f;vx=dx*speed;vy=dy*speed;}
        if(shots&&distance<520.0f&&cycle==100){
            const float px=-dy,py=dx;
            for(int k=-1;k<=1;++k){
                NativePoint origin={body.position.x+dx*24.0f+px*(float)k*10.0f,body.position.y+dy*24.0f+py*(float)k*10.0f};
                shots->Fire(origin,dx*3.0f+px*(float)k*0.9f,dy*3.0f+py*(float)k*0.9f,1.0f,8.0f,240,body.entityToken);
            }
        }
    }else if(type==284){
        //Cyclopia: longer wind-up, then a committed charge.
        const int cycle=phase%110;
        if(cycle>=20&&cycle<58){vx=dx*1.05f;vy=dy*1.05f;}
        else if(cycle<20){vx=0.0f;vy=0.0f;}
        else{vx=dx*0.30f;vy=dy*0.30f;}
    }else if(type==33||type==235||type==42||type==80||type==205||type==202||type==236||type==306){
        //Fireplace: stationary environmental hazard.
        vx=0.0f;vy=0.0f;
    }else{
        //Gaper family walk with a short periodic pause.
        const int cycle=phase%80;
        if(distance>18.0f&&!(cycle>=52&&cycle<62)){vx=dx*0.55f;vy=dy*0.55f;}
    }
    MoveBody(room,body,vx,vy);
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
