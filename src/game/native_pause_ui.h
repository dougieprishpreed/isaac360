#ifndef GENERIC_NATIVE_PAUSE_UI_H
#define GENERIC_NATIVE_PAUSE_UI_H
#include "native_sprite.h"
struct NativeFontGlyph {unsigned int id;unsigned short x,y,width,height;short xOffset,yOffset,xAdvance;unsigned char page,channel;};
struct NativeFontAsset {const char* request;const char* path;unsigned int lineHeight,base,width,height;const NativeSpriteSheet* pages;unsigned int pageCount;const NativeFontGlyph* glyphs;unsigned int glyphCount;};
//CPU owners of901240, including its prior900cc0/8fe810 resource bindings.
//Textures and fonts retain their prepared original identities for the adapter.
class NativePauseUi {
public:
    bool resourcesReady,ready;
    unsigned int state,native8,native24,native28,optionsSelection;
    NativePoint optionsPosition,optionsPrevious,optionsVelocity;
    NativeSprite pause,options,gamma,prompt;
    std::vector<int> optionEntries;
    const NativeFontAsset* fonts[4];
    unsigned int promptNative80218,promptNative8021c;
    NativePauseUi();
    bool ResetForNewRun(unsigned int* next);
};
#endif
