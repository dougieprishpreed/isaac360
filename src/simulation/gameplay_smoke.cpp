#include "../game/game_session.h"
#include "../game/native_room_effects.h"
#include "../game/native_room_view.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>
namespace {
unsigned int RunMovementTicks(PortGameSession& session,PortNormalRuntime& runtime,float x,float y,unsigned int count){
    unsigned int done=0;
    for(unsigned int i=0;i<count;++i){
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime.run.context.stage),runtime.run.context.nativeMode,0};
        NativeActionFrame frame;frame.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame+i);
        frame.SupplyConnection(true);frame.SupplyDrop(false);
        for(int action=0;action<12;++action){frame.SupplyHeld(action,false);frame.SupplyPressed(action,false);}
        for(int action=0;action<8;++action)frame.SupplyValue(action,0);
        if(x>0)frame.SupplyValue(1,x);else if(x<0)frame.SupplyValue(0,-x);
        if(y>0)frame.SupplyValue(3,y);else if(y<0)frame.SupplyValue(2,-y);
        NativeFrameActions actions(frame);
        if(session.TickNormal(runtime,files,&actions)!=NFR_OK)break;
        ++done;
    }
    return done;
}
unsigned int RunProjectileTicks(PortGameSession& session,PortNormalRuntime& runtime,int shootA,int shootB,unsigned int count){
    unsigned int done=0;
    for(unsigned int i=0;i<count;++i){
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime.run.context.stage),runtime.run.context.nativeMode,0};
        NativeActionFrame frame;frame.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame+i);
        frame.SupplyConnection(true);frame.SupplyDrop(false);
        for(int action=0;action<12;++action){frame.SupplyHeld(action,false);frame.SupplyPressed(action,false);}
        for(int action=0;action<8;++action)frame.SupplyValue(action,0);
        if(shootA>=0)frame.SupplyValue(shootA,1.0f);
        if(shootB>=0)frame.SupplyValue(shootB,1.0f);
        NativeFrameActions actions(frame);
        if(session.TickNormal(runtime,files,&actions)!=NFR_OK)break;
        ++done;
    }
    return done;
}
unsigned int AdvanceToBoundary(PortGameSession& session,PortNormalRuntime& runtime){
    unsigned int steps=0;
    while(runtime.phase==PNR_GENERATING||runtime.phase==PNR_ROOM_COMPONENTS){
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime.run.context.stage),runtime.run.context.nativeMode,0};
        //An explicit connected virtual device with released buttons. This
        //supplies only external input; initialized gameplay state is owned
        //and produced by the shared New Run, not by the host adapter.
        NativeActionFrame frame;frame.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame);
        frame.SupplyConnection(true);frame.SupplyDrop(false);
        for(int action=0;action<12;++action){frame.SupplyHeld(action,false);frame.SupplyPressed(action,false);if(action<8)frame.SupplyValue(action,0);}
        NativeFrameActions actions(frame);session.AdvanceNormalSetup(runtime,files,&actions);++steps;
        if(steps==1000000)return 0;
    }
    return steps;
}
}
int RunLockProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING)return 50;
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY)return 51;
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];NativeRoomState* room=runtime->world.Current();
    int treasure=-1;float tx=0,ty=0;
    if(live.doors)for(int i=0;i<8;++i)if((live.doors->attached&(1U<<i))&&live.doors->actors[i].targetType==10){
        float px,py;if(room->geometry.Position(live.doors->actors[i].grid,&px,&py)){treasure=i;tx=px+20.0f;ty=py+20.0f;break;}
    }
    if(treasure<0)return 52;
    runtime->playerBody.position.x=tx;runtime->playerBody.position.y=ty;runtime->playerState.keys=0;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1))return 53;
    bool closedNoKey=live.doors->actors[treasure].state==1&&runtime->doorsUnlocked==0;
    runtime->playerState.keys=1;runtime->playerBody.position.x=tx;runtime->playerBody.position.y=ty;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1))return 54;
    bool openedWithKey=live.doors->actors[treasure].state==2&&runtime->doorsUnlocked==1&&runtime->playerState.keys==0;
    int boss=-1;for(int i=0;i<8;++i)if((live.doors->attached&(1U<<i))&&i!=treasure){boss=i;break;}
    if(boss<0)return 55;
    live.doors->actors[boss].state=1;live.doors->actors[boss].previousState=1;live.doors->actors[boss].targetType=5;
    runtime->bossDefeated=false;runtime->playerBody.position.x=100.0f;runtime->playerBody.position.y=200.0f;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1))return 56;
    bool bossOpen=live.doors->actors[boss].state==2;
    printf("LOCK treasureSlot=%d noKeyClosed=%d keyOpened=%d keysLeft=%d doorsUnlocked=%u bossOpenWithoutDefeat=%d bossDefeated=%d\n",
        treasure,closedNoKey?1:0,openedWithKey?1:0,runtime->playerState.keys,runtime->doorsUnlocked,bossOpen?1:0,runtime->bossDefeated?1:0);
    bool ok=closedNoKey&&openedWithKey&&bossOpen&&!runtime->bossDefeated;
    delete runtime;return ok?0:58;
}
int RunDeathProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1035486403,*runtime);
    if(start.result!=NPS_WORLD_PENDING)return 40;
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY)return 41;
    const NativePlacedRoom* target=0;
    for(int i=0;i<runtime->world.layout.count;++i)if(runtime->world.layout.rooms[i].definition==268500997){target=&runtime->world.layout.rooms[i];break;}
    if(!target)return 42;
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime->run.context.stage),runtime->run.context.nativeMode,0};
    NativeRoomSetupContext setup={files.backdrop,runtime->run.floor.plan.configStage,false,0};NativeWorldIssue issue;
    if(runtime->world.Enter(target->gridIndex,target->dimension,files,&issue,&setup)!=NWR_OK)return 43;
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    if(live.enemies.count<=0)return 44;
    runtime->playerBody.position=live.enemies.entries[0]->body.position;
    runtime->playerBody.previousPosition=runtime->playerBody.position;
    unsigned int ticks=0;
    while(ticks<600&&!runtime->playerDead){if(!RunProjectileTicks(session,*runtime,-1,-1,1))return 45;++ticks;}
    printf("DEATH seed=1035486403 room=268500997 hits=%u hp=%d/%d soul=%d dead=%d ticks=%u enemies=%d\n",
        runtime->playerHits,runtime->playerState.redHearts,runtime->playerState.maxRedHearts,
        runtime->playerState.soulHearts,runtime->playerDead?1:0,ticks,live.enemies.count);
    NativeActionFrame frame;frame.Clear(runtime->playerCreation.native153c,runtime->playerBody.entityToken,files.frame);
    frame.SupplyConnection(true);frame.SupplyDrop(false);
    for(int action=0;action<12;++action){frame.SupplyHeld(action,false);frame.SupplyPressed(action,false);}
    for(int action=0;action<8;++action)frame.SupplyValue(action,0);
    NativeFrameActions noInput(frame);
    unsigned int dying=0;
    while(runtime->phase==PNR_DYING&&dying<120){session.TickNormal(*runtime,files,&noInput);++dying;}
    const bool gameOver=runtime->phase==PNR_GAME_OVER;const unsigned int hits=runtime->playerHits;
    PortSessionStart restart=session.RestartNormal(1035486403,*runtime,files,&noInput);
    bool restarted=restart.result==NPS_WORLD_PENDING&&runtime->phase==PNR_READY&&!runtime->playerDead&&
        runtime->playerState.redHearts==runtime->playerState.maxRedHearts&&runtime->completePlayerInitializations==1;
    printf("DEATH_FLOW dyingTicks=%u gameOver=%d restartResult=%d restartPhase=%d restartHP=%d/%d\n",
        dying,gameOver?1:0,restart.result,runtime->phase,runtime->playerState.redHearts,runtime->playerState.maxRedHearts);
    bool ok=hits>0&&ticks>0&&gameOver&&restarted;
    delete runtime;return ok?0:46;
}
int RunTransitionProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING)return 31;
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY)return 32;
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];NativeRoomState* room=runtime->world.Current();
    int slot=-1;float x=0,y=0;
    if(live.doors)for(int i=0;i<8;++i)if((live.doors->attached&(1U<<i))&&live.doors->actors[i].state==2&&live.doors->actors[i].targetType==1){
        float px,py;if(room->geometry.Position(live.doors->actors[i].grid,&px,&py)){slot=i;x=px+20.0f;y=py+20.0f;break;}
    }
    if(slot<0)return 33;
    const int from=runtime->world.currentList;runtime->playerBody.position.x=x;runtime->playerBody.position.y=y;
    for(int i=0;i<8&&runtime->roomTransitions==0;++i)if(!RunProjectileTicks(session,*runtime,-1,-1,1)){
        printf("TRANSITION_PENDING next=%08x transitions=%u room=%d phase=%d issue=%d stage=%d type=%u variant=%u\n",
            runtime->missingNativePrerequisite,runtime->roomTransitions,runtime->world.currentList,runtime->phase,
            runtime->entryIssue.result,runtime->entryIssue.construction.stage,runtime->entryIssue.construction.type,
            runtime->entryIssue.construction.variant);return 34;}
    for(int i=0;i<3;++i)if(!RunProjectileTicks(session,*runtime,-1,-1,1))return 36;
    NativeRoomState* entered=runtime->world.Current();NativeWorldResident& resident=runtime->world.resident[runtime->world.currentList];
    const NativePlacedRoom* descriptor=runtime->world.CurrentDescriptor();
    const int definition=descriptor?descriptor->definition:-1;
    printf("TRANSITION seed=1 from=%d slot=%d to=%d definition=%d room=%d enemies=%d transitions=%u player=(%.9g,%.9g)\n",
        from,slot,runtime->world.currentList,definition,entered?1:0,resident.enemies.count,runtime->roomTransitions,
        runtime->playerBody.position.x,runtime->playerBody.position.y);
    bool ok=runtime->roomTransitions==1&&runtime->world.currentList!=from&&entered&&definition>=0&&
        runtime->playerBody.position.x>=entered->space.left&&runtime->playerBody.position.x<=entered->space.right&&
        runtime->playerBody.position.y>=entered->space.top&&runtime->playerBody.position.y<=entered->space.bottom;
    delete runtime;return ok?0:35;
}
int RunEncounterProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1035486403,*runtime);
    if(start.result!=NPS_WORLD_PENDING)return 20;
    if(!AdvanceToBoundary(session,*runtime))return 21;
    if(runtime->phase!=PNR_READY)return 22;
    const NativePlacedRoom* target=0;
    for(int i=0;i<runtime->world.layout.count;++i)if(runtime->world.layout.rooms[i].definition==268500997){target=&runtime->world.layout.rooms[i];break;}
    if(!target)return 23;
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime->run.context.stage),runtime->run.context.nativeMode,0};
    NativeRoomSetupContext setup={files.backdrop,runtime->run.floor.plan.configStage,false,0};NativeWorldIssue issue;
    if(runtime->world.Enter(target->gridIndex,target->dimension,files,&issue,&setup)!=NWR_OK)return 24;
    if(runtime->world.PrepareDoorActors(random,files.frame)!=NDR_OK)return 30;
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];NativeRoomState* room=runtime->world.Current();
    const int spawned=live.enemies.count;if(!room||spawned<=0)return 25;
    const int targetKills=spawned;
    for(int guard=0;guard<12&&live.enemies.count>0;++guard){
        NativeEnemy* enemy=live.enemies.entries[0];if(!enemy)return 26;
        int action=enemy->body.position.x>=runtime->playerBody.position.x?5:4;
        float distance=enemy->body.position.x-runtime->playerBody.position.x;
        if(distance<0.0f)distance=-distance;if(distance<80.0f){
            float x=enemy->body.position.x+(action==5?-100.0f:100.0f);
            if(x<80.0f||x>560.0f)x=enemy->body.position.x;
            runtime->playerBody.position.x=x;runtime->playerBody.position.y=enemy->body.position.y;
            action=enemy->body.position.x>=runtime->playerBody.position.x?5:4;
        }
        if(!RunProjectileTicks(session,*runtime,action,-1,80))return 27;
    }
    for(int i=0;i<10&&live.enemies.count>0;++i)if(!RunProjectileTicks(session,*runtime,-1,-1,20))return 28;
    int attached=0,openDoors=0;
    if(live.doors)for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){++attached;if(live.doors->actors[slot].state==2)++openDoors;}
    if(live.doors)for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot))
        printf("ENCOUNTER_DOOR slot=%d state=%d source=%d target=%d variant=%d\n",slot,live.doors->actors[slot].state,
            live.doors->actors[slot].sourceType,live.doors->actors[slot].targetType,live.doors->actors[slot].variant);
    printf("ENCOUNTER seed=1035486403 room=268500997 enemies=%d kills=%u remaining=%d clear=%d doors=%d open=%d doorsOpened=%u projectiles=%u\n",
        targetKills,runtime->enemyKills,live.enemies.count,room->clear?1:0,attached,openDoors,runtime->doorsOpened,runtime->projectiles.spawned);
    bool ok=spawned==targetKills&&runtime->enemyKills>=(unsigned int)targetKills&&live.enemies.count==0&&room->clear&&runtime->doorsOpened>0;
    delete runtime;return ok?0:29;
}
int RunDescendProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 60;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 61;}
    int grid=-1,dimension=0;
    for(int i=0;i<runtime->world.layout.count;++i){const NativePlacedRoom& p=runtime->world.layout.rooms[i];
        const NativeRoomDef* d=NativeRoom(p.definition);if(d&&d->type==5){grid=p.gridIndex;dimension=p.dimension;break;}}
    if(grid<0){delete runtime;return 62;}
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime->run.context.stage),runtime->run.context.nativeMode,0};
    NativeWorldIssue issue;
    if(runtime->world.Enter(grid,dimension,files,&issue,0)!=NWR_OK){delete runtime;return 63;}
    NativeRoomState* room=runtime->world.Current();if(!room){delete runtime;return 64;}
    room->trapdoorState=2;room->trapdoorTimer=30;
    room->trapdoorX=(room->space.left+room->space.right)*0.5f;
    room->trapdoorY=(room->space.top+room->space.bottom)*0.5f;
    runtime->playerBody.position.x=room->trapdoorX;runtime->playerBody.position.y=room->trapdoorY;
    runtime->playerBody.previousPosition=runtime->playerBody.position;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1)){delete runtime;return 65;}
    if(!runtime->descendRequested){delete runtime;return 66;}
    const int red=runtime->playerState.redHearts,soul=runtime->playerState.soulHearts;
    if(!session.DescendNormal(*runtime,files)){delete runtime;return 67;}
    if(runtime->run.context.stage!=2||!runtime->world.Current()||runtime->phase!=PNR_READY){delete runtime;return 68;}
    if(runtime->playerState.redHearts!=red||runtime->playerState.soulHearts!=soul){delete runtime;return 69;}
    //Continue the maintained descend path through every prepared floor. This
    //exercises Caves..Dark Room generation (including the terminal combined
    //pool and single-boss path) without a rendering device.
    while(runtime->run.context.stage<12){
        files.backdrop=NativeStageBackdrop(runtime->run.context.stage+1);
        if(!session.DescendNormal(*runtime,files)){
            printf("DESCEND_STAGE_FAIL stage=%d prerequisite=%08x\n",runtime->run.context.stage,runtime->missingNativePrerequisite);
            delete runtime;return 70;
        }
        if(!runtime->world.Current()||runtime->phase!=PNR_READY){delete runtime;return 71;}
    }
    if(runtime->run.context.stage!=12){delete runtime;return 72;}
    printf("DESCEND from=1 to=%d room=%d definition=%d hp=%d/%d soul=%d\n",runtime->run.context.stage,
        runtime->world.currentList,runtime->world.CurrentDescriptor()?runtime->world.CurrentDescriptor()->definition:-1,
        runtime->playerState.redHearts,runtime->playerState.maxRedHearts,runtime->playerState.soulHearts);
    delete runtime;return 0;
}
int RunItemProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 80;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 81;}
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    if(!live.room||live.pickups.count>=64){delete runtime;return 82;}
    NativePickup* pickup=new NativePickup;
    if(!pickup->Begin(5,100,0,0,12345,0,runtime->playerBody.position)){delete pickup;delete runtime;return 83;}
    live.pickups.entries[live.pickups.count++]=pickup;
    const unsigned int before=runtime->itemPools.pools[0].counter;
    const unsigned int collectedBefore=runtime->collectiblesCollected;
    if(!RunProjectileTicks(session,*runtime,-1,-1,2)){delete runtime;return 84;}
    bool owned=false;
    if(runtime->lastCollectible<=0||!runtime->playerState.HasCollectible(runtime->lastCollectible,&owned)||!owned){delete runtime;return 85;}
    if(!pickup->collected||runtime->collectiblesCollected!=collectedBefore+1||runtime->itemPools.pools[0].counter!=before+1){delete runtime;return 86;}
    const int hpBefore=runtime->playerState.maxRedHearts,keysBefore=runtime->playerState.keys;
    if(!runtime->playerState.GrantCollectible(25)){delete runtime;return 87;} //Breakfast
    if(runtime->playerState.maxRedHearts!=std::min(14,hpBefore+2)){delete runtime;return 88;}
    if(!runtime->playerState.GrantCollectible(17)){delete runtime;return 89;} //Skeleton Key
    if(runtime->playerState.keys!=std::min(99,keysBefore+99)){delete runtime;return 90;}
    printf("ITEM pool=0 id=%d collected=%u owned=1 poolCounter=%u effect_hp=%d/%d effect_keys=%d\n",
        runtime->lastCollectible,runtime->collectiblesCollected,runtime->itemPools.pools[0].counter,
        runtime->playerState.redHearts,runtime->playerState.maxRedHearts,runtime->playerState.keys);
    delete runtime;return 0;
}
int RunBombProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 91;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 92;}
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    if(!live.room||live.enemies.count>=64){delete runtime;return 93;}
    const NativePoint at=runtime->playerBody.position;
    NativeEnemy* enemy=new NativeEnemy;
    if(!enemy->Begin(10,0,54321,at,0,1)){delete enemy;delete runtime;return 94;}
    live.enemies.entries[live.enemies.count++]=enemy;
    const int bombsBefore=runtime->playerState.bombs;
    if(!runtime->bombs.Place(*runtime)||runtime->playerState.bombs!=bombsBefore-1){delete runtime;return 95;}
    runtime->playerBody.position.x-=200.0f;runtime->playerBody.previousPosition=runtime->playerBody.position;
    if(!RunProjectileTicks(session,*runtime,-1,-1,60)){delete runtime;return 96;}
    if(runtime->bombs.explosions<1||live.enemies.count!=0||runtime->playerHits!=0){delete runtime;return 97;}
    printf("BOMB placed=%u explosions=%u enemyHits=%u bombsLeft=%d enemies=%d\n",
        runtime->bombs.placed,runtime->bombs.explosions,runtime->bombs.enemyHits,
        runtime->playerState.bombs,live.enemies.count);
    delete runtime;return 0;
}unsigned int RunActionPress(PortGameSession& session,PortNormalRuntime& runtime,int action,unsigned int count){
    unsigned int done=0;
    for(unsigned int i=0;i<count;++i){
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime.run.context.stage),runtime.run.context.nativeMode,0};
        NativeActionFrame frame;frame.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame+i);
        frame.SupplyConnection(true);frame.SupplyDrop(false);
        for(int a=0;a<12;++a){frame.SupplyHeld(a,false);frame.SupplyPressed(a,false);}
        for(int a=0;a<8;++a)frame.SupplyValue(a,0);
        frame.SupplyHeld(action,true);frame.SupplyPressed(action,true);
        NativeFrameActions actions(frame);
        if(session.TickNormal(runtime,files,&actions)!=NFR_OK)break;
        ++done;
    }
    return done;
}int RunPocketProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 107;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 108;}
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    if(!live.room||live.pickups.count+2>=64){delete runtime;return 109;}
    //Pill pickup and use.
    NativePickup* pill=new NativePickup;
    if(!pill->Begin(5,380,0,0,555,0,runtime->playerBody.position)){delete pill;delete runtime;return 110;}
    live.pickups.entries[live.pickups.count++]=pill;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1)){delete runtime;return 111;}
    if(!pill->collected||runtime->playerState.heldPill<0){delete runtime;return 112;}
    if(!RunActionPress(session,*runtime,10,1)||runtime->playerState.heldPill!=-1){delete runtime;return 113;}
    //Card pickup and use.
    NativePickup* card=new NativePickup;
    if(!card->Begin(5,350,9,0,556,0,runtime->playerBody.position)){delete card;delete runtime;return 114;}
    live.pickups.entries[live.pickups.count++]=card;
    if(!RunProjectileTicks(session,*runtime,-1,-1,1)){delete runtime;return 115;}
    if(!card->collected||runtime->playerState.heldCard!=9){delete runtime;return 116;}
    const int bombs0=runtime->playerState.bombs,keys0=runtime->playerState.keys,coins0=runtime->playerState.coins;
    if(!RunActionPress(session,*runtime,10,1)||runtime->playerState.heldCard!=0){delete runtime;return 117;}
    if(runtime->playerState.bombs!=std::min(99,bombs0+1)||runtime->playerState.keys!=std::min(99,keys0+1)||
       runtime->playerState.coins!=std::min(99,coins0+1)){delete runtime;return 118;}
    //Direct Health Up pill effect.
    const int hp0=runtime->playerState.maxRedHearts;
    runtime->playerState.heldPill=7;
    if(!RunActionPress(session,*runtime,10,1)){delete runtime;return 119;}
    if(runtime->playerState.heldPill!=-1||runtime->playerState.maxRedHearts!=std::min(14,hp0+2)){delete runtime;return 120;}
    printf("POCKET hp=%d/%d bombs=%d keys=%d coins=%d\n",runtime->playerState.redHearts,runtime->playerState.maxRedHearts,
        runtime->playerState.bombs,runtime->playerState.keys,runtime->playerState.coins);
    delete runtime;return 0;
}int RunActiveProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 121;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){delete runtime;return 122;}
    runtime->playerState.redHearts=4;
    if(!runtime->playerState.GrantCollectible(45)){delete runtime;return 123;} //Yum Heart
    if(runtime->playerState.activeItems[0]!=45){delete runtime;return 124;}
    const int maxHp=runtime->playerState.maxRedHearts;
    if(!RunActionPress(session,*runtime,9,1)){delete runtime;return 125;}
    if(runtime->playerState.redHearts!=std::min(maxHp,6)){delete runtime;return 126;}
    printf("ACTIVE id=%d hp=%d/%d\n",runtime->playerState.activeItems[0],runtime->playerState.redHearts,runtime->playerState.maxRedHearts);
    delete runtime;return 0;
}int RunStatProbe(NativeMt& random,PortFrontend& ui){
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 103;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY||!runtime->playerState.stats.ready){delete runtime;return 104;}
    const float fd0=runtime->playerState.stats.fireDelay,dmg0=runtime->playerState.stats.damage,spd0=runtime->playerState.stats.moveSpeed;
    if(!runtime->playerState.GrantCollectible(1)||!runtime->playerState.GrantCollectible(4)||
       !runtime->playerState.GrantCollectible(27)||!runtime->playerState.GrantCollectible(46)){delete runtime;return 105;}
    if(!(runtime->playerState.stats.fireDelay<fd0)||!(runtime->playerState.stats.damage>dmg0)||
       !(runtime->playerState.stats.moveSpeed>spd0)||runtime->playerState.stats.luck<1.0f){delete runtime;return 106;}
    printf("STAT fd=%.3f damage=%.3f speed=%.3f luck=%.1f\n",runtime->playerState.stats.fireDelay,
        runtime->playerState.stats.damage,runtime->playerState.stats.moveSpeed,runtime->playerState.stats.luck);
    delete runtime;return 0;
}int RunHardModeProbe(NativeMt& random,PortFrontend& ui){
    (void)random;(void)ui;
    PortFrontend local;local.Init(false);
    if(!local.BindSaveDirectory("build/gameplay/profile/"))return 98;
    local.profile.difficulty=1;local.characterPage=0;local.characterChoice=1;local.characterMode=1;
    if(!local.music.StartCharacter(0)){printf("HARD music\n");return 99;}
    PortNormalRuntime* runtime=new PortNormalRuntime;NativeMt localGlobal;localGlobal.Seed(777);PortGameSession session(localGlobal,local);
    PortSessionStart start=session.StartNormalCharacter(1,*runtime);
    if(start.result!=NPS_WORLD_PENDING){printf("HARD start=%d\n",start.result);delete runtime;return 100;}
    if(!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){printf("HARD advance phase=%d prereq=%08x\n",runtime->phase,runtime->missingNativePrerequisite);delete runtime;return 101;}
    if(!RunProjectileTicks(session,*runtime,-1,-1,2)){delete runtime;return 102;}
    printf("HARD mode=%d phase=%d player=%d hp=%d/%d slots=%d\n",runtime->run.player.mode,runtime->phase,
        runtime->playerState.playerType,runtime->playerState.redHearts,runtime->playerState.maxRedHearts,
        (int)runtime->playerState.collectibleCounts.size());
    delete runtime;return 0;
}int RunCharacterMatrixProbe(NativeMt& random,PortFrontend& ui){
    (void)random;(void)ui;
    int ok=0,fail=0;
    for(int page=0;page<2;++page)for(int choice=0;choice<kNativeCharacterCount;++choice){
        const NativeCharacterDef* c=NativeCharacter(page,choice);if(!c)continue;
        const NativePlayerDef* def=NativePlayer(c->player);if(!def){++fail;printf("CHAR_FAIL page=%d choice=%d player=%d missing-def\n",page,choice,c->player);continue;}
        PortFrontend local;local.Init(false);
        if(!local.BindSaveDirectory("build/gameplay/profile/"))return 90;
        memset(&local.profile,0,sizeof(local.profile));
        if(def->achievement>=0&&def->achievement<kNativeAwardCount)local.profile.awards[def->achievement]=true;
        if(def->id==9||def->id==30)local.profile.edenTokens=1;
        local.characterPage=page;local.characterChoice=choice;local.characterMode=0;
        if(!local.music.StartCharacter(page)){++fail;printf("CHAR_FAIL page=%d choice=%d player=%d music\n",page,choice,c->player);continue;}
        PortNormalRuntime* rt=new PortNormalRuntime;NativeMt localGlobal;localGlobal.Seed(1U+(unsigned int)(page*18+choice));PortGameSession session(localGlobal,local);
        PortSessionStart start=session.StartNormalCharacter(1,*rt);
        bool good=start.result==NPS_WORLD_PENDING;
        int startItems=0,startItemsOwned=0;
        if(good){unsigned int steps=AdvanceToBoundary(session,*rt);good=steps&&rt->phase==PNR_READY&&rt->playerState.ready&&rt->playerState.playerType==c->player;
            if(good)good=RunProjectileTicks(session,*rt,-1,-1,2)!=0;}
        if(good)for(int i=0;i<def->itemCount;++i)if(def->items[i]>0){
            bool has=false;++startItems;
            if(rt->playerState.HasCollectible(def->items[i],&has)&&has)++startItemsOwned;
        }
        if(good&&startItemsOwned!=startItems)good=false;
        if(good){++ok;printf("CHAR ok page=%d choice=%d player=%d hp=%d/%d startItems=%d/%d\n",page,choice,c->player,
            rt->playerState.redHearts,rt->playerState.maxRedHearts,startItemsOwned,startItems);}
        else{++fail;printf("CHAR_FAIL page=%d choice=%d player=%d result=%d phase=%d type=%d prereq=%08x\n",page,choice,c->player,
            start.result,rt->phase,rt->playerState.playerType,rt->missingNativePrerequisite);}
        delete rt;
    }
    printf("CHARACTERS ok=%d fail=%d\n",ok,fail);
    return 0;
}
int main(int argc,char** argv){
    unsigned int seed=argc>1?(unsigned int)strtoul(argv[1],0,0):1;
    NativeMt random;random.ColdStart();PortFrontend ui;ui.Init(false);
    if(!ui.BindSaveDirectory("build/gameplay/profile/"))return 13;
    PortMenuInput accept={0},empty={0};accept.accept=true;
    ui.Step(accept);ui.Step(empty);if(ui.Step(accept)!=UI_SELECT_SLOT)return 1;
    ui.SlotLoaded(true,false);ui.Step(empty);ui.Step(accept);ui.Step(empty);
    if(ui.Step(accept)!=UI_START_CHARACTER)return 2;
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(seed,*runtime);
    printf("NEW_RUN result=%d phase=%d resources=%d HP=%d/%d bombs=%d manager=%d\n",start.result,runtime->phase,
        runtime->playerState.resourcesReady,runtime->playerState.redHearts,runtime->playerState.maxRedHearts,runtime->playerState.bombs,runtime->playerManager.initialized);
    if(start.result!=NPS_WORLD_PENDING){delete runtime;return 3;}
    printf("OWNERS sequence=%u playerInit=%u game=%u/%u pools=%u random=%u/%u backup=%u/%u trinkets=%d soundResets=%u\n",
        runtime->run.seeds.sequence,runtime->run.seeds.playerInit,runtime->gameRandom.state,runtime->gameRandom.index,
        runtime->itemPools.seed,runtime->itemPools.random.state,runtime->itemPools.random.index,
        runtime->itemPools.backup.state,runtime->itemPools.backup.index,runtime->itemPools.availableTrinkets,(unsigned int)runtime->itemPools.soundResets.size());
    printf("PILL_COLORS");for(int i=0;i<15;++i)printf(" %d",runtime->itemPools.pillEffects[i]);printf("\n");
    printf("PLAYER active=%d registered=%d collision=%d gridCollision=%d depth=%d initializing=%d controller=%d HUDrefreshes=%u\n",
        runtime->playerBody.native16a,runtime->playerBody.native168,runtime->playerBody.entityCollisionClass,runtime->playerBody.gridCollisionClass,
        runtime->playerBody.renderDepth,runtime->playerState.runInitializing,runtime->playerCreation.native153c,runtime->playerManager.hudRefreshes);
    unsigned int steps=AdvanceToBoundary(session,*runtime);if(!steps){delete runtime;return 4;}
    printf("SESSION phase=%d floor=%d steps=%u selected=%d residents=%d next=%08x players=%u ticks=%u\n",runtime->phase,runtime->run.phase,steps,
        runtime->world.CurrentDescriptor()?runtime->world.CurrentDescriptor()->definition:-1,runtime->world.residentCount,runtime->missingNativePrerequisite,
        runtime->completePlayerInitializations,runtime->completePlayerTicks);
    printf("SETUP position=(%.9g,%.9g) native1734=%d postGeneration=%d stats=%d speed=%.9g damage=%.9g saveNormalized=%d saveCaptured=%d\n",
        runtime->playerBody.position.x,runtime->playerBody.position.y,runtime->playerState.native1734,runtime->playerCreation.postGenerationPrepared,
        runtime->playerState.stats.ready,runtime->playerState.stats.moveSpeed,runtime->playerState.stats.damage,
        runtime->playerCreation.initialSaveNormalized,runtime->initialSave.captured);
    printf("PLAYER_UPDATE address=%08x missingOffset=%x active=%d returned=%d time=%.9g updateMembers=%u\n",
        runtime->playerUpdate.address,runtime->playerUpdate.missingOffset,runtime->playerUpdate.active,runtime->playerUpdate.returned,
        runtime->playerBody.time,(unsigned int)runtime->playerManager.updateMembers.size());
    {
        static const unsigned int scalars[]={0x1304,0x1524,0x1528,0x19fc,0x1808,0x1e24,0x1320,0x134c,0x14a0,0x1280,0x1284,0x1e88,0x1548,0x154c,0x1550,0x1554,0x1558,0x1560,0x1564,0x1d70,0x1dd8,0x1e68};
        static const unsigned int bytes[]={0x1735,0x1f80,0x12e2,0x12e0,0x12e1,0x155c,0x1734,0x1804};
        printf("FIELDS bodyType=%d subtype=%d variant=%d flags=%08x/%08x position=(%.6g,%.6g) velocity=(%.6g,%.6g)\n",
            runtime->playerBody.type,runtime->playerBody.subtype,runtime->playerBody.variant,runtime->playerBody.flagsLow,runtime->playerBody.flagsHigh,
            runtime->playerBody.position.x,runtime->playerBody.position.y,runtime->playerBody.velocity.x,runtime->playerBody.velocity.y);
        for(unsigned int i=0;i<sizeof(scalars)/sizeof(scalars[0]);++i){unsigned int v;
            printf("FIELD %x=%s%u",scalars[i],runtime->playerCreation.constructorFields.Scalar(scalars[i],&v)?"":"?",runtime->playerCreation.constructorFields.Scalar(scalars[i],&v)?v:0);
            if((i&3)==3)printf("\n");else printf(" ");}
        printf("\n");
        for(unsigned int i=0;i<sizeof(bytes)/sizeof(bytes[0]);++i){unsigned char v;
            printf("BYTE %x=%s%u",bytes[i],runtime->playerCreation.constructorFields.Byte(bytes[i],&v)?"":"?",runtime->playerCreation.constructorFields.Byte(bytes[i],&v)?v:0);
            if((i&3)==3)printf("\n");else printf(" ");}
        printf("\n");
    }
    printf("SAVE bytes=%u published=%d indeterminateBytes=%u codecNext=%08x missingOffset=%x\n",
        (unsigned int)runtime->initialSaveFile.data.size(),runtime->initialSaveFileWritten,runtime->initialSaveFile.indeterminateBytes,
        runtime->initialSaveFile.nextAddress,runtime->initialSaveFile.missingOffset);
    if(runtime->checkpoints[0].valid){const NativeRunCheckpoint& c=runtime->checkpoints[0];unsigned int hp,bombs;
        const NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
        const NativePlayerPersistent& p=c.state.players[0];
        if(!p.fields.Get(4,4,&hp)||!p.fields.Get(0x28,4,&bombs)||hp!=(unsigned int)runtime->playerState.redHearts||bombs!=(unsigned int)runtime->playerState.bombs||
           !live.persistent||live.persistent->grids.size()!=448||c.state.descriptors.size()!=525||
           p.words.find(0x190)==p.words.end()||p.words.find(0x190)->second[97]!=runtime->playerStartup.banks[NPB_TRINKET][97].state||
           runtime->history.checkpointGuard||c.action!=-1){delete runtime;return 10;}
        for(int i=0;i<496;++i)if(c.events[i]!=runtime->run.profile.eventCounters[i]){delete runtime;return 11;}
        printf("CHECKPOINT buffer=%d frame=%u action=%d players=%u HP=%u bombs=%u gridRecords=%u entityRecords=%u descriptors=%u events=496\n",
            runtime->history.activeCheckpoint,c.frame,c.action,(unsigned int)c.state.players.size(),hp,bombs,(unsigned int)live.persistent->grids.size(),
            (unsigned int)live.persistent->entities.size(),(unsigned int)c.state.descriptors.size());
        const NativeRoomState* room=runtime->world.Current();NativeMt before=random;
        unsigned int callbackCount=runtime->playerManager.roomInitCallbacks;
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime->run.context.stage),runtime->run.context.nativeMode,0};
        if(session.AdvanceNormalSetup(*runtime,files)!=NFR_PENDING||room!=runtime->world.Current()||
           memcmp(&before,&random,sizeof(random))||callbackCount!=runtime->playerManager.roomInitCallbacks||!runtime->checkpoints[0].valid){delete runtime;return 12;}
        printf("RESUME residentRetained=1 RNGretained=1 roomCallbacks=%u next=%08x\n",callbackCount,runtime->missingNativePrerequisite);
    }
    if(runtime->phase==PNR_READY){
        const NativeRng rngBeforeTail=runtime->gameRandom;
        //Primary weapon: one diagonal shot whose range expires before the
        //corner, and repeated downward shots that meet the actual bottom wall.
        unsigned int projTicks=RunProjectileTicks(session,*runtime,5,6,1);
        projTicks+=RunProjectileTicks(session,*runtime,-1,-1,160);
        projTicks+=RunProjectileTicks(session,*runtime,7,-1,1);
        projTicks+=RunProjectileTicks(session,*runtime,-1,-1,40);
        printf("PROJECTILES ticks=%u spawned=%u wallHits=%u expired=%u active=%u cooldown=%.9g\n",
            projTicks,runtime->projectiles.spawned,runtime->projectiles.wallHits,runtime->projectiles.expired,
            (unsigned int)runtime->projectiles.active.size(),runtime->projectiles.cooldown);
        if(projTicks!=202||runtime->projectiles.spawned<2||runtime->projectiles.wallHits<1||
           runtime->projectiles.expired<1||!runtime->projectiles.active.empty())return 17;
        const NativePoint start=runtime->playerBody.position;
        const unsigned int right=RunMovementTicks(session,*runtime,1,0,40);
        const NativePoint afterRight=runtime->playerBody.position;
        const unsigned int wall=RunMovementTicks(session,*runtime,1,0,240);
        const NativePoint atWall=runtime->playerBody.position;
        const unsigned int settle=RunMovementTicks(session,*runtime,1,0,40);
        const NativePoint settled=runtime->playerBody.position;
        const unsigned int left=RunMovementTicks(session,*runtime,-1,0,40);
        const NativePoint afterLeft=runtime->playerBody.position;

        const NativeRoomState& room=*runtime->world.Current();
        printf("TICKS ready=%d right=%u start=(%.9g,%.9g) afterRight=(%.9g,%.9g) wall=%u atWall=(%.9g,%.9g) settle=%u settled=(%.9g,%.9g) left=%u afterLeft=(%.9g,%.9g) ticks=%u whole=%u extent=(%.9g,%.9g,%.9g,%.9g)\n",
            runtime->phase==PNR_READY,right,start.x,start.y,afterRight.x,afterRight.y,wall,atWall.x,atWall.y,settle,settled.x,settled.y,left,afterLeft.x,afterLeft.y,
            runtime->completePlayerTicks,runtime->completeWholeGameplayTicks,room.space.left,room.space.top,room.space.right,room.space.bottom);
        if(right<10||afterRight.x<=start.x+1||wall<10||settle<10||fabsf(settled.x-atWall.x)>0.01f||
            left<10||afterLeft.x>=atWall.x-1||runtime->completePlayerTicks<right+wall+settle+left||
            runtime->completeWholeGameplayTicks!=runtime->completePlayerTicks)return 14;
        printf("GAMEPLAY complete=%u playerTicks=%u wholeTicks=%u collisionStop=(%.9g,%.9g) stable=%d\n",
            runtime->completePlayerInitializations,runtime->completePlayerTicks,runtime->completeWholeGameplayTicks,
            settled.x,settled.y,fabsf(settled.x-atWall.x)<=0.01f);
        printf("TAIL_RNG unchanged=%d\n",memcmp(&rngBeforeTail,&runtime->gameRandom,sizeof(NativeRng))==0);
        printf("SIM_STATE clear=%d enemyKills=%u doorsOpened=%u enemies=%d\n",room.clear?1:0,runtime->enemyKills,
            runtime->doorsOpened,runtime->world.resident[runtime->world.currentList].enemies.count);
        unsigned int headTimerX=0,headTimerY=0,headCalls=0,headDirection=0;
        const NativePlayerCreationCache& headFields=runtime->playerCreation.constructorFields;
        const bool headPresent=headFields.Scalar(0x1280,&headTimerX)&&headFields.Scalar(0x1284,&headTimerY)&&
            headFields.Scalar(0x1e88,&headCalls)&&headFields.Scalar(0x154c,&headDirection);
        printf("HEAD_STATE timers=%d/%d calls=%u/%u direction=%u\n",(int)headTimerX,(int)headTimerY,headCalls,runtime->completePlayerTicks,headDirection);
        //The reached 72c430 ordinary path decrements0x1280/0x1284 to-1 on the
        //first update and increments0x1e88 once per whole tick.
        if(!headPresent||headTimerX!=0xffffffffU||headTimerY!=0xffffffffU||headCalls!=runtime->completePlayerTicks)return 16;
        unsigned int t12c0=0,t2cc=0,t12bc=0,t1ca4=0;float f2cc=0;
        const NativePlayerCreationCache& cf=runtime->playerCreation.constructorFields;
        if(cf.Scalar(0x2cc,&t2cc))memcpy(&f2cc,&t2cc,4);
        unsigned int t1548=0;
        bool p12c0=cf.Scalar(0x12c0,&t12c0),p2cc=cf.Scalar(0x2cc,&t2cc),p12bc=cf.Scalar(0x12bc,&t12bc),p1ca4=cf.Scalar(0x1ca4,&t1ca4);
        cf.Scalar(0x1548,&t1548);
        printf("TAIL ticks=%u trailSegments=%u renderCallbacks=%u fields=12c0/2cc/12bc/1ca4 present=%d%d%d%d values=%u/%.9g/%u/%u direction=%d spriteFlags=%08x\n",
            runtime->playerTailTicks,runtime->playerTailTrailSegments,runtime->playerTailRenderCallbacks,
            p12c0?1:0,p2cc?1:0,p12bc?1:0,p1ca4?1:0,
            t12c0,f2cc,t12bc,t1ca4,(int)t1548,runtime->playerSprites.sprites[0].renderFlags);
        //The final left tick leaves the native direction flag set, the body
        //damage at the actual record value, and the reached clear word zero.
        if(!p12c0||!p2cc||!p12bc||!p1ca4||t12c0!=2U||f2cc!=0.0f||t12bc!=0U||t1ca4!=0U||
           runtime->playerTailTrailSegments!=1U||runtime->playerTailTicks!=runtime->completePlayerTicks||
           (runtime->playerSprites.sprites[0].renderFlags&8U)==0)return 15;
    }
    if(runtime->world.Current()){
        NativeRoomState& room=*runtime->world.Current();
        NativeWorldResident& resident=runtime->world.resident[runtime->world.currentList];
        printf("ROOM type=%u variant=%u shape=%u grids=%d props=%d perimeter=%d MTindex=%d extent=(%.9g,%.9g,%.9g,%.9g)\n",room.type,room.variant,room.shape,room.staticGridCount,room.decorationCount,
            runtime->world.resident[runtime->world.currentList].perimeterReady,random.index,room.space.left,room.space.top,room.space.right,room.space.bottom);
        if(resident.effects)for(unsigned int i=0;i<resident.effects->entities.size();++i){const NativeRoomEffect& effect=*resident.effects->entities[i];
            printf("AMBIENT token=%u variant=%d seed=%u updates=%u time=%g position=(%.9g,%.9g) velocity=(%.9g,%.9g) next=%08x\n",
                effect.body.entityToken,effect.body.variant,effect.body.initialSeed,effect.updates,effect.body.time,
                effect.body.position.x,effect.body.position.y,effect.body.velocity.x,effect.body.velocity.y,effect.nextAddress);}
        if(resident.view)printf("VIEW lighting=%d camera=%d viewport=(%g,%g) center=(%g,%g)\n",resident.view->lightingReady,resident.view->cameraReady,
            resident.view->width,resident.view->height,resident.view->cameraCenter.x,resident.view->cameraCenter.y);
        const NativePlacedRoom* p=runtime->world.CurrentDescriptor();
        if(p)printf("SEEDS level=%u placement=%u spawn=%u award=%u decoration=%u auxiliary=%u list=%d count=%d\n",runtime->run.levelSeed,p->placementSeed,p->seeds.spawn,p->seeds.award,p->seeds.decoration,p->seeds.auxiliary,p->listIndex,runtime->world.layout.count);
        if(p)for(int slot=0;slot<8;++slot)if(p->doors&(1U<<slot)){
            const NativePlacedRoom* target=runtime->world.layout.DoorTarget(runtime->world.currentList,slot);
            const NativeRoomDef* d=target?NativeRoom(target->definition):0;
            printf("DOOR slot=%d grid=%d definition=%d type=%d variant=%u\n",slot,p->doorTargets[slot],target?target->definition:-1,d?d->type:-1,d?d->variant:0);
        }
    }
    bool failed=runtime->phase==PNR_ERROR||runtime->phase==PNR_SOURCE_ERROR;
    if(!failed&&argc>2&&!strcmp(argv[2],"--restart")){
        const NativePlacedRoom* selected=runtime->world.CurrentDescriptor();
        if(!selected){delete runtime;return 6;}
        const int definition=selected->definition,list=selected->listIndex;
        const unsigned int poolRandom=runtime->itemPools.random.state,sequence=runtime->run.seeds.sequence;
        const unsigned int playerHash=runtime->playerState.StateHash(),creationHash=runtime->playerCreation.StateHash();
        const NativeRoomState* live=runtime->world.Current();NativeMt before=random;
        const std::map<int,bool> soundFlags=runtime->sounds.native199;
        const PortNormalPhase phase=runtime->phase;
        PortSessionStart rejected=session.StartNormalCharacter(0,*runtime);
        if(rejected.result!=NPS_BAD_REQUEST||runtime->phase!=phase||runtime->world.Current()!=live||
            runtime->itemPools.random.state!=poolRandom||runtime->run.seeds.sequence!=sequence||
            runtime->playerState.StateHash()!=playerHash||runtime->playerCreation.StateHash()!=creationHash||
            runtime->sounds.native199!=soundFlags||memcmp(&before,&random,sizeof(random))){delete runtime;return 7;}
        PortSessionStart restarted=session.StartNormalCharacter(seed,*runtime);
        if(restarted.result!=NPS_WORLD_PENDING||runtime->phase!=PNR_GENERATING||runtime->world.residentCount||
            runtime->roomEntryPrefixPrepared||runtime->run.seeds.sequence!=sequence||runtime->itemPools.random.state!=poolRandom||
            runtime->playerManager.worldMembers.size()!=1||runtime->completePlayerInitializations||runtime->completePlayerTicks){delete runtime;return 8;}
        steps=AdvanceToBoundary(session,*runtime);selected=runtime->world.CurrentDescriptor();
        if(!steps||!selected||selected->definition!=definition||selected->listIndex!=list||
            runtime->phase==PNR_ERROR||runtime->phase==PNR_SOURCE_ERROR){delete runtime;return 9;}
        printf("RESTART invalidRetained=1 selected=%d residents=%d next=%08x players=%u ticks=%u MTindex=%d\n",selected->definition,
            runtime->world.residentCount,runtime->missingNativePrerequisite,runtime->completePlayerInitializations,runtime->completePlayerTicks,random.index);
    }
    if(!failed){int encounter=RunEncounterProbe(random,ui);if(encounter){printf("ENCOUNTER_RESULT failed=%d\n",encounter);failed=true;}}
    if(!failed){int transition=RunTransitionProbe(random,ui);if(transition){printf("TRANSITION_RESULT failed=%d\n",transition);failed=true;}}
    if(!failed){int locks=RunLockProbe(random,ui);if(locks){printf("LOCK_RESULT failed=%d\n",locks);failed=true;}}
    if(!failed){int descend=RunDescendProbe(random,ui);if(descend){printf("DESCEND_RESULT failed=%d\n",descend);failed=true;}}
    if(!failed){int item=RunItemProbe(random,ui);if(item){printf("ITEM_RESULT failed=%d\n",item);failed=true;}}
    if(!failed){int bomb=RunBombProbe(random,ui);if(bomb){printf("BOMB_RESULT failed=%d\n",bomb);failed=true;}}
    if(!failed){int hard=RunHardModeProbe(random,ui);if(hard){printf("HARD_RESULT failed=%d\n",hard);failed=true;}}
    if(!failed){int stat=RunStatProbe(random,ui);if(stat){printf("STAT_RESULT failed=%d\n",stat);failed=true;}}
    if(!failed){int pocket=RunPocketProbe(random,ui);if(pocket){printf("POCKET_RESULT failed=%d\n",pocket);failed=true;}}
    if(!failed){int active=RunActiveProbe(random,ui);if(active){printf("ACTIVE_RESULT failed=%d\n",active);failed=true;}}
    if(!failed){int chars=RunCharacterMatrixProbe(random,ui);if(chars){printf("CHARACTER_RESULT failed=%d\n",chars);failed=true;}}
    if(!failed){int death=RunDeathProbe(random,ui);if(death){printf("DEATH_RESULT failed=%d\n",death);failed=true;}}
    delete runtime;return failed?5:0;
}
