#include "native_pause_ui.h"
#include "native_pause_ui_data.h"
#include <string.h>
const NativeSpriteAsset* NativePreparedPauseSprite(unsigned int i){return i<4?kPauseAssets[i]:0;}
const NativeSpriteSheet* NativePreparedPauseDark(){return &kPauseDark;}
NativePauseUi::NativePauseUi():resourcesReady(false),ready(false),state(0),native8(0),native24(0),native28(0),optionsSelection(0),promptNative80218(0),promptNative8021c(0){
    optionsPosition.x=optionsPosition.y=optionsPrevious.x=optionsPrevious.y=optionsVelocity.x=optionsVelocity.y=0;
    for(int i=0;i<4;++i)fonts[i]=0;
}
bool NativePauseUi::ResetForNewRun(unsigned int* next){
    if(next)*next=0x901240;if(!next)return false;
    //Resource creation is shared and deterministic, never an extra RNG or
    //UI animation tick.8fe810 receives(0,0),true from900cc0 at boot.
    if(!resourcesReady){
        if(pause.Load(kPauseAssets[0]->path)!=NSPR_OK||options.Load(kPauseAssets[1]->path)!=NSPR_OK){*next=0x900cc0;return false;}
        resourcesReady=true;
    }
    state=0;native8=1;native24=native28=0;
    //4035a0(NULL) selects no main clip, clearing frame/cursor/event state;
    //it preserves the Sprite's asset and independent overlay owner.
    if(pause.mainState.Select(0,true)!=NSPR_OK)return false;
    optionEntries.clear(); //9002b0's in-game(true) branch.
    const int entries[]={1,2,3,4,6,7,8,9,11,12,13,14,16,17};
    for(unsigned int i=0;i<sizeof(entries)/sizeof(entries[0]);++i)optionEntries.push_back(entries[i]);
    for(unsigned int i=0;i<options.layerSheets.size();++i){
        if(i>=6&&i<=10)options.layerVisible[i]=false;
        //Prepared bindings use the resource index's canonical lower case.
        if(options.layerSheets[i].path.find("optionsmenu.png")!=std::string::npos)
            if(options.ReplaceLayerSheet(i,kPauseDark.path)!=NSPR_OK){*next=0x9006d4;return false;}
    }
    if(options.RefreshSheets()!=NSPR_OK){*next=0x900781;return false;}
    if(gamma.Load(kPauseAssets[2]->path)!=NSPR_OK||gamma.Play("Counter")!=NSPR_OK){*next=0x9007f5;return false;}
    optionsSelection=0;optionsPrevious=optionsPosition;optionsVelocity.x=optionsVelocity.y=0;
    //7deeb0(true) selects the original small prompt and all four requested
    //fonts.9527a0 returns false for absent optional fonts; its caller does
    //not substitute a font or stop startup. The empty registry is no mods.
    if(prompt.Load(kPauseAssets[3]->path)!=NSPR_OK){*next=0x7def75;return false;}
    for(int i=0;i<4;++i)fonts[i]=kPauseFonts[i].path?kPauseFonts+i:0;
    promptNative80218=promptNative8021c=0;
    ready=true;*next=0;return true;
}
