#include "native_room_view.h"
#include "native_room_state.h"
#include "native_entity_body.h"
#include "native_player_creation.h"
#include "native_player_manager.h"
#include "native_persistence.h"
#include "native_doors.h"
#include <algorithm>
#include <string.h>
namespace {
float Add(float a,float b){volatile float f=a+b;return f;}
float Sub(float a,float b){volatile float f=a-b;return f;}
float Mul(float a,float b){volatile float f=a*b;return f;}
float Div(float a,float b){volatile float f=a/b;return f;}
unsigned int Bits(float f){unsigned int n;memcpy(&n,&f,4);return n;}
bool Zero(const NativePlayerCreation& c,unsigned int at){unsigned int value;return c.constructorFields.Scalar(at,&value)&&!value;}
void Append(NativeRoomView& view,int ring,NativePoint p){
    const int capacity=(int)view.samples[ring].size();
    if(view.sampleCount[ring]==capacity){view.sampleHead[ring]=(view.sampleHead[ring]+1)%capacity;--view.sampleCount[ring];}
    view.samples[ring][(view.sampleHead[ring]+view.sampleCount[ring])%capacity]=p;++view.sampleCount[ring];
}
//88bf40: the original triangular weights and scalar float accumulation order.
NativePoint Average(const std::vector<NativePoint>& v,int head,int count,float fraction){
    const int pivot=(int)Mul((float)count,fraction);float total=0;NativePoint out={0,0};
    for(int i=0;i<count;++i){float endpoint=i<=pivot?-1.0f:Add((float)count,1.0f);
        float weight=Div(Sub((float)i,endpoint),Sub((float)pivot,endpoint));
        const NativePoint& p=v[(head+i)%v.size()];
        total=Add(total,weight);out.x=Add(out.x,Mul(p.x,weight));out.y=Add(out.y,Mul(p.y,weight));
    }
    float scale=Div(1,total);out.x=Mul(out.x,scale);out.y=Mul(out.y,scale);return out;
}
}
bool NativeRoomView::FinishInitialPlacement(const NativeRoomState& room,const NativeEntityBody& body,const NativePlayerCreation& creation,const NativePlayerManager& manager,NativeRunHistory& history,unsigned int* next){
    if(next)*next=0x88c610;
    if(!next||!cameraReady||placementCameraReady||cameraFollowing||manager.members.size()!=1||!body.native168||body.variant||room.shape!=1||room.type!=1)return false;
    //908bb0(false) averages the eligible primary owner; no local secondary
    //or suspended player exists.7294e0 scans these actual null weapon/target
    //references before88c800's directional look-ahead branch.
    const unsigned int empty[]={0x134c,0x1324,0x1328,0x132c,0x1330,0x1524,0x1554,0x1558};
    for(unsigned int i=0;i<sizeof(empty)/sizeof(empty[0]);++i)if(!Zero(creation,empty[i]))return false;
    unsigned char shooting;if(!creation.constructorFields.Byte(0x1336,&shooting)||shooting||body.velocity.x||body.velocity.y)return false;
    for(unsigned int i=0;i<manager.worldTypes.size();++i)if(manager.worldTypes[i]>=10&&manager.worldTypes[i]<1000){*next=0x88cc71;return false;}
    unsigned int shake;if(!history.fields.Get(0x1a2e2c,4,&shake)||shake){*next=0x88d28c;return false;}
    const int capacities[]={60,90,300,15,4,120};
    for(int i=0;i<6;++i)if((int)samples[i].size()!=capacities[i]||sampleCount[i]!=capacities[i]||sampleHead[i])return false;
    //88c610 adjusts the current translation only outside its native limits.
    //Here17*26+2*marginX==width and11*26+2*marginY==height; retain the
    //same scalar arithmetic instead of replacing the result with literal0.
    float px=Add(Mul(Sub(width,338.0f),0.5f),Mul(Sub(body.position.x,60.0f),0.65f));
    float py=Add(Mul(Sub(height,182.0f),0.5f),Mul(Sub(body.position.y,140.0f),0.65f));
    float lowX=Sub(width,Add(Mul(Add((float)room.geometry.stride,2),26),Add(cameraMargin.x,cameraMargin.x)));
    float lowY=Sub(height,Add(Mul(Add((float)room.geometry.rows,2),26),Add(cameraMargin.y,cameraMargin.y)));
    cameraCenter.x=Add(cameraCenter.x,std::max(std::min(0.0f,Sub(Mul(width,cameraScale.x),px)),lowX));
    cameraCenter.y=Add(cameraCenter.y,std::max(std::min(0.0f,Sub(Mul(height,cameraScale.y),py)),lowY));
    //88c800(false), Options CameraStyle1: the first four actual ring inputs
    //are zero (look-ahead, movement, combatant influence, local secondary).
    NativePoint zero={0,0};for(int i=0;i<4;++i)Append(*this,i,zero);
    NativePoint sentinel={-1,-1};Append(*this,5,sentinel);cameraFollowing=true;
    //88d0fc copies the position ring before resolving sentinels. It does
    //not replace the native ring storage with projected player positions.
    std::vector<NativePoint> copy(samples[5]);
    for(unsigned int i=0;i<copy.size();++i)if(copy[i].x==-1&&copy[i].y==-1)copy[i]=body.position;
    NativePoint average=Average(copy,sampleHead[5],sampleCount[5],0.5f);
    if(!CameraPoint(room,average,&cameraCenter))return false;
    Append(*this,4,zero);NativePoint wobble=Average(samples[4],sampleHead[4],sampleCount[4],0.9f);
    history.fields.Put(0x1a2e30,Bits(wobble.x));history.fields.Put(0x1a2e34,Bits(wobble.y));
    //88d4db examines the actual attached doors in slot order. The initial
    //(320,380) position lies outside every30-unit door approach region.
    for(int slot=0;slot<8;++slot)if(room.doors[slot]){
        int grid;float x,y;if(!NativeDoorGrid(room.shape,room.geometry,slot,&grid)||!room.geometry.Position(grid,&x,&y))return false;
        const float dx=Sub(body.position.x,x),dy=Sub(body.position.y,y);
        if(Add(Mul(dx,dx),Mul(dy,dy))<900){*next=0x88d580;return false;}
    }
    native60=body.position;cameraFollowing=false;++cameraUpdates;placementCameraReady=true;*next=0;return true;
}
