#ifndef GENERIC_NATIVE_PERSISTENCE_H
#define GENERIC_NATIVE_PERSISTENCE_H
#include "native_player_creation.h"
#include "native_boss_lifecycle.h"
#include "native_item_pools.h"
#include "native_layout.h"
#include <map>
#include <set>
class NativeWorld;
struct NativeWorldResident;
struct NativeRoomState;
struct NativePlacedRoom;
//Addressed fields of native persistent records. Host pointers and host struct
//padding are never native save bytes. Unwritten native payload remains marked
//unwritten; it cannot silently become a gameplay value.
class NativePersistentFields {
public:
    void Clear(){bytes.clear();unwritten.clear();}
    void Put(unsigned int offset,unsigned int value,unsigned int count=4);
    void Unwritten(unsigned int offset,unsigned int count);
    bool Get(unsigned int offset,unsigned int count,unsigned int* value) const;
    bool IsUnwritten(unsigned int offset) const;
    const std::map<unsigned int,unsigned char>& Bytes() const{return bytes;}
private:
    std::map<unsigned int,unsigned char> bytes;
    std::set<unsigned int> unwritten;
};
struct NativePersistentEffect {int type,id,count,timer;};
struct NativeRoomPersistent {
    bool captured;
    unsigned int defaultGridCount; //6aedb0 constructs448 records, retained beyond current geometry.
    std::vector<NativePersistentFields> grids,entities;
    NativeRoomPersistent():captured(false),defaultGridCount(448){}
    bool CaptureInitial(const NativeRoomState&,const NativeWorldResident&,const NativePlacedRoom&,unsigned int* next);
};
struct NativePlayerPersistent {
    bool captured;
    NativePersistentFields fields;
    //Keys are native destination member offsets, not a new file format.
    std::map<unsigned int,std::vector<unsigned int> > words;
    std::map<unsigned int,std::vector<unsigned char> > bytes;
    std::map<unsigned int,std::vector<bool> > bits;
    std::vector<NativePersistentEffect> effects,persistentEffects;
    NativePlayerPersistent():captured(false){}
    bool CaptureInitial(const NativePlayerState&,const NativePlayerStartup&,const NativePlayerCreation&,const NativeEntityBody&,unsigned int* next);
    bool CaptureAfterInitialSetup(const NativePlayerState&,const NativePlayerStartup&,const NativePlayerCreation&,const NativeEntityBody&,unsigned int* next);
    //Mid-run state capture uses the same native destination ownership as the
    //fresh save, but retains live health/resources/inventory/active/pocket
    //owners instead of requiring the clean pre-grant player.
    bool CaptureCurrent(const NativePlayerState&,const NativePlayerStartup&,const NativePlayerCreation&,const NativeEntityBody&,unsigned int* next);
private:
    bool CaptureState(const NativePlayerState&,const NativePlayerStartup&,const NativePlayerCreation&,const NativeEntityBody&,bool afterSetup,bool allowCurrent,unsigned int* next);
};
struct NativeDescriptorPersistent {
    NativePersistentFields fields;
    NativeRoomPersistent room;
    unsigned int definition[3];bool definitionPresent;
    NativeDescriptorPersistent();
    bool CaptureInitial(const NativePlacedRoom*,const NativeRoomPersistent*);
};
//Live reset/counter owners read by6b6ef0. Addressed stores retain unnamed
//native meanings; they are not an alternate file format or a player fixture.
struct NativeRunHistory {
    bool ready,checkpointGuard,restoreGuard;int activeCheckpoint;
    NativePersistentFields fields;
    NativeRunHistory():ready(false),checkpointGuard(false),restoreGuard(false),activeCheckpoint(-1){}
    std::vector<unsigned int> native243364; //6b01a0 constructor-empty auxiliary owner.
    bool BeginFresh(const NativeRunStart&,bool debugConsole);
};
struct NativeDormantStagePersistent {
    NativeDescriptorPersistent rooms[4];NativePersistentFields fields;
    NativeDormantStagePersistent(); //6af060, no active stage payload.
};
struct NormalRunFlow;
class NativeNightmares;
class NativePlayerManager;
struct NativeGamePersistent {
    bool captured;NativePersistentFields fields;
    NativeSeeds seeds;
    NativeItemPoolSnapshot pools;NativeBossIdentitySnapshot bosses;
    std::vector<NativeDescriptorPersistent> descriptors;
    int owners[3][169];
    NativeDormantStagePersistent dormant[7];
    std::vector<NativePlayerPersistent> players;
    //6c47c0/6c5440/6e7f40 capture actual empty auxiliary owners on this path.
    std::vector<NativePersistentEffect> effects;
    std::vector<unsigned int> auxiliary,extraConfigurations;
    std::vector<NativePersistentFields> levelConfigurations; //6fca60's separate18188 ring.
    NativeGamePersistent();
    bool CaptureInitial(const NormalRunFlow&,NativeWorld&,const NativeRng&,const NativeItemPools&,const NativeNightmares&,
        const NativeRunHistory&,NativePlayerState&,const NativePlayerStartup&,NativePlayerCreation&,NativePlayerSprites&,const NativeEntityBody&,const NativePlayerManager&,unsigned int* next);
    bool CaptureBeforeFileSave(const NormalRunFlow&,NativeWorld&,const NativeRng&,const NativeItemPools&,const NativeNightmares&,
        const NativeRunHistory&,NativePlayerState&,const NativePlayerStartup&,NativePlayerCreation&,NativePlayerSprites&,const NativeEntityBody&,const NativePlayerManager&,unsigned int* next);
    //Current mid-run game snapshot. The same writer layout is used; inactive
    //descriptors keep their reset records and the live player/pools are
    //overlaid instead of requiring the first-floor fresh boundary.
    bool CaptureCurrent(const NormalRunFlow&,NativeWorld&,const NativeRng&,const NativeItemPools&,const NativeNightmares&,
        const NativeRunHistory&,NativePlayerState&,const NativePlayerStartup&,NativePlayerCreation&,NativePlayerSprites&,const NativeEntityBody&,const NativePlayerManager&,unsigned int* next);
private:
    bool CaptureState(const NormalRunFlow&,NativeWorld&,const NativeRng&,const NativeItemPools&,const NativeNightmares&,
        const NativeRunHistory&,NativePlayerState&,const NativePlayerStartup&,NativePlayerCreation&,NativePlayerSprites&,const NativeEntityBody&,const NativePlayerManager&,bool checkpoint,bool allowCurrent,unsigned int* next);
};
struct NativeRunCheckpoint {
    bool valid;int action;unsigned int frame,events[496];NativeGamePersistent state;
    NativeRunCheckpoint();
};
#endif
