#ifndef GENERIC_FRONTEND_H
#define GENERIC_FRONTEND_H
#include "native_player.h"
enum PortScene { UI_TITLE,UI_MAIN,UI_SLOTS,UI_CHARACTERS };
enum PortAction { UI_NONE,UI_CONTINUE_RUN,UI_SELECT_SLOT,UI_DELETE_SLOT,UI_IMPORT_SLOT,UI_OPEN_CHALLENGES,UI_EXCLUDED_MODS,UI_OPEN_STATS,UI_OPEN_OPTIONS,UI_START_CHARACTER,UI_SEED_UNIMPLEMENTED };
enum PortMainEntry { MAIN_NEW,MAIN_CONTINUE,MAIN_CHALLENGES,MAIN_MODS,MAIN_STATS,MAIN_OPTIONS,MAIN_COUNT };
struct PortMenuInput { bool up,down,accept,back,pause; bool left,right; bool page,seed; };
struct PortFrontend {
    PortScene scene;
    int selection,tick;
    bool hasContinue,lastUp,lastDown,lastAccept,lastBack,lastPause;
    int selectedSlot,slotMode,slotChoice,slotTick,slotAnimTick;
    int slotPrevious,slotAnimation; // 0..4 files/delete/import, -1 idle.
    int slotExitMode,slotHoldTick;
    bool slotPending,lastLeft,lastRight;
    int mainHoldTick;
    NativeProfile profile;
    int characterChoice,characterPage,characterMode,characterRepeat;
    bool lastPage,lastSeed,characterPending;
    void Init(bool resume);
    PortAction Step(const PortMenuInput& input);
    void SlotLoaded(bool success,bool resume);
    void SlotDeleted(bool success);
    void OpenMain();
    void OpenCharacters();
    void CharacterStarted();
};
#endif
