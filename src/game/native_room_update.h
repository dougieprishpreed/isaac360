#ifndef GENERIC_NATIVE_ROOM_UPDATE_H
#define GENERIC_NATIVE_ROOM_UPDATE_H
#include <set>
struct PortNormalRuntime;
class NativeActionSource;
//Live Room::Init/Update owners; a suspended call retains its grid cursor.
class NativeRoomUpdate {
public:
    unsigned int enteredFrame,address,gridCursor;
    bool active,native7114,native764c;
    int native7648,native717c,native7120;
    unsigned int actorStage,actorCursor;
    std::set<int> pendingGridRemoval;
    NativeRoomUpdate(unsigned int frame);
    bool PrepareInitialActors(PortNormalRuntime&,NativeActionSource*,unsigned int* next);
    //Repeated whole gameplay tick for the owned resident: actor PreUpdate,
    //player Update and the reached grid collision response. Room-specific
    //presentation/audio services remain separate owners.
    bool TickActors(PortNormalRuntime&,NativeActionSource*,unsigned int* next);
    bool TickSimulationTail(PortNormalRuntime&,unsigned int* next);
};
#endif
