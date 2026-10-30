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
        if(!TickSimulationTail(runtime,actions,next))return false;
        if(!runtime.completePlayerInitializations)runtime.completePlayerInitializations=1;
        ++runtime.completePlayerTicks;
        ++runtime.completeWholeGameplayTicks;
        *next=0;return true;
    }
    *next=address;return false;
}
bool NativeRoomUpdate::TickSimulationTail(PortNormalRuntime& runtime,NativeActionSource* actions,unsigned int* next){
    if(!next)return false;*next=0;
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    NativeRoomState* room=runtime.world.Current();
    if(!room||!live.effects)return false;
    //7af3ab ->8d8470: the reached particle emitter is disabled by Room1798+8
    //for this selected room. If it becomes enabled, stop at that caller.
    if(room->particlesEnabled){*next=0x8d8470;return false;}
    //7af636 ->8798f0: the live room-effect cache update is the gameplay
    //owner reached by this room. Visual/room-tail services between these two
    //addresses are presentation work and do not gate simulation completion.
    if(live.effects->roomEffects.Update(0)!=NEF_OK){*next=0x8798f0;return false;}
    //Enemy actors advance before the projectile contact/damage pass.
    for(int i=0;i<live.enemies.count;++i)if(live.enemies.entries[i])live.enemies.entries[i]->Update(*room,runtime.playerBody);
    //Primary weapon/projectile lifecycle follows the room simulation owner.
    if(!runtime.projectiles.Step(runtime,actions,runtime.tickFrame,next)){
        if(*next==0)*next=0x61f6d0;return false;
    }
    //Room clear opens ordinary doors, not treasure/locked targets. The door
    //actor retains its own state and later interaction services.
    if(room->clear&&live.doors)for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){
        NativeDoorActor& door=live.doors->actors[slot];
        if(door.state==1&&door.targetType==1){door.previousState=door.state;door.state=2;++runtime.doorsOpened;}
    }
    return true;
}
bool NativeRoomUpdate::TickActors(PortNormalRuntime& runtime,NativeActionSource* actions,unsigned int* next){
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
    if(!TickSimulationTail(runtime,actions,next))return false;
    if(!runtime.completePlayerInitializations)runtime.completePlayerInitializations=1;
    ++runtime.completePlayerTicks;
    ++runtime.completeWholeGameplayTicks;
    *next=0;return true;
}
