#ifndef GENERIC_NATIVE_PLAYER_INPUT_H
#define GENERIC_NATIVE_PLAYER_INPUT_H
#include "native_room_space.h"
enum NativePlayerInputResult { NPI_OK,NPI_BAD_REQUEST,NPI_SOURCE_MISSING,NPI_BAD_VALUE };
//Resolved original action values, after device mapping/deadzone processing.
//No hardware policy, Lua/mod callback system or implicit zero fallback here.
class NativeActionSource {
public:
    virtual ~NativeActionSource(){}
    virtual NativePlayerInputResult Value(int action,int controller,unsigned int playerToken,float*)=0;
    virtual NativePlayerInputResult Pressed(int action,int controller,unsigned int playerToken,bool*)=0;
    virtual NativePlayerInputResult Held(int,int,unsigned int,bool*){return NPI_SOURCE_MISSING;}
    virtual NativePlayerInputResult Connected(int,bool*){return NPI_SOURCE_MISSING;}
};
struct NativeActionFrame {
    int controller;unsigned int playerToken,frame,knownValues;
    float values[8];bool dropKnown,dropPressed;
    unsigned int knownHeld,held,knownPressed,pressed;
    bool connectionKnown,connected;
    void Clear(int controller,unsigned int playerToken,unsigned int frame);
    bool SupplyValue(int action,float value);
    void SupplyDrop(bool value){dropKnown=true;dropPressed=value;SupplyPressed(11,value);}
    bool SupplyHeld(int action,bool value);
    bool SupplyPressed(int action,bool value);
    void SupplyConnection(bool value){connectionKnown=true;connected=value;}
};
class NativeFrameActions:public NativeActionSource {
    const NativeActionFrame& frame;
    NativeFrameActions& operator=(const NativeFrameActions&);
public:
    explicit NativeFrameActions(const NativeActionFrame& f):frame(f){}
    NativePlayerInputResult Value(int,int,unsigned int,float*);
    NativePlayerInputResult Pressed(int,int,unsigned int,bool*);
    NativePlayerInputResult Held(int,int,unsigned int,bool*);
    NativePlayerInputResult Connected(int,bool*);
};
//Evaluated fields at00729520/007297a0. Pointer relations are supplied by the
//real entity owner; native1540 keeps its offset until its full role is known.
struct NativePlayerInputContext {
    int playerType,controller;unsigned int playerToken;
    bool parentPresent;int parentEntityType;
    bool twinPresent,twinIsSelf;int native1540,twinNative1540;
    bool controlsEnabled;int controlsCooldown;
    int stage,stageType,dimension;unsigned int evaluatedStageFlags;
    bool seedEffect12,seedEffect74,seedEffect75,seedEffect76;
};
//These are input vectors, not velocity or a shot. In particular, the raw
//movement getter does not suppress every controls-disabled caller or clamp
//diagonal magnitude. Those decisions occur later in the actual player update.
NativePlayerInputResult NativeMovementInput(const NativePlayerInputContext&,NativeActionSource&,NativePoint*);
NativePlayerInputResult NativeShootingInput(const NativePlayerInputContext&,NativeActionSource&,NativePoint*);
#endif
