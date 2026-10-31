#ifndef GENERIC_NATIVE_PLAYER_EFFECTS_H
#define GENERIC_NATIVE_PLAYER_EFFECTS_H
#include <vector>
//ItemConfig fields used by008793c0..00879b20. Stable catalogue identity matters:
//two definitions with the same type/id are not the same native pointer.
struct NativeEffectDef { int type,id;unsigned int cacheFlags;int duration;bool persistent; };
const NativeEffectDef* NativePreparedEffect(int type,int id);
struct NativeEffectEntry { const NativeEffectDef* definition;int count,timer; };
class NativePlayerEffects;
class NativeEffectCallbacks {
public:
    virtual ~NativeEffectCallbacks(){}
    virtual void AddCostume(NativePlayerEffects&,const NativeEffectDef&)=0;
    virtual void Remove(NativePlayerEffects&,const NativeEffectDef&,unsigned int count,bool playerOwner)=0;
};
enum NativeEffectResult { NEF_OK,NEF_BAD_REQUEST,NEF_NEED_CALLBACKS,NEF_CALLBACK_REENTRY };
//Owned player+1440 cache. Callbacks implement710f70/746060 (or room797ce0);
//their absence is a dependency, never an implicit no-op. Read-only inspection
//during callbacks is supported; recursive mutation remains unsupported.
class NativePlayerEffects {
public:
    unsigned int flags;bool changed,disabled,playerOwner;
    NativePlayerEffects();
    const std::vector<NativeEffectEntry>& Entries() const {return entries;}
    bool Has(int type,int id) const; //collectibles include native types1/3/4.
    int Count(int type,int id) const;
    int Timer(int type,int id) const;
    bool SetTimer(int type,int id,int timer);
    NativeEffectResult Add(const NativeEffectDef*,int count,bool costume,NativeEffectCallbacks* =0);
    NativeEffectResult Remove(const NativeEffectDef*,unsigned int count,NativeEffectCallbacks* =0);
    NativeEffectResult Clear(bool all,NativeEffectCallbacks* =0);
    NativeEffectResult Update(NativeEffectCallbacks* =0);
    void Swap(NativePlayerEffects&);
    unsigned int StateHash() const;
private:
    std::vector<NativeEffectEntry> entries;bool inCallback;
    NativePlayerEffects(const NativePlayerEffects&);
    NativePlayerEffects& operator=(const NativePlayerEffects&);
};
#endif
