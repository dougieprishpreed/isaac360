#ifndef GENERIC_NATIVE_SPRITE_H
#define GENERIC_NATIVE_SPRITE_H
#include "native_room_space.h"
#include <vector>
#include <string>
struct NativeSpriteFrame {
    unsigned int geometry[10],delay,color[7],rotation,end;
    bool visible,interpolated; //Original words/bits; no baked replacement art.
};
struct NativeSpriteTrack {int id;bool visible;const NativeSpriteFrame* frames;unsigned int count;};
struct NativeSpriteEvent {unsigned int id,frame;};
struct NativeSpriteName {int id;const char* name;};
struct NativeSpriteSheet {int id;const char* path;unsigned int width,height;};
struct NativeSpriteLayer {int id,sheet;const char* name;};
struct NativeSpriteClip {
    const char* name;unsigned int frames;bool loop;
    NativeSpriteTrack root;
    const NativeSpriteTrack* layers;unsigned int layerCount;
    const NativeSpriteTrack* nulls;unsigned int nullCount;
    const NativeSpriteEvent* events;unsigned int eventCount;
};
struct NativeSpriteAsset {
    const char* path;const char* defaultAnimation;
    const NativeSpriteSheet* sheets;unsigned int sheetCount;
    const NativeSpriteLayer* layers;unsigned int layerCount;
    const NativeSpriteName* nulls;unsigned int nullCount;
    const NativeSpriteName* events;unsigned int eventCount;
    const NativeSpriteClip* clips;unsigned int clipCount;
};
const NativeSpriteAsset* NativePreparedSprite(const char* path);
const NativeSpriteSheet* NativePreparedSpriteSheet(const char* path);
unsigned int NativeSpriteAssetHash(const NativeSpriteAsset&); //Canonical data diagnostic.
enum NativeSpriteResult { NSPR_OK,NSPR_BAD_REQUEST,NSPR_ASSET_MISSING,NSPR_ANIMATION_MISSING,NSPR_UNSUPPORTED_STATE };
struct NativeSpriteLayerSheet {
    std::string path;
    unsigned int width,height;
    bool ready;
};
//004035a0,004037f0..00403c56,00405090. Numeric animation ownership;
//render interpolation, texture upload, costume changes and callbacks are open.
struct NativeAnimationState {
    const NativeSpriteClip* clip;
    float frame;bool playing;
    unsigned int events,history;
    std::vector<int> layerFrames,nullFrames;
    NativeAnimationState();
    NativeSpriteResult Select(const NativeSpriteClip*,bool reset);
    NativeSpriteResult Seek(float);
    NativeSpriteResult CanUpdate(float speed) const;
    unsigned int CursorHash() const;
    void Swap(NativeAnimationState&);
private:
    friend class NativeSprite;
    void Restart();
    void Update(float speed);
    void Emit();
    void AdvanceTo(int);
    void UpdateCursors(float);
};
class NativeSprite {
public:
    const NativeSpriteAsset* asset;
    NativeAnimationState mainState,overlayState;
    NativePoint offset,scale;
    float rotation,color[11],secondaryColor[11],speed;
    bool flipX,flipY,updateEnabled;
    unsigned int renderFlags; //Sprite+110; 739a33/73a990 set native bit8.
    //Logical bindings written by player Init. Viewport scale resolves through
    //native BA3B14/18 at drawing; explicit blend presets resolve through the
    //renderer-dependent94bb50. Neither is a guessed GPU state/default.
    bool viewportScale;
    std::vector<int> layerBlendPresets; //Native layer ordinal; -1=no override.
    std::vector<NativeSpriteLayerSheet> layerSheets; //Mutable per-instance bindings, not asset edits.
    std::vector<bool> layerVisible; //Layer+70, separate from timeline-frame visibility.
    NativeSprite();
    void Clear(); //00402360 ->00402d30, including secondary alpha0.
    NativeSpriteResult Load(const char* propertyPath);
    NativeSpriteResult Select(const char* name,bool reset,bool overlay=false);
    NativeSpriteResult Play(const char* name,bool force=false,bool overlay=false);
    NativeSpriteResult SetFrame(const char* name,int frame,bool overlay=false);
    NativeSpriteResult SetLayerBlendPreset(int layer,int preset);
    int LayerOrdinal(const char* name) const; //405f40: first exact layer name.
    const NativeSpriteSheet* OriginalLayerSheet(int ordinal) const;
    NativeSpriteResult ReplaceLayerSheet(int ordinal,const char* path); //406f30; invalidates binding.
    NativeSpriteResult RefreshSheets(); //406f90 resource resolution, independent of GPU upload.
    NativeSpriteResult Update(); //One unpaced component update, no game cadence.
    bool IsPlaying(const char* name="",bool overlay=false) const;
    bool IsFinished(const char* name="",bool overlay=false) const;
    const NativeSpriteFrame* LayerFrame(int id,bool overlay=false) const;
    const NativeSpriteFrame* NullFrame(int id,bool overlay=false) const;
    void Swap(NativeSprite&);
    unsigned int StateHash() const;
private:
    NativeSprite(const NativeSprite&);
    NativeSprite& operator=(const NativeSprite&);
};
//14 direct Sprite owners at0066e95e/00707b8c..c04. The original70a344..8db
//asset block binds blood-gush/crowns/charge owners and property-selected body.
//GPU state, costumes and complete player construction/Init remain open.
class NativePlayerSprites {
public:
    NativeSprite sprites[14];bool bodyReady,startupAssetsReady;
    NativePlayerSprites():bodyReady(false),startupAssetsReady(false){}
    NativeSpriteResult BeginFresh(const char* originalPropertyPath);
    NativeSpriteResult PrepareAuxiliary(); //70a344..70a815.
    NativeSpriteResult PrepareBody(const char* originalPropertyPath); //70a84b..70a91f.
    void Swap(NativePlayerSprites&);
    unsigned int StateHash() const;
private:
    NativePlayerSprites(const NativePlayerSprites&);
    NativePlayerSprites& operator=(const NativePlayerSprites&);
};
#endif
