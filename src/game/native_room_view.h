#ifndef GENERIC_NATIVE_ROOM_VIEW_H
#define GENERIC_NATIVE_ROOM_VIEW_H
#include "native_room_space.h"
#include <vector>
#include "native_room_background.h"
struct NativeRoomState;
class NativeEntityBody;
class NativePlayerCreation;
class NativePlayerManager;
struct NativeRunHistory;
class NativeRoomFx;
struct NativeRoomView {
    bool lightingReady,cameraReady,lightingActive;
    float width,height,lightingCurrent[6],lightingTarget[6];
    NativePoint cameraCenter,cameraScale,cameraMargin;
    NativePoint native60,native7c,native84;
    bool cameraFollowing;float cameraRate;
    bool placementCameraReady;unsigned int cameraUpdates;
    std::vector<NativePoint> samples[6];
    int sampleHead[6],sampleCount[6];
    NativeRoomBackground background;
    NativeRoomView();
    bool PrepareLighting(const NativeRoomFx&,unsigned int descriptorFlags);
    bool PrepareCamera(const NativeRoomState&);
    bool CameraPoint(const NativeRoomState&,NativePoint,NativePoint*) const;
    bool FinishInitialPlacement(const NativeRoomState&,const NativeEntityBody&,const NativePlayerCreation&,const NativePlayerManager&,NativeRunHistory&,unsigned int* next);
};
#endif
