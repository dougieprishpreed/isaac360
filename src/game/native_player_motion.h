#ifndef GENERIC_NATIVE_PLAYER_MOTION_H
#define GENERIC_NATIVE_PLAYER_MOTION_H
#include "native_player_input.h"

//0072ae20 movement service. This is called within736d70's player update;
//returning here alone is not a whole entity/world tick.
enum NativePlayerVelocityResult { NPV_RETURNED,NPV_NEED_UPDATE_TAIL,NPV_BAD_REQUEST,NPV_SOURCE_MISSING,NPV_BAD_VALUE };
class NativeMovementEffects {
public:
    virtual ~NativeMovementEffects(){}
    virtual NativePlayerInputResult Has(int cacheId,unsigned int playerToken,bool*)=0;
    virtual NativePlayerInputResult Collectible(int,unsigned int,bool*){return NPI_SOURCE_MISSING;}
};
struct NativePlayerVelocityState {
    NativePoint velocity;             //+2b0
    NativePoint previousVelocity;     //+1584
    NativePoint movement;             //+1568
    NativePoint lastMovement;         //+1578
    int direction;float angle;        //+1548,+19f4
    int native16d8,native1dc4;
    int native1ddc,native1d94;
};
//Evaluated entity fields, not guessed constructor defaults or item activation.
//Offset names retain uncertainty about fields whose full lifecycle is pending.
struct NativePlayerVelocityContext {
    NativePlayerInputContext input,twinInput;
    bool twinInputKnown;
    unsigned int flagsLow,flagsHigh;
    float native1d78,native2a0,moveSpeed,native2ec;
    int native1580,native1798,native1e24;
    bool native14a4,native1ef4,native1f80;
    bool targetPresent;NativePoint position,targetPosition;
    bool native188;NativePoint native18c,native194;
};
struct NativePlayerVelocityIssue {
    unsigned int nextAddress;int missingEffect;
    NativePlayerInputResult sourceResult;
};
//State is committed only on RETURNED or NEED_UPDATE_TAIL. External query
//effects cannot be undone; a failed call must not be retried as a partial tick.
NativePlayerVelocityResult NativePlayerVelocityCore(const NativePlayerVelocityContext&,NativeActionSource&,NativeMovementEffects&,NativePlayerVelocityState*,NativePlayerVelocityIssue* issue=0);
#endif
