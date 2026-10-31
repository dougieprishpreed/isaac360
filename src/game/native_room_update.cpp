#include "native_room_update.h"
#include "game_session.h"
#include "native_room_effects.h"
#include "native_door_resources.h"
#include "native_player_collision.h"
#include <algorithm>
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
                if(room->staticGrids[i].type!=15){*next=0x7af16a;return false;}found=true;break;
            }
            if(!found&&room->collision.occupied[cell]){*next=0x7af16a;return false;}
            ++gridCursor;
        }
        //7a10dd's0xa80-byte clear covers both76c ande6c.
        if(frame%3==0)for(int cell=0;cell<room->geometry.cells;++cell){
            int& cost=room->collision.native76c[cell];const int low=cost%1000;
            if(cost==3999)cost=900;else if(low<901)cost-=std::min(low,100);
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
            //Treasure pedestal: the prepared treasure pool selects and the
            //native collectible counter records ownership. Item stat/effect
            //branches remain separate services.
            int id=runtime.itemPools.SelectCollectible(0);
            if(id>0&&runtime.playerState.GrantCollectible(id)){
                runtime.lastCollectible=id;++runtime.collectiblesCollected;taken=true;
            }
        }else if(p->variant==300){
            //Trinket pedestal: choose the first available prepared trinket
            //from the pool's own owner draw and record ownership.
            unsigned int draw=0;
            if(runtime.itemPools.random.Next(&draw)){
                for(unsigned int k=0;k<runtime.itemPools.trinkets.size();++k){
                    unsigned int index=(draw+k)%(unsigned int)runtime.itemPools.trinkets.size();
                    const NativePoolTrinket& t=runtime.itemPools.trinkets[index];
                    if(!t.enabled||!t.unlocked||index>=runtime.playerState.trinketCounts.size())continue;
                    if(runtime.playerState.trinketCounts[index])continue;
                    runtime.playerState.trinketCounts[index]=1;
                    if(!runtime.playerState.heldTrinkets[0])runtime.playerState.heldTrinkets[0]=(int)index;
                    ++runtime.collectiblesCollected;taken=true;break;
                }
            }
        }else if(p->variant==350||p->variant==360){
            //Card/rune pocket slot. A valid subtype names the card identity;
            //otherwise the prepared item owner draws one.
            int card=p->subtype;
            if(p->variant==360)card=(p->subtype>0?1000+p->subtype:0);
            if(card<1||(card>97&&card<1001)||card>1097)card=0;
            if(!card){unsigned int draw=0;
                if(runtime.itemPools.random.Next(&draw))card=1+(int)(draw%97);}
            if(card>0){runtime.playerState.heldCard=card;taken=true;}
        }else if(p->variant==380){
            //Pill pocket slot: subtype is the pill color, or draw a color and
            //resolve its prepared effect id.
            int color=p->subtype;unsigned int draw=0;
            if(color<1||color>13){if(!runtime.itemPools.random.Next(&draw))color=0;else color=1+(int)(draw%13);}
            if(color>=1&&color<=13){
                int effect=runtime.itemPools.pillEffects[color];
                if(effect<0)effect=0;
                runtime.playerState.heldPill=effect;
                runtime.itemPools.pillIdentified[color]=true;
                taken=true;
            }
        }
        if(taken){p->collected=true;++runtime.pickupsCollected;}
    }
    //Primary weapon/projectile lifecycle follows the room simulation owner.
    if(!runtime.projectiles.Step(runtime,actions,runtime.tickFrame,next)){
        if(*next==0)*next=0x61f6d0;return false;
    }
    //Native door interaction rules reached by the ordinary runtime:
    //target1 opens on clear, target10 consumes one key on contact, target5
    //is the boss-room entry and opens on clear like an ordinary door. Other
    //locked/secret targets remain closed for their later services.
    if(room->clear&&live.doors)for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){
        NativeDoorActor& door=live.doors->actors[slot];
        if(door.state!=1)continue;
        if(door.targetType==1||door.targetType==5){door.previousState=door.state;door.state=2;++runtime.doorsOpened;openedThisTick[slot]=true;continue;}
        float px,py;if(!room->geometry.Position(door.grid,&px,&py))continue;px+=20.0f;py+=20.0f;
        float dx=runtime.playerBody.position.x-px,dy=runtime.playerBody.position.y-py;
        bool near=dx*dx+dy*dy<=60.0f*60.0f;
        if(door.targetType==10&&near&&runtime.playerState.keys>0){
            --runtime.playerState.keys;door.previousState=door.state;door.state=2;++runtime.doorsUnlocked;openedThisTick[slot]=true;
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
            NativeWorldIssue issue;
            if(runtime.world.EnterLinked(slot,*files,&issue)!=NWR_OK){runtime.entryIssue=issue;*next=0x6f3b30;return false;}
            NativeWorldResident& entered=runtime.world.resident[runtime.world.currentList];
            if(!entered.doors&&runtime.world.PrepareDoorActors(*global,files->frame)!=NDR_OK){*next=0x6c8140;return false;}
            if(runtime.world.PreparePerimeter(*global,files->frame)!=NWR_OK){*next=0x7c8a90;return false;}
            if(entered.doors&&reverse>=0&&reverse<8){
                NativeDoorActor& back=entered.doors->actors[reverse];
                if(back.grid>=0){float bx,by;NativeRoomState* enteredRoom=runtime.world.Current();if(enteredRoom&&enteredRoom->geometry.Position(back.grid,&bx,&by)){
                    runtime.playerBody.position.x=bx+20.0f;runtime.playerBody.position.y=by+20.0f;
                    NativePoint clamped;if(enteredRoom->space.Clamp(runtime.playerBody.position,0,0,0,0,&clamped))runtime.playerBody.position=clamped;
                    runtime.playerBody.previousPosition=runtime.playerBody.position;runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;
                }}
            }
            runtime.projectiles.Reset();runtime.enemyShots.Clear();runtime.bombs.Clear();runtime.familiars.Clear();runtime.doorTransitionGrace=30;++runtime.roomTransitions;
            runtime.playerState.ResetRoomConditionalCache();
            return true;
        }
    }
    return true;
}
bool NativeRoomUpdate::TickActors(PortNormalRuntime& runtime,NativeActionSource* actions,const NativeSceneFiles* files,NativeMt* global,unsigned int* next){
    if(!next)return false;*next=0;
    NativeWorld& world=runtime.world;NativeRoomState* room=world.Current();
    if(!room||!runtime.playerBody.ready||runtime.playerBody.type!=1||!runtime.playerState.runInitializing)return false;
    NativeEntityBody& body=runtime.playerBody;
    if(!body.native16a||body.removed||body.flagsLow&8U)return false;
    body.PreUpdate();
    runtime.playerCreation.constructorFields.AssignBytes(0x358,1,1);
    runtime.playerCreation.constructorFields.AssignScalar(0x35c,0);
    runtime.playerUpdate.Begin();
    if(!runtime.playerUpdate.Resume(runtime,actions,next))return false;
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
