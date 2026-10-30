#ifndef GENERIC_NATIVE_PLAYER_CREATION_H
#define GENERIC_NATIVE_PLAYER_CREATION_H
#include "native_sprite.h"
#include "native_player_state.h"
#include <map>
#include <vector>
class NativePlayerManager;

//007e0610: an empty owned sequence, its end selection, and a loaded Sprite.
//The original constructor loads type1/variant0's property; it does not Play
//the default animation. These are separate instances, never shared cursors.
class NativePlayerPreview {
public:
    NativeSprite sprite;
    float nativeColorTail[4]; //Native Sprite+c4..d0; meanings still unassigned.
    int selected;
    unsigned int native120;
    NativePlayerPreview();
    NativeSpriteResult Construct(const char* playerProperty);
    bool Empty() const;
    void Swap(NativePlayerPreview&);
    unsigned int StateHash() const;
private:
    std::vector<int> sequence; //Native sentinel/end is represented by selected=-1.
    NativePlayerPreview(const NativePlayerPreview&);
    NativePlayerPreview& operator=(const NativePlayerPreview&);
};

struct NativePlayerCostumeSlot {int native0,native4,native8;bool nativeC;};

//709b04 ->948d80 ->92c970;92cfb0 dispatches948c10 before binding owner.
//Only explicitly initialized weapon fields are represented. Native+2c is
//unwritten on this path, so no consumer may infer a zero value for it.
struct NativePlayerDefaultWeapon {
    bool ready;unsigned int playerToken;
    unsigned int native14,native28,native30,native3c,native40,native44;
    float native8,nativeC,native10;
    NativePoint native18,native20,native34;
    NativePlayerDefaultWeapon();
    bool BeginFresh(unsigned int owner);
    unsigned int StateHash() const;
};

//The 7087b0 member is a cache/snapshot, not the live player's stat owner.
//Only its explicitly written scalar bytes have values. In particular, its
//unwritten prefix must not become a zero-valued player fixture or a save ABI.
class NativePlayerCreationCache {
public:
    explicit NativePlayerCreationCache(bool snapshot=true);
    bool Scalar(unsigned int offset,unsigned int* value) const;
    bool Byte(unsigned int offset,unsigned char* value) const;
    void ApplyFreshInit(unsigned int begin,unsigned int end);
    void AssignScalar(unsigned int offset,unsigned int value);
    void AssignBytes(unsigned int offset,unsigned int value,unsigned int count);
    unsigned int StateHash() const;
    void Swap(NativePlayerCreationCache&);
private:
    std::map<unsigned int,unsigned char> initializedBytes;
};

//Constructor-owned services used by the same session on host and Xbox.
//Construction alone never enables a tick. No grant/stat provider
//is supplied by this owner, and no raw original address is a host pointer.
class NativePlayerCreation {
public:
    bool constructed,costumesReset,prefixPrepared,appearancePrepared,resetTailPrepared,postGenerationPrepared,initialSaveNormalized;
    int native1540,native1544,native153c;bool controllerAssigned;
    int native17a4,native17a8; //Resolved costume skin-color selectors.
    std::string playerName; //Native1308, resolved from the original Players table.
    NativePlayerPreview previews[2]; //Native1a44,1b68.
    NativePlayerCreationCache cache; //Native1fc4.
    NativePlayerCreationCache constructorFields; //Defined707b40 scalar storage.
    NativePlayerDefaultWeapon weapon;
    NativePlayerCostumeSlot costumeSlots[15]; //Native1174, stride10.
    unsigned int costumeCapacity;
    std::vector<bool> native1e04,native1e14;
    std::vector<unsigned char> native1ec4,native1ed0;
    unsigned int native1fac;bool native1fb0Owned; //78cdf0 null token and its owned reference wrapper.
    NativePlayerCreation();
    NativeSpriteResult Construct(const char* playerProperty);
    //The one production prefix caller. Includes the reached appearance/reset
    //tail; never projects later resources, grants or generation across a gap.
    bool PreparePrefix(const NativeRunStart&,unsigned int playerSeed,NativePlayerStartup&,NativeEntityBody&,NativePlayerSprites&,NativePlayerState&,NativePlayerManager&,int primaryController);
    bool FinishPostGeneration(const NativeRunStart&,NativePlayerStartup&,NativeEntityBody&,NativePlayerSprites&,NativePlayerState&,NativePlayerManager&,unsigned int* next);
    bool NormalizeInitialSave(NativePlayerState&,NativePlayerSprites&,unsigned int* next); //907350 ->757a50(true).
    bool FinishInitialManager(NativePlayerState&,const NativeEntityBody&,unsigned int* next); //906310 ->759b30.
    bool AssignFirstPlayer(); //009049e0/e6, before virtual Init.
    bool ResetFreshCostumes(); //70a91f/711d20: reserve32, clear, slot reset.
    void Swap(NativePlayerCreation&);
    unsigned int StateHash() const;
private:
    bool PrepareFreshAppearance(const NativeEntityBody&,NativePlayerSprites&,const NativePlayerState&);
    bool FinishFreshReset(const NativePlayerStartup&,NativePlayerState&);
    std::vector<NativeSprite*> costumeRecords; //Empty fresh records; capacity is real.
    NativePlayerCreation(const NativePlayerCreation&);
    NativePlayerCreation& operator=(const NativePlayerCreation&);
};
#endif
