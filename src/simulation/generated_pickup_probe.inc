static bool GeneratedPickupTick(PortGameSession& session,PortNormalRuntime& runtime,float x,float y,bool bomb){
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime.run.context.stage),runtime.run.context.nativeMode,0};
    NativeActionFrame raw;raw.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame+runtime.tickFrame);
    raw.SupplyConnection(true);raw.SupplyDrop(false);
    for(int a=0;a<12;++a){raw.SupplyHeld(a,false);raw.SupplyPressed(a,false);if(a<8)raw.SupplyValue(a,0);}
    if(x>0)raw.SupplyValue(1,x);else if(x<0)raw.SupplyValue(0,-x);
    if(y>0)raw.SupplyValue(3,y);else if(y<0)raw.SupplyValue(2,-y);
    if(bomb){raw.SupplyHeld(8,true);raw.SupplyPressed(8,true);}
    NativeFrameActions actions(raw);
    return session.TickNormal(runtime,files,&actions)==NFR_OK;
}

static bool GeneratedPickupLineClear(const NativeRoomState& room,const NativePoint& from,const NativePoint& to){
    const float dx=to.x-from.x,dy=to.y-from.y;
    const float distance=sqrtf(dx*dx+dy*dy);
    const int steps=distance>0.0f?(int)(distance/8.0f)+1:1;
    for(int i=1;i<steps;++i){
        NativePoint at;at.x=from.x+dx*(float)i/(float)steps;at.y=from.y+dy*(float)i/(float)steps;
        int collision=-1;
        if(!room.collision.AtPosition(room.geometry,at,room.space.zeroCollision,&collision))return false;
        if(collision!=0&&collision!=5)return false;
    }
    return true;
}

static bool GeneratedPickupDoorActorContact(const NativeRoomState& room,const NativeDoorActor& door,int cell){
    float dx=0,dy=0,nx=0,ny=0;
    if(cell<0||!room.geometry.Position(door.grid,&dx,&dy)||!room.geometry.Position(cell,&nx,&ny))return true;
    dx+=20.0f-nx-10.0f;dy+=20.0f-ny-10.0f;
    return dx*dx+dy*dy<=70.0f*70.0f;
}

static bool GeneratedPickupDoorContactCell(const NativeRoomState& room,const NativeWorldResident& live,int cell){
    if(!live.doors||cell<0)return false;
    for(int slot=0;slot<8;++slot)if((live.doors->attached&(1U<<slot))&&
        GeneratedPickupDoorActorContact(room,live.doors->actors[slot],cell))return true;
    return false;
}

static bool GeneratedPickupApproachesDoor(const NativeRoomState& room,const NativeWorldResident& live,
    const NativePoint& origin,int cell){
    if(!live.doors)return false;
    for(int slot=0;slot<8;++slot){
        if(!(live.doors->attached&(1U<<slot))||!GeneratedPickupDoorActorContact(room,live.doors->actors[slot],cell))continue;
        float dx=0,dy=0,nx=0,ny=0;
        if(!room.geometry.Position(live.doors->actors[slot].grid,&dx,&dy)||!room.geometry.Position(cell,&nx,&ny))return true;
        dx+=20.0f-nx-10.0f;dy+=20.0f-ny-10.0f;
        if(dx*dx+dy*dy>70.0f*70.0f)continue;
        dx+=nx+10.0f;dy+=ny+10.0f;
        const float from=sqrtf((origin.x-dx)*(origin.x-dx)+(origin.y-dy)*(origin.y-dy));
        const float to=sqrtf((nx+10.0f-dx)*(nx+10.0f-dx)+(ny+10.0f-dy)*(ny+10.0f-dy));
        if(to+0.001f<from)return true;
    }
    return false;
}

struct GeneratedPickupRoomContext {
    int list,definition,controlX,controlY;NativeRoomState* room;unsigned int transitions;
};

static bool GeneratedPickupSavedEqualsLive(const NativeSaveSummary::Pickup& saved,const NativePickup& live){
    return saved.type==live.type&&saved.variant==live.variant&&saved.subtype==live.subtype&&
        saved.grid==live.grid&&saved.currentSeed==live.currentSeed&&saved.initialSeed==live.initialSeed&&
        saved.native46c==live.native46c&&saved.native478==live.native478&&saved.native480==live.native480&&
        saved.native484==live.native484&&saved.native27c==live.native27c&&
        saved.native474==live.native474&&saved.native476==live.native476&&
        saved.native4cc==live.native4cc&&saved.native470==live.native470&&saved.native34==live.native34&&
        saved.native38==live.native38&&saved.native360==live.native360&&saved.native4e0==live.native4e0&&
        saved.native47c==live.native47c&&saved.nativeExtraCount==live.nativeExtraCount&&
        saved.nativeChildCount==live.nativeChildCount&&!memcmp(&saved.x,&live.position.x,4)&&
        !memcmp(&saved.y,&live.position.y,4)&&!memcmp(&saved.nativeD8,&live.nativeD8,4);
}

static bool GeneratedPickupSavedRoomMatches(const NativeSaveSummary& summary,const NativePlacedRoom& room){
    const NativeRoomDef* definition=NativeRoom(room.definition);
    return definition&&summary.hasCurrentRoom&&summary.currentGrid==room.gridIndex&&
        summary.dimension==room.dimension&&summary.roomDefinition==definition->stage&&
        summary.roomDefinitionType==definition->type&&summary.roomDefinitionVariant==(int)definition->variant;
}

static bool GeneratedPickupRoomIdentity(const PortNormalRuntime& runtime,const GeneratedPickupRoomContext& context){
    const NativePlacedRoom* descriptor=runtime.world.CurrentDescriptor();
    return runtime.world.currentList==context.list&&runtime.world.Current()==context.room&&descriptor&&
        descriptor->definition==context.definition&&runtime.roomTransitions==context.transitions;
}

static bool GeneratedPickupRoomTick(PortGameSession& session,PortNormalRuntime& runtime,
    GeneratedPickupRoomContext& context,float x,float y,bool bomb){
    if(!GeneratedPickupTick(session,runtime,x,y,bomb))return false;
    if(!GeneratedPickupRoomIdentity(runtime,context)){
        const NativePlacedRoom* descriptor=runtime.world.CurrentDescriptor();
        printf("GENERATED_PICKUP_IDENTITY fail during tick list=%d def=%d room=%d transitions=%u/%u\n",
            runtime.world.currentList,descriptor?descriptor->definition:-1,runtime.world.Current()?1:0,
            runtime.roomTransitions,context.transitions);return false;
    }
    return true;
}

static bool GeneratedPickupMoveToCell(PortGameSession& session,PortNormalRuntime& runtime,int goal,unsigned int* ticks,
    bool allowTransition,GeneratedPickupRoomContext* context);

static bool GeneratedPickupCombatTick(PortGameSession& session,PortNormalRuntime& runtime,int shoot,
    GeneratedPickupRoomContext& context){
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime.run.context.stage),runtime.run.context.nativeMode,0};
    NativeActionFrame raw;raw.Clear(runtime.playerCreation.native153c,runtime.playerBody.entityToken,files.frame+runtime.tickFrame);
    raw.SupplyConnection(true);raw.SupplyDrop(false);
    for(int a=0;a<12;++a){raw.SupplyHeld(a,false);raw.SupplyPressed(a,false);if(a<8)raw.SupplyValue(a,0);}
    if(shoot>=0)raw.SupplyValue(shoot,1.0f);
    NativeFrameActions actions(raw);
    if(session.TickNormal(runtime,files,&actions)!=NFR_OK)return false;
    if(!GeneratedPickupRoomIdentity(runtime,context))return false;
    return true;
}

static bool GeneratedPickupClearRoom(PortGameSession& session,PortNormalRuntime& runtime,
    GeneratedPickupRoomContext& context,unsigned int* ticks){
    if(!ticks)return false;
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    NativeRoomState* room=runtime.world.Current();
    if(!room)return false;
    int guard=0;
    while(live.enemies.count>0&&guard++<80){
        NativeEnemy* target=0;float nearest=1.0e9f;
        for(int i=0;i<live.enemies.count;++i){
            NativeEnemy* enemy=live.enemies.entries[i];
            if(!enemy||!enemy->ready||enemy->dead)continue;
            const float dx=enemy->body.position.x-runtime.playerBody.position.x;
            const float dy=enemy->body.position.y-runtime.playerBody.position.y;
            const float d=dx*dx+dy*dy;
            if(d<nearest){nearest=d;target=enemy;}
        }
        if(!target)break;
        NativePoint here=runtime.playerBody.position,enemyAt=target->body.position;
        float directX=enemyAt.x-here.x,directY=enemyAt.y-here.y;
        const float directDistance=sqrtf(directX*directX+directY*directY);
        const bool inPlace=directDistance>=55.0f&&directDistance<=260.0f&&GeneratedPickupLineClear(*room,here,enemyAt);
        int bestCell=-1;float bestScore=1.0e9f;
        if(!inPlace)for(int cell=0;cell<room->geometry.cells;++cell){
            if(room->collision.occupied[cell]||GeneratedPickupDoorContactCell(*room,live,cell))continue;
            float x=0,y=0;if(!room->geometry.Position(cell,&x,&y))continue;x+=20.0f;y+=20.0f;
            const float dx=target->body.position.x-x,dy=target->body.position.y-y;
            const float distance=sqrtf(dx*dx+dy*dy);
            const float pdx=x-runtime.playerBody.position.x,pdy=y-runtime.playerBody.position.y;
            const float playerDistance=sqrtf(pdx*pdx+pdy*pdy);
            if(distance<70.0f||distance>200.0f||playerDistance>110.0f)continue;
            NativePoint from,to;from.x=x;from.y=y;to=target->body.position;
            if(!GeneratedPickupLineClear(*room,from,to))continue;
            const float score=playerDistance*0.4f+fabsf(distance-130.0f);
            if(score<bestScore){bestScore=score;bestCell=cell;}
        }
        if(!inPlace&&bestCell<0){
            //Fallback: nearest free cell to the player that can see the target.
            for(int cell=0;cell<room->geometry.cells;++cell){
                if(room->collision.occupied[cell]||GeneratedPickupDoorContactCell(*room,live,cell))continue;
                float x=0,y=0;if(!room->geometry.Position(cell,&x,&y))continue;x+=20.0f;y+=20.0f;
                NativePoint from,to;from.x=x;from.y=y;to=target->body.position;
                if(!GeneratedPickupLineClear(*room,from,to))continue;
                const float pdx=x-runtime.playerBody.position.x,pdy=y-runtime.playerBody.position.y;
                const float score=pdx*pdx+pdy*pdy;
                if(score<bestScore){bestScore=score;bestCell=cell;}
            }
        }
        if(!inPlace&&bestCell<0)return false;
        unsigned int moved=0;
        if(!inPlace){
            if(!GeneratedPickupMoveToCell(session,runtime,bestCell,&moved,false,&context))return false;
            *ticks+=moved;
        }
        printf("GENERATED_PICKUP_COMBAT iter=%d enemies=%d hp=%d/%d target=(%.1f,%.1f) player=(%.1f,%.1f) cell=%d moved=%u inPlace=%d\n",
            guard,live.enemies.count,runtime.playerState.redHearts,runtime.playerState.maxRedHearts,
            target->body.position.x,target->body.position.y,runtime.playerBody.position.x,runtime.playerBody.position.y,
            bestCell,moved,inPlace?1:0);
        for(int shot=0;shot<16&&live.enemies.count>0;++shot){
            bool alive=false;
            for(int i=0;i<live.enemies.count;++i)if(live.enemies.entries[i]==target&&!live.enemies.entries[i]->dead)alive=true;
            if(!alive)break;
            const int action=target->body.position.x>=runtime.playerBody.position.x?5:4;
            if(!GeneratedPickupCombatTick(session,runtime,action,context))return false;
            ++*ticks;
            if(runtime.playerDead)return false;
        }
    }
    return live.enemies.count==0;
}

