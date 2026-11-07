#include "native_room_update.h"
#include "game_session.h"
#include "native_room_effects.h"
#include "native_door_resources.h"
#include "native_player_collision.h"
#include "native_audio.h"
#include <algorithm>
#include <math.h>
#include <string.h>
namespace {
bool ItemHeld(const NativePlayerState& player,int id,bool* held){
    return player.HasCollectible(id,held);
}
//Maintained walk-cycle selection. The native sprites are already bound to the
//original Walk/Head clips; this drives the body state from the evaluated
//velocity and leaves the actual frame advance to EntityBody::UpdateUnmodified.
void StepPlayerWalkAnimation(PortNormalRuntime& runtime){
    NativePlayerSprites& sprites=runtime.playerSprites;
    if(!sprites.bodyReady)return;
    NativeSprite& sprite=sprites.sprites[0];
    if(!sprite.asset)return;
    const float vx=runtime.playerBody.velocity.x,vy=runtime.playerBody.velocity.y;
    const float speed=(float)sqrt((double)(vx*vx+vy*vy));
    const char* walk;
    const char* head;
    if(fabsf(vx)>fabsf(vy)){
        walk=vx>0.0f?"WalkRight":"WalkLeft";
        head=vx>0.0f?"HeadRight":"HeadLeft";
    }else{
        walk=vy>0.0f?"WalkDown":"WalkUp";
        head=vy>0.0f?"HeadDown":"HeadUp";
    }
    if(speed>0.03f){
        if(!sprite.mainState.clip||strcmp(sprite.mainState.clip->name,walk)!=0||!sprite.mainState.playing)
            sprite.Play(walk,true);
        if(!sprite.overlayState.clip||strcmp(sprite.overlayState.clip->name,head)!=0)
            sprite.SetFrame(head,0,true);
    }else{
        if(sprite.mainState.playing)sprite.mainState.playing=false;
    }
}

//007a6f10's selector for the reached ordinary type1 clear reward. Chance is
//xorshift index35 from descriptor+60 (decoration seed); the item/luck gates
//and the exact variant thresholds are from 007a6f10. The final xorshift state
//is the pickup-construction owner, so content creation cannot reroll identity.
//Return -1 only for a source/owner failure; 0 is a legitimate native no-reward
//outcome. -2 means the room-type branch is not this ordinary type1 path.
int SelectReachedClearReward(const NativeRoomState& room,const NativePlacedRoom& descriptor,
    const NativePlayerState& player,unsigned int* ownerState){
    if(room.type!=1)return -2;
    NativeRng rng;rng.state=descriptor.seeds.decoration;rng.index=35;
    bool held=false;
    float chance=0.0f;
    if(!rng.Float(&chance))return -1;
    if(!ItemHeld(player,46,&held))return -1;
    if(held)chance=chance*0.9f+0.1f;
    float luck=player.stats.luck;
    if(luck<0.0f)luck=0.0f;if(luck>10.0f)luck=10.0f;
    float luckDraw=0.0f;
    if(!rng.Float(&luckDraw))return -1;
    chance+=luckDraw*luck*0.1f;
    if(!ItemHeld(player,42,&held))return -1;
    if(held){
        bool direct=false;
        if(!ItemHeld(player,46,&direct))return -1;
        chance=(direct&&chance>0.0f)?chance*0.98f+0.02f:chance*0.9f+0.1f;
    }
    if(!ItemHeld(player,670,&held))return -1;
    if(held&&chance<0.06f){
        float scaled=chance/0.06f;
        if(scaled<0.0f)scaled=0.0f;if(scaled>1.0f)scaled=1.0f;
        chance=0.5f-scaled*0.78f;
    }
    int variant=0;
    const bool lowChance=chance<=0.22f;
    if(!lowChance){
        if(chance<0.3f){
            unsigned int draw=0;
            if(!rng.Next(&draw))return -1;
            if(draw%3U==0U)variant=300;
            else{
                if(!rng.Next(&draw))return -1;
                variant=(draw&1U)?70:350;
            }
        }else if(chance<0.45f)variant=20;
        else{
            if(chance<0.5f){
                bool item84=false;
                if(!ItemHeld(player,84,&item84))return -1;
                if(item84)variant=20;
            }
            if(variant==0&&chance<0.6f){
                bool item22=false;
                if(!ItemHeld(player,22,&item22))return -1;
                if(!item22)variant=10;
                else{
                    unsigned int draw=0;
                    if(!rng.Next(&draw))return -1;
                    if(draw%5U==0U)variant=10;
                }
            }
            //007a7277..98: values below0.8 are variant30, then0.8/0.95 split
            //the ordinary40/50 branches. This also covers the item22 fallthrough.
            if(variant==0&&chance<0.8f)variant=30;
            else if(variant==0&&chance<0.95f)variant=40;
            else if(variant==0)variant=50;
        }
        unsigned int draw=0;
        //007a72a7..e8: variant90 gate, then the unconditional 1/50 grab bag.
        if(!rng.Next(&draw))return -1;
        if(draw%15U==0U){
            bool held72=false;
            if(!ItemHeld(player,72,&held72))return -1;
            if(held72)variant=90;
        }
        if(!rng.Next(&draw))return -1;
        if(draw%50U==0U)variant=69;
        //Later item-gated rerolls. Absent items consume no draw in the native
        //branch because their query guards the following xorshift.
        bool present=false;
        if(!ItemHeld(player,45,&present))return -1;
        if(present){if(!rng.Next(&draw))return -1;if(draw%10U==0U)variant=300;}
        if(!ItemHeld(player,44,&present))return -1;
        if(present){if(!rng.Next(&draw))return -1;if(draw%10U==0U)variant=60;}
        if(!ItemHeld(player,41,&present))return -1;
        if(present){if(!rng.Next(&draw))return -1;if(draw%10U==0U)variant=30;}
        if(!ItemHeld(player,34,&present))return -1;
        if(present){
            if(!rng.Next(&draw))return -1;
            bool item22=false;
            if(!ItemHeld(player,22,&item22))return -1;
            if(draw%10U==0U&&!item22)variant=10;
        }
        if(!ItemHeld(player,36,&present))return -1;
        if(present){if(!rng.Next(&draw))return -1;if(draw%10U==0U)variant=30;}
        if(!ItemHeld(player,479,&present))return -1;
        if(present){if(!rng.Next(&draw))return -1;if(draw%50U==0U)variant=350;}
    }
    //007a7579..: item134 owns its own additional branch. Calling flag is0 for
    //the ordinary type1 clear path reached here.
    bool item134=false;
    if(!ItemHeld(player,134,&item134))return -1;
    if(item134){
        unsigned int draw=0;
        if(!rng.Next(&draw))return -1;
        if(draw%3U==0U){
            if(!rng.Next(&draw))return -1;
            variant=(draw&1U)?60:50;
        }else{
            if(!rng.Next(&draw))return -1;
            if(draw%50U==0U)variant=350;
        }
    }
    if(ownerState)*ownerState=rng.state;
    return variant;
}

int ChooseRewardCell(const NativeRoomState& room){
    const int centerX=room.geometry.width/2;
    const int centerY=room.geometry.height/2;
    int best=-1,bestDistance=0x7fffffff;
    for(int y=0;y<room.geometry.height;++y)for(int x=0;x<room.geometry.width;++x){
        const int cell=y*room.geometry.stride+x;
        if(cell<0||cell>=room.geometry.cells||room.collision.occupied[cell])continue;
        int collision=0;
        if(!room.collision.At(cell,false,&collision)||(collision!=0&&collision!=5))continue;
        const int dx=x-centerX,dy=y-centerY,distance=dx*dx+dy*dy;
        if(distance<bestDistance){best=cell;bestDistance=distance;}
    }
    return best;
}

bool FinalizeDeadRoomActors(PortNormalRuntime& runtime,NativeWorldResident& live,NativeRoomState& room){
    for(int i=0;i<live.enemies.count;){
        NativeEnemy* enemy=live.enemies.entries[i];
        if(enemy&&enemy->dead){
            if(enemy->participates&&NativeBossEntityType(enemy->type,enemy->body.variant))room.bossDead=true;
            if(enemy->participates)runtime.deathFx.Spawn(*enemy);delete enemy;
            for(int j=i+1;j<live.enemies.count;++j)live.enemies.entries[j-1]=live.enemies.entries[j];
            --live.enemies.count;live.enemies.entries[live.enemies.count]=0;continue;
        }
        ++i;
    }
    return true;
}

//00414dc0 is called at007af39e with ECX=room+0x1218. It updates the main
//actor container and0041578a..a2 stores +0xac = +0xa8 (pending registrations)
//+ the active participant count, then clears +0xa8. The reached type80/29
//actors are the live members of that container. Do not substitute the
//enemy-list count for the gate: both +0x12c0 and +0x12c4 are maintained here.
int UpdateRoomActorContainer(NativeRoomState& room,const NativeWorldResident& live){
    int active=0;
    for(int i=0;i<live.enemies.count;++i){
        const NativeEnemy* enemy=live.enemies.entries[i];
        if(enemy&&enemy->participates&&enemy->ready&&!enemy->dead&&!enemy->body.removed)++active;
    }
    room.native12c4=room.native12c0+active;
    room.native12c0=0;
    return room.native12c4;
}

//007cc890(room,9) walks only the list9 component vector at room+720c. The
//reached ordinary room has no represented list9 components; main type80/29
//actors are not members of this list and are owned by the +1218 container
//above. Keep this an explicit ordered boundary before007b1cc0.
bool CleanupReachedList9(NativeRoomState& room){
    return room.list9Count==0;
}

bool CommitRoomClear(PortNormalRuntime& runtime,NativeWorldResident& live,NativeRoomState& room,unsigned int* next){
    if(room.clear)return true;
    NativePlacedRoom* descriptor=runtime.world.CurrentDescriptor();
    if(!descriptor)return false;
    if(descriptor->flags&1U){
        room.clear=true;room.rewardResolved=true;return true;
    }
    //007af651..af69f is evaluated after the025/29/30/31 room updates. The
    //second gate at007af6b5 repeats it after007cc890(room,9).
    if(room.native12c0>0||room.native12c4>0||room.native7130>0||room.native11ec>0)return true;
    if(!CleanupReachedList9(room))return true;

    int variant=0;
    unsigned int ownerState=descriptor->seeds.decoration;
    if(room.encounter){
        const int selected=SelectReachedClearReward(room,*descriptor,runtime.playerState,&ownerState);
        if(selected==-1){if(next)*next=0x007a76a0;return false;}
        //-2 is a non-type1 room branch of007a76a0. The ordinary type1 reward
        //selector and its card/trinket identities are the reached transaction;
        //boss/type5 loot remains a separate explicit native dependency.
        if(selected>0||room.type==1)variant=selected>0?selected:0;
    }
    int subtype=0;
    if(variant){
        //00796e50 owns the constructor seed before006bb0c0. Variant100's
        //00799d40 pool selection consumes that same creation owner.
        NativeRng pickupOwner;pickupOwner.state=ownerState;pickupOwner.index=35;
        unsigned int seed=0;if(!pickupOwner.Next(&seed)){if(next)*next=0x00796e50;return false;}
        subtype=runtime.itemPools.PrepareRewardSubtype(variant,room.type,seed);
        if(subtype<0){if(next)*next=0x006a1900;return false;}
        int cell=ChooseRewardCell(room);float x=0.0f,y=0.0f;
        if(cell<0||!room.geometry.Position(cell,&x,&y)){if(next)*next=0x007bde40;return false;}
        NativePoint position;position.x=x+20.0f;position.y=y+20.0f;
        if(live.pickups.count>=64){if(next)*next=0x006bb0c0;return false;}
        NativePickup* pickup=new NativePickup;
        if(!pickup->Begin(5,variant,subtype,cell,seed,runtime.tickFrame,position)){
            delete pickup;if(next)*next=0x006bb0c0;return false;
        }
        live.pickups.entries[live.pickups.count++]=pickup;
        room.rewardVariant=variant;room.rewardSubtype=subtype;room.rewardResolved=true;
    }else room.rewardResolved=true;

    //007b1cd7/dde: descriptor+44 bit0, descriptor+4a word and event925120.
    descriptor->flags|=1U;
    ++descriptor->clearEventCount;
    room.clear=true;
    live.setup.flags=descriptor->flags;
    //Boss exit: the exact native producer remains unresolved. Keep the
    //maintained trapdoor creation behind the authoritative clear transaction
    //(never while +12c0/+12c4 or the other clear blockers remain), exactly once.
    if((room.type==5||room.type==17)&&room.bossDead&&room.trapdoorState==0){
        room.trapdoorState=1;room.trapdoorTimer=0;
        //A room-record type9000 variant0 grid owns the native cell. Only the
        //unreached rooms without that record retain the maintained center.
        if(room.trapdoorCell<0){
            room.trapdoorX=(room.space.left+room.space.right)*0.5f;
            room.trapdoorY=(room.space.top+room.space.bottom)*0.5f;
        }
    }
    return true;
}
}
NativeRoomUpdate::NativeRoomUpdate(unsigned int frame):enteredFrame(frame),address(0x7ae620),gridCursor(0),active(false),native7114(false),native764c(false),native7648(0),native717c(-1),native7120(0),actorStage(0),actorCursor(0){
    //7a02e2,7a03a6,7a0e07.7a03ac..3cd clears the pending-grid tree.
}
bool NativeRoomUpdate::PrepareInitialActors(PortNormalRuntime& runtime,NativeActionSource* actions,unsigned int* next){
    if(!next)return false;*next=address;
    NativeWorld& world=runtime.world;NativeRoomState* room=world.Current();
    if(!room||!active||!runtime.playerState.runInitializing||runtime.run.context.challenge||!runtime.history.ready)return false;
    NativeWorldResident& live=world.resident[world.currentList];unsigned int frame;
    if(!runtime.history.fields.Get(0x1a2e1c,4,&frame)||frame!=enteredFrame)return false;
    if(address==0x7ae620){
        native7114=false;
        //Delta0 excludes7ae685's delta1 room-entry reward branch.
        if(native7648>=0){
            if(native7648==0&&world.currentGrid==-1){*next=0x7aea4b;return false;}
            --native7648;
        }
        bool held;
        if(!runtime.playerState.HasCollectible(653,&held)){*next=0x7aeefa;return false;}native764c=held;
        if(!live.effects){*next=0x7aef15;return false;}
        if(live.effects->roomEffects.Has(1,677)){*next=0x7aef15;return false;}
        //The actual room effect-list scan for pill64 is empty.
        if(!live.effects->roomEffects.Entries().empty()){*next=0x7aef50;return false;}
        //Challenge40 is excluded by the owned normal challenge0.
        if(native717c>0)--native717c;
        if(!pendingGridRemoval.empty()){*next=0x7af0a4;return false;}
        gridCursor=0;address=0x7af16a;
    }
    if(address==0x7af16a){
        while(gridCursor<(unsigned int)room->geometry.cells){
            const int cell=(int)gridCursor;bool found=false;
            for(int slot=0;slot<8;++slot)if((live.doors->attached&(1U<<slot))&&live.doors->actors[slot].grid==cell){
                //A door whose target sprite binding is an explicit presentation
                //gap still owns its collision and state. Its visual update is
                //not synthesized.
                if(!live.doorVisuals[slot].ready&&live.doorVisuals[slot].unsupportedTarget){room->collision.classes[cell]=4;found=true;break;}
                const NativePlacedRoom* target=world.layout.DoorTarget(world.currentList,slot);
                if(!target||!live.doorVisuals[slot].UpdateInitialClosed(live.doors->actors[slot],*room,target->flags,runtime.run.context.effectiveCurses,runtime.playerBody,runtime.playerState,next))return false;
                room->collision.classes[cell]=4;found=true;break;
            }
            if(!found)for(int i=0;i<room->staticGridCount;++i)if(room->staticGrids[i].grid==cell){
                //6c69c6 installsb2a774: Update+8 is407240. Init and
                //post-construction6d8ce0 are distinct earlier calls.
                if(room->staticGrids[i].nativeType==6){
                    //vtable b2a7d4 virtual+8=6ce290, state0/variant0: no RNG;
                    //virtual+28/6ced40 initializes timer154=0, and update
                    //writes live collision cost1500 with class3.
                    if(room->staticGrids[i].variant!=0||room->staticGrids[i].state!=0){*next=0x7af16a;return false;}
                    room->collision.classes[cell]=3;room->collision.native76c[cell]=1500;
                    room->collision.types[cell]=1002;
                }else if(room->staticGrids[i].nativeType!=4&&room->staticGrids[i].nativeType!=5&&
                         room->staticGrids[i].nativeType!=13&&room->staticGrids[i].nativeType!=14&&
                         room->staticGrids[i].nativeType!=15&&room->staticGrids[i].nativeType!=21&&
                         room->staticGrids[i].nativeType!=24&&room->staticGrids[i].nativeType!=25&&
                         room->staticGrids[i].nativeType!=26){
                    *next=0x7af16a;return false;}
                found=true;break;
            }
            if(!found&&room->collision.occupied[cell]){*next=0x7af16a;return false;}
            ++gridCursor;
        }
        //7a10dd's0xa80-byte clear covers both76c ande6c.
        if(frame%3==0)for(int cell=0;cell<room->geometry.cells;++cell){
            bool destroyedPoop=false;
            for(int i=0;i<room->staticGridCount;++i)
                if(room->staticGrids[i].grid==cell&&room->staticGrids[i].state==1000&&
                   (room->staticGrids[i].nativeType==6||room->staticGrids[i].nativeType==14)){
                    destroyedPoop=true;break;}
            int& cost=room->collision.native76c[cell];const int low=cost%1000;
            if(destroyedPoop){if(cost>0)cost-=std::min(cost,100);}
            else if(cost==3999)cost=900;else if(low<901)cost-=std::min(low,100);
            unsigned short& timer=room->collision.nativeE6c[cell];if(timer>=1&&timer<=99)--timer;
        }
        address=0x7af265;
    }
    if(address==0x7af265){
        bool held;if(!runtime.playerState.HasCollectible(550,&held)||held){*next=0x7af265;return false;}
        //This selected room has no type11/17 or animated backdrop. Its
        //registered1/1000 actors leave the NPC counters12c8/12cc at0.
        if(room->type!=1||live.backdrop<1||live.backdrop>2){*next=0x7af310;return false;}
        for(unsigned int i=0;i<runtime.playerManager.worldTypes.size();++i)
            if(runtime.playerManager.worldTypes[i]>=10&&runtime.playerManager.worldTypes[i]<=999){*next=0x7af369;return false;}
        address=0x414dc0;
    }
    if(address==0x414dc0){
        //414bc0 inserts only actors without flag8 into e_update. The
        //ambient actors were updated explicitly during Init and retain8;
        //they do not get another gameplay Update on this first call.
        const std::vector<unsigned int>& actors=runtime.playerManager.updateMembers;
        if(actors.empty()){*next=0x414f42;return false;}
        if(actorStage==0){
            while(actorCursor<actors.size()){
                if(actors[actorCursor]!=runtime.playerBody.entityToken){*next=0x414f89;return false;}
                NativeEntityBody& body=runtime.playerBody;
                if(body.native16a&&(!(body.flagsLow&8U)||(body.flagsLow&0x20000U)))body.PreUpdate();
                ++actorCursor;
            }
            actorStage=1;actorCursor=0;
        }
        if(actorStage==1){
            while(actorCursor<actors.size()){
                if(actors[actorCursor]!=runtime.playerBody.entityToken){*next=0x4150ba;return false;}
                NativeEntityBody& body=runtime.playerBody;
                //7d9690's real player loop enables the primary controls byte
                //immediately before vtable+0xc. Its fresh cooldown is the
                //initial zero owner; no input or gameplay default is supplied.
                runtime.playerCreation.constructorFields.AssignBytes(0x358,1,1);
                runtime.playerCreation.constructorFields.AssignScalar(0x35c,0);
                if(body.native16a&&(!(body.flagsLow&8U)||(body.flagsLow&0x20000U))&&
                   !runtime.playerUpdate.Resume(runtime,actions,next))return false;
                ++actorCursor;
            }
            actorStage=2;
        }
        //414dc0's player collision pass sets4000, invokes the player update
        //again for collision state, then6712a0 performs the swept response.
        //The second update is represented here by the actual grid response
        //owner; it does not replay the ordinary movement prefix.
        NativeEntityBody& body=runtime.playerBody;
        body.flagsLow|=0x4000U;
        const bool collided=NativePlayerGridCollision(runtime,false,next);
        body.flagsLow&=~0x4000U;
        if(!collided)return false;
        if(!TickSimulationTail(runtime,actions,0,0,next))return false;
        if(!runtime.completePlayerInitializations)runtime.completePlayerInitializations=1;
        ++runtime.completePlayerTicks;
        ++runtime.completeWholeGameplayTicks;
        *next=0;return true;
    }
    *next=address;return false;
}
bool NativeRoomUpdate::TickSimulationTail(PortNormalRuntime& runtime,NativeActionSource* actions,const NativeSceneFiles* files,NativeMt* global,unsigned int* next){
    if(!next)return false;*next=0;
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    NativeRoomState* room=runtime.world.Current();
    if(!room||!live.effects)return false;
    if(runtime.doorTransitionGrace>0)--runtime.doorTransitionGrace;
    bool openedThisTick[8]={false,false,false,false,false,false,false,false};
    //7af3ab ->8d8470: the reached particle emitter is disabled by Room1798+8
    //for this selected room. If it becomes enabled, stop at that caller.
    if(room->particlesEnabled){*next=0x8d8470;return false;}
    //7af636 ->8798f0: the live room-effect cache update is the gameplay
    //owner reached by this room. Visual/room-tail services between these two
    //addresses are presentation work and do not gate simulation completion.
    if(live.effects->roomEffects.Update(0)!=NEF_OK){*next=0x8798f0;return false;}
    //Enemy actors advance before the projectile contact/damage pass.
    for(int i=0;i<live.enemies.count;++i)if(live.enemies.entries[i])live.enemies.entries[i]->Update(*room,runtime.playerBody,&runtime.enemyShots);
    if(runtime.playerInvulnerable>0)--runtime.playerInvulnerable;
    if(!runtime.playerDead)for(int i=0;i<live.enemies.count;++i){NativeEnemy* enemy=live.enemies.entries[i];
        if(!enemy||!enemy->ready||enemy->dead)continue;
        float dx=runtime.playerBody.position.x-enemy->body.position.x,dy=runtime.playerBody.position.y-enemy->body.position.y;
        float radius=enemy->body.collision.radius+runtime.playerBody.collision.radius;
        if(dx*dx+dy*dy>radius*radius)continue;
        int damage=(int)enemy->body.collisionDamage;if(damage<=0)continue;
        if(runtime.playerInvulnerable<=0){
            bool dead=runtime.playerState.Damage(damage);++runtime.playerHits;runtime.playerInvulnerable=30;
            if(dead){runtime.playerDead=true;runtime.playerBody.removed=true;runtime.phase=PNR_DYING;runtime.playerDeathTicks=0;}
            break;
        }
    }
    if(!runtime.enemyShots.Step(runtime)){*next=0x61f6d0;return false;}
    if(!runtime.bombs.Step(runtime)){*next=0x733ef0;return false;}
    if(!runtime.familiars.Sync(runtime)||!runtime.familiars.Step(runtime)){*next=0x73c823;return false;}
    //Boss trapdoor: the opening animation lasts five ticks per original
    //frame, then the hole is usable. Contact requests the maintained
    //next-floor descent from the adapter/session owner.
    if(room->trapdoorState==1){if(++room->trapdoorTimer>=30)room->trapdoorState=2;}
    if(room->trapdoorState==2&&!runtime.playerDead){
        float dx=runtime.playerBody.position.x-room->trapdoorX,dy=runtime.playerBody.position.y-room->trapdoorY;
        if(dx*dx+dy*dy<=24.0f*24.0f)runtime.descendRequested=true;
    }
    //Ordinary pickup contact. Hearts heal red health first then soul health;
    //coins/keys/bombs increment the native resource counters. Other type5
    //variants remain explicit identity owners until their services exist.
    if(!runtime.playerDead)for(int i=0;i<live.pickups.count;++i){NativePickup* p=live.pickups.entries[i];
        if(!p||!p->ready||p->collected)continue;
        float dx=runtime.playerBody.position.x-p->position.x,dy=runtime.playerBody.position.y-p->position.y;
        float radius=runtime.playerBody.collision.radius+12.0f;if(dx*dx+dy*dy>radius*radius)continue;
        bool taken=false;
        if(p->variant==10){
            if(runtime.playerState.redHearts<runtime.playerState.maxRedHearts)
                runtime.playerState.redHearts=std::min(runtime.playerState.maxRedHearts,runtime.playerState.redHearts+2);
            else runtime.playerState.soulHearts=std::min(99,runtime.playerState.soulHearts+2);
            taken=true;
        }else if(p->variant==20){runtime.playerState.coins=std::min(99,runtime.playerState.coins+1);taken=true;}
        else if(p->variant==30){runtime.playerState.keys=std::min(99,runtime.playerState.keys+1);taken=true;}
        else if(p->variant==40){runtime.playerState.bombs=std::min(99,runtime.playerState.bombs+1);taken=true;}
        else if(p->variant==100){
            //Treasure pedestal identity is selected by006a1900 at creation.
            //Collection consumes that stored subtype and never rerolls here.
            int id=p->subtype;
            if(id>0&&runtime.playerState.GrantCollectible(id)){
                unsigned int sample=0;if(global)sample=global->Next();
                if(id==258)runtime.playerState.ApplyMissingNo(runtime.itemPools,sample);
                runtime.lastCollectible=id;++runtime.collectiblesCollected;taken=true;
            }
        }else if(p->variant==300){
            //Original entities2.xml: variant300 is Tarot/Suit Card. The card
            //identity is established by006a1900 and stored in pickup+30.
            int card=p->subtype;
            if(card>0){runtime.playerState.heldCard=card;taken=true;}
        }else if(p->variant==350){
            //Original entities2.xml: variant350 is Trinket. Its available-pool
            //identity was selected at construction and is stored in subtype.
            int index=p->subtype;
            if(index>=0&&index<(int)runtime.playerState.trinketCounts.size()){
                runtime.playerState.trinketCounts[index]=1;
                if(!runtime.playerState.heldTrinkets[0])runtime.playerState.heldTrinkets[0]=index;
                ++runtime.collectiblesCollected;taken=true;
            }
        }else if(p->variant==360){
            //Original entities2.xml: variant360 is Red Chest. Its contents and
            //lifecycle are separate services; it is not a pocket card.
            taken=false;
        }else if(p->variant==70){
            //Original entities2.xml: variant70 is Pill. Subtype is the pill
            //color selected by construction; effect lookup occurs on use.
            int color=p->subtype;
            if(color>=1&&color<=13){
                int effect=runtime.itemPools.pillEffects[color];
                if(effect<0)effect=0;
                runtime.playerState.heldPill=effect;
                runtime.itemPools.pillIdentified[color]=true;
                taken=true;
            }
        }else if(p->variant==380){
            //Original entities2.xml: variant380 is Isaac's Bed, not a pill.
            taken=false;
        }
        if(taken){p->collected=true;++runtime.pickupsCollected;
            if(runtime.audio){
                if(p->variant==10)runtime.audio->PlaySfx("resources/sfx/heartin.wav",0.55f);
                else if(p->variant==20)runtime.audio->PlaySfx("resources/sfx/feedback/penny pickup 1.wav",0.5f);
                else if(p->variant==30)runtime.audio->PlaySfx("resources/sfx/feedback/golden key.wav",0.55f);
                else if(p->variant==40)runtime.audio->PlaySfx("resources/sfx/v2/pickup_lucky_02.wav",0.5f);
                else runtime.audio->PlaySfx("resources/sfx/feedback/chest open 1.wav",0.55f);
            }
        }
    }
    //Primary weapon/projectile lifecycle follows the room simulation owner.
    if(!runtime.projectiles.Step(runtime,actions,runtime.tickFrame,next)){
        if(*next==0)*next=0x61f6d0;return false;
    }
    if(!FinalizeDeadRoomActors(runtime,live,*room)){*next=0x007cc7e0;return false;}
    //007af39e: the room actor container update precedes the clear gates.
    UpdateRoomActorContainer(*room,live);
    if(!CommitRoomClear(runtime,live,*room,next))return false;
    //Native door interaction rules reached by the ordinary runtime:
    //target1 opens on clear, target10 consumes one key on contact, target5
    //is the boss-room entry and opens on clear like an ordinary door. Other
    //locked/secret targets remain closed for their later services.
    if(room->clear&&live.doors)for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){
        NativeDoorActor& door=live.doors->actors[slot];
        if(door.state!=1)continue;
        if(door.targetType==1||door.targetType==5){door.previousState=door.state;door.state=2;++runtime.doorsOpened;openedThisTick[slot]=true;
            if(live.doorVisuals&&live.doorVisuals[slot].ready)live.doorVisuals[slot].Open(door);
            if(runtime.audio)runtime.audio->PlaySfx("resources/sfx/v2/metaldooropen.wav",0.5f);continue;}
        float px,py;if(!room->geometry.Position(door.grid,&px,&py))continue;px+=20.0f;py+=20.0f;
        float dx=runtime.playerBody.position.x-px,dy=runtime.playerBody.position.y-py;
        bool near=dx*dx+dy*dy<=60.0f*60.0f;
        if(door.targetType==10&&near&&runtime.playerState.keys>0){
            --runtime.playerState.keys;door.previousState=door.state;door.state=2;++runtime.doorsUnlocked;openedThisTick[slot]=true;
            if(live.doorVisuals&&live.doorVisuals[slot].ready)live.doorVisuals[slot].Open(door);
            if(runtime.audio)runtime.audio->PlaySfx("resources/sfx/feedback/door heavy open.wav",0.55f);
        }
    }
    //Interactive passage on a cleared normal door. The target resident is
    //constructed from the exact linked descriptor and the player is placed at
    //the reciprocal door cell before the next whole tick.
    if(files&&global&&room->clear&&live.doors&&runtime.doorTransitionGrace==0){
        for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){
            NativeDoorActor& door=live.doors->actors[slot];
            if(door.state!=2||openedThisTick[slot]||(door.targetType!=1&&door.targetType!=5&&door.targetType!=10))continue;
            float px,py;if(!room->geometry.Position(door.grid,&px,&py))continue;px+=20.0f;py+=20.0f;
            float ax=runtime.playerBody.position.x-px,ay=runtime.playerBody.position.y-py;
            if(ax*ax+ay*ay>60.0f*60.0f)continue;
            int reverse=-1;const NativePlacedRoom* target=runtime.world.layout.DoorTarget(runtime.world.currentList,slot,&reverse);
            if(!target)return false;
            //6f5037 snapshots the outgoing live room before6f50c0 switches
            //selected room IDs. This is what makes a later save from another
            //resident carry the consumed pickups and destroyed grids.
            unsigned int captureAddress=0;
            if(!runtime.world.CaptureInitialRoom(&captureAddress)){
                *next=captureAddress?captureAddress:0x79d610;return false;
            }
            NativeWorldIssue issue;
            if(runtime.world.EnterLinked(slot,*files,&issue)!=NWR_OK){runtime.entryIssue=issue;*next=0x6f3b30;return false;}
            NativeWorldResident& entered=runtime.world.resident[runtime.world.currentList];
            if(!entered.doors&&runtime.world.PrepareDoorActors(*global,files->frame)!=NDR_OK){*next=0x6c8140;return false;}
            if(runtime.world.PreparePerimeter(*global,files->frame)!=NWR_OK){*next=0x7c8a90;return false;}
            //7a1ec4 calls each grid's virtual+4. For a non-type16 room,
            //006d8ce0 returns immediately at006d8ef5; door/decor dispatch is
            //407240, a direct no-op. The reached ordinary linked room has no
            //type>=10 room entity for the later007cd3e0 pass, so this bounded
            //post-construction phase is complete before7a3b8a.
            if(!entered.gridPostConstructed&&entered.room->type!=16)
                entered.gridPostConstructed=true;
            //7a3b8a's visit/type bookkeeping follows successful Init and the
            //fresh perimeter. It runs for the first entry and genuine reentry;
            //the clear bit is still sourced only from the live room owner.
            if(runtime.world.RecordActivation(runtime.run)!=NWR_OK){*next=0x7a3b8a;return false;}
            //Door target kinds/variants are gameplay state. Bind them at room
            //entry instead of waiting for the renderer's lazy DoorVisuals call,
            //so clear-state opening and passage work in headless/native ticks.
            runtime.world.PrepareDoorVisuals(runtime.run.context.stage,runtime.run.context.stageType,
                runtime.run.context.nativeMode,runtime.run.context.challenge,runtime.playerState);
            //6fab36 ->6f4e20: both owned map draw-list owners are rebuilt for
            //the entered resident without consuming a gameplay RNG stream.
            if(!runtime.minimaps.EnterLinked(runtime.run,runtime.world,runtime.playerState,next)){
                runtime.missingNativePrerequisite=*next;return false;
            }
            if(entered.doors&&reverse>=0&&reverse<8){
                NativeDoorActor& back=entered.doors->actors[reverse];
                if(back.grid>=0){float bx,by;NativeRoomState* enteredRoom=runtime.world.Current();if(enteredRoom&&enteredRoom->geometry.Position(back.grid,&bx,&by)){
                    runtime.playerBody.position.x=bx+20.0f;runtime.playerBody.position.y=by+20.0f;
                    NativePoint clamped;if(enteredRoom->space.Clamp(runtime.playerBody.position,0,0,0,0,&clamped))runtime.playerBody.position=clamped;
                    runtime.playerBody.previousPosition=runtime.playerBody.position;runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;
                }}
            }
            runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();runtime.deathFx.Clear();runtime.doorTransitionGrace=30;++runtime.roomTransitions;
            {unsigned int sample=0;if(global)sample=global->Next();runtime.playerState.ResetRoomConditionalCache(sample);}
            return true;
        }
    }
    return true;
}
bool NativeRoomUpdate::TickActors(PortNormalRuntime& runtime,NativeActionSource* actions,const NativeSceneFiles* files,NativeMt* global,unsigned int* next){
    if(!next)return false;*next=0;
    NativeWorld& world=runtime.world;NativeRoomState* room=world.Current();
    if(!room||!runtime.playerBody.ready||runtime.playerBody.type!=1||!runtime.playerState.runInitializing)return false;
    runtime.deathFx.Step();
    NativeEntityBody& body=runtime.playerBody;
    if(!body.native16a||body.removed||body.flagsLow&8U)return false;
    body.PreUpdate();
    runtime.playerCreation.constructorFields.AssignBytes(0x358,1,1);
    runtime.playerCreation.constructorFields.AssignScalar(0x35c,0);
    runtime.playerUpdate.Begin();
    if(!runtime.playerUpdate.Resume(runtime,actions,next))return false;
    StepPlayerWalkAnimation(runtime);
    body.flagsLow|=0x4000U;
    const bool collided=NativePlayerGridCollision(runtime,false,next);
    body.flagsLow&=~0x4000U;
    if(!collided)return false;
    if(!TickSimulationTail(runtime,actions,files,global,next))return false;
    if(!runtime.completePlayerInitializations)runtime.completePlayerInitializations=1;
    ++runtime.completePlayerTicks;
    ++runtime.completeWholeGameplayTicks;
    *next=0;return true;
}
