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
    if(type==13||type==18||type==25||type==61||type==240||type==242||type==248||type==38||type==90||type==91||type==92||type==93||type==212||type==219||type==254||(type==260&&body.variant==10)||type==213||type==223||type==246||type==258||type==288||type==83||type==868||type==58||type==60||(type==229&&body.variant==1)||type==286||type==859||type==285||type==259||type==816||type==225||type==279){
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
    }else if(type==226||type==237||type==21||type==23||type==41||type==881||type==834){
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
    }else if(type==36){
        //Gurdy: stationary, with a five-shot blood spread and a radial burst.
        vx=0.0f;vy=0.0f;
        const int cycle=phase%180;
        if(shots&&distance<600.0f){
            if(cycle==55){
                const float px=-dy,py=dx;
                for(int k=-2;k<=2;++k){
                    NativePoint origin={body.position.x+dx*30.0f,body.position.y+dy*30.0f};
                    shots->Fire(origin,dx*3.0f+px*(float)k*0.65f,dy*3.0f+py*(float)k*0.65f,1.0f,8.0f,300,body.entityToken);
                }
            }else if(cycle==135){
                for(int k=0;k<8;++k){
                    const float angle=(float)(6.283185307179586*(double)k/8.0);
                    shots->Fire(body.position,(float)cos((double)angle)*2.6f,(float)sin((double)angle)*2.6f,1.0f,8.0f,300,body.entityToken);
                }
            }
        }
    }else if(type==62&&body.variant==0){
        //Pin: stationary wind-up, committed dash, then a three-shot spread.
        const int cycle=phase%130;
        if(cycle<50){vx=dx*0.12f;vy=dy*0.12f;}
        else if(cycle<88){vx=dx*2.15f;vy=dy*2.15f;}
        else{vx=0.0f;vy=0.0f;}
        if(shots&&distance<560.0f&&cycle==100){
            const float px=-dy,py=dx;
            for(int k=-1;k<=1;++k){
                NativePoint origin={body.position.x+dx*26.0f,body.position.y+dy*26.0f};
                shots->Fire(origin,dx*3.2f+px*(float)k*0.8f,dy*3.2f+py*(float)k*0.8f,1.0f,8.0f,300,body.entityToken);
            }
        }
    }else if(type==68){
        //Peep: short hops, a four-shot spread and a periodic ring.
        const int cycle=phase%160;
        const float hop=cycle<16?1.55f:0.20f;
        vx=dx*hop;vy=dy*hop;
        if(shots&&distance<580.0f){
            const float px=-dy,py=dx;
            if(cycle==60){
                for(int k=-2;k<=2;k+=2){
                    NativePoint origin={body.position.x+dx*30.0f,body.position.y+dy*30.0f};
                    shots->Fire(origin,dx*3.0f+px*(float)k*0.6f,dy*3.0f+py*(float)k*0.6f,1.0f,8.0f,300,body.entityToken);
                }
            }else if(cycle==132){
                for(int k=0;k<6;++k){
                    const float angle=(float)(6.283185307179586*(double)k/6.0);
                    shots->Fire(body.position,(float)cos((double)angle)*2.7f,(float)sin((double)angle)*2.7f,1.0f,8.0f,300,body.entityToken);
                }
            }
        }
    }else if(type==71){
        //Fistula: slow elastic bounce with a three-shot spread.
        const int cycle=phase%140;
        vx=dx*0.48f+sinf(phase*0.18f+(float)salt*0.07f)*0.55f;
        vy=dy*0.48f+cosf(phase*0.21f+(float)salt*0.09f)*0.55f;
        if(shots&&distance<560.0f&&cycle==82){
            const float px=-dy,py=dx;
            for(int k=-1;k<=1;++k){
                NativePoint origin={body.position.x+dx*28.0f,body.position.y+dy*28.0f};
                shots->Fire(origin,dx*2.9f+px*(float)k*0.85f,dy*2.9f+py*(float)k*0.85f,1.0f,8.0f,300,body.entityToken);
            }
        }
    }else if(type==262){
        //Mega Maw: charge window followed by a five-shot spread.
        const int cycle=phase%200;
        if(cycle<70){vx=dx*0.25f;vy=dy*0.25f;}
        else if(cycle<112){vx=dx*1.85f;vy=dy*1.85f;}
        else{vx=0.0f;vy=0.0f;}
        if(shots&&distance<600.0f&&cycle==145){
            const float px=-dy,py=dx;
            for(int k=-2;k<=2;++k){
                NativePoint origin={body.position.x+dx*32.0f,body.position.y+dy*32.0f};
                shots->Fire(origin,dx*3.1f+px*(float)k*0.7f,dy*3.1f+py*(float)k*0.7f,1.0f,8.0f,300,body.entityToken);
            }
        }
    }else if(type==67){
        //Duke of Flies: slow float with a four-way cross and a wider ring.
        const int cycle=phase%160;
        vx=dx*0.30f;vy=dy*0.30f;
        if(shots&&distance<560.0f){
            if(cycle==70){
                const float px=-dy,py=dx;
                for(int k=-1;k<=1;k+=2){
                    NativePoint origin={body.position.x+px*(float)k*14.0f,body.position.y+py*(float)k*14.0f};
                    shots->Fire(origin,px*(float)k*3.0f,py*(float)k*3.0f,1.0f,8.0f,260,body.entityToken);
                    shots->Fire(origin,dx*3.0f,dy*3.0f,1.0f,8.0f,260,body.entityToken);
                }
            }else if(cycle==122){
                for(int k=0;k<6;++k){
                    const float angle=(float)(6.283185307179586*(double)k/6.0);
                    shots->Fire(body.position,(float)cos((double)angle)*2.6f,(float)sin((double)angle)*2.6f,1.0f,8.0f,260,body.entityToken);
                }
            }
        }
    }else if(type==260){
        //Haunt: fade in, ghost dash, then spectral spread and aimed burst.
        const int cycle=phase%190;
        if(cycle>=60&&cycle<92){vx=dx*2.35f;vy=dy*2.35f;}
        else{vx=dx*0.34f;vy=dy*0.34f;}
        if(shots&&distance<600.0f){
            if(cycle==122){
                const float px=-dy,py=dx;
                for(int k=-2;k<=2;++k){
                    NativePoint origin={body.position.x+dx*26.0f,body.position.y+dy*26.0f};
                    shots->Fire(origin,dx*2.9f+px*(float)k*0.62f,dy*2.9f+py*(float)k*0.62f,1.0f,8.0f,260,body.entityToken);
                }
            }else if(cycle==160){
                for(int k=-1;k<=1;++k){
                    const float px=-dy,py=dx;
                    NativePoint origin={body.position.x+dx*20.0f,body.position.y+dy*20.0f};
                    shots->Fire(origin,dx*3.3f+px*(float)k*0.9f,dy*3.3f+py*(float)k*0.9f,1.0f,8.0f,260,body.entityToken);
                }
            }
        }
    }else if(type==79){
        //Gemini: close contact charge with a short spread recovery.
        const int cycle=phase%120;
        if(cycle>=22&&cycle<58){vx=dx*1.75f;vy=dy*1.75f;}
        else{vx=dx*0.42f;vy=dy*0.42f;}
        if(shots&&distance<460.0f&&cycle==82){
            const float px=-dy,py=dx;
            for(int k=-1;k<=1;++k){
                NativePoint origin={body.position.x+dx*18.0f,body.position.y+dy*18.0f};
                shots->Fire(origin,dx*3.0f+px*(float)k*0.8f,dy*3.0f+py*(float)k*0.8f,1.0f,7.0f,220,body.entityToken);
            }
        }
    }else if(type==28){
        //Chub: charge, recovery, then a radial burst.
        const int cycle=phase%170;
        if(cycle>=42&&cycle<86){vx=dx*1.95f;vy=dy*1.95f;}
        else{vx=dx*0.24f;vy=dy*0.24f;}
        if(shots&&distance<560.0f&&cycle==112){
            for(int k=0;k<8;++k){
                const float angle=(float)(6.283185307179586*(double)k/8.0);
                shots->Fire(body.position,(float)cos((double)angle)*2.55f,(float)sin((double)angle)*2.55f,1.0f,8.0f,260,body.entityToken);
            }
        }
    }else if(type==99){
        //Gurdy Jr.: bouncing charge with a six-way ring.
        const int cycle=phase%140;
        vx=dx*0.55f+sinf(phase*0.23f+(float)salt*0.08f)*0.75f;
        vy=dy*0.55f+cosf(phase*0.19f+(float)salt*0.06f)*0.75f;
        if(shots&&distance<560.0f&&cycle==96){
            for(int k=0;k<6;++k){
                const float angle=(float)(6.283185307179586*(double)k/6.0);
                shots->Fire(body.position,(float)cos((double)angle)*2.8f,(float)sin((double)angle)*2.8f,1.0f,8.0f,260,body.entityToken);
            }
        }
    }else if(type==264){
        //Mega Fatty: crouch, committed leap, then an aimed five-tear fan.
        const int cycle=phase%210;
        if(cycle<55){vx=dx*0.16f;vy=dy*0.16f;}
        else if(cycle<102){vx=dx*2.3f;vy=dy*2.3f;}
        else{vx=dx*0.28f;vy=dy*0.28f;}
        if(shots&&distance<620.0f&&cycle==142){
            const float px=-dy,py=dx;
            for(int k=-2;k<=2;++k){
                NativePoint origin={body.position.x+dx*30.0f,body.position.y+dy*30.0f};
                shots->Fire(origin,dx*3.1f+px*(float)k*0.68f,dy*3.1f+py*(float)k*0.68f,1.0f,8.0f,280,body.entityToken);
            }
        }
    }else if(type==916){
        //Bumbino: short charge and a staggered three-shot fan.
        const int cycle=phase%190;
        if(cycle>=30&&cycle<82){vx=dx*1.85f;vy=dy*1.85f;}
        else{vx=dx*0.38f;vy=dy*0.38f;}
        if(shots&&distance<560.0f){
            if(cycle==120){
                const float px=-dy,py=dx;
                for(int k=-1;k<=1;++k){
                    NativePoint origin={body.position.x+dx*24.0f,body.position.y+dy*24.0f};
                    shots->Fire(origin,dx*3.2f+px*(float)k*0.8f,dy*3.2f+py*(float)k*0.8f,1.0f,8.0f,260,body.entityToken);
                }
            }else if(cycle==158){
                for(int k=-1;k<=1;k+=2){
                    const float px=-dy,py=dx;
                    NativePoint origin={body.position.x+px*(float)k*16.0f,body.position.y+py*(float)k*16.0f};
                    shots->Fire(origin,px*(float)k*2.7f,py*(float)k*2.7f,1.0f,8.0f,260,body.entityToken);
                }
            }
        }
    }else if(type==908){
        //Baby Plum: short hops, a radial burst and a returning spread.
        const int cycle=phase%160;
        if(cycle<14){vx=dx*1.45f;vy=dy*1.45f;}
        else{vx=dx*0.30f;vy=dy*0.30f;}
        if(shots&&distance<560.0f){
            if(cycle==70){
                for(int k=0;k<6;++k){
                    const float angle=(float)(6.283185307179586*(double)k/6.0);
                    shots->Fire(body.position,(float)cos((double)angle)*2.65f,(float)sin((double)angle)*2.65f,1.0f,7.0f,240,body.entityToken);
                }
            }else if(cycle==122){
                const float px=-dy,py=dx;
                for(int k=-1;k<=1;++k){
                    NativePoint origin={body.position.x+dx*22.0f,body.position.y+dy*22.0f};
                    shots->Fire(origin,dx*3.0f+px*(float)k*0.75f,dy*3.0f+py*(float)k*0.75f,1.0f,7.0f,240,body.entityToken);
                }
            }
        }
    }else if(type==69){
        //Loki: quick dash, a four-way cross, then an aimed three-shot fan.
        const int cycle=phase%180;
        if(cycle>=40&&cycle<72){vx=dx*2.05f;vy=dy*2.05f;}
        else{vx=dx*0.32f;vy=dy*0.32f;}
        if(shots&&distance<580.0f){
            if(cycle==100){
                const float px=-dy,py=dx;
                shots->Fire(body.position,dx*3.1f,dy*3.1f,1.0f,8.0f,260,body.entityToken);
                shots->Fire(body.position,-dx*3.1f,-dy*3.1f,1.0f,8.0f,260,body.entityToken);
                shots->Fire(body.position,px*3.1f,py*3.1f,1.0f,8.0f,260,body.entityToken);
                shots->Fire(body.position,-px*3.1f,-py*3.1f,1.0f,8.0f,260,body.entityToken);
            }else if(cycle==150){
                const float px=-dy,py=dx;
                for(int k=-1;k<=1;++k){
                    NativePoint origin={body.position.x+dx*24.0f,body.position.y+dy*24.0f};
                    shots->Fire(origin,dx*3.2f+px*(float)k*0.85f,dy*3.2f+py*(float)k*0.85f,1.0f,8.0f,260,body.entityToken);
                }
            }
        }
    }else if(type==406){
        //Ultra Greed: slow chase, committed dash, radial coin burst and an
        //aimed spread. The prepared coin actor is not spawned yet, so these
        //are the boss's hostile shot owners.
        const bool greedier=body.variant==1;
        const int cycle=phase%240;
        if(cycle>=60&&cycle<96){vx=dx*(greedier?2.8f:2.4f);vy=dy*(greedier?2.8f:2.4f);}
        else{vx=dx*(greedier?0.42f:0.35f);vy=dy*(greedier?0.42f:0.35f);}
        if(shots&&distance<620.0f){
            if(cycle==120){
                for(int k=0;k<8;++k){
                    const float angle=(float)(6.283185307179586*(double)k/8.0);
                    shots->Fire(body.position,(float)cos((double)angle)*3.0f,(float)sin((double)angle)*3.0f,
                        greedier?1.5f:1.0f,8.0f,300,body.entityToken);
                }
            }else if(cycle==190){
                const float px=-dy,py=dx;
                for(int k=-1;k<=1;++k){
                    NativePoint origin={body.position.x+dx*30.0f,body.position.y+dy*30.0f};
                    shots->Fire(origin,dx*3.4f+px*(float)k*0.8f,dy*3.4f+py*(float)k*0.8f,
                        greedier?2.0f:1.5f,8.0f,300,body.entityToken);
                }
            }
        }
    }else if(type==67||type==260||type==79||type==908||type==28||type==36||type==62||type==68||type==71||type==99||type==262||type==264||type==916||type==72||type==43||type==45||type==263||type==265||type==900||type==75||type==76||type==78||type==266||type==270||type==74||type==81||type==50||type==52||type==69||type==84||type==100||type==102||type==273||type==412||type==267||type==268||type==269){
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
    }else if(type==33||type==235||type==42||type==80||type==205||type==202||type==236||type==306||type==203||type==291||type==833){
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

NativeDeathFxSystem::NativeDeathFxSystem():spawned(0){}
void NativeDeathFxSystem::Clear(){active.clear();spawned=0;}
void NativeDeathFxSystem::Spawn(const NativeEnemy& enemy){
    if(!enemy.ready)return;
    NativeDeathFx fx;fx.type=enemy.type;fx.variant=enemy.body.variant;
    fx.position=enemy.body.position;fx.time=0;
    active.push_back(fx);++spawned;
}
void NativeDeathFxSystem::Step(){
    for(unsigned int i=0;i<active.size();){
        active[i].time+=1.0f;
        if(active[i].time>30.0f){active.erase(active.begin()+i);continue;}
        ++i;
    }
}
void NativeDeathFxSystem::Swap(NativeDeathFxSystem& b){
    active.swap(b.active);unsigned int n=spawned;spawned=b.spawned;b.spawned=n;
}