static bool GeneratedPickupMoveToCell(PortGameSession& session,PortNormalRuntime& runtime,int goal,unsigned int* ticks,
    bool allowTransition=false,GeneratedPickupRoomContext* context=0){
    if(!ticks)return false;*ticks=0;
    NativeRoomState* room=runtime.world.Current();if(!room)return false;
    const NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    const unsigned int transitions=runtime.roomTransitions;
    const NativePoint origin=runtime.playerBody.position;
    const NativeRoomGrid& grid=room->geometry;
    const int start=grid.PositionIndex(runtime.playerBody.position.x,runtime.playerBody.position.y);
    if(start<0||goal<0)return false;
    int goalCollision=-1;
    if(goal>=grid.cells||room->collision.occupied[goal]||!room->collision.At(goal,false,&goalCollision)||
       (goalCollision!=0&&goalCollision!=5)){
        printf("GENERATED_PICKUP_GOAL_INVALID goal=%d occupied=%d collision=%d start=%d\n",goal,
            goal<grid.cells&&room->collision.occupied[goal]?1:0,goalCollision,start);return false;
    }
    int parent[kNativeRoomGridLimit],queue[kNativeRoomGridLimit],head=0,tail=0;
    for(int i=0;i<kNativeRoomGridLimit;++i)parent[i]=-2;
    parent[start]=-1;queue[tail++]=start;
    while(head<tail&&parent[goal]==-2){
        const int at=queue[head++],cx=at%grid.stride,cy=at/grid.stride;
        const int dx[4]={1,-1,0,0},dy[4]={0,0,1,-1};
        for(int k=0;k<4;++k){
            const int nx=cx+dx[k],ny=cy+dy[k];if(nx<0||nx>=grid.stride||ny<0||ny>=grid.rows)continue;
            const int next=ny*grid.stride+nx;
            if(next<0||parent[next]!=-2||room->collision.occupied[next]||
               (!allowTransition&&GeneratedPickupApproachesDoor(*room,live,origin,next)))continue;
            parent[next]=at;queue[tail++]=next;
        }
    }
    if(parent[goal]==-2){
        printf("GENERATED_PICKUP_PATH missing start=%d goal=%d size=%dx%d stride=%d occupied=%d\n",
            start,goal,grid.width,grid.height,grid.stride,room->collision.occupied[start]?1:0);
        for(int y=0;y<grid.rows;++y){
            printf("GENERATED_PICKUP_GRID row=%d ",y);
            for(int x=0;x<grid.stride;++x){const int at=y*grid.stride+x;putchar(room->collision.occupied[at]?'#':'.');}
            putchar('\n');
        }
        return false;
    }
    int path[kNativeRoomGridLimit],count=0;
    for(int at=goal;at!=start&&at>=0;at=parent[at])path[count++]=at;
    for(int p=count-1;p>=0;--p){
        //The native position/index scalar rounds at the cell midpoint. Aim
        //inside the cell's lower half so pathfind indices remain stable.
        float x=0,y=0;if(!grid.Position(path[p],&x,&y))return false;x+=10.0f;y+=10.0f;
        if(grid.PositionIndex(x,y)!=path[p]){
            printf("GENERATED_PICKUP_WAYPOINT_MAP fail cell=%d mapped=%d pos=(%.3f,%.3f)\n",
                path[p],grid.PositionIndex(x,y),x,y);return false;
        }
        int stalled=0;bool arrived=false;
        for(int guard=0;guard<500;++guard){
            const float dx=x-runtime.playerBody.position.x,dy=y-runtime.playerBody.position.y;
            const int mapped=grid.PositionIndex(runtime.playerBody.position.x,runtime.playerBody.position.y);
            const float speed2=runtime.playerBody.velocity.x*runtime.playerBody.velocity.x+
                runtime.playerBody.velocity.y*runtime.playerBody.velocity.y;
            if(mapped==path[p]&&speed2<=0.0025f){arrived=true;break;}
            float vx=0,vy=0;
            if(mapped!=path[p]){
                const float distance=sqrtf(dx*dx+dy*dy);
                const float magnitude=distance>40.0f?1.0f:distance/40.0f;
                vx=fabsf(dx)>=fabsf(dy)?(dx>0?magnitude:-magnitude):0.0f;
                vy=fabsf(dy)>fabsf(dx)?(dy>0?magnitude:-magnitude):0.0f;
            }
            const float ox=runtime.playerBody.position.x,oy=runtime.playerBody.position.y;
            if(context){if(!GeneratedPickupRoomTick(session,runtime,*context,vx,vy,false))return false;}
            else if(!GeneratedPickupTick(session,runtime,vx,vy,false))return false;
            ++*ticks;
            if(runtime.roomTransitions!=transitions)return allowTransition;
            if(fabsf(ox-runtime.playerBody.position.x)>=0.001f||fabsf(oy-runtime.playerBody.position.y)>=0.001f)stalled=0;
            else if(++stalled>=8)break;
        }
        if(!arrived){
            printf("GENERATED_PICKUP_MOVE_TIMEOUT cell=%d target=(%.3f,%.3f) actual=(%.3f,%.3f) mapped=%d stalled=%d\n",
                path[p],x,y,runtime.playerBody.position.x,runtime.playerBody.position.y,
                grid.PositionIndex(runtime.playerBody.position.x,runtime.playerBody.position.y),stalled);return false;
        }
    }
    if(context&&!GeneratedPickupRoomIdentity(runtime,*context))return false;
    return true;
}

static bool GeneratedPickupApproachDoor(PortGameSession& session,PortNormalRuntime& runtime,const NativeDoorActor& door,unsigned int* ticks){
    if(!ticks)return false;*ticks=0;
    const unsigned int transitions=runtime.roomTransitions;
    const int startingList=runtime.world.currentList;
    NativeRoomState* room=runtime.world.Current();if(!room)return false;
    float x=0,y=0;if(!room->geometry.Position(door.grid,&x,&y))return false;x+=20.0f;y+=20.0f;
    int best=-1;float bestD=1.0e9f;
    for(int cell=0;cell<room->geometry.cells;++cell){
        if(room->collision.occupied[cell])continue;
        float px=0,py=0;room->geometry.Position(cell,&px,&py);px+=10.0f;py+=10.0f;
        const float ddx=px-x,ddy=py-y,d=ddx*ddx+ddy*ddy;if(d<bestD){bestD=d;best=cell;}
    }
    if(best<0)return false;
    printf("GENERATED_PICKUP_DOOR_NAV door=%d best=%d start=%d distance=%.3f\n",door.grid,best,
        room->geometry.PositionIndex(runtime.playerBody.position.x,runtime.playerBody.position.y),sqrtf(bestD));
    unsigned int moved=0;const bool reachedStaging=GeneratedPickupMoveToCell(session,runtime,best,&moved,true);
    *ticks+=moved;
    {
        const float dx=runtime.playerBody.position.x-x,dy=runtime.playerBody.position.y-y;
        if(!reachedStaging&&dx*dx+dy*dy>60.0f*60.0f){
            printf("GENERATED_PICKUP_DOOR_NAV fail moved=%u player=(%.3f,%.3f) doorDistance=%.3f\n",moved,
                runtime.playerBody.position.x,runtime.playerBody.position.y,sqrtf(dx*dx+dy*dy));return false;
        }
    }
    if(runtime.roomTransitions!=transitions)return true;
    if(runtime.world.currentList!=startingList)return true;
    for(int guard=0;guard<80;++guard){
        float dx=x-runtime.playerBody.position.x,dy=y-runtime.playerBody.position.y;
        if(dx*dx+dy*dy<=2500.0f)break;
        const float vx=fabsf(dx)>=fabsf(dy)?(dx>0?1.0f:-1.0f):0.0f;
        const float vy=fabsf(dy)>fabsf(dx)?(dy>0?1.0f:-1.0f):0.0f;
        if(!GeneratedPickupTick(session,runtime,vx,vy,false))return false;++*ticks;
    }
    if(runtime.roomTransitions!=transitions)return true;
    if(runtime.world.currentList!=startingList)return true;
    const unsigned int before=transitions;
    for(int guard=0;guard<120&&runtime.roomTransitions==before;++guard){
        float dx=x-runtime.playerBody.position.x,dy=y-runtime.playerBody.position.y;
        const float vx=fabsf(dx)>=fabsf(dy)?(dx>0?1.0f:-1.0f):0.0f;
        const float vy=fabsf(dy)>fabsf(dx)?(dy>0?1.0f:-1.0f):0.0f;
        if(!GeneratedPickupTick(session,runtime,vx,vy,false))return false;++*ticks;
    }
    if(runtime.roomTransitions!=before+1&&runtime.world.currentList==startingList){
        const float dx=x-runtime.playerBody.position.x,dy=y-runtime.playerBody.position.y;
        const NativePlacedRoom* current=runtime.world.CurrentDescriptor();
        printf("GENERATED_PICKUP_DOOR_CROSS fail room=%d player=(%.3f,%.3f) distance=%.3f transitions=%u/%u\n",
            runtime.world.currentList,runtime.playerBody.position.x,runtime.playerBody.position.y,sqrtf(dx*dx+dy*dy),
            runtime.roomTransitions,before);
        printf("GENERATED_PICKUP_DOOR_CROSS_STATE list=%d def=%d clear=%d grace=%d state=%d target=%d\n",
            runtime.world.currentList,current?current->definition:-1,runtime.world.Current()->clear?1:0,
            runtime.doorTransitionGrace,runtime.world.resident[runtime.world.currentList].doors?runtime.world.resident[runtime.world.currentList].doors->actors[3].state:-1,
            runtime.world.resident[runtime.world.currentList].doors?runtime.world.resident[runtime.world.currentList].doors->actors[3].targetType:-1);
        return false;
    }
    return true;
}

static bool GeneratedPickupLeaveDoor(PortGameSession& session,PortNormalRuntime& runtime,unsigned int* ticks){
    if(!ticks)return false;
    NativeRoomState* room=runtime.world.Current();if(!room)return false;
    const NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    const unsigned int transitions=runtime.roomTransitions;
    const float cx=(room->space.left+room->space.right)*0.5f;
    const float cy=(room->space.top+room->space.bottom)*0.5f;
    int stalled=0;
    for(int guard=0;guard<24;++guard){
        const int cell=room->geometry.PositionIndex(runtime.playerBody.position.x,runtime.playerBody.position.y);
        if(cell>=0&&!GeneratedPickupDoorContactCell(*room,live,cell))return true;
        const float dx=cx-runtime.playerBody.position.x,dy=cy-runtime.playerBody.position.y;
        const float vx=fabsf(dx)>=fabsf(dy)?(dx>0?1.0f:-1.0f):0.0f;
        const float vy=fabsf(dy)>fabsf(dx)?(dy>0?1.0f:-1.0f):0.0f;
        const float ox=runtime.playerBody.position.x,oy=runtime.playerBody.position.y;
        if(!GeneratedPickupTick(session,runtime,vx,vy,false))return false;++*ticks;
        if(runtime.roomTransitions!=transitions)return false;
        if(fabsf(ox-runtime.playerBody.position.x)>=0.001f||fabsf(oy-runtime.playerBody.position.y)>=0.001f)stalled=0;
        else if(++stalled>=8)return false;
    }
    const int cell=room->geometry.PositionIndex(runtime.playerBody.position.x,runtime.playerBody.position.y);
    return cell>=0&&!GeneratedPickupDoorContactCell(*room,live,cell);
}

