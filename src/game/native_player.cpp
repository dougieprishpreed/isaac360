#include "native_player.h"
#include "player_setup_data.h"
#include <math.h>
bool NativeProfile::Award(int id) const {
    if(id==-2)return false;
    return id<=0 || (id<kNativeAwardCount && awards[id]);
}
const NativePlayerDef* NativePlayer(int id) {
    for(unsigned int i=0;i<sizeof(kNativePlayers)/sizeof(kNativePlayers[0]);++i)if(kNativePlayers[i].id==id)return &kNativePlayers[i];return 0;
}
const NativeCharacterDef* NativeCharacter(int page,int choice) {return page>=0&&page<2&&choice>=0&&choice<18?&kNativeCharacters[page*18+choice]:0;}
bool NativeCharacterUnlocked(const NativeProfile& p,int page,int choice) {
    const NativeCharacterDef* c=NativeCharacter(page,choice);if(!c)return false;if(!choice)return true;
    const NativePlayerDef* d=NativePlayer(c->player);
    //0088fc40 permits negative metadata achievements; helper forms are not
    //menu entries. Eden and its alternate additionally require a token.
    return d && (d->achievement<0 || p.Award(d->achievement)) && ((d->id!=9&&d->id!=30)||p.edenTokens!=0);
}
bool NativeCharacterPageAvailable(const NativeProfile& p) {for(int i=1;i<18;++i)if(NativeCharacterUnlocked(p,1,i))return true;return false;}
int NativeCharacterMove(const NativeProfile& p,int page,int choice,int direction) {
    if(!NativeCharacter(page,choice)||(direction!=-1&&direction!=1))return choice;
    for(int i=0;i<18;++i) {choice=(choice+18+direction)%18;const NativeCharacterDef* c=NativeCharacter(page,choice);if(!c->hideLocked||NativeCharacterUnlocked(p,page,choice))return choice;}
    return 0;
}
int NativeDifficultyMove(const NativeProfile& p,int mode,int direction) {
    if(mode<0||mode>3||(direction!=-1&&direction!=1))return mode;
    do {mode=(mode+4+direction)%4;}while(mode==3&&!p.Award(341));return mode;
}
bool NativeRandomCharacter(const NativeProfile& p,int page,NativeMt* global,int* choice) {
    if(!global||!choice||!global->Valid()||page<0||page>1)return false;
    int candidates[17],count=0;
    for(int i=1;i<18;++i)if(NativeCharacterUnlocked(p,page,i))candidates[count++]=i;
    if(!count)return false;
    unsigned int value=global->Next();*choice=candidates[value%(unsigned int)count];return true;
}
NativePlayerStartResult NativePlayerStart(const NativeProfile& p,int page,int choice,int mode,NativeRunStart* out) {
    if(!out||choice<1||!NativeCharacter(page,choice)||mode<0||mode>3)return NPS_BAD_REQUEST;
    if(!NativeCharacterUnlocked(p,page,choice)||(mode==3&&!p.Award(341)))return NPS_LOCKED;
    const NativeCharacterDef* c=NativeCharacter(page,choice);
    if(c->player!=0)return NPS_UNSUPPORTED_PLAYER;
    if(mode>1)return NPS_UNSUPPORTED_MODE;
    NativeRunStart result={c->player,mode,p.Award(29)};*out=result;return NPS_OK;
}
bool NativeRunStartValid(const NativeRunStart& s) {
    return s.player==0&&s.mode>=0&&s.mode<=1;
}
int NativeCharacterWheel(const NativeProfile& p,int page,int choice,NativeCharacterPortrait* out) {
    if(!out||!NativeCharacter(page,choice))return 0;
    int choices[18],count=0,selected=-1;
    for(int i=0;i<18;++i)if(!NativeCharacter(page,i)->hideLocked||NativeCharacterUnlocked(p,page,i)) {if(i==choice)selected=count;choices[count++]=i;}
    if(selected<0)return 0;
    float extra=(float)(count>10?count-10:0),perspective=extra*extra*0.01f+2.0f;
    float half=(perspective-1)*0.5f,denom=half+1,ratio=half/denom;
    float aspect=((denom-ratio*half)*1.3f)/sqrtf(1-ratio*ratio);
    for(int i=0;i<count;++i) {
        float angle=(selected-i)*6.2831855f/count,c=cosf(angle),depth=(perspective-1)*(1-c)*0.5f+1;
        NativeCharacterPortrait v={choices[i],240+sinf(angle)*70/depth*aspect,85+c*28/depth,1/depth};out[i]=v;
    }
    for(int i=1;i<count;++i)for(int j=i;j>0&&out[j].y<out[j-1].y;--j) {NativeCharacterPortrait v=out[j];out[j]=out[j-1];out[j-1]=v;}
    return count;
}
