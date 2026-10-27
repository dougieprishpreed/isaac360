#ifndef GENERIC_NATIVE_BOSS_IDENTITY_H
#define GENERIC_NATIVE_BOSS_IDENTITY_H
#include "native_boss.h"
static const int kNativeBossIds=104,kNativeBossPools=37,kNativeBossEntries=512;
struct NativeBossIdentityEntry {
    unsigned int id;
    float baseWeight,currentWeight;
    int achievement,variant;
};
struct NativeBossIdentityPoolData { int first,count,pairedVariant; float total; };
struct NativeBossIdentityData {
    int profile,count;
    const NativeBossIdentityPoolData* pools; //Exactly37 config-stage pools.
    const NativeBossIdentityEntry* entries;
};
//Explicit prepared profiles4/12; neither changes the active scene/package policy.
const NativeBossIdentityData* NativePreparedBossIdentityData(int profile);
struct NativeBossIdentityContext {
    int stage,stageType,nativeMode,challenge;
    bool sequence13,profile66,profile86,profile49,profileOverride,runFlag6;
    bool unlocked[kNativeBossIds]; //Effective achievement service for gated IDs.
};
enum NativeBossIdentityResult { NBI_OK,NBI_BAD_REQUEST,NBI_BAD_POOL };
enum NativeBossIdentityPath { NBI_FIXED,NBI_SPECIAL22,NBI_SPECIAL_STAGE,NBI_PAIRED_VARIANT,NBI_SPECIAL23,NBI_WEIGHTED,NBI_DEFAULT };
struct NativeBossIdentityReport {
    int identity,poolIndex,entryIndex,ownerDraws,localDraws,attempts,scanPasses,remaps,linearVisits,resets;
    unsigned int localState;
    NativeBossIdentityPath path;
};
struct NativeBossIdentityManager {
    int profile,entryCount;
    NativeBossIdentityPoolData pools[kNativeBossPools];
    NativeBossIdentityEntry entries[kNativeBossEntries];
    NativeRng owners[kNativeBossPools];
    unsigned int persistent[4],temporary[4]; //104 meaningful bits each.
    bool Initialize(unsigned int seed,const NativeBossIdentityData& data);
    bool Blocked(unsigned int id) const;
    bool SetBlocked(unsigned int id,bool persistentSet,bool value);
    void ClearTemporary(); //006ee790, preserves persistent set and owner streams.
    void CommitTemporary(); //0041e130, union into persistent then clear temporary.
    NativeBossIdentityResult Select(const NativeBossIdentityContext& context,NativeBossIdentityReport* output);
    //0041ec60 has a caller-supplied scalar sample; no RNG or weight depletion.
    NativeBossIdentityResult Sample(int poolIndex,float sample,const NativeBossIdentityContext& context,NativeBossIdentityReport* output);
};
struct NativeManagedBossReport {
    NativeBossIdentityReport identity;
    NativeBossIdentityResult identityResult;
    NativeBossPlacementReport placement;
    NativeBossSequenceResult sequenceResult;
};
//One sequential identity/placement request; caller owns temporary reset/commit
//at the proper floor lifecycle boundary. Never reselects for actor support.
NativeBossSequenceResult NativeAdvanceManagedBoss(NativeBossSequence& sequence,NativeBossIdentityManager& manager,
    const NativeBossIdentityContext& context,NativeTopology& topology,NativeRoomPool& roomPool,NativeRng& layoutStream,
    NativeLevelLayout& layout,NativeManagedBossReport* output);
#endif
