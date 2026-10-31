//Windows gameplay target for fast iteration. It runs the same shared runtime as
//the Xbox adapter, but renders through Windows D3D9 and accepts in-process
//scripted or keyboard input without emulator focus.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <vector>
#include "../game/game_session.h"
#include "../game/native_room_view.h"
#include "../game/native_room_background.h"
#include "../game/native_door_resources.h"
#include "gameplay_atlas_data.h"

static PortFrontend gUi;
static NativeMt gRandom;
static PortNormalRuntime* gRuntime=0;
static FILE* gLog=0;
static IDirect3D9* gD3D=0;
static IDirect3DDevice9* gDevice=0;
static IDirect3DTexture9* gAtlas=0;
static IDirect3DSurface9* gSystem=0;
static HWND gWindow=0;
static const int kWidth=1280,kHeight=720;
static const float kScale=kWidth/480.0f;

static void Log(const char* fmt,...){
    char line[1024];va_list args;va_start(args,fmt);_vsnprintf(line,sizeof(line)-2,fmt,args);va_end(args);
    line[sizeof(line)-2]=0;strcat(line,"\n");fputs(line,stdout);fflush(stdout);if(gLog){fputs(line,gLog);fflush(gLog);}
}
static unsigned int LE32(const unsigned char* p){return (unsigned int)p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24);}
static bool LoadFileBytes(const char* path,unsigned char** data,unsigned int* size){
    FILE* f=fopen(path,"rb");if(!f)return false;fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    if(n<=0){fclose(f);return false;}unsigned char* p=new unsigned char[n];
    if(fread(p,1,n,f)!=(size_t)n){delete[] p;fclose(f);return false;}fclose(f);*data=p;*size=(unsigned int)n;return true;
}
static bool CreateHiddenWindow(bool hidden){
    WNDCLASSA wc;ZeroMemory(&wc,sizeof(wc));wc.lpfnWndProc=DefWindowProcA;wc.hInstance=GetModuleHandleA(0);wc.lpszClassName="Boi360WindowsTarget";
    if(!RegisterClassA(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
    gWindow=CreateWindowExA(0,"Boi360WindowsTarget","boi360 Windows target",WS_POPUP,0,0,kWidth,kHeight,0,0,wc.hInstance,0);
    if(!gWindow)return false;
    //A D3D9 window must be shown for reliable presentation. Keep automated
    //runs off-screen and non-activating so they never steal keyboard focus.
    if(hidden){SetWindowPos(gWindow,0,-32000,-32000,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);ShowWindow(gWindow,4);}
    else{ShowWindow(gWindow,SW_SHOW);UpdateWindow(gWindow);}
    return true;
}
static bool InitD3D(){
    gD3D=Direct3DCreate9(D3D_SDK_VERSION);if(!gD3D)return false;
    D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));
    pp.Windowed=TRUE;pp.BackBufferWidth=kWidth;pp.BackBufferHeight=kHeight;pp.BackBufferFormat=D3DFMT_A8R8G8B8;
    pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_COPY;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr=gD3D->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,gWindow,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&gDevice);
    if(FAILED(hr)){Log("WIN device failed %08x",hr);return false;}
    hr=gDevice->CreateOffscreenPlainSurface(kWidth,kHeight,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&gSystem,0);
    if(FAILED(hr)){Log("WIN system surface failed %08x",hr);return false;}
    D3DVIEWPORT9 vp={0,0,kWidth,kHeight,0,1};gDevice->SetViewport(&vp);
    gDevice->SetRenderState(D3DRS_LIGHTING,FALSE);gDevice->SetRenderState(D3DRS_ZENABLE,FALSE);
    gDevice->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);gDevice->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    gDevice->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);gDevice->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    gDevice->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);gDevice->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    gDevice->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);gDevice->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    gDevice->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);gDevice->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    gDevice->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);gDevice->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    Log("WIN graphics initialized %dx%d",kWidth,kHeight);return true;
}
static bool LoadAtlas(){
    unsigned char* data=0;unsigned int size=0;
    if(!LoadFileBytes("runtime/port/gameplay/atlas.bin",&data,&size)){Log("WIN atlas missing");return false;}
    if(size!=16+kGameplayAtlasWidth*kGameplayAtlasHeight*4||memcmp(data,"GPLAY1\0\0",8)!=0){delete[] data;return false;}
    HRESULT hr=gDevice->CreateTexture(kGameplayAtlasWidth,kGameplayAtlasHeight,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&gAtlas,0);
    if(FAILED(hr)){delete[] data;return false;}
    D3DLOCKED_RECT locked;hr=gAtlas->LockRect(0,&locked,0,0);
    if(FAILED(hr)){delete[] data;return false;}
    for(unsigned int y=0;y<kGameplayAtlasHeight;++y){
        DWORD* row=(DWORD*)((unsigned char*)locked.pBits+y*locked.Pitch);
        const unsigned char* p=data+16+y*kGameplayAtlasWidth*4;
        for(unsigned int x=0;x<kGameplayAtlasWidth;++x,p+=4)row[x]=((DWORD)p[3]<<24)|((DWORD)p[0]<<16)|((DWORD)p[1]<<8)|p[2];
    }
    gAtlas->UnlockRect(0);delete[] data;D3DSURFACE_DESC ad;gAtlas->GetLevelDesc(0,&ad);Log("WIN gameplay atlas loaded %ux%u",ad.Width,ad.Height);return true;
}
struct Vertex{float x,y,z,rhw;DWORD color;float u,v;};
static const DWORD kFVF=D3DFVF_XYZRHW|D3DFVF_TEX1|D3DFVF_DIFFUSE;
static float Bits(unsigned int u){float f;memcpy(&f,&u,4);return f;}
static void TexQuad(const float* xy,const float* uv,float texW,float texH,DWORD color){
    const int order[6]={0,1,3,0,3,2};Vertex v[6];
    for(int i=0;i<6;++i){int j=order[i];v[i].x=xy[j*2]*kScale;v[i].y=xy[j*2+1]*kScale;v[i].z=0;v[i].rhw=1;
        v[i].u=uv[j*2]/texW;v[i].v=uv[j*2+1]/texH;v[i].color=color;}
    gDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,v,sizeof(Vertex));
}
static void TexRect(float x,float y,float w,float h,float sx,float sy,float sw,float sh){
    const float xy[8]={x,y,x+w,y,x,y+h,x+w,y+h};
    const float uv[8]={sx,sy,sx+sw,sy,sx,sy+sh,sx+sw,sy+sh};
    TexQuad(xy,uv,(float)kGameplayAtlasWidth,(float)kGameplayAtlasHeight,0xffffffff);
}
static bool BackdropAtlasRect(int id,int* ox,int* oy){
    if(id==1){*ox=kGameplayBackdrop1X;*oy=kGameplayBackdrop1Y;return true;}
    if(id==2){*ox=kGameplayBackdrop2X;*oy=kGameplayBackdrop2Y;return true;}
    if(id==4){*ox=kGameplayBackdrop4X;*oy=kGameplayBackdrop4Y;return true;}
    if(id==5){*ox=kGameplayBackdrop5X;*oy=kGameplayBackdrop5Y;return true;}
    if(id==7){*ox=kGameplayBackdrop7X;*oy=kGameplayBackdrop7Y;return true;}
    if(id==8){*ox=kGameplayBackdrop8X;*oy=kGameplayBackdrop8Y;return true;}
    if(id==10){*ox=kGameplayBackdrop10X;*oy=kGameplayBackdrop10Y;return true;}
    if(id==11){*ox=kGameplayBackdrop11X;*oy=kGameplayBackdrop11Y;return true;}
    if(id==14){*ox=kGameplayBackdrop14X;*oy=kGameplayBackdrop14Y;return true;}
    if(id==15){*ox=kGameplayBackdrop15X;*oy=kGameplayBackdrop15Y;return true;}
    if(id==16){*ox=kGameplayBackdrop16X;*oy=kGameplayBackdrop16Y;return true;}
    if(id==17){*ox=kGameplayBackdrop17X;*oy=kGameplayBackdrop17Y;return true;}
    return false;
}
static void DrawBackground(){
    NativeWorldResident& live=gRuntime->world.resident[gRuntime->world.currentList];
    if(!live.view)live.view=new NativeRoomView;
    if(live.room&&!live.view->background.ready){const NativePlacedRoom* d=gRuntime->world.CurrentDescriptor();if(d)live.view->background.Prepare(*live.room,live.backdrop,d->seeds.spawn);}
    if(!live.view->background.ready||!live.view->background.definition){static bool nl=false;if(!nl){nl=true;const NativePlacedRoom* d=gRuntime->world.CurrentDescriptor();
        Log("WIN bg not ready=%d def=%d definition=%d type=%d",live.view->background.ready?1:0,live.view->background.definition?1:0,d?d->definition:-1,live.room?live.room->type:-1);}return;}
    const NativeRoomBackground& bg=live.view->background;int ox,oy;if(!BackdropAtlasRect(bg.definition->id,&ox,&oy)){static bool bl=false;if(!bl){bl=true;Log("WIN bg unsupported id=%d",bg.definition->id);}return;}
    for(unsigned int pass=0;pass<2;++pass){const std::vector<NativeBackgroundQuad>& list=pass?bg.wall:bg.floor;
        for(unsigned int i=0;i<list.size();++i){const NativeBackgroundQuad& q=list[i];float xy[8],uv[8];
            for(int k=0;k<4;++k){xy[k*2]=q.destination[k].x;xy[k*2+1]=q.destination[k].y;uv[k*2]=q.source[k].x+ox;uv[k*2+1]=q.source[k].y+oy;}
            TexQuad(xy,uv,(float)kGameplayAtlasWidth,(float)kGameplayAtlasHeight,0xffffffff);}}
}
static void DrawDoors(){
    NativeWorldResident& live=gRuntime->world.resident[gRuntime->world.currentList];
    if(!live.room||!live.doors||!live.view||!live.view->background.ready)return;
    if(!live.doorVisuals)gRuntime->world.PrepareDoorVisuals(gRuntime->run.context.stage,gRuntime->run.context.stageType,
        gRuntime->run.context.nativeMode,gRuntime->run.context.challenge,gRuntime->playerState);
    if(!live.doorVisuals)return;
    const NativeRoomBackground& bg=live.view->background;const NativeRoomSpace& space=live.room->space;
    float rw=bg.right-bg.left,rh=bg.bottom-bg.top,ww=space.right-space.left,wh=space.bottom-space.top;if(ww<=0||wh<=0)return;
    for(int slot=0;slot<8;++slot){if(!(live.doors->attached&(1U<<slot)))continue;NativeDoorVisual& v=live.doorVisuals[slot];
        if(!v.ready||!v.main.asset||v.main.layerSheets.empty()||strstr(v.main.layerSheets[0].path.c_str(),"door_01_normaldoor")==0)continue;
        float wx,wy;if(!live.room->geometry.Position(live.doors->actors[slot].grid,&wx,&wy))continue;
        float px=bg.left+(wx+20-space.left)*rw/ww,py=bg.top+(wy+20-space.top)*rh/wh;
        for(unsigned int i=0;i<v.main.asset->layerCount;++i){const NativeSpriteFrame* f=v.main.LayerFrame(v.main.asset->layers[i].id);
            if(!f||!f->visible)continue;float cx=Bits(f->geometry[0]),cy=Bits(f->geometry[1]),w=Bits(f->geometry[2]),h=Bits(f->geometry[3]);
            float ox=Bits(f->geometry[4]),oy=Bits(f->geometry[5]),sx=Bits(f->geometry[6]),sy=Bits(f->geometry[7]);
            float pvx=Bits(f->geometry[8]),pvy=Bits(f->geometry[9]);if(w<=0||h<=0)continue;if(sx==0||sy==0)sx=sy=1;
            float x=px+(ox-pvx)*fabsf(sx),y=py+(oy-pvy)*fabsf(sy);
            TexRect(x,y,w*fabsf(sx),h*fabsf(sy),cx+kGameplayDoornormalX,cy+kGameplayDoornormalY,w,h);}
    }
}
static void DrawRocks(){
    NativeWorldResident& live=gRuntime->world.resident[gRuntime->world.currentList];
    if(!live.room||!live.view||!live.view->background.ready)return;
    const NativeRoomBackground& bg=live.view->background;const NativeRoomSpace& space=live.room->space;
    float rw=bg.right-bg.left,rh=bg.bottom-bg.top,ww=space.right-space.left,wh=space.bottom-space.top;if(ww<=0||wh<=0)return;
    float cw=40*rw/ww,ch=40*rh/wh;
    for(int i=0;i<live.room->staticGridCount;++i){const NativeStaticGrid& g=live.room->staticGrids[i];
        if(g.type!=1000&&g.type!=1001&&g.type!=1002&&g.type!=1008&&g.type!=1010&&g.type!=1400&&g.type!=1500)continue;float wx,wy;
        if(!live.room->geometry.Position(g.grid,&wx,&wy))continue;float px=bg.left+(wx-space.left)*rw/ww,py=bg.top+(wy-space.top)*rh/wh;
        int sx=kGameplayRocksbasementX,sy=kGameplayRocksbasementY,cx=0,cy=0;
        if(g.type==1002)cy=32;else if(g.type==1400){sx=kGameplayFireplacesheetX;sy=kGameplayFireplacesheetY;}
        else if(g.type==1500){sx=kGameplayPoopsheetX;sy=kGameplayPoopsheetY;}else cx=(g.variant&3)*32;
        TexRect(px,py,cw,ch,(float)(sx+cx),(float)(sy+cy),32,32);}
}
static void DrawPickups(){
    NativeWorldResident& live=gRuntime->world.resident[gRuntime->world.currentList];
    if(!live.room||!live.view||!live.view->background.ready)return;
    const NativeRoomBackground& bg=live.view->background;const NativeRoomSpace& space=live.room->space;
    float rw=bg.right-bg.left,rh=bg.bottom-bg.top,ww=space.right-space.left,wh=space.bottom-space.top;if(ww<=0||wh<=0)return;
    for(int i=0;i<live.pickups.count;++i){NativePickup* p=live.pickups.entries[i];if(!p||!p->ready||p->collected)continue;
        int sx=0,sy=0;if(p->variant==10){sx=kGameplayPickupheartX;sy=kGameplayPickupheartY;}
        else if(p->variant==20){sx=kGameplayPickupcoinX;sy=kGameplayPickupcoinY;}
        else if(p->variant==30){sx=kGameplayPickupkeyX;sy=kGameplayPickupkeyY;}
        else if(p->variant==40){sx=kGameplayPickupbombX;sy=kGameplayPickupbombY;}else continue;
        float px=bg.left+(p->position.x-space.left)*rw/ww,py=bg.top+(p->position.y-space.top)*rh/wh;
        TexRect(px-8,py-8,16,16,(float)sx,(float)sy,32,32);}
}
static bool EnemyWalkRect(int type,int variant,int* sx,int* sy,int* frames,int* fw,int* fh){
    for(unsigned int i=0;i<sizeof(kGameplayEnemySprites)/sizeof(kGameplayEnemySprites[0]);++i){
        const GameplayEnemySprite& s=kGameplayEnemySprites[i];
        if(s.type!=type||s.variant!=variant)continue;
        *sx=s.x;*sy=s.y;*frames=s.frames;*fw=s.frameW;*fh=s.frameH;return true;
    }
    return false;
}
static void DrawTrapdoor(){
    NativeWorldResident& live=gRuntime->world.resident[gRuntime->world.currentList];
    if(!live.room||!live.view||!live.view->background.ready)return;
    NativeRoomState* room=live.room;if(!room||room->trapdoorState==0)return;
    const NativeRoomBackground& bg=live.view->background;const NativeRoomSpace& space=room->space;
    float rw=bg.right-bg.left,rh=bg.bottom-bg.top,ww=space.right-space.left,wh=space.bottom-space.top;if(ww<=0||wh<=0)return;
    int frame=room->trapdoorState==1?room->trapdoorTimer/5:5;if(frame<0)frame=0;if(frame>5)frame=5;
    float px=bg.left+(room->trapdoorX-space.left)*rw/ww,py=bg.top+(room->trapdoorY-space.top)*rh/wh;
    TexRect(px-32,py-32,64,64,(float)(kGameplayTrapdooropenX+frame*64),(float)kGameplayTrapdooropenY,64,64);
}
static void DrawFamiliars(){
    NativeWorldResident& live=gRuntime->world.resident[gRuntime->world.currentList];
    if(!live.room||!live.view||!live.view->background.ready)return;
    const NativeRoomBackground& bg=live.view->background;const NativeRoomSpace& space=live.room->space;
    float rw=bg.right-bg.left,rh=bg.bottom-bg.top,ww=space.right-space.left,wh=space.bottom-space.top;if(ww<=0||wh<=0)return;
    for(unsigned int i=0;i<gRuntime->familiars.active.size();++i){
        const NativeFamiliar& f=gRuntime->familiars.active[i];
        int sx=0,sy=0,frames=0;
        if(f.item==8){sx=kGameplayBrotherbobbywalkX;sy=kGameplayBrotherbobbywalkY;frames=kGameplayBrotherbobbywalkFrames;}
        else if(f.item==10){sx=kGameplayFlywalkX;sy=kGameplayFlywalkY;frames=kGameplayFlywalkFrames;}
        else if(f.item==117){sx=kGameplayDeadbirdwalkX;sy=kGameplayDeadbirdwalkY;frames=kGameplayDeadbirdwalkFrames;}
        else continue;
        if(frames<=0)continue;
        int frame=((int)(f.time/6))%frames;
        float px=bg.left+(f.position.x-space.left)*rw/ww,py=bg.top+(f.position.y-space.top)*rh/wh;
        TexRect(px-32,py-48,64,64,(float)(sx+frame*64),(float)sy,64,64);
    }
}static void DrawEnemies(){
    NativeWorldResident& live=gRuntime->world.resident[gRuntime->world.currentList];
    if(!live.room||!live.view||!live.view->background.ready)return;
    const NativeRoomBackground& bg=live.view->background;const NativeRoomSpace& space=live.room->space;
    float rw=bg.right-bg.left,rh=bg.bottom-bg.top,ww=space.right-space.left,wh=space.bottom-space.top;if(ww<=0||wh<=0)return;
    for(int i=0;i<live.enemies.count;++i){NativeEnemy* e=live.enemies.entries[i];if(!e||!e->ready||e->dead)continue;
        int sx=0,sy=0,frames=0,fw=64,fh=64;if(!EnemyWalkRect(e->type,e->body.variant,&sx,&sy,&frames,&fw,&fh)||frames<=0)continue;
        int frame=((int)(e->body.time/6))%frames;
        float px=bg.left+(e->body.position.x-space.left)*rw/ww,py=bg.top+(e->body.position.y-space.top)*rh/wh;
        TexRect(px-fw*0.5f,py-fh*0.75f,(float)fw,(float)fh,(float)(sx+frame*fw),(float)sy,(float)fw,(float)fh);}
}
static void DrawPlayer(){
    if(gRuntime->playerBody.removed||!gRuntime->playerSprites.bodyReady)return;
    NativeWorldResident& live=gRuntime->world.resident[gRuntime->world.currentList];if(!live.room||!live.view||!live.view->background.ready)return;
    NativeSprite& sprite=gRuntime->playerSprites.sprites[0];if(sprite.layerSheets.size()<=1)return;
    if(strstr(sprite.layerSheets[1].path.c_str(),"character_001_isaac")==0)return;
    const NativeRoomBackground& bg=live.view->background;const NativeRoomSpace& space=live.room->space;
    float rw=bg.right-bg.left,rh=bg.bottom-bg.top,ww=space.right-space.left,wh=space.bottom-space.top;if(ww<=0||wh<=0)return;
    float px=bg.left+(gRuntime->playerBody.position.x-space.left)*rw/ww,py=bg.top+(gRuntime->playerBody.position.y-space.top)*rh/wh;
    const NativeSpriteFrame* frames[2]={sprite.LayerFrame(1),sprite.LayerFrame(4,true)};
    for(int i=0;i<2;++i){const NativeSpriteFrame* f=frames[i];if(!f||!f->visible)continue;
        float cx=Bits(f->geometry[0]),cy=Bits(f->geometry[1]),w=Bits(f->geometry[2]),h=Bits(f->geometry[3]);
        float ox=Bits(f->geometry[4]),oy=Bits(f->geometry[5]),sx=Bits(f->geometry[6]),sy=Bits(f->geometry[7]);
        float pvx=Bits(f->geometry[8]),pvy=Bits(f->geometry[9]);if(w<=0||h<=0)continue;if(sx==0||sy==0)sx=sy=1;
        float x=px+(ox-pvx)*fabsf(sx),y=py+(oy-pvy)*fabsf(sy);
        TexRect(x,y,w*fabsf(sx),h*fabsf(sy),cx+kGameplayPlayerX,cy+kGameplayPlayerY,w,h);}
}
static void DrawProjectiles(){
    NativeWorldResident& live=gRuntime->world.resident[gRuntime->world.currentList];if(!live.room||!live.view||!live.view->background.ready)return;
    const NativeRoomBackground& bg=live.view->background;const NativeRoomSpace& space=live.room->space;
    float rw=bg.right-bg.left,rh=bg.bottom-bg.top,ww=space.right-space.left,wh=space.bottom-space.top;if(ww<=0||wh<=0)return;
    for(unsigned int i=0;i<gRuntime->projectiles.active.size();++i){const NativeProjectile& p=gRuntime->projectiles.active[i];
        float px=bg.left+(p.position.x-space.left)*rw/ww,py=bg.top+(p.position.y-space.top)*rh/wh;
        TexRect(px-16,py-16,32,32,(float)kGameplayTearsX,(float)kGameplayTearsY,32,32);}
    for(unsigned int i=0;i<gRuntime->enemyShots.active.size();++i){const NativeEnemyShot& p=gRuntime->enemyShots.active[i];
        float px=bg.left+(p.position.x-space.left)*rw/ww,py=bg.top+(p.position.y-space.top)*rh/wh;
        TexRect(px-14,py-14,28,28,(float)(kGameplayTearsX+128),(float)(kGameplayTearsY+64),32,32);}
}
static void DrawHud(){
    if(!gRuntime->playerState.ready)return;float x=10,y=10;
    int red=gRuntime->playerState.redHearts,containers=gRuntime->playerState.maxRedHearts/2;
    for(int i=0;i<containers;++i){float sx=32,sy=0;if(red>=2*(i+1))sx=0;else if(red==2*i+1)sx=16;
        TexRect(x,y,16,16,sx+kGameplayHeartsX,sy+kGameplayHeartsY,16,16);x+=16;}
    int soul=gRuntime->playerState.soulHearts;for(int i=0;i<soul/2;++i){TexRect(x,y,16,16,(float)kGameplayHeartsX,(float)(kGameplayHeartsY+16),16,16);x+=16;}
    if(soul&1)TexRect(x,y,16,16,(float)(kGameplayHeartsX+16),(float)(kGameplayHeartsY+16),16,16);
}
static void Render(){
    IDirect3DSurface9* back=0;if(gDevice->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back)!=D3D_OK)return;
    gDevice->SetRenderTarget(0,back);back->Release();
    gDevice->Clear(0,0,D3DCLEAR_TARGET,0xff000000,1,0);
    gDevice->BeginScene();
    gDevice->SetFVF(kFVF);
    gDevice->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);gDevice->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    gDevice->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);gDevice->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
    gDevice->SetTexture(0,gAtlas);
    DrawBackground();DrawDoors();DrawRocks();DrawPickups();DrawTrapdoor();DrawEnemies();DrawFamiliars();DrawPlayer();DrawProjectiles();DrawHud();
    gDevice->EndScene();
}
static bool SaveBMP(const char* path){
    IDirect3DSurface9* targetSurface=0;if(gDevice->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&targetSurface)!=D3D_OK)return false;
    HRESULT copy=gDevice->GetRenderTargetData(targetSurface,gSystem);targetSurface->Release();
    if(copy!=D3D_OK)return false;
    D3DLOCKED_RECT lr;if(gSystem->LockRect(&lr,0,D3DLOCK_READONLY)!=D3D_OK)return false;
    FILE* f=fopen(path,"wb");if(!f){gSystem->UnlockRect();return false;}
    BITMAPFILEHEADER bf;BITMAPINFOHEADER bi;ZeroMemory(&bf,sizeof(bf));ZeroMemory(&bi,sizeof(bi));
    bf.bfType=0x4d42;bf.bfOffBits=sizeof(bf)+sizeof(bi);bf.bfSize=bf.bfOffBits+kWidth*kHeight*4;
    bi.biSize=sizeof(bi);bi.biWidth=kWidth;bi.biHeight=kHeight;bi.biPlanes=1;bi.biBitCount=32;bi.biCompression=BI_RGB;bi.biSizeImage=kWidth*kHeight*4;
    fwrite(&bf,1,sizeof(bf),f);fwrite(&bi,1,sizeof(bi),f);
    for(int y=kHeight-1;y>=0;--y)fwrite((unsigned char*)lr.pBits+y*lr.Pitch,1,kWidth*4,f);
    fclose(f);gSystem->UnlockRect();return true;
}
static bool SaveWindowBMP(const char* path){
    RECT rc;if(!GetClientRect(gWindow,&rc))return false;int w=rc.right-rc.left,h=rc.bottom-rc.top;if(w<=0||h<=0)return false;
    HDC dc=GetDC(gWindow);if(!dc)return false;HDC mem=CreateCompatibleDC(dc);HBITMAP bmp=CreateCompatibleBitmap(dc,w,h);
    if(!mem||!bmp){if(mem)DeleteDC(mem);if(bmp)DeleteObject(bmp);ReleaseDC(gWindow,dc);return false;}
    HGDIOBJ old=SelectObject(mem,bmp);BOOL ok=PrintWindow(gWindow,mem,3);SelectObject(mem,old);
    BITMAPINFO info;ZeroMemory(&info,sizeof(info));info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    unsigned char* pixels=new unsigned char[w*h*4];bool got=ok&&GetDIBits(mem,bmp,0,h,pixels,&info,DIB_RGB_COLORS)!=0;
    if(got){FILE* f=fopen(path,"wb");if(f){BITMAPFILEHEADER bf;ZeroMemory(&bf,sizeof(bf));bf.bfType=0x4d42;bf.bfOffBits=sizeof(bf)+sizeof(BITMAPINFOHEADER);bf.bfSize=bf.bfOffBits+w*h*4;
        fwrite(&bf,1,sizeof(bf),f);fwrite(&info.bmiHeader,1,sizeof(BITMAPINFOHEADER),f);fwrite(pixels,1,w*h*4,f);fclose(f);}else got=false;}
    delete[] pixels;DeleteObject(bmp);DeleteDC(mem);ReleaseDC(gWindow,dc);return got;
}
struct Segment{int first,last;bool held[12];Segment(){first=last=0;memset(held,0,sizeof(held));}};
static bool ParseScript(const char* text,std::vector<Segment>& out){
    const char* p=text;while(p&&*p){int a=atoi(p);while(*p&&*p!=':')++p;if(*p==':')++p;int b=atoi(p);while(*p&&*p!=':')++p;if(*p==':')++p;
        Segment s;s.first=a;s.last=b;while(*p&&*p!=';'){char c=*p++;
            if(c=='W'||c=='w')s.held[2]=true;else if(c=='A'||c=='a')s.held[0]=true;else if(c=='S'||c=='s')s.held[3]=true;else if(c=='D'||c=='d')s.held[1]=true;
            else if(c=='J'||c=='j')s.held[4]=true;else if(c=='L'||c=='l')s.held[5]=true;else if(c=='I'||c=='i')s.held[6]=true;else if(c=='K'||c=='k')s.held[7]=true;
            else if(c=='X'||c=='x')s.held[8]=true;else if(c=='Z'||c=='z')s.held[9]=true;else if(c=='C'||c=='c')s.held[10]=true;else if(c=='V'||c=='v')s.held[11]=true;}
        out.push_back(s);if(*p==';')++p;}
    return true;
}
static bool gPrev[12]={false,false,false,false,false,false,false,false,false,false,false,false};
static void ReadHeld(int frame,const std::vector<Segment>& script,bool interactive,bool* held){
    for(int i=0;i<12;++i)held[i]=false;
    if(interactive){held[0]=GetAsyncKeyState('A')<0;held[1]=GetAsyncKeyState('D')<0;held[2]=GetAsyncKeyState('W')<0;held[3]=GetAsyncKeyState('S')<0;
        held[4]=GetAsyncKeyState(VK_LEFT)<0;held[5]=GetAsyncKeyState(VK_RIGHT)<0;held[6]=GetAsyncKeyState(VK_UP)<0;held[7]=GetAsyncKeyState(VK_DOWN)<0;
        held[8]=GetAsyncKeyState('X')<0;held[9]=GetAsyncKeyState('Z')<0;held[10]=GetAsyncKeyState('C')<0;held[11]=GetAsyncKeyState('V')<0;}
    for(unsigned int i=0;i<script.size();++i)if(frame>=script[i].first&&frame<script[i].last)for(int k=0;k<12;++k)held[k]=held[k]||script[i].held[k];
}
static void BuildActions(const bool* held,unsigned int frame,NativeActionFrame* af,NativeFrameActions* source){
    af->Clear(gRuntime->playerCreation.native153c,gRuntime->playerBody.entityToken,frame);af->SupplyConnection(true);af->SupplyDrop(held[11]);
    for(int i=0;i<12;++i){af->SupplyHeld(i,held[i]);af->SupplyPressed(i,held[i]&&!gPrev[i]);}
    float v[8]={held[0]?1.0f:0.0f,held[1]?1.0f:0.0f,held[2]?1.0f:0.0f,held[3]?1.0f:0.0f,
        held[4]?1.0f:0.0f,held[5]?1.0f:0.0f,held[6]?1.0f:0.0f,held[7]?1.0f:0.0f};
    for(int i=0;i<8;++i)af->SupplyValue(i,v[i]);
    for(int i=0;i<12;++i)gPrev[i]=held[i];
}
int main(int argc,char** argv){
    unsigned int seed=1;int frames=0,shotFrame=-1,descendTimes=0;bool hidden=false,fast=false,interactive=true,teleport=false,rewardTest=false,windowShot=false,layoutDump=false,bossEnter=false,invulnerable=false,continueRun=false,saveOnExit=false;const char* scriptText=0;const char* shotFile="build/windows/frame.bmp";const char* continueFile="build/windows/continue.dat";const char* nativeContinueFile="build/windows/profile/gamestate1.dat";
    for(int i=1;i<argc;++i){if(!strcmp(argv[i],"--seed")&&i+1<argc)seed=(unsigned int)strtoul(argv[++i],0,10);
        else if(!strcmp(argv[i],"--frames")&&i+1<argc)frames=atoi(argv[++i]);else if(!strcmp(argv[i],"--hidden"))hidden=true;
        else if(!strcmp(argv[i],"--fast"))fast=true;else if(!strcmp(argv[i],"--script")&&i+1<argc){scriptText=argv[++i];interactive=false;}
        else if(!strcmp(argv[i],"--shot-frame")&&i+1<argc)shotFrame=atoi(argv[++i]);else if(!strcmp(argv[i],"--shot-file")&&i+1<argc)shotFile=argv[++i];
        else if(!strcmp(argv[i],"--teleport-pickups"))teleport=true;else if(!strcmp(argv[i],"--reward-test"))rewardTest=true;
        else if(!strcmp(argv[i],"--window-shot"))windowShot=true;else if(!strcmp(argv[i],"--layout"))layoutDump=true;else if(!strcmp(argv[i],"--boss"))bossEnter=true;
        else if(!strcmp(argv[i],"--invulnerable"))invulnerable=true;else if(!strcmp(argv[i],"--continue"))continueRun=true;else if(!strcmp(argv[i],"--save"))saveOnExit=true;
        else if(!strcmp(argv[i],"--continue-file")&&i+1<argc)continueFile=argv[++i];
        else if(!strcmp(argv[i],"--descend")&&i+1<argc)descendTimes=atoi(argv[++i]);}
    CreateDirectoryA("build\\windows",0);CreateDirectoryA("build\\windows\\profile",0);gLog=fopen("build/windows/port.log","wt");
    std::vector<Segment> script;if(scriptText)ParseScript(scriptText,script);
    if(!CreateHiddenWindow(hidden)||!InitD3D())return 2;
    LoadAtlas();gUi.Init(false);gUi.BindSaveDirectory("build/windows/profile/");memset(&gUi.profile,0,sizeof(gUi.profile));gUi.characterPage=0;gUi.characterChoice=1;gUi.characterMode=0;
    gRandom.Seed(seed?seed:1);PortNormalRuntime runtime;gRuntime=&runtime;PortGameSession session(gRandom,gUi);
    if(!gUi.music.StartCharacter(gUi.characterPage)){Log("WIN character music stinger failed");return 5;}
    PortSessionStart start=session.StartNormalCharacter(seed?seed:1,runtime);
    Log("WIN start result=%d seed=%u player=%d",start.result,seed,start.player);if(start.result!=NPS_WORLD_PENDING)return 3;
    NativeSceneFiles files={"runtime/game/file_index.bin","runtime/game/",NativeStageBackdrop(runtime.run.context.stage),runtime.run.context.nativeMode,0};
    int setup=0;bool held[12];NativeActionFrame af;NativeFrameActions fa(af);
    for(int i=0;i<12;++i)held[i]=false;
    //One setup call runs the complete reached boundary; continue only while
    //the flow itself still owns generation/room-component phases.
    while(runtime.phase==PNR_GENERATING||runtime.phase==PNR_ROOM_COMPONENTS){
        BuildActions(held,(unsigned int)setup,&af,&fa);
        NormalRunResult r=session.AdvanceNormalSetup(runtime,files,&fa);++setup;
        if(r==NFR_BAD_REQUEST){Log("WIN setup rejected prerequisite=%08x",runtime.missingNativePrerequisite);break;}}
    Log("WIN setup phase=%d steps=%d resident=%d prerequisite=%08x",runtime.phase,setup,runtime.world.residentCount,runtime.missingNativePrerequisite);
    if(runtime.phase!=PNR_READY)return 4;
    for(int d=0;d<descendTimes;++d){
        if(!session.DescendNormal(runtime,files,&fa)){Log("WIN descend test failed at %d prerequisite=%08x",d,runtime.missingNativePrerequisite);break;}
        Log("WIN descend test stage=%d room=%d backdrop=%d",runtime.run.context.stage,runtime.world.currentList,files.backdrop);
    }
    if(bossEnter){int grid=-1,dimension=0;const NativePlacedRoom* target=0;
        for(int i=0;i<runtime.world.layout.count;++i){const NativePlacedRoom& p=runtime.world.layout.rooms[i];const NativeRoomDef* d=NativeRoom(p.definition);
            if(d&&d->type==5){grid=p.gridIndex;dimension=p.dimension;target=&p;break;}}
        if(grid>=0){NativeWorldIssue issue;NativeWorldResult result=runtime.world.Enter(grid,dimension,files,&issue,0);
            NativeRoomState* entered=runtime.world.Current();
            if(entered){runtime.playerBody.position.x=(entered->space.left+entered->space.right)*0.5f;
                runtime.playerBody.position.y=(entered->space.top+entered->space.bottom)*0.5f;
                runtime.playerBody.previousPosition=runtime.playerBody.position;runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;}
            NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
            Log("WIN boss enter result=%d grid=%d dim=%d mode=%d def=%d enemies=%d src=%d stage=%d type=%u",result,grid,dimension,files.nativeMode,target?target->definition:-1,live.enemies.count,issue.source,issue.construction.stage,issue.construction.type);}}
    if(layoutDump){const NativeLevelLayout& L=runtime.world.layout;for(int i=0;i<L.count;++i){const NativePlacedRoom& p=L.rooms[i];const NativeRoomDef* d=NativeRoom(p.definition);
        Log("WIN layout list=%d grid=%d def=%d source=%d ordinal=%d type=%d variant=%u path=%s",p.listIndex,p.gridIndex,p.definition,d?d->source:-1,d?d->ordinal:-1,d?d->type:-1,d?d->variant:0,d&&NativeRoomSource(d->source)?NativeRoomSource(d->source):"?");}return 0;}
    LARGE_INTEGER freq,last;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&last);
    int frame=0;for(;;){if(frames>0&&frame>=frames)break;
        MSG msg;while(PeekMessage(&msg,0,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);}
        unsigned int tickFrame=(unsigned int)frame;
        ReadHeld(frame,script,interactive,held);BuildActions(held,tickFrame,&af,&fa);
        if(rewardTest&&frame>=20&&frame<24){NativeWorldResident& tr=runtime.world.resident[runtime.world.currentList];
            int variant=frame==20?10:(frame==21?20:(frame==22?30:40));
            if(tr.pickups.count<64){NativePickup* p=new NativePickup;NativePoint at=runtime.playerBody.position;
                if(p->Begin(5,variant,0,0,1,0,at)){tr.pickups.entries[tr.pickups.count++]=p;Log("WIN reward-test inject variant=%d",variant);}else delete p;}}
        if(teleport){NativeWorldResident& tr=runtime.world.resident[runtime.world.currentList];
            int index=(frame>=70&&frame<80)?0:((frame>=100&&frame<110)?1:-1);
            if(index>=0&&index<tr.pickups.count&&tr.pickups.entries[index]){
                runtime.playerBody.position=tr.pickups.entries[index]->position;runtime.playerBody.previousPosition=runtime.playerBody.position;
                runtime.playerBody.velocity.x=runtime.playerBody.velocity.y=0;
                Log("WIN teleport frame=%d pickup=%d variant=%d",frame,index,tr.pickups.entries[index]->variant);}}
        NormalRunResult tickResult=NFR_PENDING;
        if(runtime.phase==PNR_READY||runtime.phase==PNR_DYING||runtime.phase==PNR_GAME_OVER)tickResult=session.TickNormal(runtime,files,&fa);
        if(tickResult!=NFR_OK){static unsigned int lastPrerequisite=0;if(runtime.missingNativePrerequisite&&runtime.missingNativePrerequisite!=lastPrerequisite){lastPrerequisite=runtime.missingNativePrerequisite;Log("WIN tick result=%d prerequisite=%08x room=%d type=%u variant=%u",tickResult,runtime.missingNativePrerequisite,runtime.world.currentList,runtime.entryIssue.construction.type,runtime.entryIssue.construction.variant);}}
        if(invulnerable&&runtime.phase==PNR_READY)runtime.playerInvulnerable=30;
        if(runtime.descendRequested&&runtime.phase==PNR_READY){
            if(session.DescendNormal(runtime,files,&fa))
                Log("WIN descend stage=%d room=%d def=%d",runtime.run.context.stage,runtime.world.currentList,runtime.world.CurrentDescriptor()?runtime.world.CurrentDescriptor()->definition:-1);
            else{runtime.descendRequested=false;Log("WIN descend failed stage=%d prerequisite=%08x",runtime.run.context.stage,runtime.missingNativePrerequisite);}
        }
        if(frame%30==0){NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];const NativePlacedRoom* d=runtime.world.CurrentDescriptor();
            static int loggedDoorList=-1;
            if(loggedDoorList!=runtime.world.currentList&&live.doors){loggedDoorList=runtime.world.currentList;
                for(int slot=0;slot<8;++slot)if(live.doors->attached&(1U<<slot)){
                    const NativeDoorActor& door=live.doors->actors[slot];const NativePlacedRoom* target=runtime.world.layout.DoorTarget(runtime.world.currentList,slot);
                    float wx=0,wy=0;if(live.room)live.room->geometry.Position(door.grid,&wx,&wy);
                    Log("WIN door slot=%d grid=%d pos=(%.1f,%.1f) state=%d targetType=%d definition=%d failSlot=%d failKind=%d",slot,door.grid,wx,wy,door.state,door.targetType,target?target->definition:-1,live.doorFailureSlot,live.doorFailureKind);}}
            Log("WIN tick=%d phase=%d stage=%d room=%d list=%d act=%u pos=(%.2f,%.2f) hp=%d/%d soul=%d enemies=%d shots=%u fired=%u wall=%u hits=%u pickups=%d collected=%u coins=%d keys=%d bombs=%d transitions=%u boss=%d clear=%d doors=%d items=%u lastItem=%d bombsActive=%u explosions=%u",
                frame,runtime.phase,runtime.run.context.stage,d?d->definition:-1,runtime.world.currentList,live.activations,runtime.playerBody.position.x,runtime.playerBody.position.y,runtime.playerState.redHearts,runtime.playerState.maxRedHearts,runtime.playerState.soulHearts,
                live.enemies.count,(unsigned int)runtime.enemyShots.active.size(),(unsigned int)runtime.enemyShots.spawned,(unsigned int)runtime.enemyShots.wallHits,runtime.playerHits,live.pickups.count,runtime.pickupsCollected,runtime.playerState.coins,runtime.playerState.keys,runtime.playerState.bombs,runtime.roomTransitions,runtime.bossDefeated?1:0,live.room?live.room->clear:0,live.doors?live.doors->attached:0,runtime.collectiblesCollected,runtime.lastCollectible,(unsigned int)runtime.bombs.active.size(),runtime.bombs.explosions);}
        Render();
        if(frame==shotFrame){bool ok=windowShot?SaveWindowBMP(shotFile):SaveBMP(shotFile);Log("WIN screenshot frame=%d file=%s ok=%d",frame,shotFile,ok?1:0);}
        gDevice->Present(0,0,0,0);
        ++frame;
        if(!fast)Sleep(33);else Sleep(0);
    }
    if(saveOnExit&&runtime.phase==PNR_READY)Log("WIN save result=%d file=%s",session.SaveContinue(continueFile,runtime)?1:0,continueFile);
    Log("WIN done frames=%d phase=%d",frame,runtime.phase);if(gLog)fclose(gLog);return 0;
}
