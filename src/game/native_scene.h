#ifndef GENERIC_NATIVE_SCENE_H
#define GENERIC_NATIVE_SCENE_H
#include "native_room_state.h"
#include "native_layout.h"
#include "native_room_spawn.h"
#include "native_enemy.h"
#include "native_pickup.h"
enum NativeSceneResult { NSC_OK,NSC_BAD_REQUEST,NSC_UNSUPPORTED_SHAPE,NSC_SOURCE_ERROR,NSC_UNSUPPORTED_CONTENT,NSC_UNSUPPORTED_EXTENT };
enum NativeConstructionStage { NCS_HEADER,NCS_GRID,NCS_PLAN,NCS_ENTRY,NCS_READY,NCS_FORMAT };
struct NativeConstructionIssue {NativeConstructionStage stage;int choice;unsigned int type,variant,subtype;NativeRoomSpawnResult plan;};
//Inspect every selected dependency without constructing anything or consuming
//an owner. The list follows original main-pass stack order, including repeated
//identities; it is not a support-filtered selection or dynamic-content closure.
struct NativeSceneDependency {int choice,grid;unsigned int seed,type,variant,subtype;};
struct NativeSceneDependencies {int count,supported;NativeSceneDependency missing[kNativeRoomGridLimit];};
NativeSceneResult NativeSceneInspectPlan(const RoomRecord&,const NativeSpawnPlan&,int backdrop,int nativeMode,NativeSceneDependencies*);
//Selected raw record and original seed ownership. This prepares room components;
//complete native Init, player/actors/doors and dynamic saves remain unfinished.
NativeSceneResult NativeSceneBuildRoom(const RoomRecord&,const NativeRoomSeeds&,int backdrop,int nativeMode,int configStage,
    NativeRoomState*,NativeConstructionIssue* issue=0,NativeRoomMarkers* markers=0,unsigned int frame=0,NativeEnemyList* enemies=0,NativePickupList* pickups=0);
struct NativeSceneFiles {const char* indexPath;const char* objectRoot;int backdrop,nativeMode;unsigned int frame;};
NativeSceneResult NativeSceneReadRecord(const NativeSceneFiles&,int definition,unsigned char** bytes,unsigned int* size,unsigned int* recordHash);
#endif
