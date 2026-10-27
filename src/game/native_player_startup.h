#ifndef GENERIC_NATIVE_PLAYER_STARTUP_H
#define GENERIC_NATIVE_PLAYER_STARTUP_H
#include "native_rng.h"
#include <vector>
enum NativePlayerSeedBank { NPB_COLLECTIBLE,NPB_TRINKET,NPB_PILL,NPB_CARD,NPB_COUNT };
//Owned fresh-player startup data from00904960/00709330. This is not an
//initialized entity or a run-save layout. No action/effect defaults are supplied.
class NativePlayerStartup {
public:
    NativeRng manager; //00904988..a5: seed/index13, undrawn for the first player.
    unsigned int entityInitSeed; //Fifth index5 draw passed to0066f440.
    bool ready;
    std::vector<NativeRng> banks[NPB_COUNT]; //Native+1688,+1694,+16a0,+16ac.
    NativePlayerStartup();
    //Uses the prepared original catalogue's actual vector extents, including
    //empty indices. Only the fresh first-player path is implemented here.
    bool BeginFresh(unsigned int playerInitSeed);
    void Swap(NativePlayerStartup&);
    unsigned int StateHash() const; //Canonical diagnostic, not serialization.
private:
    NativePlayerStartup(const NativePlayerStartup&);
    NativePlayerStartup& operator=(const NativePlayerStartup&);
};
#endif
