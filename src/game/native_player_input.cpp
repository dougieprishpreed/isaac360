#include "native_player_input.h"
#include <string.h>
#include <math.h>
namespace {
bool Finite(float n){unsigned int bits;memcpy(&bits,&n,4);return (bits&0x7fffffffU)<0x7f800000U;}
float Add(float a,float b){volatile float n=a+b;return n;}
float Sub(float a,float b){volatile float n=a-b;return n;}
float Mul(float a,float b){volatile float n=a*b;return n;}
bool Valid(const NativePlayerInputContext& c){return c.playerType>=0&&c.stage>=0&&c.stageType>=0&&c.stageType<=5&&c.dimension>=0&&c.dimension<=2;}
bool Reflected(const NativePlayerInputContext& c){return c.dimension==1&&(c.stageType==4||c.stageType==5)&&(c.stage==2||(c.stage==1&&(c.evaluatedStageFlags&2)!=0));}
NativePlayerInputResult AxisValues(const NativePlayerInputContext& c,NativeActionSource& source,int first,NativePoint* output) {
    float values[4]={0};for(int i=0;i<4;++i){NativePlayerInputResult r=source.Value(first+i,c.controller,c.playerToken,&values[i]);
        if(r!=NPI_OK)return r;if(!Finite(values[i]))return NPI_BAD_VALUE;}
    output->x=Add(Sub(0,values[0]),values[1]);output->y=Add(Sub(0,values[2]),values[3]);
    return Finite(output->x)&&Finite(output->y)?NPI_OK:NPI_BAD_VALUE;
}
bool Cardinal(NativePoint& p,bool movement) {
    //00423db0 converts the rounded scalar sum to double, SQRTSD, then back
    //to float. Preserve the two native callers' multiplication/add order.
    float xx=Mul(p.x,p.x),yy=Mul(p.y,p.y),sum=movement?Add(yy,xx):Add(xx,yy);
    if(!Finite(sum))return false;volatile float length=(float)sqrt((double)sum);
    if(fabsf(p.x)>fabsf(p.y)){p.x=Mul(p.x<0?-1.0f:1.0f,length);p.y=Mul(0,length);}
    else {p.x=Mul(0,length);p.y=Mul(p.y<0?-1.0f:1.0f,length);}return true;
}
}
void NativeActionFrame::Clear(int device,unsigned int owner,unsigned int tick){memset(this,0,sizeof(*this));controller=device;playerToken=owner;frame=tick;}
bool NativeActionFrame::SupplyValue(int action,float n){if(action<0||action>7||!Finite(n))return false;values[action]=n;knownValues|=1U<<action;return true;}
bool NativeActionFrame::SupplyHeld(int action,bool value){if(action<0||action>11)return false;unsigned int bit=1U<<action;knownHeld|=bit;if(value)held|=bit;else held&=~bit;return true;}
bool NativeActionFrame::SupplyPressed(int action,bool value){if(action<0||action>11)return false;unsigned int bit=1U<<action;knownPressed|=bit;if(value)pressed|=bit;else pressed&=~bit;return true;}
NativePlayerInputResult NativeFrameActions::Value(int action,int controller,unsigned int owner,float* out) {
    if(!out||action<0||action>7)return NPI_BAD_REQUEST;
    if(controller!=frame.controller||owner!=frame.playerToken||!(frame.knownValues&(1U<<action)))return NPI_SOURCE_MISSING;
    if(!Finite(frame.values[action]))return NPI_BAD_VALUE;*out=frame.values[action];return NPI_OK;
}
NativePlayerInputResult NativeFrameActions::Pressed(int action,int controller,unsigned int owner,bool* out) {
    if(!out||action<0||action>11)return NPI_BAD_REQUEST;
    if(controller!=frame.controller||owner!=frame.playerToken)return NPI_SOURCE_MISSING;
    if(action==11){if(!frame.dropKnown)return NPI_SOURCE_MISSING;*out=frame.dropPressed;return NPI_OK;}
    if(!(frame.knownPressed&(1U<<action)))return NPI_SOURCE_MISSING;
    *out=(frame.pressed&(1U<<action))!=0;return NPI_OK;
}
NativePlayerInputResult NativeFrameActions::Held(int action,int controller,unsigned int owner,bool* out){
    if(!out||action<0||action>11)return NPI_BAD_REQUEST;
    if(controller!=frame.controller||owner!=frame.playerToken||!(frame.knownHeld&(1U<<action)))return NPI_SOURCE_MISSING;
    *out=(frame.held&(1U<<action))!=0;return NPI_OK;
}
NativePlayerInputResult NativeFrameActions::Connected(int controller,bool* out){
    if(!out)return NPI_BAD_REQUEST;if(controller!=frame.controller||!frame.connectionKnown)return NPI_SOURCE_MISSING;
    *out=frame.connected;return NPI_OK;
}
NativePlayerInputResult NativeMovementInput(const NativePlayerInputContext& c,NativeActionSource& source,NativePoint* output) {
    if(!output||!Valid(c))return NPI_BAD_REQUEST;
    bool paired=c.playerType==20||(c.parentPresent&&c.parentEntityType==1)||
        ((c.playerType==29||c.playerType==38)&&c.twinPresent&&!c.twinIsSelf&&c.twinNative1540<c.native1540);
    if(paired&&c.controlsEnabled&&c.controlsCooldown<=0){bool held=false;NativePlayerInputResult r=source.Pressed(11,c.controller,c.playerToken,&held);
        if(r!=NPI_OK)return r;if(held){NativePoint zero={0,0};*output=zero;return NPI_OK;}}
    NativePoint p={0,0};NativePlayerInputResult r=AxisValues(c,source,0,&p);if(r!=NPI_OK)return r;
    if(c.seedEffect12){p.x=Mul(p.x,-1);p.y=Mul(p.y,-1);}
    if(c.seedEffect76&&!Cardinal(p,true))return NPI_BAD_VALUE;
    if(Reflected(c))p.x=Mul(p.x,-1);*output=p;return NPI_OK;
}
NativePlayerInputResult NativeShootingInput(const NativePlayerInputContext& c,NativeActionSource& source,NativePoint* output) {
    if(!output||!Valid(c))return NPI_BAD_REQUEST;
    NativePoint p={0,0};NativePlayerInputResult r;
    if(c.seedEffect74||c.seedEffect75){r=NativeMovementInput(c,source,&p);if(r!=NPI_OK)return r;
        if(!c.seedEffect74){p.x=Mul(p.x,-1);p.y=Mul(p.y,-1);}}
    else {r=AxisValues(c,source,4,&p);if(r!=NPI_OK)return r;if(c.seedEffect12){p.x=Mul(p.x,-1);p.y=Mul(p.y,-1);}}
    //The native nested movement call is deliberately not cached or folded:
    //it applies its own effects/reflection before the shooting transform.
    if(c.seedEffect76&&!Cardinal(p,false))return NPI_BAD_VALUE;
    if(Reflected(c))p.x=Mul(p.x,-1);*output=p;return NPI_OK;
}