static bool GeneratedPickupEnterRoom(PortGameSession& session,PortNormalRuntime& runtime,unsigned int* ticks){
    if(!ticks)return false;
    NativeRoomState* room=runtime.world.Current();if(!room)return false;
    const NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    if(!GeneratedPickupLeaveDoor(session,runtime,ticks))return false;
    const float targetX=(room->space.left+room->space.right)*0.5f;
    const float targetY=(room->space.top+room->space.bottom)*0.5f;
    int best=-1;float bestD=1.0e9f;
    for(int cell=0;cell<room->geometry.cells;++cell){
        if(room->collision.occupied[cell]||GeneratedPickupDoorContactCell(*room,live,cell))continue;
        float px=0,py=0;room->geometry.Position(cell,&px,&py);px+=10.0f;py+=10.0f;
        const float dx=px-targetX,dy=py-targetY,d=dx*dx+dy*dy;
        if(d<bestD){bestD=d;best=cell;}
    }
    if(best<0)return false;
    float x=0,y=0;room->geometry.Position(best,&x,&y);x+=10.0f;y+=10.0f;
    const int start=room->geometry.PositionIndex(runtime.playerBody.position.x,runtime.playerBody.position.y);
    printf("GENERATED_PICKUP_ENTRY_NAV start=%d best=%d distance=%.3f\n",start,best,
        sqrtf((x-runtime.playerBody.position.x)*(x-runtime.playerBody.position.x)+
        (y-runtime.playerBody.position.y)*(y-runtime.playerBody.position.y)));
    if(!GeneratedPickupMoveToCell(session,runtime,best,ticks)){
        printf("GENERATED_PICKUP_ENTRY_NAV fail moved=%u player=(%.3f,%.3f)\n",*ticks,
            runtime.playerBody.position.x,runtime.playerBody.position.y);return false;
    }
    return true;
}

static bool GeneratedPickupMoveToDoor(PortGameSession& session,PortNormalRuntime& runtime,int slot,unsigned int* ticks){
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    if(!live.doors||slot<0||slot>=8||!(live.doors->attached&(1U<<slot))){
        printf("GENERATED_PICKUP_DOOR_STATE slot=%d doors=%d attached=%08x\n",slot,live.doors?1:0,
            live.doors?live.doors->attached:0U);return false;
    }
    NativeDoorActor& door=live.doors->actors[slot];
    printf("GENERATED_PICKUP_DOOR_STATE slot=%d grid=%d state=%d targetType=%d clear=%d player=(%.3f,%.3f)\n",
        slot,door.grid,door.state,door.targetType,runtime.world.Current()->clear?1:0,
        runtime.playerBody.position.x,runtime.playerBody.position.y);
    return GeneratedPickupApproachDoor(session,runtime,live.doors->actors[slot],ticks);
}

struct GeneratedPickupGridExpectation {int cell,type;};
static bool GeneratedPickupGridPresent(const NativeRoomState& room,int cell){
    for(int i=0;i<room.staticGridCount;++i)if(room.staticGrids[i].grid==cell)return true;
    return false;
}
static bool GeneratedPickupBombGrids(const NativeRoomState& room,const NativePoint& at,float radius,
    std::vector<GeneratedPickupGridExpectation>* inside,std::vector<GeneratedPickupGridExpectation>* outside){
    if(!inside||!outside)return false;
    inside->clear();outside->clear();
    for(int i=0;i<room.staticGridCount;++i){
        const NativeStaticGrid& grid=room.staticGrids[i];
        if(grid.type!=1000&&grid.type!=1001&&grid.type!=1002&&grid.type!=1500&&grid.type!=1400)continue;
        float x=0,y=0;if(!room.geometry.Position(grid.grid,&x,&y))return false;
        const float dx=x+20.0f-at.x,dy=y+20.0f-at.y;GeneratedPickupGridExpectation item={grid.grid,room.collision.types[grid.grid]};
        if(dx*dx+dy*dy<=radius*radius)inside->push_back(item);else outside->push_back(item);
    }
    return true;
}
static bool GeneratedPickupBombOutcome(const NativeRoomState& room,const std::vector<GeneratedPickupGridExpectation>& inside,
    const std::vector<GeneratedPickupGridExpectation>& outside,int beforeCount){
    if(room.staticGridCount!=beforeCount-(int)inside.size())return false;
    for(unsigned int i=0;i<inside.size();++i){
        const int cell=inside[i].cell;
        if(GeneratedPickupGridPresent(room,cell)||room.collision.occupied[cell]||room.collision.types[cell])return false;
    }
    for(unsigned int i=0;i<outside.size();++i){
        const int cell=outside[i].cell;
        if(!GeneratedPickupGridPresent(room,cell)||!room.collision.occupied[cell]||room.collision.types[cell]!=outside[i].type)return false;
    }
    return true;
}
static bool GeneratedPickupSavedGridsMatch(const NativeSaveSummary::SavedRoom& saved,
    const NativeRoomState& room,const NativeWorldResident& live,bool restored,unsigned int frame,
    int doorClass,int type6Cost,int* failure=0){
    if(failure)*failure=1;
    if(saved.grids.size()!=kNativeRoomGridLimit)return false;
    int expectedStatic=0,expectedDoors=0,expectedDecorations=0;
    for(int cell=0;cell<kNativeRoomGridLimit;++cell){
        const NativeSaveSummary::Grid& value=saved.grids[cell];
        if(cell>=room.geometry.cells){
            if(value.type||value.variant||value.state||value.count||value.seed||value.auxiliary||value.tail){if(failure)*failure=2;return false;}
            continue;
        }
        if(!value.type){
            for(int i=0;i<room.staticGridCount;++i)if(room.staticGrids[i].grid==cell){if(failure)*failure=3;return false;}
            for(int i=0;i<room.decorationCount;++i)if(room.decorations[i].cell==cell){if(failure)*failure=4;return false;}
            if(room.collision.occupied[cell]||room.collision.classes[cell]||room.collision.types[cell]||room.collision.native76c[cell]){
                if(failure)*failure=cell*100+5;return false;}
            continue;
        }
        if(value.type==16){
            ++expectedDoors;bool found=false;
            for(int slot=0;live.doors&&slot<8;++slot)if((live.doors->attached&(1U<<slot))&&live.doors->actors[slot].grid==cell){
                const NativeDoorActor& door=live.doors->actors[slot];
                //The constructor applies 0/2->1, then the normal cleared-room
                //door lifecycle may reopen/reselect the final saved state.
                //Final Continue validation therefore compares the serialized
                //state; the constructor reset itself is covered independently.
                const int expectedState=value.state;
                const unsigned int expectedCount=restored?value.count+1:value.count;
                if(door.variant!=value.variant){if(failure)*failure=cell*100+20;return false;}
                if(door.state!=expectedState){if(failure)*failure=cell*100+30+door.state;return false;}
                if(door.auxiliary!=(int)value.auxiliary){if(failure)*failure=cell*100+22;return false;}
                if(door.seed!=value.seed){if(failure)*failure=cell*100+23;return false;}
                if(door.tail!=value.tail){if(failure)*failure=cell*100+27;return false;}
                if(door.initializations!=(int)expectedCount){if(failure)*failure=cell*100+24;return false;}
                if(!door.initialized){if(failure)*failure=cell*100+25;return false;}
                if(restored&&door.spawnFrame!=frame){if(failure)*failure=cell*100+26;return false;}
                if(door.local.state!=0xaa17414fU||door.local.index!=35){if(failure)*failure=cell*100+28;return false;}
                found=true;break;
            }
            const int expectedDoorClass=doorClass;
            if(!found){if(failure)*failure=cell*100+7;return false;}
            if(!room.collision.occupied[cell]||room.collision.classes[cell]!=expectedDoorClass||
               room.collision.types[cell]!=16||room.collision.native76c[cell]!=1000){
                if(failure)*failure=cell*100+40+room.collision.classes[cell];return false;}
            continue;
        }
        if(value.type==1){
            ++expectedDecorations;
            bool found=false;for(int i=0;i<room.decorationCount;++i)if(room.decorations[i].cell==cell&&
                room.decorations[i].variant==value.variant&&room.decorations[i].initialSeed==value.seed){found=true;break;}
            if(!found||!room.collision.occupied[cell]||room.collision.types[cell]!=1){if(failure)*failure=8;return false;}continue;
        }
        ++expectedStatic;
        bool found=false;
        for(int i=0;i<room.staticGridCount;++i){const NativeStaticGrid& grid=room.staticGrids[i];
            if(grid.grid!=cell)continue;
            const unsigned int expectedCount=restored?value.count+1:value.count;
            const int raw=NativeRawGridTypeForVariant(value.type,value.variant);
            if(grid.nativeType!=value.type||grid.variant!=value.variant||grid.state!=value.state||
               grid.initializations!=(int)expectedCount||grid.seed!=value.seed||grid.native18!=value.auxiliary||
               grid.native20!=value.tail||grid.type!=raw||!grid.initialized||
               (restored&&grid.initializedFrame!=frame)){if(failure)*failure=9;return false;}
            found=true;break;
        }
        const int raw=NativeRawGridTypeForVariant(value.type,value.variant);const bool wall=value.type==15;
        const bool poop=value.type==14;const bool teleporter=value.type==21;
        const int expectedClass=(value.type==6||poop)?3:(teleporter?0:4);
        const int expectedCost=wall?1000:((value.type==21||value.type==24)?1000:((value.type==6||poop)?type6Cost:0));
        if(!found||!room.collision.occupied[cell]||room.collision.classes[cell]!=expectedClass||
           room.collision.types[cell]!=(wall?15:raw)||room.collision.native76c[cell]!=expectedCost){
            if(failure)*failure=cell*100+10;return false;}
    }
    int attachedDoors=0;for(int slot=0;slot<8;++slot)if(live.doors&&(live.doors->attached&(1U<<slot)))++attachedDoors;
    if(room.staticGridCount!=expectedStatic||room.decorationCount!=expectedDecorations||attachedDoors!=expectedDoors){if(failure)*failure=11;return false;}
    if(failure)*failure=0;return true;
}
static void GeneratedPickupPrintDoorCommon(const char* scope,const NativeWorldResident& live){
    if(!live.doors)return;
    for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){
        const NativeDoorActor& door=live.doors->actors[slot];
        printf("GENERATED_PICKUP_DOOR_COMMON scope=%s slot=%d grid=%d variant=%d state=%d aux=%u seed=%u count=%u frame=%u tail=%08x local=%08x/%u\n",
            scope,slot,door.grid,door.variant,door.state,door.auxiliary,door.seed,door.initializations,
            door.spawnFrame,door.tail,door.local.state,door.local.index);
    }
}

struct GeneratedPickupAmbientStats {
    unsigned int variant33,variant68,initDraws,postDraws;
    GeneratedPickupAmbientStats():variant33(0),variant68(0),initDraws(0),postDraws(0){}
};

