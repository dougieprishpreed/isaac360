#ifndef GENERIC_NATIVE_PROJECTILE_H
#define GENERIC_NATIVE_PROJECTILE_H
#include "native_room_space.h"
#include <vector>
class NativeActionSource;
struct PortNormalRuntime;
//Maintained primary-tear lifecycle reached from the ordinary player weapon.
//It consumes the evaluated shot vector and the real fireDelay/shotSpeed/
//damage/range stat owners. Full native projectile variants, splash effects,
//enemy hits and rendering remain separate services.
struct NativeProjectile {
    NativePoint position,velocity;
    float damage,travelled,range;
    unsigned int owner;
};
class NativeProjectileSystem {
public:
    std::vector<NativeProjectile> active;
    unsigned int spawned,wallHits,expired;
    float cooldown;
    NativeProjectileSystem();
    void Reset();
    bool Step(PortNormalRuntime&,NativeActionSource*,unsigned int frame,unsigned int* next);
    void Swap(NativeProjectileSystem&);
private:
    NativeProjectileSystem(const NativeProjectileSystem&);
    NativeProjectileSystem& operator=(const NativeProjectileSystem&);
};
#endif
