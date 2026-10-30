#include "native_room_view.h"
#include "native_room_state.h"
#include "native_room_fx.h"
#include "native_room_view_data.h"
#include <string.h>
namespace {
float F(unsigned int n){float f;memcpy(&f,&n,4);return f;}
float Add(float a,float b){volatile float f=a+b;return f;}
float Sub(float a,float b){volatile float f=a-b;return f;}
float Mul(float a,float b){volatile float f=a*b;return f;}
float Clamp(float f,float low,float high){return (low>f?low:f)>high?(low>high?low:high):(low>f?low:f);}
}
NativeRoomView::NativeRoomView():lightingReady(false),cameraReady(false),lightingActive(false),width(F(kViewViewportWidth)),height(F(kViewViewportHeight)),cameraFollowing(false),cameraRate(0),placementCameraReady(false),cameraUpdates(0){
    memset(lightingCurrent,0,sizeof(lightingCurrent));memset(lightingTarget,0,sizeof(lightingTarget));
    cameraCenter.x=cameraCenter.y=cameraScale.x=cameraScale.y=cameraMargin.x=cameraMargin.y=0;
    native60.x=native60.y=native7c.x=native7c.y=native84.x=native84.y=0;
    for(int i=0;i<6;++i)sampleHead[i]=sampleCount[i]=0;
}
bool NativeRoomView::PrepareLighting(const NativeRoomFx& fx,unsigned int flags){
    if(lightingReady||!fx.ready||!fx.shadingTexture)return false;
    //7ccb80 ordinary room1: no secret/devil/custom/alternate-color branch.
    for(int i=0;i<6;++i)lightingTarget[i]=fx.parameters.modifier[i];
    if(flags&0x200U)lightingTarget[4]=Add(lightingTarget[4],F(kViewFlag512Brightness));
    //7ccd70 observes New Run's243378=true.6c24d0(false) copies the target
    //immediately and clears interpolation; it does not inspect old step data.
    for(int i=0;i<6;++i)lightingCurrent[i]=lightingTarget[i];
    lightingActive=false;lightingReady=true;return true;
}
bool NativeRoomView::CameraPoint(const NativeRoomState& room,NativePoint point,NativePoint* result) const{
    if(!result||room.shape!=1||room.type!=1)return false;
    const NativeRoomSpace& s=room.space;float scale=F(kViewGridScale),padding=F(kViewCameraPadding);
    //88c0a0: view limits from the actual post-grid bounds and current margin.
    float loX=Sub(-cameraMargin.x,Sub(Mul(Sub(Sub(s.left,F(kViewCameraXReference)),padding),scale),cameraMargin.x));
    float loY=Sub(-cameraMargin.y,Sub(Mul(Sub(Sub(s.top,F(kViewCameraYReference)),padding),scale),cameraMargin.y));
    float hiX=Sub(Sub(width,cameraMargin.x),Add(Mul(Add(Sub(s.right,F(kViewCameraXReference)),padding),scale),cameraMargin.x));
    float hiY=Sub(Sub(height,cameraMargin.y),Add(Mul(Add(Sub(s.bottom,F(kViewCameraYReference)),padding),scale),cameraMargin.y));
    if(hiX>loX)loX=hiX=Mul(Add(loX,hiX),0.5f);
    if(hiY>loY)loY=hiY=Mul(Add(loY,hiY),0.5f);
    //88c250: transform then native max/min clamp (no room flag15 here).
    float x=Sub(Mul(width,cameraScale.x),Add(Mul(Sub(width,F(kViewRoomWidth)),0.5f),Mul(Sub(point.x,F(kViewWorldXReference)),scale)));
    float y=Sub(Mul(height,cameraScale.y),Add(Mul(Sub(height,F(kViewRoomHeight)),0.5f),Mul(Sub(point.y,F(kViewWorldYReference)),scale)));
    result->x=Clamp(x,loX,hiX);result->y=Clamp(y,loY,hiY);return true;
}
bool NativeRoomView::PrepareCamera(const NativeRoomState& room){
    if(cameraReady||!lightingReady||room.shape!=1||room.type!=1)return false;
    //797991..9d3 computes the margin at room construction. The original
    //480x27016:9 logical viewport is also the adapter's inherited presentation.
    cameraMargin.x=Sub(Mul(Sub(width,F(kViewRoomWidth)),0.5f),F(kViewCameraBorder));
    cameraMargin.y=Sub(Mul(Sub(height,F(kViewRoomHeight)),0.5f),F(kViewCameraBorder));
    cameraScale.x=cameraScale.y=0.5f;NativePoint zero={0,0};
    if(!CameraPoint(room,zero,&cameraCenter))return false;
    const int sizes[]={60,90,300,15,4,120};
    for(int i=0;i<6;++i){NativePoint value={0,0};if(i==5)value.x=value.y=-1;
        samples[i].assign(sizes[i],value);sampleHead[i]=0;sampleCount[i]=sizes[i];}
    //88bdd2: New Run has no entry-door slot.6febf0 returns zero with the
    //same current/previous descriptor. Neither optional center replacement runs.
    cameraFollowing=false;cameraRate=0.1f;cameraReady=true;return true;
}
