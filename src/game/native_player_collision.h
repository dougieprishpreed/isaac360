#ifndef GENERIC_NATIVE_PLAYER_COLLISION_H
#define GENERIC_NATIVE_PLAYER_COLLISION_H
struct PortNormalRuntime;
//6712a0: actual player sample contacts, velocity response and twelve-step
//grid-boundary refinement. Entity/entity collision belongs to4168c0 earlier.
bool NativePlayerGridCollision(PortNormalRuntime&,bool positionOnly,unsigned int* next);
#endif