static bool GeneratedPickupSavedAmbientMatches(const NativeSaveSummary::SavedRoom& saved,const NativeWorldResident& live,
    bool restored,GeneratedPickupAmbientStats* stats=0){
    if(!live.effects)return false;
    unsigned int effect=0,expected=0;
    GeneratedPickupAmbientStats local;if(!stats)stats=&local;else *stats=GeneratedPickupAmbientStats();
    for(unsigned int i=0;i<saved.entities.size();++i){
        const NativeSaveSummary::Entity& entity=saved.entities[i];
        if(entity.fields[0x00]==5)continue;++expected;
        if(effect>=live.effects->entities.size())return false;
        const NativeRoomEffect& current=*live.effects->entities[effect++];
        unsigned int x=0,y=0;memcpy(&x,&current.body.position.x,4);memcpy(&y,&current.body.position.y,4);
        const unsigned int signed27c=(entity.fields[0x20]&0x8000U)?(entity.fields[0x20]|0xffff0000U):entity.fields[0x20];
        if((unsigned int)current.body.type!=entity.fields[0x00]||(unsigned int)current.body.variant!=entity.fields[0x04]||
           (unsigned int)current.body.subtype!=entity.fields[0x08]||entity.fields[0x18]!=0xffffffffU||
           entity.fields[0x1c]!=0xffffffffU||(unsigned int)current.body.native27c!=signed27c||
           current.body.initialSeed!=entity.fields[0x34]||current.body.random.state!=entity.fields[0x38]||
           current.body.frameCount!=0)return false;
        if(!restored&&(x!=entity.fields[0x28]||y!=entity.fields[0x2c]))return false;
        if(restored){
            const unsigned int variant=(unsigned int)current.body.variant;
            if(variant==33){
                if(current.restoreInitGlobalDraws!=0||current.restorePostGlobalDraws!=2)return false;
                ++stats->variant33;
            }else if(variant==68){
                if(current.restoreInitGlobalDraws!=2||current.restorePostGlobalDraws<3||
                   current.restorePostGlobalDraws>4)return false;
                ++stats->variant68;
            }else return false;
            stats->initDraws+=current.restoreInitGlobalDraws;stats->postDraws+=current.restorePostGlobalDraws;
        }
    }
    return effect==live.effects->entities.size()&&effect==expected;
}
static void GeneratedPickupPrintAmbient(const char* scope,const NativeSaveSummary::SavedRoom& saved,
    const NativeWorldResident& live,const GeneratedPickupAmbientStats& stats){
    unsigned int effect=0;
    for(unsigned int i=0;i<saved.entities.size();++i){
        const NativeSaveSummary::Entity& entity=saved.entities[i];
        if(entity.fields[0x00]==5)continue;
        if(!live.effects||effect>=live.effects->entities.size())return;
        const NativeRoomEffect& current=*live.effects->entities[effect++];
        printf("GENERATED_PICKUP_AMBIENT_RECORD scope=%s index=%u variant=%u wire20=%04x native27c=%d frameCount=%d initDraws=%u postDraws=%u\n",
            scope,i,(unsigned int)current.body.variant,entity.fields[0x20]&0xffffU,current.body.native27c,
            current.body.frameCount,current.restoreInitGlobalDraws,current.restorePostGlobalDraws);
    }
    printf("GENERATED_PICKUP_AMBIENT scope=%s variant33=%u variant68=%u initDraws=%u postDraws=%u\n",
        scope,stats.variant33,stats.variant68,stats.initDraws,stats.postDraws);
}

