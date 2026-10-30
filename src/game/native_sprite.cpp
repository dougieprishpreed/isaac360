#include "native_sprite.h"
#include "../content/game_content.h"
#include "native_player_sprite_data.h"
struct NativePlayerSpriteInit {int slot;const char* request;const NativeSpriteAsset* asset;const char* play;int blendLayer;};
#include "native_player_aux_sprite_data.h"
#include "native_player_appearance_data.h"
#include <math.h>
#include <string.h>
const NativeSpriteAsset* NativePreparedRoomVeins();
const NativeSpriteAsset* NativePreparedRoomEffectSprite(unsigned int);
const NativeSpriteAsset* NativePreparedBackgroundSprite(unsigned int);
const NativeSpriteAsset* NativePreparedDoorSprite(unsigned int);
const NativeSpriteSheet* NativePreparedDoorSheet(unsigned int);
const NativeSpriteAsset* NativePreparedMinimapSprite(unsigned int);
const NativeSpriteAsset* NativePreparedPauseSprite(unsigned int);
const NativeSpriteSheet* NativePreparedPauseDark();
namespace {
bool Finite(float n){unsigned int b;memcpy(&b,&n,4);return (b&0x7fffffffU)<0x7f800000U;}
void Fold(unsigned int& h,unsigned int n){for(int i=0;i<4;++i)h=(h^((n>>(8*i))&255))*16777619U;}
void Scalar(unsigned int& h,float n){unsigned int b;memcpy(&b,&n,4);Fold(h,b);}
void String(unsigned int& h,const char* s){do{h=(h^(unsigned char)*s)*16777619U;}while(*s++);}
void TrackHash(unsigned int& h,const NativeSpriteTrack& t){
    Fold(h,t.id);Fold(h,t.visible?1:0);Fold(h,t.count);
    for(unsigned int i=0;i<t.count;++i){const NativeSpriteFrame& f=t.frames[i];
        for(int j=0;j<10;++j)Fold(h,f.geometry[j]);Fold(h,f.delay);
        for(int j=0;j<7;++j)Fold(h,f.color[j]);Fold(h,f.rotation);Fold(h,f.end);Fold(h,f.visible?1:0);Fold(h,f.interpolated?1:0);}
}
const NativeSpriteClip* Clip(const NativeSpriteAsset* a,const char* name){
    if(a&&name)for(unsigned int i=0;i<a->clipCount;++i)if(!strcmp(a->clips[i].name,name))return a->clips+i;return 0;
}
int ClipIndex(const NativeSpriteAsset* a,const NativeSpriteClip* c){
    if(a&&c)for(unsigned int i=0;i<a->clipCount;++i)if(a->clips+i==c)return (int)i;return -1;
}
void Cursors(const NativeSpriteTrack* tracks,unsigned int count,std::vector<int>& indices,float frame){
    for(unsigned int i=0;i<count;++i){int& cursor=indices[i];const NativeSpriteTrack& track=tracks[i];
        while(cursor>=0&&(unsigned int)(cursor+1)<track.count&&frame>=(float)track.frames[cursor].end)++cursor;}
}
const NativeSpriteFrame* Frame(const NativeSpriteTrack* tracks,unsigned int count,const std::vector<int>& cursors,int id){
    for(unsigned int i=0;i<count;++i)if(tracks[i].id==id){int n=cursors[i];return n>=0&&(unsigned int)n<tracks[i].count?tracks[i].frames+n:0;}return 0;
}
}
const NativeSpriteAsset* NativePreparedSprite(const char* path){
    if(!path)return 0;unsigned int a,b;GpcHashName(path,&a,&b);
    unsigned int x,y;GpcHashName(kNativePlayerSprite.path,&x,&y);
    if(a==x&&b==y)return &kNativePlayerSprite;
    const NativeSpriteAsset* veins=NativePreparedRoomVeins();GpcHashName(veins->path,&x,&y);
    if(a==x&&b==y)return veins;
    GpcHashName("gfx/Veins.anm2",&x,&y);if(a==x&&b==y)return veins;
    for(unsigned int i=0;;++i){const NativeSpriteAsset* effect=NativePreparedRoomEffectSprite(i);if(!effect)break;
        GpcHashName(effect->path,&x,&y);if(a==x&&b==y)return effect;}
    for(unsigned int i=0;;++i){const NativeSpriteAsset* s=NativePreparedBackgroundSprite(i);if(!s)break;
        GpcHashName(s->path,&x,&y);if(a==x&&b==y)return s;}
    for(unsigned int i=0;;++i){const NativeSpriteAsset* s=NativePreparedDoorSprite(i);if(!s)break;
        GpcHashName(s->path,&x,&y);if(a==x&&b==y)return s;}
    for(unsigned int i=0;;++i){const NativeSpriteAsset* s=NativePreparedMinimapSprite(i);if(!s)break;
        GpcHashName(s->path,&x,&y);if(a==x&&b==y)return s;}
    for(unsigned int i=0;;++i){const NativeSpriteAsset* s=NativePreparedPauseSprite(i);if(!s)break;
        GpcHashName(s->path,&x,&y);if(a==x&&b==y)return s;}
    for(unsigned int i=0;i<sizeof(kNativePlayerSpriteInit)/sizeof(kNativePlayerSpriteInit[0]);++i){const NativePlayerSpriteInit& entry=kNativePlayerSpriteInit[i];
        GpcHashName(entry.request,&x,&y);if(a==x&&b==y)return entry.asset;
        GpcHashName(entry.asset->path,&x,&y);if(a==x&&b==y)return entry.asset;
    }
    return 0;
}
const NativeSpriteSheet* NativePreparedSpriteSheet(const char* path){
    if(!path)return 0;unsigned int a,b,x,y;GpcHashName(path,&a,&b);
    const NativeSpriteSheet* dark=NativePreparedPauseDark();GpcHashName(dark->path,&x,&y);if(a==x&&b==y)return dark;
    const NativeSpriteAsset* asset=&kNativePlayerSprite;
    for(unsigned int group=0;group<=sizeof(kNativePlayerSpriteInit)/sizeof(kNativePlayerSpriteInit[0]);++group){
        if(group)asset=kNativePlayerSpriteInit[group-1].asset;
        for(unsigned int i=0;i<asset->sheetCount;++i){GpcHashName(asset->sheets[i].path,&x,&y);if(a==x&&b==y)return asset->sheets+i;}
    }
    for(unsigned int i=0;i<sizeof(kNativeAppearanceSheets)/sizeof(kNativeAppearanceSheets[0]);++i){
        GpcHashName(kNativeAppearanceSheets[i].path,&x,&y);if(a==x&&b==y)return kNativeAppearanceSheets+i;}
    asset=NativePreparedRoomVeins();for(unsigned int i=0;i<asset->sheetCount;++i){
        GpcHashName(asset->sheets[i].path,&x,&y);if(a==x&&b==y)return asset->sheets+i;}
    for(unsigned int group=0;;++group){asset=NativePreparedRoomEffectSprite(group);if(!asset)break;
        for(unsigned int i=0;i<asset->sheetCount;++i){GpcHashName(asset->sheets[i].path,&x,&y);if(a==x&&b==y)return asset->sheets+i;}}
    for(unsigned int group=0;;++group){asset=NativePreparedDoorSprite(group);if(!asset)break;
        for(unsigned int i=0;i<asset->sheetCount;++i){GpcHashName(asset->sheets[i].path,&x,&y);if(a==x&&b==y)return asset->sheets+i;}}
    for(unsigned int group=0;;++group){const NativeSpriteSheet* s=NativePreparedDoorSheet(group);if(!s)break;
        GpcHashName(s->path,&x,&y);if(a==x&&b==y)return s;}
    for(unsigned int group=0;;++group){asset=NativePreparedMinimapSprite(group);if(!asset)break;
        for(unsigned int i=0;i<asset->sheetCount;++i){GpcHashName(asset->sheets[i].path,&x,&y);if(a==x&&b==y)return asset->sheets+i;}}
    return 0;
}
unsigned int NativeSpriteAssetHash(const NativeSpriteAsset& a){
    unsigned int h=2166136261U;String(h,a.path);String(h,a.defaultAnimation);Fold(h,a.sheetCount);
    for(unsigned int i=0;i<a.sheetCount;++i){Fold(h,a.sheets[i].id);String(h,a.sheets[i].path);Fold(h,a.sheets[i].width);Fold(h,a.sheets[i].height);}
    Fold(h,a.layerCount);for(unsigned int i=0;i<a.layerCount;++i){Fold(h,a.layers[i].id);Fold(h,a.layers[i].sheet);String(h,a.layers[i].name);}
    Fold(h,a.nullCount);for(unsigned int i=0;i<a.nullCount;++i){Fold(h,a.nulls[i].id);String(h,a.nulls[i].name);}
    Fold(h,a.eventCount);for(unsigned int i=0;i<a.eventCount;++i){Fold(h,a.events[i].id);String(h,a.events[i].name);}
    Fold(h,a.clipCount);for(unsigned int i=0;i<a.clipCount;++i){const NativeSpriteClip& c=a.clips[i];String(h,c.name);Fold(h,c.frames);Fold(h,c.loop?1:0);TrackHash(h,c.root);
        Fold(h,c.layerCount);for(unsigned int j=0;j<c.layerCount;++j)TrackHash(h,c.layers[j]);Fold(h,c.nullCount);for(unsigned int j=0;j<c.nullCount;++j)TrackHash(h,c.nulls[j]);
        Fold(h,c.eventCount);for(unsigned int j=0;j<c.eventCount;++j){Fold(h,c.events[j].id);Fold(h,c.events[j].frame);}}
    return h;
}
NativeAnimationState::NativeAnimationState():clip(0),frame(-1),playing(false),events(0),history(0){}
NativeSpriteResult NativeAnimationState::Select(const NativeSpriteClip* c,bool reset){
    if(!reset){
        //00403680's changed-dimension copy path remains unresolved. Equal
        //dimensions retain state for the body and auxiliary prepared assets.
        if((c?c->layerCount:0)!=layerFrames.size()||(c?c->nullCount:0)!=nullFrames.size())return NSPR_UNSUPPORTED_STATE;
        clip=c;return NSPR_OK;
    }
    std::vector<int> layers(c?c->layerCount:0,-1),nulls(c?c->nullCount:0,-1);
    clip=c;frame=-1;playing=false;events=history=0;layerFrames.swap(layers);nullFrames.swap(nulls);return NSPR_OK;
}
void NativeAnimationState::Emit(){
    for(unsigned int i=0;i<clip->eventCount;++i)if(frame==(float)clip->events[i].frame){unsigned int bit=1U<<(clip->events[i].id&31);events|=bit;history|=bit;}
}
void NativeAnimationState::Restart(){
    frame=0;events=history=0;Emit();
    for(unsigned int i=0;i<layerFrames.size();++i)layerFrames[i]=0;
    for(unsigned int i=0;i<nullFrames.size();++i)nullFrames[i]=0;
}
void NativeAnimationState::UpdateCursors(float target){Cursors(clip->layers,clip->layerCount,layerFrames,target);Cursors(clip->nulls,clip->nullCount,nullFrames,target);}
NativeSpriteResult NativeAnimationState::Seek(float target){
    if(!clip||!Finite(target))return NSPR_BAD_REQUEST;
    frame=target<(float)clip->frames?target:(float)clip->frames;events=0;Emit();
    for(unsigned int i=0;i<layerFrames.size();++i)layerFrames[i]=0;
    for(unsigned int i=0;i<nullFrames.size();++i)nullFrames[i]=0;
    //00403a80 uses the requested value for cursors even when frame is clamped.
    UpdateCursors(target);return NSPR_OK;
}
void NativeAnimationState::AdvanceTo(int target){
    events=0;while((float)target>frame){frame=(float)floor((double)frame)+1;Emit();}UpdateCursors((float)target);
}
NativeSpriteResult NativeAnimationState::CanUpdate(float speed) const{
    if(!clip||!playing)return NSPR_OK;
    volatile float next=frame+speed;
    if(!Finite(speed)||!Finite(next))return NSPR_BAD_REQUEST;
    if(speed<0||next>=2147483520.0f)return NSPR_UNSUPPORTED_STATE;
    int target=(int)floor((double)next);
    if(target>=(int)clip->frames&&!clip->loop)target=(int)clip->frames-1;
    if(!(clip->loop&&next>=(float)clip->frames)&&target<(int)floor((double)frame))return NSPR_UNSUPPORTED_STATE;
    return NSPR_OK;
}
void NativeAnimationState::Update(float speed){
    if(!clip||!playing)return;volatile float next=frame+speed;int target=(int)floor((double)next);
    //The nonloop call only advances forward. A preceding last-frame fraction
    //survives this finish; do not force frame to the integer last frame.
    if(target>=(int)clip->frames){if(clip->loop)Restart();else{AdvanceTo((int)clip->frames-1);playing=false;}}
    else{AdvanceTo(target);frame=next;}
}
unsigned int NativeAnimationState::CursorHash() const{
    unsigned int h=2166136261U;Fold(h,(unsigned int)layerFrames.size());for(unsigned int i=0;i<layerFrames.size();++i)Fold(h,layerFrames[i]);
    Fold(h,(unsigned int)nullFrames.size());for(unsigned int i=0;i<nullFrames.size();++i)Fold(h,nullFrames[i]);return h;
}
void NativeAnimationState::Swap(NativeAnimationState& o){
    const NativeSpriteClip* c=clip;clip=o.clip;o.clip=c;float f=frame;frame=o.frame;o.frame=f;bool p=playing;playing=o.playing;o.playing=p;
    unsigned int e=events,h=history;events=o.events;history=o.history;o.events=e;o.history=h;layerFrames.swap(o.layerFrames);nullFrames.swap(o.nullFrames);
}
NativeSprite::NativeSprite():asset(0){Clear();}
void NativeSprite::Clear(){
    asset=0;mainState.Select(0,true);overlayState.Select(0,true);offset.x=offset.y=rotation=0;scale.x=scale.y=speed=1;
    for(int i=0;i<11;++i){color[i]=i<4?1.0f:0;secondaryColor[i]=i<3?1.0f:0;}flipX=flipY=updateEnabled=false;
    viewportScale=false;layerBlendPresets.clear();layerSheets.clear();layerVisible.clear();
}
NativeSpriteResult NativeSprite::Load(const char* path){
    const NativeSpriteAsset* next=NativePreparedSprite(path);if(!next)return NSPR_ASSET_MISSING;
    std::vector<int> blends(next->layerCount,-1);
    std::vector<NativeSpriteLayerSheet> sheets(next->layerCount);
    for(unsigned int i=0;i<next->layerCount;++i){const NativeSpriteSheet* sheet=0;
        for(unsigned int j=0;j<next->sheetCount;++j)if(next->sheets[j].id==next->layers[i].sheet){sheet=next->sheets+j;break;}
        if(!sheet)return NSPR_ASSET_MISSING;
        sheets[i].path=sheet->path;sheets[i].width=sheet->width;sheets[i].height=sheet->height;sheets[i].ready=true;
    }
    mainState.Select(0,true);overlayState.Select(0,true);asset=next;layerBlendPresets.swap(blends);layerSheets.swap(sheets);layerVisible.assign(next->layerCount,true);updateEnabled=true;return NSPR_OK;
}
int NativeSprite::LayerOrdinal(const char* name) const{
    if(asset&&name)for(unsigned int i=0;i<asset->layerCount;++i)if(!strcmp(asset->layers[i].name,name))return (int)i;return -1;
}
const NativeSpriteSheet* NativeSprite::OriginalLayerSheet(int ordinal) const{
    if(!asset||ordinal<0||(unsigned int)ordinal>=asset->layerCount)return 0;
    for(unsigned int i=0;i<asset->sheetCount;++i)if(asset->sheets[i].id==asset->layers[ordinal].sheet)return asset->sheets+i;return 0;
}
NativeSpriteResult NativeSprite::ReplaceLayerSheet(int ordinal,const char* path){
    if(!asset||!path||!*path||ordinal<0||(unsigned int)ordinal>=layerSheets.size())return NSPR_BAD_REQUEST;
    NativeSpriteLayerSheet& s=layerSheets[ordinal];s.path=path;s.ready=false;return NSPR_OK;
}
NativeSpriteResult NativeSprite::RefreshSheets(){
    if(!asset||layerSheets.size()!=asset->layerCount)return NSPR_BAD_REQUEST;
    //Validate the entire resolution before committing; a missing source must
    //not silently reuse a previous layer's texture. Native+2c is the ready bit.
    for(unsigned int i=0;i<layerSheets.size();++i)if(!layerSheets[i].ready&&!NativePreparedSpriteSheet(layerSheets[i].path.c_str()))return NSPR_ASSET_MISSING;
    for(unsigned int i=0;i<layerSheets.size();++i)if(!layerSheets[i].ready){const NativeSpriteSheet* s=NativePreparedSpriteSheet(layerSheets[i].path.c_str());
        layerSheets[i].width=s->width;layerSheets[i].height=s->height;layerSheets[i].ready=true;}
    return NSPR_OK;
}
NativeSpriteResult NativeSprite::SetLayerBlendPreset(int layer,int preset){
    if(!asset||layer<0||(unsigned int)layer>=layerBlendPresets.size()||preset<0||preset>2)return NSPR_BAD_REQUEST;
    layerBlendPresets[layer]=preset;return NSPR_OK;
}
NativeSpriteResult NativeSprite::Select(const char* name,bool reset,bool overlay){
    const NativeSpriteClip* c=Clip(asset,name);if(!c)return NSPR_ANIMATION_MISSING;
    return (overlay?overlayState:mainState).Select(c,reset);
}
NativeSpriteResult NativeSprite::Play(const char* name,bool force,bool overlay){
    NativeAnimationState& s=overlay?overlayState:mainState;
    if(!name)return NSPR_BAD_REQUEST;
    if(!force&&s.clip&&!strcmp(s.clip->name,name)&&(!s.clip->loop||s.playing))return NSPR_OK;
    NativeSpriteResult r=Select(name,true,overlay);if(r!=NSPR_OK)return r;s.Restart();s.playing=true;return NSPR_OK;
}
NativeSpriteResult NativeSprite::SetFrame(const char* name,int frame,bool overlay){
    NativeSpriteResult r=Select(name,true,overlay);if(r!=NSPR_OK)return r;
    NativeAnimationState& s=overlay?overlayState:mainState;r=s.Seek((float)frame);s.playing=false;return r;
}
NativeSpriteResult NativeSprite::Update(){
    if(!updateEnabled)return NSPR_OK;
    NativeSpriteResult r=mainState.CanUpdate(speed);if(r!=NSPR_OK)return r;r=overlayState.CanUpdate(speed);if(r!=NSPR_OK)return r;
    mainState.Update(speed);overlayState.Update(speed);return NSPR_OK;
}
bool NativeSprite::IsPlaying(const char* name,bool overlay) const{
    const NativeAnimationState& s=overlay?overlayState:mainState;return name&&s.clip&&s.playing&&(!*name||!strcmp(s.clip->name,name));
}
bool NativeSprite::IsFinished(const char* name,bool overlay) const{
    const NativeAnimationState& s=overlay?overlayState:mainState;
    return name&&!s.playing&&((!s.clip&&!*name)||(s.clip&&(!*name||!strcmp(s.clip->name,name))));
}
const NativeSpriteFrame* NativeSprite::LayerFrame(int id,bool overlay) const{
    const NativeAnimationState& s=overlay?overlayState:mainState;return s.clip?Frame(s.clip->layers,s.clip->layerCount,s.layerFrames,id):0;
}
const NativeSpriteFrame* NativeSprite::NullFrame(int id,bool overlay) const{
    const NativeAnimationState& s=overlay?overlayState:mainState;return s.clip?Frame(s.clip->nulls,s.clip->nullCount,s.nullFrames,id):0;
}
void NativeSprite::Swap(NativeSprite& o){
    const NativeSpriteAsset* a=asset;asset=o.asset;o.asset=a;mainState.Swap(o.mainState);overlayState.Swap(o.overlayState);
    NativePoint p=offset;offset=o.offset;o.offset=p;p=scale;scale=o.scale;o.scale=p;
    float f=rotation;rotation=o.rotation;o.rotation=f;f=speed;speed=o.speed;o.speed=f;
    for(int i=0;i<11;++i){f=color[i];color[i]=o.color[i];o.color[i]=f;f=secondaryColor[i];secondaryColor[i]=o.secondaryColor[i];o.secondaryColor[i]=f;}
    bool b=flipX;flipX=o.flipX;o.flipX=b;b=flipY;flipY=o.flipY;o.flipY=b;b=updateEnabled;updateEnabled=o.updateEnabled;o.updateEnabled=b;
    b=viewportScale;viewportScale=o.viewportScale;o.viewportScale=b;layerBlendPresets.swap(o.layerBlendPresets);layerSheets.swap(o.layerSheets);
    layerVisible.swap(o.layerVisible);
}
unsigned int NativeSprite::StateHash() const{
    unsigned int h=2166136261U;String(h,asset?asset->path:"");const NativeAnimationState* states[]={&mainState,&overlayState};
    for(int i=0;i<2;++i){const NativeAnimationState& s=*states[i];Fold(h,ClipIndex(asset,s.clip));Scalar(h,s.frame);Fold(h,s.playing?1:0);Fold(h,s.events);Fold(h,s.history);Fold(h,s.CursorHash());}
    Scalar(h,offset.x);Scalar(h,offset.y);Scalar(h,scale.x);Scalar(h,scale.y);Scalar(h,rotation);Scalar(h,speed);
    for(int i=0;i<11;++i){Scalar(h,color[i]);Scalar(h,secondaryColor[i]);}Fold(h,flipX?1:0);Fold(h,flipY?1:0);Fold(h,updateEnabled?1:0);
    Fold(h,viewportScale?1:0);Fold(h,(unsigned int)layerBlendPresets.size());for(unsigned int i=0;i<layerBlendPresets.size();++i)Fold(h,layerBlendPresets[i]);
    Fold(h,(unsigned int)layerVisible.size());for(unsigned int i=0;i<layerVisible.size();++i)Fold(h,layerVisible[i]?1:0);
    Fold(h,(unsigned int)layerSheets.size());for(unsigned int i=0;i<layerSheets.size();++i){const NativeSpriteLayerSheet& s=layerSheets[i];String(h,s.path.c_str());Fold(h,s.width);Fold(h,s.height);Fold(h,s.ready);}return h;
}
NativeSpriteResult NativePlayerSprites::BeginFresh(const char* path){
    NativePlayerSprites next;NativeSpriteResult r=next.PrepareAuxiliary();if(r!=NSPR_OK)return r;
    r=next.PrepareBody(path);if(r!=NSPR_OK)return r;Swap(next);return NSPR_OK;
}
NativeSpriteResult NativePlayerSprites::PrepareAuxiliary(){
    if(startupAssetsReady||bodyReady)return NSPR_BAD_REQUEST;
    //Preserve the original Init load order and independent instance ownership.
    //A shared original charge asset never shares mutable cursors or overrides.
    for(unsigned int i=0;i<sizeof(kNativePlayerSpriteInit)/sizeof(kNativePlayerSpriteInit[0]);++i){
        const NativePlayerSpriteInit& entry=kNativePlayerSpriteInit[i];NativeSprite& s=sprites[entry.slot];
        NativeSpriteResult r=s.Load(entry.request);if(r!=NSPR_OK)return r;
        if(entry.play){r=s.Play(entry.play,true);if(r!=NSPR_OK)return r;}
        if(entry.blendLayer>=0){r=s.SetLayerBlendPreset(entry.blendLayer,2);if(r!=NSPR_OK)return r;}
        s.viewportScale=entry.slot!=11;
    }
    startupAssetsReady=true;return NSPR_OK;
}
NativeSpriteResult NativePlayerSprites::PrepareBody(const char* path){
    if(!startupAssetsReady||bodyReady)return NSPR_BAD_REQUEST;
    NativeSprite& body=sprites[0];NativeSpriteResult r=body.Load(path);if(r!=NSPR_OK)return r;
    r=body.SetFrame("WalkDown",0);if(r!=NSPR_OK)return r;r=body.SetFrame("HeadDown",0,true);if(r!=NSPR_OK)return r;
    bodyReady=true;return NSPR_OK;
}
void NativePlayerSprites::Swap(NativePlayerSprites& o){for(int i=0;i<14;++i)sprites[i].Swap(o.sprites[i]);bool r=bodyReady;bodyReady=o.bodyReady;o.bodyReady=r;r=startupAssetsReady;startupAssetsReady=o.startupAssetsReady;o.startupAssetsReady=r;}
unsigned int NativePlayerSprites::StateHash() const{unsigned int h=2166136261U;Fold(h,bodyReady?1:0);Fold(h,startupAssetsReady?1:0);for(int i=0;i<14;++i)Fold(h,sprites[i].StateHash());return h;}
