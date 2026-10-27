#include "native_room_space.h"
#include "native_room_space_data.h"
#include <string.h>
#include <math.h>
namespace {
bool Finite(float value){unsigned int n;memcpy(&n,&value,4);return (n&0x7fffffffU)<0x7f800000U;}
float Add(float a,float b){volatile float n=a+b;return n;}
float Sub(float a,float b){volatile float n=a-b;return n;}
float Restrict(float value,float low,float high){return value<high?(value>low?value:low):(high>low?high:low);}
float FromBits(unsigned int n){float v;memcpy(&v,&n,4);return v;}
bool ValidGrid(const NativeRoomGrid& g){return g.width>0&&g.width<=26&&g.height>0&&g.height<=14&&g.stride==g.width+2&&g.rows==g.height+2&&g.cells==g.stride*g.rows;}
}
bool NativeRoomSpace::Init(int s,const NativeRoomGrid& grid,int stage,int type) {
    NativeRoomGrid expected;if(stage<0||type<0||!expected.Init(s,grid.width,grid.height)||
        grid.stride!=expected.stride||grid.rows!=expected.rows||grid.cells!=expected.cells)return false;
    NativeRoomSpace next={0};next.shape=s;next.zeroCollision=stage==35&&type==16;
    next.left=60;next.top=140;next.right=(float)(grid.stride-2)*40+60;next.bottom=(float)(grid.rows-2)*40+140;
    const int* t=kNativeRoomSpaceCells[s];
    for(int i=0;i<2;++i){next.regions[i*4]=(float)t[i*4]*40+20;next.regions[i*4+1]=(float)t[i*4+1]*40+100;
        next.regions[i*4+2]=(float)t[i*4+2]*40+60;next.regions[i*4+3]=(float)t[i*4+3]*40+140;}
    //0079fda0(true) uses the outer rectangle for L rooms, table endpoints
    //for thin rooms, and the literal original special-stage rectangle.
    if(s==2||s==3||s==5||s==7){next.left=next.regions[0];next.top=next.regions[1];next.right=next.regions[6];next.bottom=next.regions[7];}
    if(next.zeroCollision){next.left=FromBits(kNativeSpecialRoomBounds[0]);next.top=FromBits(kNativeSpecialRoomBounds[1]);
        next.right=FromBits(kNativeSpecialRoomBounds[2]);next.bottom=FromBits(kNativeSpecialRoomBounds[3]);}
    if(s<9)for(int i=0;i<2;++i){next.regions[4*i]=next.left;next.regions[4*i+1]=next.top;next.regions[4*i+2]=next.right;next.regions[4*i+3]=next.bottom;}
    *this=next;return true;
}
bool NativeRoomSpace::RefreshBounds(const NativeRoomGrid& grid,const NativeRoomCollision& collision) {
    NativeRoomGrid expected;if(!expected.Init(shape,grid.width,grid.height)||!ValidGrid(grid))return false;
    if(zeroCollision){left=FromBits(kNativeSpecialRoomBounds[0]);top=FromBits(kNativeSpecialRoomBounds[1]);
        right=FromBits(kNativeSpecialRoomBounds[2]);bottom=FromBits(kNativeSpecialRoomBounds[3]);
        if(shape<9)for(int i=0;i<2;++i){regions[4*i]=left;regions[4*i+1]=top;regions[4*i+2]=right;regions[4*i+3]=bottom;}return true;}
    int firstX=1,firstY=1,lastX=grid.stride-2,lastY=grid.rows-2;
    if(shape==2||shape==3||shape==5||shape==7){const int* t=kNativeRoomSpaceCells[shape];firstX=t[0];firstY=t[1];lastX=t[6];lastY=t[7];}
    int lowX=firstX,highX=lastX,lowY=firstY,highY=lastY;bool solidRows[16];for(int y=0;y<grid.rows;++y)solidRows[y]=true;
    for(int x=firstX;x<=lastX;++x) {
        bool solid=true;
        for(int y=firstY;y<=lastY;++y){int cell=y*grid.stride+x;
            if(!collision.occupied[cell]||(collision.types[cell]!=15&&collision.types[cell]!=16)){solid=false;solidRows[y]=false;}}
        if(!solid)highX=lastX;
        else {if(x==lowX)++lowX;if(highX==lastX)highX=x-1;}
    }
    for(int y=firstY;y<=lastY;++y) {
        if(!solidRows[y])highY=lastY;
        else {if(y==lowY)++lowY;if(highY==lastY)highY=y-1;}
    }
    NativeRoomSpace next=*this;next.left=(float)lowX*40+20;next.top=(float)lowY*40+100;
    next.right=(float)highX*40+60;next.bottom=(float)highY*40+140;
    if(shape<9)for(int i=0;i<2;++i){next.regions[4*i]=next.left;next.regions[4*i+1]=next.top;next.regions[4*i+2]=next.right;next.regions[4*i+3]=next.bottom;}
    *this=next;return true;
}
bool NativeRoomSpace::Clamp(const NativePoint& p,float lm,float tm,float rm,float bm,NativePoint* output) const {
    if(!output||shape<1||shape>12||!Finite(p.x)||!Finite(p.y)||!Finite(lm)||!Finite(tm)||!Finite(rm)||!Finite(bm))return false;
    float loX=Add(left,lm),loY=Add(top,tm),hiX=Sub(right,rm),hiY=Sub(bottom,bm);
    if(!Finite(loX)||!Finite(loY)||!Finite(hiX)||!Finite(hiY))return false;
    NativePoint q={Restrict(p.x,loX,hiX),Restrict(p.y,loY,hiY)};
    if(shape>=9) {
        float aLeft=Add(regions[0],lm),aRight=Sub(regions[2],rm),bLeft=Add(regions[4],lm),bRight=Sub(regions[6],rm);
        float y=Sub(bRight,bLeft)>Sub(aRight,aLeft)?Add(regions[5],tm):Sub(regions[3],bm);
        if(!Finite(aLeft)||!Finite(aRight)||!Finite(bLeft)||!Finite(bRight)||!Finite(y))return false;
        float xEdge=shape==9?aLeft:shape==10?aRight:shape==11?bLeft:bRight;
        bool missingX=(shape==9||shape==11)?q.x<xEdge:q.x>xEdge;
        bool missingY=shape<=10?q.y<y:q.y>y;
        if(missingX&&missingY){float dx=(shape==9||shape==11)?Sub(xEdge,q.x):Sub(q.x,xEdge);
            float dy=shape<=10?Sub(y,q.y):Sub(q.y,y);if(dy>dx)q.x=xEdge;else q.y=y;}
    }
    *output=q;return true;
}
bool NativeRoomSpace::Wrap(const NativePoint& p,float lm,float tm,float rm,float bm,NativePoint* output) const {
    NativePoint q;if(!output||!Clamp(p,lm,tm,rm,bm,&q))return false;
    if(q.x==p.x&&q.y==p.y){*output=p;return true;}
    float dx=Sub(q.x,p.x),dy=Sub(q.y,p.y);bool horizontal=dy==0||(dx!=0&&fabsf(dy)>fabsf(dx));
    float w=Sub(Sub(right,rm),Add(left,lm)),h=Sub(Sub(bottom,bm),Add(top,tm));q=p;
    if(horizontal){if(dx>0)q.x=Add(p.x,w);else if(dx<0)q.x=Sub(p.x,w);}
    else {if(dy>0)q.y=Add(p.y,h);else if(dy<0)q.y=Sub(p.y,h);}
    if(!Finite(q.x)||!Finite(q.y))return false;*output=q;return true;
}
bool NativeRoomSpace::Contains(const NativePoint& p,float margin) const {
    if(shape<9){if(shape<1||!Finite(p.x)||!Finite(p.y)||!Finite(margin))return false;
        return p.x>=Add(left,margin)&&p.x<=Sub(right,margin)&&p.y>=Add(top,margin)&&p.y<=Sub(bottom,margin);}
    NativePoint q;return Clamp(p,margin,margin,margin,margin,&q)&&q.x==p.x&&q.y==p.y;
}
void NativeRoomCollision::Clear(){memset(this,0,sizeof(*this));}
bool NativeRoomCollision::Set(int cell,bool occupant,int collisionClass,int value,int type) {
    if(cell<0||cell>=kNativeRoomGridLimit)return false;occupied[cell]=occupant;classes[cell]=collisionClass;native76c[cell]=value;types[cell]=type;return true;
}
bool NativeRoomCollision::At(int cell,bool zero,int* output) const {
    if(!output)return false;
    if(zero){*output=0;return true;}
    if(cell<0){*output=4;return true;}
    if(cell>=kNativeRoomGridLimit)return false; //Original positive indexing is unchecked.
    if(occupied[cell]){*output=classes[cell];return true;}
    int value=native76c[cell];*output=value>=3000&&value<=3999?1:value>=1000?3:0;return true;
}
bool NativeRoomCollision::AtPosition(const NativeRoomGrid& grid,const NativePoint& p,bool zero,int* output) const {
    if(!Finite(p.x)||!Finite(p.y)||!ValidGrid(grid))return false;
    return At(grid.PositionIndex(p.x,p.y),zero,output);
}
bool NativeRoomCollision::CostAtPosition(const NativeRoomGrid& grid,const NativePoint& p,int* output) const {
    if(!output||!Finite(p.x)||!Finite(p.y)||!ValidGrid(grid))return false;
    int cell=grid.PositionIndex(p.x,p.y);if(cell>=kNativeRoomGridLimit)return false;*output=cell<0?0:native76c[cell];return true;
}
