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
    unsigned int spawned,wallHits,expired,enemyKills;
    float cooldown;
    NativeProjectileSystem();
    void Reset();
    bool Step(PortNormalRuntime&,NativeActionSource*,unsigned int frame,unsigned int* next);
    void Swap(NativeProjectileSystem&);
private:
    NativeProjectileSystem(const NativeProjectileSystem&);
    NativeProjectileSystem& operator=(const NativeProjectileSystem&);
};
//Enemy-fired tear (Pooter and later shooters). Behavior comes from the
//owning enemy update; this owner advances, collides and damages the player.
struct NativeEnemyShot {
    NativePoint position,velocity;
    float damage,radius;
    int life;
    unsigned int owner;
};
class NativeEnemyShotSystem {
public:
    std::vector<NativeEnemyShot> active;
    unsigned int spawned,wallHits;
    NativeEnemyShotSystem();
    void Clear();
    void Fire(const NativePoint& position,float vx,float vy,float damage,float radius,int life,unsigned int owner);
    bool Step(PortNormalRuntime& runtime);
    void Swap(NativeEnemyShotSystem&);
private:
    NativeEnemyShotSystem(const NativeEnemyShotSystem&);
    NativeEnemyShotSystem& operator=(const NativeEnemyShotSystem&);
};
#endif
