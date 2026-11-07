#include "native_room_spawn.h"
#include "native_spawns.h"
#include "native_decoration_data.h"
#include <string.h>
NativeRoomSpawnResult NativePlanRoomSpawns(const RoomRecord& room,const NativeRoomSeeds& seeds,NativeSpawnPlan* output) {
    NativeRoomGrid grid;
    if(!output || !grid.Init(room.shape,room.width,room.height)) return NRS_BAD_DATA;
    if(room.stackCount>kNativeRoomGridLimit) return NRS_CAPACITY;
    NativeRng rng;if(!rng.Set(seeds.award,11)) return NRS_ZERO_SEED;
    NativeSpawnPlan plan;memset(&plan,0,sizeof(plan));plan.count=room.stackCount;
    for(int pass=0;pass<2;++pass) {
        //007a0b64/007a0b98 copy the original award owner for the preliminary
        //pass.007a12e5 uses the original owner, not that advanced copy.
        rng.Set(seeds.award,11);
        RoomRecordCursor cursor=room.Stacks();RoomRecordStack stack;int i=0;
        while(cursor.Next(&stack)) {
            //007a1301..130e flattens without a rectangle-extent clamp. Native
            //grid/skip storage has448 slots, even in a nominal15x9 room. The
            //prepared stage29 lobby has a real spawn at cell172 in shape1.
            //Preserve it in the plan; physical construction is a later service.
            int cell=(stack.y+1)*grid.stride+stack.x+1;
            if(cell<0 || cell>=kNativeRoomGridLimit || i>=kNativeRoomGridLimit) return NRS_BAD_DATA;
            float sample;rng.Float(&sample); // also for an empty stack
            NativeSpawnChoice& choice=plan.choices[i++];
            if(!stack.count) continue;
            float weight;unsigned int selected;RoomRecordOption entry;
            if(NativeSpawnWeight(stack,&weight)!=NS_OK || NativePickSpawn(stack,sample,weight,&selected,&entry)!=NS_OK) return NRS_BAD_DATA;
            if(pass==0)NativeApplyRoomMarker(entry,&plan.markers);
            if(pass==1) {
                choice.x=stack.x;choice.y=stack.y;choice.grid=cell;
                choice.present=true;choice.option=selected;choice.seed=rng.state;choice.entry=entry;
            }
        }
        if(cursor.left || cursor.remaining || i!=plan.count) return NRS_BAD_DATA;
        if(!pass) plan.preliminaryState=rng.state;
    }
    plan.finalState=rng.state;*output=plan;return NRS_OK;
}
int NativeDecorationAnimationCount(int backdrop) {
    if(backdrop<0) return 0;
    for(unsigned int i=0;i<sizeof(kNativeDecorationSets)/sizeof(kNativeDecorationSets[0]);++i)
        if((int)kNativeDecorationSets[i].backdrop==backdrop) return (int)kNativeDecorationSets[i].animations;
    return 0;
}
bool NativeRoomDecoration(unsigned int spawnSeed,const NativeRoomGrid& geometry,int grid,
                         int backdrop,unsigned int* seed,int* animation) {
    int x,y,count=NativeDecorationAnimationCount(backdrop);
    if(!count || !seed || !animation || !geometry.Tile(grid,&x,&y) ||
       x<0 || x>=geometry.width || y<0 || y>=geometry.height) return false;
    unsigned int local=spawnSeed+(unsigned int)grid;
    if(!spawnSeed || !local) return false;
    local=NativeShift(local,5,9,7);
    *seed=local;*animation=local%(unsigned int)count;return true;
}
