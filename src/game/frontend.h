#ifndef GENERIC_FRONTEND_H
#define GENERIC_FRONTEND_H
#include "native_player.h"
#include "native_music.h"
#include "native_profile_storage.h"
#include <vector>
#include <string>
enum PortScene { UI_TITLE,UI_MAIN,UI_SLOTS,UI_CHARACTERS };
enum PortAction { UI_NONE,UI_CONTINUE_RUN,UI_SELECT_SLOT,UI_DELETE_SLOT,UI_IMPORT_SLOT,UI_OPEN_CHALLENGES,UI_EXCLUDED_MODS,UI_OPEN_STATS,UI_OPEN_OPTIONS,UI_START_CHARACTER,UI_SEED_UNIMPLEMENTED };
enum PortMainEntry { MAIN_NEW,MAIN_CONTINUE,MAIN_CHALLENGES,MAIN_MODS,MAIN_STATS,MAIN_OPTIONS,MAIN_COUNT };
struct PortMenuInput { bool up,down,accept,back,pause; bool left,right; bool page,seed; };
struct PortFrontend {
    PortScene scene;
    int selection,tick;
    int controller; //89b5fe primary input owner; adapter updates on menu input.
    NativeMusicState music;
    //Main+2a0b8 owns the loaded-mod list. This offline port never loads mods;
    //the constructor-empty owner still supplies861d60's actual query.
    std::vector<unsigned char> loadedModEnabled;
    bool AnyEnabledMod() const;
    std::string saveDirectory;
    bool BindSaveDirectory(const char* directory);
    bool LocalRunFilenames(std::string* primary,std::string* rerun) const;
    bool debugConsoleEnabled; //Original Options+4d default0 (86e42c..448), copied89b57f/6b45de.
    bool hasContinue,lastUp,lastDown,lastAccept,lastBack,lastPause;
    int selectedSlot,slotMode,slotChoice,slotTick,slotAnimTick;
    int slotPrevious,slotAnimation; // 0..4 files/delete/import, -1 idle.
    int slotExitMode,slotHoldTick;
    bool slotPending,lastLeft,lastRight;
    int mainHoldTick;
    NativeProfile profile;
    NativeProfileStorage profileStorage;
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
