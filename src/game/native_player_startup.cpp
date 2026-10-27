#include "native_player_startup.h"
#include "native_player_startup_data.h"
namespace {
void Fold(unsigned int& h,unsigned int n){for(int i=0;i<4;++i)h=(h^((n>>(i*8))&255))*16777619U;}
}
NativePlayerStartup::NativePlayerStartup():entityInitSeed(0),ready(false){manager.Default();}
void NativePlayerStartup::Swap(NativePlayerStartup& other) {
    NativeRng owner=manager;manager=other.manager;other.manager=owner;
    unsigned int seed=entityInitSeed;entityInitSeed=other.entityInitSeed;other.entityInitSeed=seed;
    bool r=ready;ready=other.ready;other.ready=r;
    for(int i=0;i<NPB_COUNT;++i)banks[i].swap(other.banks[i]);
}
bool NativePlayerStartup::BeginFresh(unsigned int seed) {
    if(!seed)return false;
    NativePlayerStartup next;NativeRng root;
    if(!next.manager.Set(seed,13)||!root.Set(seed,5))return false;
    //0070939a,007094cd,00709606,0070973c: independent parent advances.
    //Each bank uses its own index6/7/8/9 clone; child owners all use35.
    for(int bank=0;bank<NPB_COUNT;++bank){unsigned int base;root.Next(&base);NativeRng local;local.Set(base,6+bank);
        next.banks[bank].resize(kNativePlayerSeedSlots[bank]);
        for(unsigned int id=0;id<next.banks[bank].size();++id){unsigned int value;local.Next(&value);next.banks[bank][id].Set(value,35);}
    }
    root.Next(&next.entityInitSeed); //00709955..83; no bank-local state leaks.
    next.ready=true;Swap(next);return true;
}
unsigned int NativePlayerStartup::StateHash() const {
    unsigned int h=2166136261U;Fold(h,ready?1:0);Fold(h,manager.state);Fold(h,manager.index);Fold(h,entityInitSeed);
    for(int bank=0;bank<NPB_COUNT;++bank){Fold(h,(unsigned int)banks[bank].size());
        for(unsigned int id=0;id<banks[bank].size();++id){Fold(h,banks[bank][id].state);Fold(h,banks[bank][id].index);}}
    return h;
}
