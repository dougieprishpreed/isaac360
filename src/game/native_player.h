#ifndef GENERIC_NATIVE_PLAYER_H
#define GENERIC_NATIVE_PLAYER_H
#include "native_rng.h"
static const int kNativeAwardCount=638;
static const int kNativeCharacterCount=18;
struct NativeProfile {
    bool awards[kNativeAwardCount];
    //PersistentGameData+2b8 (Main+2c4).86f5c0..c5 clears exactly496
    //dwords;6c217a..218d captures these profile event counters, not input.
    unsigned int eventCounters[496];
    unsigned int edenTokens;
    int difficulty;
    bool Award(int id) const;
};
struct NativePlayerDef {
    int id,achievement,hp,armor,black,coins,bombs,keys,card,pill,pocketActive;
    bool canShoot;
    int itemCount,items[4];
};
struct NativeCharacterDef {
    const char* animation;
    int player;
    bool hideLocked;
    int itemAwards[3];
};
struct NativeRunStart {
    int player,mode;bool isaacD6;
};
const NativePlayerDef* NativePlayer(int id);
const NativeCharacterDef* NativeCharacter(int page,int choice);
bool NativeCharacterUnlocked(const NativeProfile& profile,int page,int choice);
bool NativeCharacterPageAvailable(const NativeProfile& profile);
int NativeCharacterMove(const NativeProfile& profile,int page,int choice,int direction);
int NativeDifficultyMove(const NativeProfile& profile,int mode,int direction);
//00896fa0: all eligible candidates, one GLOBAL MT draw, no support filtering.
//False leaves the caller's stream and output unchanged.
bool NativeRandomCharacter(const NativeProfile& profile,int page,NativeMt* global,int* choice);
enum NativePlayerStartResult { NPS_OK,NPS_BAD_REQUEST,NPS_LOCKED,NPS_UNSUPPORTED_PLAYER,NPS_UNSUPPORTED_MODE,NPS_WORLD_PENDING };
//Only player0 ordinary/hard gameplay is integrated so far. Eligibility remains
//separate from port implementation; other unlocked characters are not relocked.
NativePlayerStartResult NativePlayerStart(const NativeProfile& profile,int page,int choice,int mode,NativeRunStart* output);
bool NativeRunStartValid(const NativeRunStart& start);
struct NativeCharacterPortrait { int choice; float x,y,scale; };
// Resting geometry from00891cf0/00896340, sorted back to front for00894d70.
int NativeCharacterWheel(const NativeProfile& profile,int page,int choice,NativeCharacterPortrait* portraits);
#endif
