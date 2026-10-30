#ifndef GENERIC_NATIVE_PLAYER_UPDATE_H
#define GENERIC_NATIVE_PLAYER_UPDATE_H
#include "native_player_input.h"
struct PortNormalRuntime;
//The live736d70 call. The program counter belongs to the suspended native
//caller; adapters supply device actions, never evaluated player/effect state.
class NativePlayerUpdate {
public:
    unsigned int address,missingOffset;
    bool active,returned,firstTick;
    NativePlayerUpdate():address(0x736d70),missingOffset(0),active(false),returned(false),firstTick(false){}
    void Begin();
    bool Resume(PortNormalRuntime&,NativeActionSource*,unsigned int* next);
};
#endif
