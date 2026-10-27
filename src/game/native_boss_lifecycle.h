#ifndef GENERIC_NATIVE_BOSS_LIFECYCLE_H
#define GENERIC_NATIVE_BOSS_LIFECYCLE_H
#include "native_boss_identity.h"
#include "native_floor.h"
//Logical native save fragment, not the pointer-containing native vector ABI.
//Temporary exclusions, pool order and weights are not serialized by0041f4a0.
struct NativeBossIdentitySnapshot {
    unsigned int ownerSeeds[kNativeBossPools],persistent[4];
};
bool NativeCaptureBossState(const NativeBossIdentityManager& manager,NativeBossIdentitySnapshot* output);
//006b65a3..6681: patch already initialized metadata/order, restore37 owners with
//2/21/9 shifts and104 persistent bits, clear temporary; no shuffle/reseeding.
bool NativeApplyBossState(NativeBossIdentityManager& manager,const NativeBossIdentitySnapshot& state);
//Explicit port composition around native bootstrap's START seed, not the
//sequence/player/stage seed. Resource profile remains an explicit caller input.
bool NativeInitializeRunBosses(const NativeSeeds& seeds,const NativeBossIdentityData& data,NativeBossIdentityManager* output);
bool NativeRestoreRunBosses(const NativeSeeds& seeds,const NativeBossIdentityData& data,const NativeBossIdentitySnapshot& state,NativeBossIdentityManager* output);
//006f6bd0 is reached on every retry, before topology generation/guard. The
//ordinary floor helper still has its explicit100-attempt port guard.
NativeFloorResult NativeStepFloorWithBosses(NativeFloorGenerator& floor,NativeBossIdentityManager& manager,NativeTopology* topology);
//Call only after the FULL content and post-content branches have succeeded,
//not merely after a boss prefix. Rejection retains temporary bits until Step.
bool NativeAcceptFloorWithBosses(NativeFloorGenerator& floor,NativeBossIdentityManager& manager,const NativeTopology* topology=0);
#endif