int RunGeneratedPickupProbe(NativeMt& random,PortFrontend& ui){
    const unsigned int probeFrame=0;
    ui.characterPage=0;ui.characterChoice=1;ui.characterMode=0;
    PortNormalRuntime* runtime=new PortNormalRuntime;PortGameSession session(random,ui);
    PortSessionStart start=session.StartNormalCharacter(12352,*runtime);
    if(start.result!=NPS_WORLD_PENDING||!AdvanceToBoundary(session,*runtime)||runtime->phase!=PNR_READY){
        printf("GENERATED_PICKUP_START fail=%d\n",start.result);delete runtime;return 285;
    }
    NativeLevelLayout& layout=runtime->world.layout;const int count=layout.count,startList=runtime->world.currentList;
    int targetList=-1;for(int i=0;i<count;++i)if(layout.rooms[i].definition==268501785){targetList=i;break;}
    if(targetList<0){printf("GENERATED_PICKUP_ROOM missing\n");delete runtime;return 286;}
    std::vector<int> parent(count,-1),via(count,-1),queue;
    parent[startList]=startList;queue.push_back(startList);
    for(unsigned int q=0;q<queue.size();++q){
        const int at=queue[q];const NativePlacedRoom* p=layout.ByList(at);if(!p)continue;
        for(int s=0;s<8;++s){const NativePlacedRoom* target=layout.DoorTarget(at,s);if(!target)continue;
            const int next=target->listIndex;if(next<0||next>=count||parent[next]>=0)continue;
            parent[next]=at;via[next]=s;queue.push_back(next);}
    }
    if(parent[targetList]<0){delete runtime;return 287;}
    std::vector<int> path;for(int at=targetList;;at=parent[at]){path.push_back(at);if(at==startList)break;}
    std::reverse(path.begin(),path.end());
    printf("GENERATED_PICKUP_PATH seed=12352 rooms=%u route=",(unsigned int)path.size());
    for(unsigned int i=0;i<path.size();++i)printf("%s%d",i?",":"",layout.rooms[path[i]].gridIndex);printf("\n");

    unsigned int doorTicks=0;
    for(unsigned int i=0;i+1<path.size();++i){
        if(!GeneratedPickupMoveToDoor(session,*runtime,via[path[i+1]],&doorTicks)||runtime->world.currentList!=path[i+1]){
            printf("GENERATED_PICKUP_DOOR fail step=%u current=%d want=%d ticks=%u\n",i,
                runtime->world.currentList,path[i+1],doorTicks);delete runtime;return 288;
        }
    }
    NativeWorldResident& live=runtime->world.resident[runtime->world.currentList];
    NativeRoomState* room=runtime->world.Current();const NativePlacedRoom* descriptor=runtime->world.CurrentDescriptor();
    if(!room||!descriptor||descriptor->definition!=268501785||live.enemies.count||live.pickups.count!=2){
        printf("GENERATED_PICKUP_ROOM fail enemies=%d pickups=%d\n",live.enemies.count,live.pickups.count);
        delete runtime;return 289;
    }
    NativePickup* key=0;NativePickup* bombPickup=0;
    for(int i=0;i<live.pickups.count;++i){
        NativePickup* p=live.pickups.entries[i];if(!p)continue;
        if(p->variant==30)key=p;else if(p->variant==40)bombPickup=p;
    }
    if(!key||!bombPickup||key->grid!=42||bombPickup->grid!=92){delete runtime;return 290;}
    printf("GENERATED_PICKUP_ROOM seeds=%u/%u/%u/%u key=42 bomb=92\n",descriptor->seeds.spawn,
        descriptor->seeds.award,descriptor->seeds.decoration,descriptor->seeds.auxiliary);
    for(int slot=0;slot<8;++slot)if(live.doors&&(live.doors->attached&(1U<<slot)))
        printf("GENERATED_PICKUP_DOOR_ROOM slot=%d grid=%d state=%d target=%d\n",slot,
            live.doors->actors[slot].grid,live.doors->actors[slot].state,live.doors->actors[slot].targetType);
    for(int i=0;i<room->staticGridCount;++i)if(room->staticGrids[i].grid==77||room->staticGrids[i].grid==91||
       room->staticGrids[i].grid==92||room->staticGrids[i].grid==93||room->staticGrids[i].grid==107)
        printf("GENERATED_PICKUP_GRID_STATE cell=%d type=%d nativeType=%d variant=%d tail=%08x count=%d frame=%u\n",
            room->staticGrids[i].grid,room->staticGrids[i].type,room->staticGrids[i].nativeType,
            room->staticGrids[i].variant,room->staticGrids[i].native20,room->staticGrids[i].initializations,
            room->staticGrids[i].initializedFrame);
    for(int cell=0;cell<room->geometry.cells;++cell)if(cell==42||cell==77||cell==91||cell==92||cell==93||cell==107)
        printf("GENERATED_PICKUP_COLLISION cell=%d occupied=%d class=%d type=%d cost=%d\n",cell,
            room->collision.occupied[cell]?1:0,room->collision.classes[cell],room->collision.types[cell],
            room->collision.native76c[cell]);

    unsigned int internalTicks=0;
    if(!GeneratedPickupEnterRoom(session,*runtime,&internalTicks)){delete runtime;return 291;}
    GeneratedPickupRoomContext interactionRoom={runtime->world.currentList,descriptor->definition,1,1,room,runtime->roomTransitions};
    const NativeMt interactionRng=random;
    unsigned int ticks=0;
    printf("GENERATED_PICKUP_MOVE_PRE first player=(%.3f,%.3f) start=%d goal=62\n",
        runtime->playerBody.position.x,runtime->playerBody.position.y,
        room->geometry.PositionIndex(runtime->playerBody.position.x,runtime->playerBody.position.y));
    if(!GeneratedPickupMoveToCell(session,*runtime,62,&ticks,false,&interactionRoom)){delete runtime;return 292;}
    printf("GENERATED_PICKUP_MOVE_POST first player=(%.3f,%.3f) cell=%d ticks=%u\n",
        runtime->playerBody.position.x,runtime->playerBody.position.y,
        room->geometry.PositionIndex(runtime->playerBody.position.x,runtime->playerBody.position.y),ticks);
    const int bombs0=runtime->playerState.bombs;const unsigned int placed0=runtime->bombs.placed;
    if(!GeneratedPickupRoomTick(session,*runtime,interactionRoom,0,0,true)||runtime->playerState.bombs!=bombs0-1||
       runtime->bombs.placed!=placed0+1){delete runtime;return 293;}
    if(runtime->bombs.active.empty()){delete runtime;return 293;}
    const NativePoint firstBomb=runtime->bombs.active.back().position;
    const float firstRadius=runtime->bombs.active.back().radius;
    std::vector<GeneratedPickupGridExpectation> firstInside,firstOutside;const int firstGridCount=room->staticGridCount;
    if(!GeneratedPickupBombGrids(*room,firstBomb,firstRadius,&firstInside,&firstOutside)){delete runtime;return 293;}
    printf("GENERATED_PICKUP_BLAST first pos=(%.3f,%.3f) radius=%.3f inside=%u outside=%u playerCell=%d cells=",
        firstBomb.x,firstBomb.y,firstRadius,(unsigned int)firstInside.size(),(unsigned int)firstOutside.size(),
        room->geometry.PositionIndex(runtime->playerBody.position.x,runtime->playerBody.position.y));
    for(unsigned int i=0;i<firstInside.size();++i)printf("%s%d",i?",":"",firstInside[i].cell);printf("\n");
    if(!GeneratedPickupMoveToCell(session,*runtime,64,&ticks,false,&interactionRoom)){delete runtime;return 294;}
    for(int i=0;i<60;++i)if(!GeneratedPickupRoomTick(session,*runtime,interactionRoom,0,0,false)){delete runtime;return 295;}
    if(runtime->bombs.explosions!=1||!GeneratedPickupBombOutcome(*room,firstInside,firstOutside,firstGridCount)){
        printf("GENERATED_PICKUP_BOMB fail explosions=%u inside=%u outside=%u grids=%d want=%d\n",
            runtime->bombs.explosions,(unsigned int)firstInside.size(),(unsigned int)firstOutside.size(),
            room->staticGridCount,firstGridCount-(int)firstInside.size());delete runtime;return 296;
    }
    const unsigned int rocksAfterFirst=runtime->bombs.rockHits;
    for(int i=0;i<live.pickups.count;++i)if(live.pickups.entries[i]){const NativePickup& p=*live.pickups.entries[i];
        unsigned int d8bits=0;memcpy(&d8bits,&p.nativeD8,4);
        printf("GENERATED_PICKUP_CONTROL_LIVE index=%d type=%d variant=%d subtype=%d grid=%d current=%u initial=%u fields=%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u pos=(%.3f,%.3f)\n",
            i,p.type,p.variant,p.subtype,p.grid,p.currentSeed,p.initialSeed,p.native46c,p.native478,p.native480,
            p.native484,p.native27c,p.native474,p.native476,p.native4cc,p.native470,p.native34,p.native38,p.native360,
            p.native4e0,p.native47c,p.nativeExtraCount,p.nativeChildCount,p.position.x,p.position.y);
        printf("GENERATED_PICKUP_CONTROL_LIVE_D8 index=%d bits=%08x\n",i,d8bits);}
    {int invalidOwners=0;for(int i=0;i<37;++i)if(!runtime->run.bosses.owners[i].state||runtime->run.bosses.owners[i].index!=17)++invalidOwners;
     printf("GENERATED_PICKUP_BOSS_STATE entries=%d invalidOwners=%d\n",runtime->run.bosses.entryCount,invalidOwners);}
    printf("GENERATED_PICKUP_PERSIST_STATE visited=%u flags=%u effects=%d doors=%d gridPost=%d native6f60=%d native11ec=%d cells=%d\n",
        descriptor->visitedCount,descriptor->flags,live.effects?1:0,live.doors?1:0,live.gridPostConstructed?1:0,
        room->native6f60,room->native11ec,room->geometry.cells);
    GeneratedPickupPrintDoorCommon("control-live",live);
    const bool controlSaved=session.SaveNativeContinue("build/gameplay/generated_pickup_native_control.dat",*runtime);
    NativeSaveSummary controlSummary;
    if(controlSaved)NativeReadSaveSummary("build/gameplay/generated_pickup_native_control.dat",controlSummary);
    for(unsigned int i=0;i<controlSummary.currentRoomPickups.size();++i){const NativeSaveSummary::Pickup& p=controlSummary.currentRoomPickups[i];
        unsigned int d8bits=0;memcpy(&d8bits,&p.nativeD8,4);
        printf("GENERATED_PICKUP_CONTROL_RECORD index=%u type=%d variant=%d subtype=%d grid=%d current=%u initial=%u fields=%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u/%u pos=(%.3f,%.3f)\n",
            i,p.type,p.variant,p.subtype,p.grid,p.currentSeed,p.initialSeed,p.native46c,p.native478,p.native480,
            p.native484,p.native27c,p.native474,p.native476,p.native4cc,p.native470,p.native34,p.native38,p.native360,
            p.native4e0,p.native47c,p.nativeExtraCount,p.nativeChildCount,p.x,p.y);
        printf("GENERATED_PICKUP_CONTROL_RECORD_D8 index=%u bits=%08x\n",i,d8bits);}
    const NativeSaveSummary::SavedRoom* controlSavedRoom=
        controlSummary.currentSavedRoomIndex>=0&&controlSummary.currentSavedRoomIndex<(int)controlSummary.savedRooms.size()?
        &controlSummary.savedRooms[controlSummary.currentSavedRoomIndex]:0;
    const bool controlRoomMatches=controlSummary.valid&&GeneratedPickupSavedRoomMatches(controlSummary,*descriptor);
    int controlGridFailure=1;
    const bool controlGridsSaved=controlSavedRoom&&
        GeneratedPickupSavedGridsMatch(*controlSavedRoom,*room,live,false,0,0,1500,&controlGridFailure);
    const bool controlActorsSaved=controlSavedRoom&&GeneratedPickupSavedAmbientMatches(*controlSavedRoom,live,false);
    const bool controlCaptureOk=controlSaved&&controlSummary.valid&&controlRoomMatches&&
        controlSummary.currentRoomPickups.size()==2&&controlSavedRoom&&controlGridsSaved&&controlActorsSaved;
    printf("GENERATED_PICKUP_CONTROL_DECODE valid=%d room=%d records=%u grids=%d actors=%d index=%d\n",
        controlSummary.valid?1:0,controlRoomMatches?1:0,(unsigned int)controlSummary.currentRoomPickups.size(),
        controlGridsSaved?1:0,controlActorsSaved?1:0,controlSummary.currentSavedRoomIndex);
    if(!controlGridsSaved)printf("GENERATED_PICKUP_CONTROL_GRID_FAIL code=%d\n",controlGridFailure);
    bool controlRestored=false,controlPresent=false,controlRecords=false,controlGrids=false,controlActors=false;
    GeneratedPickupAmbientStats controlAmbientRestored;
    if(controlSaved){
        PortNormalRuntime* controlRuntime=new PortNormalRuntime;NativeMt controlRandom;controlRandom.Seed(7331);
        PortGameSession controlSession(controlRandom,ui);
        NativeSceneFiles controlFiles={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(1),runtime->run.context.nativeMode,0};
        controlRestored=controlSession.ContinueNativeSave("build/gameplay/generated_pickup_native_control.dat",*controlRuntime,controlFiles);
        if(!controlRestored)printf("GENERATED_PICKUP_CONTROL_RESTORE_FAIL phase=%d next=%08x residents=%d\n",
            controlRuntime->phase,controlRuntime->missingNativePrerequisite,controlRuntime->world.residentCount);
        if(controlRestored){
            const NativePlacedRoom* controlRoom=controlRuntime->world.CurrentDescriptor();
            const NativeWorldResident& controlLive=controlRuntime->world.resident[controlRuntime->world.currentList];
            int active=0;for(int i=0;i<controlLive.pickups.count;++i)if(controlLive.pickups.entries[i]&&!controlLive.pickups.entries[i]->collected)++active;
            std::vector<bool> used(controlLive.pickups.count?controlLive.pickups.count:0,false);
            unsigned int matched=0;
            for(unsigned int s=0;s<controlSummary.currentRoomPickups.size();++s){
                int found=-1;
                for(int i=0;i<controlLive.pickups.count;++i){
                    NativePickup* pickup=controlLive.pickups.entries[i];
                    if(!pickup||used[i]||!GeneratedPickupSavedEqualsLive(controlSummary.currentRoomPickups[s],*pickup))continue;
                    if(found>=0){found=-2;break;}found=i;
                }
                if(found>=0){used[found]=true;++matched;}
            }
            const bool roomOk=controlRoom&&controlRoom->definition==268501785&&
                GeneratedPickupSavedRoomMatches(controlSummary,*controlRoom);
            controlRecords=matched==controlSummary.currentRoomPickups.size()&&active==(int)matched&&
                controlLive.pickups.count==(int)matched;
            int controlRestoreGridFailure=1;
            controlGrids=controlSavedRoom&&GeneratedPickupSavedGridsMatch(*controlSavedRoom,*controlRuntime->world.Current(),controlLive,true,probeFrame,4,1400,&controlRestoreGridFailure);
            controlActors=controlSavedRoom&&GeneratedPickupSavedAmbientMatches(*controlSavedRoom,controlLive,true,&controlAmbientRestored);
            if(controlGrids)GeneratedPickupPrintDoorCommon("control-restored",controlLive);
            controlPresent=roomOk&&active==2&&controlRecords&&controlGrids&&controlActors;
            if(controlActors)GeneratedPickupPrintAmbient("control",*controlSavedRoom,controlLive,controlAmbientRestored);
            printf("GENERATED_PICKUP_CONTROL_ROOM grid=%d dim=%d def=%d pickups=%d active=%d matched=%u roomOk=%d grids=%d actors=%d frame=%u\n",
                controlRuntime->world.currentGrid,controlRuntime->world.layout.currentDimension,
                controlRoom?controlRoom->definition:-1,controlLive.pickups.count,active,matched,roomOk?1:0,
                controlGrids?1:0,controlActors?1:0,probeFrame);
            if(!controlGrids)printf("GENERATED_PICKUP_CONTROL_RESTORE_GRID_FAIL code=%d\n",controlRestoreGridFailure);
        }
        delete controlRuntime;
    }
    printf("GENERATED_PICKUP_CONTROL saved=%d captured=%d restored=%d records=%d present=%d capture=%08x entities=%u savedPickups=%u currentGrid=%d\n",controlSaved?1:0,
        controlCaptureOk?1:0,controlRestored?1:0,controlRecords?1:0,controlPresent?1:0,runtime->nativeSaveCaptureAddress,
        live.persistent?(unsigned int)live.persistent->entities.size():0U,
        (unsigned int)controlSummary.currentRoomPickups.size(),controlSummary.currentGrid);
    if(!controlCaptureOk||!controlRestored||!controlPresent){delete runtime;return 309;}
    if(!GeneratedPickupMoveToCell(session,*runtime,92,&ticks,false,&interactionRoom)||
       !GeneratedPickupRoomTick(session,*runtime,interactionRoom,0,0,false)||
       !bombPickup->collected||runtime->playerState.bombs!=1){delete runtime;return 297;}
    printf("GENERATED_PICKUP_BOMB first removed=%u inside=%u retainedOutOfRadius=%u bombPickup=1 bombs=1 hp=%d/%d hits=%u\n",
        rocksAfterFirst,(unsigned int)firstInside.size(),(unsigned int)firstOutside.size(),
        runtime->playerState.redHearts,runtime->playerState.maxRedHearts,runtime->playerHits);

    if(!GeneratedPickupMoveToCell(session,*runtime,72,&ticks,false,&interactionRoom)){delete runtime;return 298;}
    const int bombs1=runtime->playerState.bombs;const unsigned int placed1=runtime->bombs.placed;
    if(!GeneratedPickupRoomTick(session,*runtime,interactionRoom,0,0,true)||runtime->playerState.bombs!=bombs1-1||
       runtime->bombs.placed!=placed1+1){delete runtime;return 299;}
    if(runtime->bombs.active.empty()){delete runtime;return 299;}
    const NativePoint secondBomb=runtime->bombs.active.back().position;
    const float secondRadius=runtime->bombs.active.back().radius;
    std::vector<GeneratedPickupGridExpectation> secondInside,secondOutside;const int secondGridCount=room->staticGridCount;
    if(!GeneratedPickupBombGrids(*room,secondBomb,secondRadius,&secondInside,&secondOutside)){delete runtime;return 299;}
    printf("GENERATED_PICKUP_BLAST second pos=(%.3f,%.3f) radius=%.3f inside=%u outside=%u cells=",
        secondBomb.x,secondBomb.y,secondRadius,(unsigned int)secondInside.size(),(unsigned int)secondOutside.size());
    for(unsigned int i=0;i<secondInside.size();++i)printf("%s%d",i?",":"",secondInside[i].cell);printf("\n");
    if(!GeneratedPickupMoveToCell(session,*runtime,68,&ticks,false,&interactionRoom)){delete runtime;return 300;}
    for(int i=0;i<60;++i)if(!GeneratedPickupRoomTick(session,*runtime,interactionRoom,0,0,false)){delete runtime;return 301;}
    if(runtime->bombs.explosions!=2||!GeneratedPickupBombOutcome(*room,secondInside,secondOutside,secondGridCount)){
        printf("GENERATED_PICKUP_BOMB second-fail explosions=%u inside=%u outside=%u grids=%d want=%d\n",
            runtime->bombs.explosions,(unsigned int)secondInside.size(),(unsigned int)secondOutside.size(),
            room->staticGridCount,secondGridCount-(int)secondInside.size());delete runtime;return 302;
    }
    printf("GENERATED_PICKUP_BOMB second removed=%u inside=%u retainedOutOfRadius=%u hp=%d/%d hits=%u\n",
        runtime->bombs.rockHits-rocksAfterFirst,(unsigned int)secondInside.size(),(unsigned int)secondOutside.size(),
        runtime->playerState.redHearts,runtime->playerState.maxRedHearts,runtime->playerHits);
    if(!GeneratedPickupMoveToCell(session,*runtime,42,&ticks,false,&interactionRoom)||
       !GeneratedPickupRoomTick(session,*runtime,interactionRoom,0,0,false)||
       !key->collected||runtime->playerState.keys!=1){delete runtime;return 303;}
    if(memcmp(&interactionRng,&random,sizeof(random))){printf("GENERATED_PICKUP_RNG changed\n");delete runtime;return 304;}
    printf("GENERATED_PICKUP_KEY cell=42 keys=1 bombs=0 ticks=%u globalMTUnchanged=1 hp=%d/%d hits=%u\n",
        ticks+internalTicks,runtime->playerState.redHearts,runtime->playerState.maxRedHearts,runtime->playerHits);

    const int savedKeys=runtime->playerState.keys,savedBombs=runtime->playerState.bombs;
    int startDoor=-1,reverseDoor=-1;
    for(int s=0;s<8;++s){const NativePlacedRoom* linked=layout.DoorTarget(targetList,s,&reverseDoor);
        if(linked&&linked->listIndex==startList){startDoor=s;break;}}
    if(startDoor<0||!live.doors||!(live.doors->attached&(1U<<startDoor))){delete runtime;return 305;}
    if(!GeneratedPickupMoveToDoor(session,*runtime,startDoor,&ticks)||runtime->world.currentList!=startList){delete runtime;return 306;}
    if(!GeneratedPickupMoveToDoor(session,*runtime,via[targetList],&ticks)||runtime->world.currentList!=targetList){delete runtime;return 308;}
    if(!GeneratedPickupEnterRoom(session,*runtime,&ticks)){delete runtime;return 308;}
    NativeWorldResident& retained=runtime->world.resident[targetList];
    const NativePlacedRoom* retainedDescriptor=runtime->world.CurrentDescriptor();
    unsigned int consumed=0,active=0;for(int i=0;i<retained.pickups.count;++i)if(retained.pickups.entries[i]){
        if(retained.pickups.entries[i]->collected)++consumed;else ++active;}
    if(!retainedDescriptor||retainedDescriptor->definition!=268501785||consumed!=2||active!=0){delete runtime;return 307;}
    GeneratedPickupRoomContext reentryRoom={runtime->world.currentList,retainedDescriptor->definition,1,1,
        runtime->world.Current(),runtime->roomTransitions};
    const int noCollectKeys=runtime->playerState.keys,noCollectBombs=runtime->playerState.bombs;
    if(!GeneratedPickupMoveToCell(session,*runtime,92,&ticks,false,&reentryRoom)||
       !GeneratedPickupRoomTick(session,*runtime,reentryRoom,0,0,false)||
       !bombPickup->collected||runtime->playerState.keys!=noCollectKeys||runtime->playerState.bombs!=noCollectBombs){
        delete runtime;return 307;}
    printf("GENERATED_PICKUP_REENTRY consumed=2/2 active=0 activations=%u noRecollect=1\n",retained.activations);

    const NativePoint savedBombPosition=bombPickup->position;
    const NativePoint savedKeyPosition=key->position;
    const bool savedNative=session.SaveNativeContinue("build/gameplay/generated_pickup_native.dat",*runtime);
    GeneratedPickupPrintDoorCommon("native-live",live);
    NativeSaveSummary nativeSummary;
    if(savedNative)NativeReadSaveSummary("build/gameplay/generated_pickup_native.dat",nativeSummary);
    const NativeSaveSummary::SavedRoom* nativeSavedRoom=
        nativeSummary.currentSavedRoomIndex>=0&&nativeSummary.currentSavedRoomIndex<(int)nativeSummary.savedRooms.size()?
        &nativeSummary.savedRooms[nativeSummary.currentSavedRoomIndex]:0;
    NativeRoomSpace nativeExpectedSpace=room->space;
    if(!nativeExpectedSpace.RefreshBounds(room->geometry,room->collision))return 310;
    const bool nativeCaptureOk=savedNative&&nativeSummary.valid&&NativeRoom(runtime->world.CurrentDescriptor()->definition)&&
        GeneratedPickupSavedRoomMatches(nativeSummary,*runtime->world.CurrentDescriptor())&&
        nativeSummary.currentRoomPickups.empty()&&nativeSavedRoom&&
        GeneratedPickupSavedGridsMatch(*nativeSavedRoom,*room,live,false,0,0,1500)&&
        GeneratedPickupSavedAmbientMatches(*nativeSavedRoom,live,false);
    bool restored=false,resources=false,absent=false,noRestoredContact=false,bombContactInRadius=false;
    bool nativeGrids=false,nativeActors=false,nativeSpace=false,nativePath=false;
    unsigned int restoredActive=0,duplicates=0;
    GeneratedPickupAmbientStats nativeAmbientRestored;
    if(savedNative){
        PortNormalRuntime* resumed=new PortNormalRuntime;NativeMt resumedRandom;resumedRandom.Seed(7331);
        PortGameSession resumedSession(resumedRandom,ui);
        NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(1),runtime->run.context.nativeMode,0};
        restored=resumedSession.ContinueNativeSave("build/gameplay/generated_pickup_native.dat",*resumed,files);
        if(restored){
            resources=resumed->playerState.keys==savedKeys&&resumed->playerState.bombs==savedBombs;
            const NativePlacedRoom* resumedDescriptor=resumed->world.CurrentDescriptor();
            NativeWorldResident& resumedLive=resumed->world.resident[resumed->world.currentList];
            restoredActive=(unsigned int)resumedLive.pickups.count;
            for(int i=0;i<resumedLive.pickups.count;++i)if(resumedLive.pickups.entries[i]&&!resumedLive.pickups.entries[i]->collected)++duplicates;
            absent=duplicates==0;
            const int contactCell=resumed->world.Current()->geometry.PositionIndex(resumed->playerBody.position.x,resumed->playerBody.position.y);
            const float contactRadius=resumed->playerBody.collision.radius+12.0f;
            const float bombDx=resumed->playerBody.position.x-savedBombPosition.x;
            const float bombDy=resumed->playerBody.position.y-savedBombPosition.y;
            bombContactInRadius=bombDx*bombDx+bombDy*bombDy<=contactRadius*contactRadius;
            const bool roomOk=resumedDescriptor&&resumedDescriptor->definition==268501785&&
                GeneratedPickupSavedRoomMatches(nativeSummary,*resumedDescriptor);
            if(roomOk&&contactCell==92&&bombContactInRadius&&GeneratedPickupTick(resumedSession,*resumed,0,0,false)){
                noRestoredContact=resumed->world.resident[resumed->world.currentList].pickups.count==0&&
                    resumed->playerState.keys==savedKeys&&resumed->playerState.bombs==savedBombs;
            }
            nativeGrids=nativeSavedRoom&&GeneratedPickupSavedGridsMatch(*nativeSavedRoom,*resumed->world.Current(),resumedLive,true,files.frame,4,1400);
            nativeActors=nativeSavedRoom&&GeneratedPickupSavedAmbientMatches(*nativeSavedRoom,resumedLive,true,&nativeAmbientRestored);
            if(nativeGrids)GeneratedPickupPrintDoorCommon("current-restored",resumedLive);
            if(nativeActors)GeneratedPickupPrintAmbient("current",*nativeSavedRoom,resumedLive,nativeAmbientRestored);
            nativeSpace=!memcmp(&nativeExpectedSpace,&resumed->world.Current()->space,sizeof(nativeExpectedSpace));
            GeneratedPickupRoomContext restoredRoom={resumed->world.currentList,resumedDescriptor?resumedDescriptor->definition:-1,1,1,
                resumed->world.Current(),resumed->roomTransitions};
            unsigned int pathTicks=0;
            nativePath=noRestoredContact&&
                GeneratedPickupMoveToCell(resumedSession,*resumed,77,&pathTicks,false,&restoredRoom)&&
                GeneratedPickupMoveToCell(resumedSession,*resumed,52,&pathTicks,false,&restoredRoom);
            printf("GENERATED_PICKUP_RESTORED_CONTACT cell=%d bombRadius=%.3f bombWithin=%d keyCell=%d roomOk=%d\n",
                contactCell,contactRadius,bombContactInRadius?1:0,
                resumed->world.Current()->geometry.PositionIndex(savedKeyPosition.x,savedKeyPosition.y),roomOk?1:0);
            printf("GENERATED_PICKUP_RESTORED_STATE grids=%d records=%d actors=%d space=%d frame=%u path=%u\n",
                nativeGrids?1:0,nativeSavedRoom?((int)nativeSavedRoom->entities.size()):0,nativeActors?1:0,
                nativeSpace?1:0,files.frame,nativePath?1:0);
        }
        delete resumed;
    }
    printf("GENERATED_PICKUP_NATIVE saved=%d captured=%d restored=%d resources=%d consumedAbsent=%d active=%u duplicates=%u contactRadius=%d noRestoredContact=%d capture=%08x gap=%s\n",
        savedNative?1:0,nativeCaptureOk?1:0,restored?1:0,resources?1:0,absent?1:0,restoredActive,duplicates,
        bombContactInRadius?1:0,noRestoredContact?1:0,runtime->nativeSaveCaptureAddress,
        absent?"none":"native-current-room-pickup-state");
    int result=!nativeCaptureOk||!restored||!resources||!absent||!noRestoredContact||duplicates||
        !nativeGrids||!nativeActors||!nativeSpace||!nativePath?310:0;
    if(!result)printf("GENERATED_PICKUP_COMPLETE bomb=1 key=1 reentry=1 native=1\n");
    bool noncurrent=false,noncurrentActors=false,noncurrentSpace=false,noncurrentPath=false,untouchedEntry=false;
    GeneratedPickupAmbientStats noncurrentAmbientRestored;
    bool noncurrentStartActors=false;
    GeneratedPickupAmbientStats noncurrentStartAmbientRestored;
    unsigned int noncurrentGrids=0;int untouchedControl=0;
    if(!result){
        //Capture the now-consumed target while leaving it normally, then save
        //from the linked starting room. This follows6f5037's pre-switch owner.
        if(!GeneratedPickupMoveToDoor(session,*runtime,startDoor,&ticks)||runtime->world.currentList!=startList)
            result=311;
        else if(!session.SaveNativeContinue("build/gameplay/generated_pickup_noncurrent.dat",*runtime))
            result=312;
        else{
            NativeSaveSummary noncurrentSummary;
            if(!NativeReadSaveSummary("build/gameplay/generated_pickup_noncurrent.dat",noncurrentSummary)||!noncurrentSummary.valid)
                result=313;
            const NativeSaveSummary::SavedRoom* savedTarget=0;
            unsigned int visited=0;
            for(unsigned int i=0;i<noncurrentSummary.savedRooms.size();++i){
                if(noncurrentSummary.savedRooms[i].visitedCount)++visited;
                if(noncurrentSummary.savedRooms[i].listIndex==targetList)savedTarget=&noncurrentSummary.savedRooms[i];
            }
            if(!savedTarget||!NativeRoom(268501785)||savedTarget->definitionType!=NativeRoom(268501785)->type||
               savedTarget->definitionVariant!=(int)NativeRoom(268501785)->variant||!savedTarget->pickups.empty())
                result=314;
            else{
                PortNormalRuntime* resumed=new PortNormalRuntime;NativeMt resumedRandom;resumedRandom.Seed(7441);
                PortGameSession resumedSession(resumedRandom,ui);
                NativeSceneFiles resumedFiles={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(1),runtime->run.context.nativeMode,0};
                if(!resumedSession.ContinueNativeSave("build/gameplay/generated_pickup_noncurrent.dat",*resumed,resumedFiles))
                    result=315;
                else{
                    const NativeSaveSummary::SavedRoom* savedStart=
                        noncurrentSummary.currentSavedRoomIndex>=0&&
                        noncurrentSummary.currentSavedRoomIndex<(int)noncurrentSummary.savedRooms.size()?
                        &noncurrentSummary.savedRooms[noncurrentSummary.currentSavedRoomIndex]:0;
                    if(!savedStart||savedStart->grid!=resumed->world.currentGrid)result=316;
                    if(result==0){
                        const NativeWorldResident& resumedStart=resumed->world.resident[resumed->world.currentList];
                        int startGridFailure=1;
                        const bool startGrids=GeneratedPickupSavedGridsMatch(*savedStart,*resumed->world.Current(),
                            resumedStart,true,probeFrame,4,1400,&startGridFailure);
                        noncurrentStartActors=GeneratedPickupSavedAmbientMatches(*savedStart,resumedStart,true,
                            &noncurrentStartAmbientRestored);
                        if(!startGrids||!noncurrentStartActors||noncurrentStartAmbientRestored.variant33==0||
                           noncurrentStartAmbientRestored.variant68==0)result=316;
                        else{
                            GeneratedPickupPrintDoorCommon("noncurrent-start",resumedStart);
                            GeneratedPickupPrintAmbient("noncurrent-start",*savedStart,resumedStart,
                                noncurrentStartAmbientRestored);
                        }
                    }
                    bool untouchedFound=false;
                    for(unsigned int i=0;i<noncurrentSummary.savedRooms.size();++i)
                        if(!noncurrentSummary.savedRooms[i].visitedCount&&noncurrentSummary.savedRooms[i].listIndex>=0&&
                           noncurrentSummary.savedRooms[i].listIndex<kNativeWorldRoomLimit&&
                           !resumed->world.RestoredRoom(noncurrentSummary.savedRooms[i].listIndex)){
                            untouchedControl=1;untouchedFound=true;break;
                        }
                    if(resumed->world.restoredRooms.size()!=visited||!untouchedFound)result=316;
                    const int resumedStartList=resumed->world.currentList;
                    int returnSlot=-1;
                    for(int slot=0;slot<8;++slot){
                        const NativePlacedRoom* linked=resumed->world.layout.DoorTarget(resumedStartList,slot);
                        if(linked&&linked->listIndex==targetList){returnSlot=slot;break;}
                    }
                    if(result==0&&(returnSlot<0||!GeneratedPickupMoveToDoor(resumedSession,*resumed,returnSlot,&ticks)||
                       resumed->world.currentList!=targetList))result=317;
                    if(result==0&&!GeneratedPickupEnterRoom(resumedSession,*resumed,&ticks))result=318;
                    if(result==0){
                        const NativeWorldResident& resumedTarget=resumed->world.resident[targetList];
                        const NativeRoomState* returnedRoom=resumed->world.Current();
                        noncurrentGrids=(unsigned int)resumed->world.Current()->staticGridCount;
                        int activePickups=0;for(int i=0;i<resumedTarget.pickups.count;++i)
                            if(resumedTarget.pickups.entries[i]&&!resumedTarget.pickups.entries[i]->collected)++activePickups;
                        NativeRoomSpace expectedSpace=returnedRoom->space;
                        if(!expectedSpace.RefreshBounds(returnedRoom->geometry,returnedRoom->collision))result=319;
                        noncurrentActors=GeneratedPickupSavedAmbientMatches(*savedTarget,resumedTarget,true,&noncurrentAmbientRestored);
                        if(noncurrentActors)GeneratedPickupPrintAmbient("noncurrent",*savedTarget,resumedTarget,
                            noncurrentAmbientRestored);
                        int noncurrentGridFailure=1;
                        noncurrent=resumed->world.CurrentDescriptor()&&
                            resumed->world.CurrentDescriptor()->definition==268501785&&activePickups==0&&
                            resumed->playerState.keys==savedKeys&&resumed->playerState.bombs==savedBombs&&
                            GeneratedPickupSavedGridsMatch(*savedTarget,*returnedRoom,resumedTarget,true,probeFrame,0,1500,&noncurrentGridFailure)&&
                            noncurrentActors;
                        if(noncurrent)GeneratedPickupPrintDoorCommon("noncurrent-restored",resumedTarget);
                        if(!noncurrent){
                            printf("GENERATED_PICKUP_NONCURRENT_FAIL room=%d def=%d active=%d resources=%d gridFail=%d actors=%d\n",
                                resumed->world.currentGrid,resumed->world.CurrentDescriptor()?resumed->world.CurrentDescriptor()->definition:-1,
                                activePickups,resumed->playerState.keys==savedKeys&&resumed->playerState.bombs==savedBombs?1:0,
                                noncurrentGridFailure,noncurrentActors?1:0);
                            result=319;
                        }
                        if(result==0){
                            GeneratedPickupRoomContext returnedContext={resumed->world.currentList,
                                resumed->world.CurrentDescriptor()->definition,1,1,resumed->world.Current(),resumed->roomTransitions};
                            unsigned int pathTicks=0;
                            noncurrentPath=GeneratedPickupMoveToCell(resumedSession,*resumed,77,&pathTicks,false,&returnedContext)&&
                                GeneratedPickupMoveToCell(resumedSession,*resumed,52,&pathTicks,false,&returnedContext);
                            noncurrentSpace=!memcmp(&expectedSpace,&resumed->world.Current()->space,sizeof(expectedSpace));
                            int postGridFailure=1;
                            const bool postGrids=GeneratedPickupSavedGridsMatch(*savedTarget,*resumed->world.Current(),
                                resumedTarget,true,probeFrame,0,1500,&postGridFailure);
                            if(!noncurrentPath||!noncurrentSpace||!postGrids){
                                printf("GENERATED_PICKUP_NONCURRENT_POST_FAIL path=%d space=%d gridFail=%d position=%d\n",
                                    noncurrentPath?1:0,noncurrentSpace?1:0,postGridFailure,resumed->world.Current()->geometry.PositionIndex(
                                        resumed->playerBody.position.x,resumed->playerBody.position.y));
                                result=320;
                            }
                            if(result==0){
                                const int beforeUntouched=resumed->world.currentList;
                                if(!GeneratedPickupMoveToDoor(resumedSession,*resumed,startDoor,&ticks)||
                                   resumed->world.currentList!=startList)result=321;
                                int untouchedSlot=-1,untouchedList=-1;
                                if(result==0)for(int slot=0;slot<8;++slot){
                                    const NativePlacedRoom* linked=resumed->world.layout.DoorTarget(resumed->world.currentList,slot);
                                    if(!linked)continue;
                                    for(unsigned int i=0;i<noncurrentSummary.savedRooms.size();++i)
                                        if(noncurrentSummary.savedRooms[i].listIndex==linked->listIndex&&
                                           !noncurrentSummary.savedRooms[i].visitedCount){
                                            untouchedSlot=slot;untouchedList=linked->listIndex;break;}
                                    if(untouchedSlot>=0)break;
                                }
                                if(result==0&&(beforeUntouched!=targetList||untouchedSlot<0||untouchedList<0||
                                   !GeneratedPickupMoveToDoor(resumedSession,*resumed,untouchedSlot,&ticks)||
                                   resumed->world.currentList!=untouchedList))result=321;
                                if(result==0){
                                    const NativeWorldResident& fresh=resumed->world.resident[resumed->world.currentList];
                                    untouchedEntry=resumed->world.Current()&&!resumed->world.RestoredRoom(resumed->world.currentList)&&
                                        (fresh.room->staticGridCount>0||fresh.room->decorationCount>0||fresh.enemies.count>0);
                                    printf("GENERATED_PICKUP_UNTOUCHED entered=%d list=%d grid=%d restored=%d static=%d cells=%d enemies=%d\n",
                                        untouchedEntry?1:0,resumed->world.currentList,resumed->world.currentGrid,
                                        resumed->world.RestoredRoom(resumed->world.currentList)?1:0,
                                        fresh.room?fresh.room->staticGridCount:-1,fresh.room?fresh.room->geometry.cells:-1,fresh.enemies.count);
                                    const NativePlacedRoom* untouchedDescriptor=resumed->world.CurrentDescriptor();
                                    if(untouchedDescriptor)printf("GENERATED_PICKUP_UNTOUCHED_SEEDS spawn=%u award=%u decoration=%u auxiliary=%u flags=%08x\n",
                                        untouchedDescriptor->seeds.spawn,untouchedDescriptor->seeds.award,
                                        untouchedDescriptor->seeds.decoration,untouchedDescriptor->seeds.auxiliary,untouchedDescriptor->flags);
                                    for(int enemy=0;enemy<fresh.enemies.count;++enemy)if(fresh.enemies.entries[enemy])
                                        printf("GENERATED_PICKUP_UNTOUCHED_ACTOR index=%d type=%d variant=%d subtype=%d seed=%u\n",
                                            enemy,fresh.enemies.entries[enemy]->type,fresh.enemies.entries[enemy]->body.variant,
                                            fresh.enemies.entries[enemy]->body.subtype,fresh.enemies.entries[enemy]->seed);
                                    for(int pickup=0;pickup<fresh.pickups.count;++pickup)if(fresh.pickups.entries[pickup])
                                        printf("GENERATED_PICKUP_UNTOUCHED_REWARD index=%d type=%d variant=%d subtype=%d grid=%d seed=%u\n",
                                            pickup,fresh.pickups.entries[pickup]->type,fresh.pickups.entries[pickup]->variant,
                                            fresh.pickups.entries[pickup]->subtype,fresh.pickups.entries[pickup]->grid,
                                            fresh.pickups.entries[pickup]->initialSeed);
                                    for(int slot=0;slot<8;++slot){
                                        const NativePlacedRoom* linked=resumed->world.layout.DoorTarget(resumed->world.currentList,slot);
                                        if(linked)printf("GENERATED_PICKUP_UNTOUCHED_DOOR slot=%d targetList=%d targetGrid=%d definition=%d\n",
                                            slot,linked->listIndex,linked->gridIndex,linked->definition);
                                    }
                                    if(!untouchedEntry)result=322;
                                    if(result==0){
                                        //Clear this actual generated room with normal movement/fire.
                                        GeneratedPickupRoomContext untouchedContext={resumed->world.currentList,
                                            untouchedDescriptor?untouchedDescriptor->definition:-1,0,0,resumed->world.Current(),
                                            resumed->roomTransitions};
                                        unsigned int combatTicks=0;
                                        if(!GeneratedPickupClearRoom(resumedSession,*resumed,untouchedContext,&combatTicks)){
                                            printf("GENERATED_PICKUP_UNTOUCHED_CLEAR fail ticks=%u enemies=%d phase=%d\n",
                                                combatTicks,resumed->world.resident[resumed->world.currentList].enemies.count,resumed->phase);
                                            result=323;
                                        }
                                        NativeRoomState* clearedRoom=resumed->world.Current();
                                        NativeWorldResident& clearedLive=resumed->world.resident[resumed->world.currentList];
                                        if(result==0&&!clearedRoom->clear){
                                            if(!GeneratedPickupTick(resumedSession,*resumed,0,0,false))result=324;
                                        }
                                        if(result==0&&(!clearedRoom->clear||!clearedRoom->rewardResolved))result=324;
                                        int reward=-1,activeRewards=0;
                                        if(result==0)for(int i=0;i<clearedLive.pickups.count;++i){
                                            NativePickup* p=clearedLive.pickups.entries[i];
                                            if(p&&!p->collected){++activeRewards;reward=i;}
                                        }
                                        if(result==0)printf("GENERATED_PICKUP_UNTOUCHED_CLEAR clear=%d resolved=%d variant=%d subtype=%d pickups=%d active=%d ticks=%u\n",
                                            clearedRoom->clear?1:0,clearedRoom->rewardResolved?1:0,clearedRoom->rewardVariant,
                                            clearedRoom->rewardSubtype,clearedLive.pickups.count,activeRewards,combatTicks);
                                        if(result==0&&activeRewards>1)result=325;
                                        if(result==0&&clearedRoom->rewardVariant>0){
                                            if(reward<0)result=325;
                                            else{
                                                NativePickup* p=clearedLive.pickups.entries[reward];
                                                if(p->type!=5||p->variant!=clearedRoom->rewardVariant||
                                                   p->subtype!=clearedRoom->rewardSubtype)result=326;
                                                else printf("GENERATED_PICKUP_UNTOUCHED_REWARD variant=%d subtype=%d grid=%d seed=%u\n",
                                                    p->variant,p->subtype,p->grid,p->initialSeed);
                                            }
                                        }
                                        //Save/Continue before collection: identity must survive.
                                        if(result==0&&reward>=0){
                                            NativePickup* p=clearedLive.pickups.entries[reward];
                                            if(!resumedSession.SaveNativeContinue("build/gameplay/generated_pickup_untouched_before.dat",*resumed)){
                                                printf("GENERATED_PICKUP_UNTOUCHED_BEFORE_SAVE fail=%08x\n",resumed->nativeSaveCaptureAddress);
                                                result=327;
                                            }else{
                                                NativeSaveSummary beforeSummary;
                                                if(!NativeReadSaveSummary("build/gameplay/generated_pickup_untouched_before.dat",beforeSummary)||
                                                   !beforeSummary.valid){printf("GENERATED_PICKUP_UNTOUCHED_BEFORE_READ fail\n");result=328;}
                                                else{
                                                    bool found=false;
                                                    for(unsigned int i=0;i<beforeSummary.currentRoomPickups.size();++i){
                                                        const NativeSaveSummary::Pickup& s=beforeSummary.currentRoomPickups[i];
                                                        if(s.variant==p->variant&&s.subtype==p->subtype&&s.grid==p->grid&&
                                                           s.initialSeed==p->initialSeed)found=true;
                                                    }
                                                    if(!found)result=328;
                                                }
                                                if(result==0){
                                                    PortNormalRuntime* beforeRuntime=new PortNormalRuntime;
                                                    NativeMt beforeMt;beforeMt.Seed(777);
                                                    PortGameSession beforeSession(beforeMt,ui);
                                                    NativeSceneFiles beforeFiles={"runtime/game/file_index.bin","runtime/game/",
                                                        NativeStageBackdrop(resumed->run.context.stage),resumed->run.context.nativeMode,0};
                                                    if(!beforeSession.ContinueNativeSave("build/gameplay/generated_pickup_untouched_before.dat",
                                                        *beforeRuntime,beforeFiles)){
                                                        printf("GENERATED_PICKUP_UNTOUCHED_BEFORE_CONTINUE fail prereq=%08x phase=%d entry=%d nativeNext=%08x\n",
                                                            beforeRuntime->missingNativePrerequisite,beforeRuntime->phase,
                                                            beforeRuntime->entryIssue.result,beforeRuntime->entryIssue.nativeNext);
                                                        result=329;
                                                    }else{
                                                        NativeRoomState* beforeRoom=beforeRuntime->world.Current();
                                                        NativeWorldResident& beforeLive=beforeRuntime->world.resident[beforeRuntime->world.currentList];
                                                        int beforeActive=0;bool same=false;
                                                        if(beforeRoom)for(int i=0;i<beforeLive.pickups.count;++i){
                                                            NativePickup* q=beforeLive.pickups.entries[i];
                                                            if(!q||q->collected)continue;++beforeActive;
                                                            if(q->variant==p->variant&&q->subtype==p->subtype&&q->grid==p->grid&&
                                                               q->initialSeed==p->initialSeed)same=true;
                                                        }
                                                        if(beforeRuntime->world.currentList!=untouchedContext.list||!beforeRoom||
                                                           !beforeRoom->clear||beforeActive!=1||!same){
                                                            printf("GENERATED_PICKUP_UNTOUCHED_BEFORE_STATE list=%d/%d room=%d clear=%d active=%d same=%d pickups=%d\n",
                                                                beforeRuntime->world.currentList,untouchedContext.list,beforeRoom?1:0,
                                                                beforeRoom?beforeRoom->clear?1:0:0,beforeActive,same?1:0,
                                                                beforeLive.pickups.count);
                                                            result=329;
                                                        }
                                                    }
                                                    delete beforeRuntime;
                                                }
                                            }
                                        }
                                        //Collect the stored identity through contact only.
                                        if(result==0&&reward>=0){
                                            NativePickup* p=clearedLive.pickups.entries[reward];
                                            unsigned int collectTicks=0;
                                            if(!GeneratedPickupMoveToCell(resumedSession,*resumed,p->grid,&collectTicks,false,&untouchedContext))result=330;
                                            else{
                                                for(int t=0;t<20&&!p->collected;++t)
                                                    if(!GeneratedPickupTick(resumedSession,*resumed,0,0,false)){result=331;break;}
                                                if(!p->collected)result=331;
                                            }
                                            if(result==0)printf("GENERATED_PICKUP_UNTOUCHED_COLLECT variant=%d subtype=%d collected=%d card=%d pill=%d keys=%d bombs=%d ticks=%u\n",
                                                p->variant,p->subtype,p->collected?1:0,resumed->playerState.heldCard,
                                                resumed->playerState.heldPill,resumed->playerState.keys,resumed->playerState.bombs,
                                                collectTicks);
                                        }
                                        //Save/Continue after collection: consumed reward must stay absent.
                                        if(result==0){
                                            if(!resumedSession.SaveNativeContinue("build/gameplay/generated_pickup_untouched_after.dat",*resumed))result=332;
                                            else{
                                                NativeSaveSummary afterSummary;
                                                if(!NativeReadSaveSummary("build/gameplay/generated_pickup_untouched_after.dat",afterSummary)||
                                                   !afterSummary.valid)result=333;
                                                else if(!afterSummary.currentRoomPickups.empty())result=333;
                                                if(result==0){
                                                    PortNormalRuntime* afterRuntime=new PortNormalRuntime;
                                                    NativeMt afterMt;afterMt.Seed(778);
                                                    PortGameSession afterSession(afterMt,ui);
                                                    NativeSceneFiles afterFiles={"runtime/game/file_index.bin","runtime/game/",
                                                        NativeStageBackdrop(resumed->run.context.stage),resumed->run.context.nativeMode,0};
                                                    if(!afterSession.ContinueNativeSave("build/gameplay/generated_pickup_untouched_after.dat",
                                                        *afterRuntime,afterFiles))result=334;
                                                    else{
                                                        NativeWorldResident& afterLive=afterRuntime->world.resident[afterRuntime->world.currentList];
                                                        int afterActive=0;
                                                        for(int i=0;i<afterLive.pickups.count;++i)
                                                            if(afterLive.pickups.entries[i]&&!afterLive.pickups.entries[i]->collected)++afterActive;
                                                        if(afterRuntime->world.currentList!=untouchedContext.list||
                                                           !afterRuntime->world.Current()||!afterRuntime->world.Current()->clear||
                                                           afterActive!=0)result=334;
                                                    }
                                                    delete afterRuntime;
                                                }
                                            }
                                        }
                                        //Normal-door reentry must retain clear and consumed absence.
                                        if(result==0){
                                            int backSlot=-1,backTarget=-1;
                                            NativeWorldResident& reentrySource=resumed->world.resident[resumed->world.currentList];
                                            for(int slot=0;reentrySource.doors&&slot<8;++slot){
                                                if(!(reentrySource.doors->attached&(1U<<slot))||reentrySource.doors->actors[slot].state!=2)continue;
                                                const NativePlacedRoom* linked=resumed->world.layout.DoorTarget(resumed->world.currentList,slot);
                                                if(linked&&linked->listIndex!=resumed->world.currentList){
                                                    backSlot=slot;backTarget=linked->listIndex;break;
                                                }
                                            }
                                            unsigned int reentryTicks=0;int forwardSlot=-1;
                                            if(backSlot<0||!GeneratedPickupMoveToDoor(resumedSession,*resumed,backSlot,&reentryTicks)||
                                               resumed->world.currentList!=backTarget){
                                                printf("GENERATED_PICKUP_UNTOUCHED_REENTRY_BACK fail slot=%d want=%d got=%d ticks=%u\n",
                                                    backSlot,backTarget,resumed->world.currentList,reentryTicks);
                                                result=335;
                                            }else{
                                                NativeWorldResident& backRoom=resumed->world.resident[resumed->world.currentList];
                                                for(int slot=0;backRoom.doors&&slot<8;++slot){
                                                    if(!(backRoom.doors->attached&(1U<<slot))||backRoom.doors->actors[slot].state!=2)continue;
                                                    const NativePlacedRoom* linked=resumed->world.layout.DoorTarget(resumed->world.currentList,slot);
                                                    if(linked&&linked->listIndex==untouchedList){forwardSlot=slot;break;}
                                                }
                                                if(forwardSlot<0||!GeneratedPickupMoveToDoor(resumedSession,*resumed,forwardSlot,&reentryTicks)||
                                                   resumed->world.currentList!=untouchedList){
                                                    printf("GENERATED_PICKUP_UNTOUCHED_REENTRY_FORWARD fail slot=%d want=%d got=%d ticks=%u\n",
                                                        forwardSlot,untouchedList,resumed->world.currentList,reentryTicks);
                                                    result=336;
                                                }
                                            }
                                            if(result==0){
                                                NativeRoomState* reentryRoom=resumed->world.Current();
                                                NativeWorldResident& reentryLive=resumed->world.resident[resumed->world.currentList];
                                                int reentryActive=0;
                                                for(int i=0;i<reentryLive.pickups.count;++i)
                                                    if(reentryLive.pickups.entries[i]&&!reentryLive.pickups.entries[i]->collected)++reentryActive;
                                                if(!reentryRoom||!reentryRoom->clear||reentryActive!=0)result=337;
                                                else printf("GENERATED_PICKUP_UNTOUCHED_REENTRY back=%d(%d) forward=%d clear=1 active=0 activations=%u ticks=%u\n",
                                                    backSlot,backTarget,forwardSlot,reentryLive.activations,reentryTicks);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                delete resumed;
            }
        }
    }
    if(!result)
        printf("GENERATED_PICKUP_NONCURRENT restored=%d grids=%u actors=%d space=%d path=%d pickups=0 untouchedControl=%d untouchedEntry=%d resources=%d keyCell=42 bombCell=92 frame=%u\n",
            noncurrent?1:0,noncurrentGrids,noncurrentActors?1:0,noncurrentSpace?1:0,noncurrentPath?1:0,
            untouchedControl,untouchedEntry?1:0,savedKeys==1&&savedBombs==0?1:0,probeFrame);
    delete runtime;return result;
}
