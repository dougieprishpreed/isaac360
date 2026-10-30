#include "frontend.h"
bool PortFrontend::AnyEnabledMod() const {
    for(unsigned int i=0;i<loadedModEnabled.size();++i)if(loadedModEnabled[i])return true;
    return false;
}
#include <string.h>
#include <stdio.h>
bool PortFrontend::BindSaveDirectory(const char* directory){
    if(!directory||!*directory)return false;saveDirectory=directory;
    const char last=saveDirectory[saveDirectory.size()-1];
    if(last!='/'&&last!='\\')saveDirectory+='/';return true;
}
bool PortFrontend::LocalRunFilenames(std::string* primary,std::string* rerun) const{
    if(!primary||!rerun||saveDirectory.empty()||selectedSlot<0||selectedSlot>2)return false;
    //8a2860/8a2a01's local-slot path. The platform supplies the directory
    //(nativebc9f18); save-slot selection supplies the original1-based index.
    char name[48];sprintf(name,"gamestate%d.dat",selectedSlot+1);*primary=saveDirectory+name;
    sprintf(name,"rep_rerunstate%d.dat",selectedSlot+1);*rerun=saveDirectory+name;return true;
}
void PortFrontend::Init(bool resume) {
    //Container owners are constructed by C++; do not byte-clear them with
    //the menu's scalar state, including when the frontend is initialized again.
    loadedModEnabled.clear();saveDirectory.clear();music.Begin();scene=UI_TITLE;
    selection=tick=controller=0;debugConsoleEnabled=false;hasContinue=resume;
    lastUp=lastDown=lastAccept=lastBack=lastPause=false;
    selectedSlot=slotMode=slotChoice=slotTick=slotAnimTick=0;
    slotAnimation=slotPrevious=-1;slotExitMode=slotHoldTick=0;
    slotPending=lastLeft=lastRight=false;mainHoldTick=0;
    memset(&profile,0,sizeof(profile));
    profileStorage.ResetFresh();
    characterChoice=characterPage=characterMode=characterRepeat=0;
    lastPage=lastSeed=characterPending=false;
}
void PortFrontend::OpenMain() { scene=UI_MAIN; selection=hasContinue?MAIN_CONTINUE:MAIN_NEW; mainHoldTick=0; }
void PortFrontend::OpenCharacters() {scene=UI_CHARACTERS;characterChoice=1;characterPage=0;characterMode=profile.difficulty;characterRepeat=0;characterPending=false;}
void PortFrontend::CharacterStarted() {characterPending=false;}
void PortFrontend::SlotLoaded(bool success,bool resume) {
    if(!slotPending) return;
    slotPending=false;
    if(success) { hasContinue=resume; OpenMain(); }
}
void PortFrontend::SlotDeleted(bool success) {
    if(!slotPending) return;
    slotPending=false;
    if(success) { slotMode=3; slotExitMode=0; slotTick=0; slotChoice=1; hasContinue=false; }
}
PortAction PortFrontend::Step(const PortMenuInput& in) {
    bool up=in.up&&!lastUp,down=in.down&&!lastDown,accept=in.accept&&!lastAccept,back=in.back&&!lastBack,pause=in.pause&&!lastPause;
    lastUp=in.up; lastDown=in.down; lastAccept=in.accept; lastBack=in.back; lastPause=in.pause; ++tick;
    bool left=in.left&&!lastLeft,right=in.right&&!lastRight;
    lastLeft=in.left; lastRight=in.right;
    bool page=in.page&&!lastPage,seed=in.seed&&!lastSeed;lastPage=in.page;lastSeed=in.seed;
    if(scene==UI_CHARACTERS) {
        if(characterPending)return UI_NONE;
        if(back){OpenMain();return UI_NONE;}
        if(page&&NativeCharacterPageAvailable(profile)) {
            characterPage=1-characterPage;characterRepeat=0;
            if(NativeCharacter(characterPage,characterChoice)->hideLocked&&!NativeCharacterUnlocked(profile,characterPage,characterChoice))characterChoice=NativeCharacterMove(profile,characterPage,characterChoice,1);
        }
        if(up||down) {characterMode=NativeDifficultyMove(profile,characterMode,up?-1:1);profile.difficulty=characterMode;}
        bool horizontal=left||right;
        if(!horizontal&&(in.left||in.right)) {if(characterRepeat--<=0)horizontal=true;}
        if(horizontal) {characterChoice=NativeCharacterMove(profile,characterPage,characterChoice,in.left?-1:1);characterRepeat=10;}
        if(!(in.left||in.right))characterRepeat=0;
        if(seed)return UI_SEED_UNIMPLEMENTED;
        if(accept&&NativeCharacterUnlocked(profile,characterPage,characterChoice)) {if(!music.StartCharacter(characterPage))return UI_NONE;characterPending=true;return UI_START_CHARACTER;}
        return UI_NONE;
    }
    if(scene==UI_TITLE) {
        if(accept || pause) { scene=UI_SLOTS; selection=selectedSlot; slotMode=0; slotTick=slotAnimTick=0; slotAnimation=selection; slotPrevious=-1; }
        return UI_NONE;
    }
    if(scene==UI_SLOTS) {
        ++slotTick; ++slotAnimTick;
        if(slotPending) return UI_NONE;
        if(in.left || in.right) {
            if(left || right) slotHoldTick=0; else ++slotHoldTick;
            if(slotHoldTick>49 && slotHoldTick%10==0) { left=in.left; right=in.right; }
        } else slotHoldTick=0;
        if(slotMode==3) { if(slotTick>=10) { slotMode=slotExitMode; slotTick=0; } return UI_NONE; }
        if(slotMode==2) {
            // Native 00919f50: ten-frame entrance; cancel is the default (1).
            if(left || right) slotChoice=1-slotChoice;
            // Back may dismiss the entrance too. Accept commits only once the
            // idle animation is reached. 0091c970 dismisses to normal files.
            if(back || (slotTick>=10 && accept && slotChoice==1)) { slotMode=3; slotExitMode=0; slotTick=0; }
            else if(slotTick>=10 && accept) { slotPending=true; return UI_DELETE_SLOT; }
            return UI_NONE;
        }
        if(back) {
            if(slotMode) { slotMode=0; slotTick=0; }
            else scene=UI_TITLE;
            return UI_NONE;
        }
        int previous=selection;
        if(left || right) {
            if(selection<3) selection=(selection+(left?2:1))%3;
            else selection=selection==3?4:3;
        } else if(down && !slotMode) { if(selection<3) { selectedSlot=selection; selection=selection==2?4:3; } }
        else if(up && !slotMode) { if(selection>=3) selection=selection==3?1:2; }
        if(previous!=selection) { slotPrevious=previous; slotAnimation=selection; slotAnimTick=0; }
        if(selection<3) selectedSlot=selection;
        if(accept) {
            if(selection<3) {
                if(slotMode==1) { slotMode=2; slotTick=0; slotChoice=1; }
                else { slotPending=true; return UI_SELECT_SLOT; }
            } else if(selection==3) { slotMode=1; selection=selectedSlot=0; slotTick=slotAnimTick=0; slotAnimation=0; slotPrevious=3; }
            else return UI_IMPORT_SLOT;
        }
        return UI_NONE;
    }
    if(scene==UI_MAIN) {
        // Saved 008c6800/008c7900: New, Continue, Challenges, Mods, Stats,
        // Options. Skip Continue when no live/replay record exists.
        if(!hasContinue && selection==MAIN_CONTINUE) { selection=MAIN_NEW; if(accept) return UI_NONE; }
        if(in.up || in.down) {
            if(up || down) mainHoldTick=0; else ++mainHoldTick;
            if(mainHoldTick>49 && mainHoldTick%10==0) { up=in.up; down=in.down; }
        } else mainHoldTick=0;
        int direction=up?-1:down?1:0;
        if(direction) do { selection=(selection+MAIN_COUNT+direction)%MAIN_COUNT; } while(selection==MAIN_CONTINUE && !hasContinue);
        if(back) { scene=UI_SLOTS; selection=selectedSlot; slotMode=0; slotTick=slotAnimTick=0; slotAnimation=selection; slotPrevious=-1; return UI_NONE; }
        if(!accept) return UI_NONE;
        if(selection==MAIN_CONTINUE && hasContinue) { return UI_CONTINUE_RUN; }
        if(selection==MAIN_NEW) {
            OpenCharacters();return UI_NONE;
        }
        if(selection==MAIN_CHALLENGES) return UI_OPEN_CHALLENGES;
        if(selection==MAIN_MODS) return UI_EXCLUDED_MODS;
        if(selection==MAIN_STATS) return UI_OPEN_STATS;
        if(selection==MAIN_OPTIONS) return UI_OPEN_OPTIONS;
    }
    return UI_NONE;
}
