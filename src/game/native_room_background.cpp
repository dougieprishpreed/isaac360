#include "native_room_background.h"
#include "native_room_state.h"
#include "native_room_background_data.h"
#include <string.h>
namespace {
float F(unsigned int u){float f;memcpy(&f,&u,4);return f;}
float Add(float a,float b){volatile float f=a+b;return f;}
float Mul(float a,float b){volatile float f=a*b;return f;}
}
const NativeSpriteAsset* NativePreparedBackgroundSprite(unsigned int i){
    return i<sizeof(kBackgroundSprites)/sizeof(kBackgroundSprites[0])?kBackgroundSprites[i]:0;
}
NativeRoomBackground::NativeRoomBackground():ready(false),nextAddress(0),spawnSeed(0),draws(0),floorVariant(-1),wallVariant(-1),
    left(0),top(0),right(0),bottom(0),definition(0),controlsRequested(false){}
unsigned int NativeRoomBackground::Int(unsigned int n){unsigned int r=0;random.Int(n,&r);++draws;return r;}
unsigned int NativeRoomBackground::WallVariation(){return Int(3)==0?Int(definition->wallVariants):0;}
void NativeRoomBackground::Quad(std::vector<NativeBackgroundQuad>& list,float sx,float sy,float sw,float sh,
    float x,float y,float w,float h,bool fx,bool fy){
    NativeBackgroundQuad q;float x1=Add(sx,sw),y1=Add(sy,sh),dx=Add(x,w),dy=Add(y,h);
    q.source[0].x=q.source[2].x=fx?x1:sx;q.source[1].x=q.source[3].x=fx?sx:x1;
    q.source[0].y=q.source[1].y=fy?y1:sy;q.source[2].y=q.source[3].y=fy?sy:y1;
    q.destination[0].x=q.destination[2].x=x;q.destination[1].x=q.destination[3].x=dx;
    q.destination[0].y=q.destination[1].y=y;q.destination[2].y=q.destination[3].y=dy;list.push_back(q);
}
bool NativeRoomBackground::Prepare(const NativeRoomState& room,int backdrop,unsigned int spawn){
    nextAddress=0x8867f0;
    if(ready||spawnSeed||!spawn||room.type!=1||room.shape!=1||room.geometry.stride!=15||room.geometry.rows!=9||backdrop<1||backdrop>2)return false;
    definition=kBackgroundDefs+backdrop-1;spawnSeed=spawn;
    //79f1c5: separate mutable owner at Room+69e4, seeded from descriptor+58.
    if(!random.Set(spawn,35))return false;
    const float unit=F(kBackgroundUnit),border=F(kBackgroundBorder);
    left=top=border;right=Add(Mul((float)room.geometry.width,unit),border);bottom=Add(Mul((float)room.geometry.height,unit),border);
    //8868c3/88697a: original resources, including their cached timelines.
    if(controls.Load(kBackgroundSprites[0]->path)!=NSPR_OK||buttons.Load(kBackgroundSprites[1]->path)!=NSPR_OK)return false;
    //87df2a is deliberately discarded.87e171 calls the selector again.
    Int(definition->floors);floorVariant=(int)Int(definition->floors);
    //87f429..f568: original floor patches for the selected floor.87f592
    //duplicates the entire vector until it contains at least32 records.
    std::vector<int> patches;
    for(int i=0;i<definition->floorVariants;++i)patches.push_back(floorVariant*definition->floorVariants+i);
    if(patches.empty())return false;
    while(patches.size()<32){std::vector<int> copy(patches);patches.insert(patches.end(),copy.begin(),copy.end());}
    for(int i=(int)patches.size()-1;i>0;--i){unsigned int j=Int(i+1);int n=patches[i];patches[i]=patches[j];patches[j]=n;}
    //87f95c..8802ea, shape1: two horizontal patches and two vertically
    //mirrored patches, reusing indices0/1 as in the original bottom pass.
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){int p=patches[x];
        Quad(floor,Mul((float)(2+(p&1)*9),unit),Mul((float)(2+(p>>1)*6),unit),Mul(7,unit),Mul(4,unit),
            Add(left,Mul((float)(x*6),unit)),Add(top,Mul((float)(y*3),unit)),Mul(7,unit),Mul(4,unit),x!=0,y!=0);}
    //87ed2c draws the original controls into this texture. It uses an
    //independent clone and does not advance the backdrop or global owner.
    controlsRequested=true;
    //8804fc, then880b80: one wall selection, four corners in native order.
    wallVariant=(int)Int(definition->walls);
    for(int corner=0;corner<4;++corner){int v=wallVariant*definition->wallVariants+(int)WallVariation();
        Quad(wall,Mul((float)((v&1)*9),unit),Mul((float)((v>>1)*6),unit),Mul(2,unit),Mul(2,unit),
            corner&1?right:0,corner&2?bottom:0,border,border,(corner&1)!=0,(corner&2)!=0);}
    //881a80/8823d2: left seven top/bottom cells, then the remaining right
    //six. The two per-cell choices precede both draw submissions.
    for(int x=0;x<13;++x){int a=wallVariant*definition->wallVariants+(int)WallVariation();
        int b=wallVariant*definition->wallVariants+(int)WallVariation();int tile=x<7?x+2:14-x;
        Quad(wall,Mul((float)((a&1)*9+tile),unit),Mul((float)((a>>1)*6),unit),unit,Mul(2,unit),
            Add(left,Mul((float)x,unit)),0,unit,border,x>=7,false);
        Quad(wall,Mul((float)((b&1)*9+tile),unit),Mul((float)((b>>1)*6),unit),unit,Mul(2,unit),
            Add(left,Mul((float)x,unit)),bottom,unit,border,x>=7,true);}
    //882891/883205: top four left/right cells, then remaining bottom three.
    for(int y=0;y<7;++y){int a=wallVariant*definition->wallVariants+(int)WallVariation();
        int b=wallVariant*definition->wallVariants+(int)WallVariation();int tile=y<4?y+2:8-y;
        Quad(wall,Mul((float)((a&1)*9),unit),Mul((float)((a>>1)*6+tile),unit),Mul(2,unit),unit,
            0,Add(top,Mul((float)y,unit)),border,unit,false,y>=4);
        Quad(wall,Mul((float)((b&1)*9),unit),Mul((float)((b>>1)*6+tile),unit),Mul(2,unit),unit,
            right,Add(top,Mul((float)y,unit)),border,unit,true,y>=4);}
    //884880 returns886054 for ordinary backdrops1/2. Boss-floor and other
    //backdrop effect factories cannot execute.8867f0 left the overlay sprite
    //cleared, so883e98 bypasses the overlay emitter. No global MT is consumed.
    ready=true;nextAddress=0;return true;
}
