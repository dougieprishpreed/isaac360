#ifndef GENERIC_NATIVE_ITEM_POOLS_H
#define GENERIC_NATIVE_ITEM_POOLS_H
#include "native_player.h"
#include <vector>
#include <map>
struct NativeItemPoolEntry {
    int id;float initialWeight,weight,decreaseBy,removeOn;bool unlocked,special; //Native+4/+8.
};
struct NativeItemPool {
    float totalWeight;std::vector<NativeItemPoolEntry> entries;
    NativeRng selection,upgrade;unsigned int counter;
    NativeItemPool():totalWeight(0),counter(0){selection.Default();upgrade.Default();}
};
struct NativePoolTrinket {int id;bool enabled,unlocked;};
struct NativePocketSoundReset {int type,id,announcer;}; //Pill effect0/card/rune1.
struct NativeSoundState {
    //The original SoundManager owns these flags across run restarts. Presence
    //means an addressed write is known; absence never implies a false flag.
    std::map<int,bool> native199;
};
//6ecf40's persistent transfer. Definitions/lock flags and both per-pool
//streams are reconstructed from seed/counter by the native restore path;
//they are not independently written into this record.
struct NativeItemPoolSnapshot {
    std::vector<float> weights[31];unsigned int poolCounters[31];
    unsigned int seed,counter,random,backup;float treasureMultiplier,devilMultiplier;
    std::vector<unsigned char> seen,trinketEnabled;
    int pillEffects[15];bool pillIdentified[15];unsigned char pocketFlags[62];
};
class NativeItemPools {
public:
    bool initialized;unsigned int seed,counter;NativeRng random,backup;
    NativeItemPool pools[31];float treasureMultiplier,devilMultiplier;
    std::vector<unsigned char> seen,removed;
    std::vector<NativePoolTrinket> trinkets;int availableTrinkets;
    int pillEffects[15];bool pillIdentified[15];unsigned char pocketFlags[62];
    std::vector<NativePocketSoundReset> soundResets;
    NativeItemPools();
    //6eb5e0 called by6b47f8 before904960. Restricted to its actual ordinary,
    //empty-manager path; this is not a replacement for later item selection.
    bool InitializeOrdinary(unsigned int seed,const NativeRunStart&,const NativeProfile&,NativeSoundState&);
    //Maintained weighted selector over one prepared pool. Uses that pool's
    //native selection stream, applies DecreaseBy to the chosen entry and
    //removes it below RemoveOn. Returns0 when no eligible entry remains.
    int SelectCollectible(int poolIndex);
    bool Capture(NativeItemPoolSnapshot&) const;
    void Swap(NativeItemPools&);
};
#endif
