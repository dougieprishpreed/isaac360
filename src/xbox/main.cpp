#include <xtl.h>
#include <d3dx9.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "../content/game_content.h"
#include "../game/game_session.h"
#include "../game/native_room_view.h"
#include "../game/native_room_background.h"
#include "../game/native_door_resources.h"
#include "frontend_assets.h"
#include "gameplay_atlas_data.h"
#include "sprite_vs.h"
#include "sprite_ps.h"
static FILE* gTrace=0;
static IDirect3DDevice9* gDevice=0;
static IDirect3DTexture9* gAtlas=0;
static IDirect3DVertexShader9* gVS=0;
static IDirect3DPixelShader9* gPS=0;
static IDirect3DVertexDeclaration9* gDecl=0;
static PortFrontend gUi;
static NativeMt gMenuRandom;
static PortNormalRuntime gNormalRuntime;
static NativeProfile gProfiles[3];
static bool gPrevActions[12]={false,false,false,false,false,false,false,false,false,false,false,false};
static bool PreviousAction(int action){return action>=0&&action<12?gPrevActions[action]:false;}
static float Axis(float value){const float dead=0.25f;float v=value/32767.0f;return fabsf(v)<dead?0.0f:v;}
static void BuildActions(const XINPUT_GAMEPAD& p,unsigned int frameNumber,NativeActionFrame* frame){
    frame->Clear(gNormalRuntime.playerCreation.native153c,gNormalRuntime.playerBody.entityToken,frameNumber);
    frame->SupplyConnection(true);
    const bool held[12]={
        p.sThumbLX< -8000,p.sThumbLX>8000,p.sThumbLY>8000,p.sThumbLY< -8000,
        p.sThumbRX< -8000,p.sThumbRX>8000,p.sThumbRY>8000,p.sThumbRY< -8000,
        (p.wButtons&XINPUT_GAMEPAD_X)!=0,(p.wButtons&XINPUT_GAMEPAD_A)!=0,
        (p.wButtons&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0,(p.wButtons&XINPUT_GAMEPAD_B)!=0};
    const float values[8]={Axis(p.sThumbLX)<0?-Axis(p.sThumbLX):0,Axis(p.sThumbLX)>0?Axis(p.sThumbLX):0,
        Axis(p.sThumbLY)>0?Axis(p.sThumbLY):0,Axis(p.sThumbLY)<0?-Axis(p.sThumbLY):0,
        Axis(p.sThumbRX)<0?-Axis(p.sThumbRX):0,Axis(p.sThumbRX)>0?Axis(p.sThumbRX):0,
        Axis(p.sThumbRY)>0?Axis(p.sThumbRY):0,Axis(p.sThumbRY)<0?-Axis(p.sThumbRY):0};
    frame->SupplyDrop((p.wButtons&XINPUT_GAMEPAD_B)!=0);
    for(int i=0;i<12;++i){frame->SupplyHeld(i,held[i]);frame->SupplyPressed(i,held[i]&&!PreviousAction(i));}
    for(int i=0;i<8;++i)frame->SupplyValue(i,values[i]);
    for(int i=0;i<12;++i)gPrevActions[i]=held[i];
}
static void Log(const char* fmt,...) {
    char line[1024]; va_list args; va_start(args,fmt);
    vsnprintf(line,sizeof(line)-2,fmt,args); va_end(args);
    line[sizeof(line)-2]=0; strcat(line,"\n");
    OutputDebugStringA(line);
    if(gTrace) { fputs(line,gTrace); fflush(gTrace); }
}
static unsigned int LE32(const unsigned char* p) {
    return (unsigned int)p[0]|((unsigned int)p[1]<<8)|((unsigned int)p[2]<<16)|((unsigned int)p[3]<<24);
}
static unsigned char* LoadFile(const char* path,unsigned int* size,unsigned int limit) {
    FILE* f=fopen(path,"rb"); if(!f) { Log("PORT file open failed %s",path); return 0; }
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    if(n<=0 || (unsigned long)n>limit) { fclose(f); return 0; }
    unsigned char* data=new unsigned char[n];
    if(fread(data,1,n,f)!=(size_t)n) { delete[] data; fclose(f); return 0; }
    fclose(f); *size=(unsigned int)n; return data;
}
static bool InitGraphics() {
    IDirect3D9* d3d=Direct3DCreate9(D3D_SDK_VERSION); if(!d3d) return false;
    D3DPRESENT_PARAMETERS pp; ZeroMemory(&pp,sizeof(pp));
    pp.BackBufferWidth=1280; pp.BackBufferHeight=720;
    pp.BackBufferFormat=D3DFMT_A8R8G8B8; pp.FrontBufferFormat=D3DFMT_LE_X8R8G8B8;
    pp.BackBufferCount=1; pp.MultiSampleType=D3DMULTISAMPLE_NONE;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD; pp.PresentationInterval=D3DPRESENT_INTERVAL_ONE;
    HRESULT hr=d3d->CreateDevice(0,D3DDEVTYPE_HAL,0,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,&gDevice);
    d3d->Release(); if(FAILED(hr)) { Log("PORT device failed %08x",hr); return false; }
    hr=gDevice->CreateVertexShader((const DWORD*)SpriteVS,&gVS);
    if(FAILED(hr)) { Log("PORT vertex shader failed %08x",hr); return false; }
    hr=gDevice->CreatePixelShader((const DWORD*)SpritePS,&gPS);
    if(FAILED(hr)) { Log("PORT pixel shader failed %08x",hr); return false; }
    const D3DVERTEXELEMENT9 elements[]={
        {0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,8,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},
        {0,16,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},
        {0,20,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,1},D3DDECL_END()};
    hr=gDevice->CreateVertexDeclaration(elements,&gDecl);
    if(FAILED(hr)) return false;
    Log("PORT graphics initialized 1280x720"); return true;
}
static bool LoadAtlas() {
    unsigned int n=0; unsigned char* data=LoadFile("game:\\port\\frontend\\atlas.bin",&n,32*1024*1024+16);
    if(!data) return false;
    if(n<16 || memcmp(data,"GATLAS1\0",8)!=0 || LE32(data+8)!=kAtlasWidth || LE32(data+12)!=kAtlasHeight || n!=16+kAtlasWidth*kAtlasHeight*4) { delete[] data; return false; }
    HRESULT hr=gDevice->CreateTexture(kAtlasWidth,kAtlasHeight,1,0,D3DFMT_LIN_A8R8G8B8,D3DPOOL_DEFAULT,&gAtlas,0);
    if(FAILED(hr)) { Log("PORT texture create failed %08x",hr); delete[] data; return false; }
    D3DLOCKED_RECT locked;
    hr=gAtlas->LockRect(0,&locked,0,0);
    if(FAILED(hr)) { delete[] data; return false; }
    for(unsigned int y=0;y<kAtlasHeight;++y) {
        DWORD* row=(DWORD*)((unsigned char*)locked.pBits+y*locked.Pitch);
        const unsigned char* p=data+16+y*kAtlasWidth*4;
        for(unsigned int x=0;x<kAtlasWidth;++x,p+=4) row[x]=((DWORD)p[3]<<24)|((DWORD)p[0]<<16)|((DWORD)p[1]<<8)|p[2];
    }
    gAtlas->UnlockRect(0); delete[] data;
    Log("PORT prepared atlas loaded bytes=%d",kAtlasWidth*kAtlasHeight*4); return true;
}
static bool CheckOriginalBindings() {
    FILE* index=fopen("game:\\game\\file_index.bin","rb"); if(!index) return false;
    bool ok=true;
    for(int i=0;i<kOriginalBindingCount && ok;++i) {
        GpcEntry entry; GpcResult result=GpcFindOriginalResource(index,kOriginalBindings[i].request,&entry);
        ok=result==GPC_OK && entry.package==kOriginalBindings[i].package;
        Log("PORT inherited request=%s package=%d result=%d matched=%d",kOriginalBindings[i].request,result==GPC_OK?entry.package:0,result,ok);
    }
    fclose(index); return ok;
}
struct Vertex { float x,y,u,v; DWORD color; float add[3]; };
static void Quad(const float* xy,float tx,float ty,float tw,float th,DWORD color,const float* add=0) {
    const float u[4]={(tx+0.5f)/kAtlasWidth,(tx+tw-0.5f)/kAtlasWidth,(tx+tw-0.5f)/kAtlasWidth,(tx+0.5f)/kAtlasWidth};
    const float v[4]={(ty+0.5f)/kAtlasHeight,(ty+0.5f)/kAtlasHeight,(ty+th-0.5f)/kAtlasHeight,(ty+th-0.5f)/kAtlasHeight};
    const int order[6]={0,1,2,0,2,3}; Vertex vertices[6];
    for(int i=0;i<6;++i) { int j=order[i]; vertices[i].x=xy[j*2]; vertices[i].y=xy[j*2+1]; vertices[i].u=u[j]; vertices[i].v=v[j]; vertices[i].color=color;
        for(int c=0;c<3;++c) vertices[i].add[c]=add?add[c]:0;
    }
    gDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,vertices,sizeof(Vertex));
}
static DWORD Modulate(DWORD a,DWORD b) {
    DWORD color=0; for(int shift=0;shift<32;shift+=8) color|=((((a>>shift)&255)*((b>>shift)&255)/255)<<shift); return color;
}
static const float kFrontendScale=1280.0f/480.0f;
//The native room view is authored in a 480x270 logical viewport. Gameplay
//uses that same inherited presentation scale.
static const float kGameScale=kFrontendScale;
static float Bits(unsigned int u){float f;memcpy(&f,&u,4);return f;}
static IDirect3DTexture9* gGameplayAtlas=0;
static bool LoadGameplayAtlas(){
    unsigned int size=0;unsigned char* data=LoadFile("game:\\port\\gameplay\\atlas.bin",&size,kGameplayAtlasWidth*kGameplayAtlasHeight*4+16);
    if(!data)return false;
    if(size!=16+kGameplayAtlasWidth*kGameplayAtlasHeight*4||memcmp(data,"GPLAY1\0\0",8)!=0||
        LE32(data+8)!=kGameplayAtlasWidth||LE32(data+12)!=kGameplayAtlasHeight){delete[] data;return false;}
    HRESULT hr=gDevice->CreateTexture(kGameplayAtlasWidth,kGameplayAtlasHeight,1,0,D3DFMT_LIN_A8R8G8B8,D3DPOOL_DEFAULT,&gGameplayAtlas,0);
    if(FAILED(hr)){Log("PORT gameplay texture create failed %08x",hr);delete[] data;return false;}
    D3DLOCKED_RECT locked;hr=gGameplayAtlas->LockRect(0,&locked,0,0);
    if(FAILED(hr)){delete[] data;return false;}
    for(unsigned int y=0;y<kGameplayAtlasHeight;++y){
        DWORD* row=(DWORD*)((unsigned char*)locked.pBits+y*locked.Pitch);
        const unsigned char* p=data+16+y*kGameplayAtlasWidth*4;
        for(unsigned int x=0;x<kGameplayAtlasWidth;++x,p+=4)row[x]=((DWORD)p[3]<<24)|((DWORD)p[0]<<16)|((DWORD)p[1]<<8)|p[2];
    }
    gGameplayAtlas->UnlockRect(0);delete[] data;
    Log("PORT gameplay atlas loaded %dx%d",kGameplayAtlasWidth,kGameplayAtlasHeight);return true;
}
static void TexQuad(IDirect3DTexture9* texture,const float* xy,const float* uv,DWORD color,float uSpan,float vSpan){
    const int order[6]={0,1,3,0,3,2};Vertex vertices[6];
    for(int i=0;i<6;++i){int j=order[i];vertices[i].x=xy[j*2]*kGameScale;vertices[i].y=xy[j*2+1]*kGameScale;
        vertices[i].u=uv[j*2]/uSpan;vertices[i].v=uv[j*2+1]/vSpan;vertices[i].color=color;
        vertices[i].add[0]=vertices[i].add[1]=vertices[i].add[2]=0;}
    gDevice->SetTexture(0,texture);gDevice->DrawPrimitiveUP(D3DPT_TRIANGLELIST,2,vertices,sizeof(Vertex));
}
static void TexRect(IDirect3DTexture9* texture,float x,float y,float w,float h,
    float sx,float sy,float sw,float sh,float texW,float texH,DWORD color=0xffffffff){
    const float xy[8]={x,y,x+w,y,x,y+h,x+w,y+h};
    const float uv[8]={sx,sy,sx+sw,sy,sx,sy+sh,sx+sw,sy+sh};
    TexQuad(texture,xy,uv,color,texW,texH);
}
static bool GameplayVisible(){return gNormalRuntime.phase==PNR_READY||gNormalRuntime.phase==PNR_DYING||gNormalRuntime.phase==PNR_GAME_OVER;}
static bool BackdropAtlasRect(int id,int* ox,int* oy){
    if(id==1){*ox=kGameplayBackdrop1X;*oy=kGameplayBackdrop1Y;return true;}
    if(id==2){*ox=kGameplayBackdrop2X;*oy=kGameplayBackdrop2Y;return true;}
    return false;
}
static void DrawBackground(){
    if(!gGameplayAtlas)return;
    if(gNormalRuntime.world.currentList<0||gNormalRuntime.world.currentList>=kNativeWorldRoomLimit)return;
    NativeWorldResident& live=gNormalRuntime.world.resident[gNormalRuntime.world.currentList];
    //Transitioned residents are constructed by EnterLinked without the initial
    //Room::Init presentation block. Prepare the same original background
    //commands lazily; this never advances simulation or RNG.
    if(!live.view)live.view=new NativeRoomView;
    if(live.room&&!live.view->background.ready){
        const NativePlacedRoom* descriptor=gNormalRuntime.world.CurrentDescriptor();
        if(descriptor)live.view->background.Prepare(*live.room,live.backdrop,descriptor->seeds.spawn);
    }
    if(!live.view->background.ready||!live.view->background.definition)return;
    const NativeRoomBackground& background=live.view->background;
    int ox,oy;if(!BackdropAtlasRect(background.definition->id,&ox,&oy))return;
    //Floor and wall commands both address the original backdrop sheet. The
    //floor-variant region is part of that same 520x468 sheet.
    for(unsigned int pass=0;pass<2;++pass){
        const std::vector<NativeBackgroundQuad>& list=pass?background.wall:background.floor;
        for(unsigned int i=0;i<list.size();++i){
            const NativeBackgroundQuad& q=list[i];float xy[8],uv[8];
            for(int k=0;k<4;++k){xy[k*2]=q.destination[k].x;xy[k*2+1]=q.destination[k].y;
                uv[k*2]=q.source[k].x+ox;uv[k*2+1]=q.source[k].y+oy;}
            TexQuad(gGameplayAtlas,xy,uv,0xffffffff,(float)kGameplayAtlasWidth,(float)kGameplayAtlasHeight);
        }
    }
}
static void DrawPlayer(){
    if(!gGameplayAtlas||gNormalRuntime.world.currentList<0||gNormalRuntime.world.currentList>=kNativeWorldRoomLimit||
        gNormalRuntime.playerBody.removed||!gNormalRuntime.playerSprites.bodyReady)return;
    NativeWorldResident& live=gNormalRuntime.world.resident[gNormalRuntime.world.currentList];
    if(!live.room||!live.view||!live.view->background.ready)return;
    NativeSprite& sprite=gNormalRuntime.playerSprites.sprites[0];
    if(sprite.layerSheets.size()<=1)return;
    //The prepared atlas currently contains the fresh-profile Isaac costume.
    //Other sheets remain explicit rendering stops rather than wrong art.
    if(strstr(sprite.layerSheets[1].path.c_str(),"character_001_isaac")==0)return;
    const NativeRoomBackground& background=live.view->background;
    const NativeRoomSpace& space=live.room->space;
    const float roomW=background.right-background.left,worldW=space.right-space.left;
    const float roomH=background.bottom-background.top,worldH=space.bottom-space.top;
    if(worldW<=0||worldH<=0)return;
    const float px=background.left+(gNormalRuntime.playerBody.position.x-space.left)*roomW/worldW;
    const float py=background.top+(gNormalRuntime.playerBody.position.y-space.top)*roomH/worldH;
    const NativeSpriteFrame* frames[2]={sprite.LayerFrame(1),sprite.LayerFrame(4,true)};
    for(int i=0;i<2;++i){
        const NativeSpriteFrame* f=frames[i];if(!f||!f->visible)continue;
        const float cropX=Bits(f->geometry[0]),cropY=Bits(f->geometry[1]);
        const float w=Bits(f->geometry[2]),h=Bits(f->geometry[3]);
        const float offX=Bits(f->geometry[4]),offY=Bits(f->geometry[5]);
        float scaleX=Bits(f->geometry[6]),scaleY=Bits(f->geometry[7]);
        const float pivotX=Bits(f->geometry[8]),pivotY=Bits(f->geometry[9]);
        if(w<=0||h<=0)continue;
        if(scaleX==0||scaleY==0){scaleX=scaleY=1;}
        const float x=px+(offX-pivotX)*fabsf(scaleX),y=py+(offY-pivotY)*fabsf(scaleY);
        const float dw=w*fabsf(scaleX),dh=h*fabsf(scaleY);
        TexRect(gGameplayAtlas,x,y,dw,dh,cropX+(float)kGameplayPlayerX,cropY+(float)kGameplayPlayerY,
            w,h,(float)kGameplayAtlasWidth,(float)kGameplayAtlasHeight);
    }
}
static void DrawDoors(){
    if(!gGameplayAtlas||gNormalRuntime.world.currentList<0||gNormalRuntime.world.currentList>=kNativeWorldRoomLimit)return;
    NativeWorldResident& live=gNormalRuntime.world.resident[gNormalRuntime.world.currentList];
    if(!live.room||!live.doors||!live.view||!live.view->background.ready)return;
    bool needsVisuals=!live.doorVisuals;
    if(live.doorVisuals)for(int slot=0;slot<8;++slot)if((live.doors->attached&(1U<<slot))&&
        !live.doorVisuals[slot].ready&&!live.doorVisuals[slot].unsupportedTarget){needsVisuals=true;break;}
    if(needsVisuals)gNormalRuntime.world.PrepareDoorVisuals(gNormalRuntime.run.context.stage,
        gNormalRuntime.run.context.stageType,gNormalRuntime.run.context.nativeMode,gNormalRuntime.run.context.challenge,
        gNormalRuntime.playerState);
    if(!live.doorVisuals)return;
    const NativeRoomBackground& background=live.view->background;
    const NativeRoomSpace& space=live.room->space;
    const float roomW=background.right-background.left,worldW=space.right-space.left;
    const float roomH=background.bottom-background.top,worldH=space.bottom-space.top;
    if(worldW<=0||worldH<=0)return;
    for(int slot=0;slot<8;++slot){
        if(!(live.doors->attached&(1U<<slot)))continue;
        NativeDoorVisual& visual=live.doorVisuals[slot];
        if(!visual.ready||!visual.main.asset)continue;
        if(visual.main.layerSheets.empty()||strstr(visual.main.layerSheets[0].path.c_str(),"door_01_normaldoor")==0)continue;
        float wx,wy;if(!live.room->geometry.Position(live.doors->actors[slot].grid,&wx,&wy))continue;
        const float px=background.left+(wx+20.0f-space.left)*roomW/worldW;
        const float py=background.top+(wy+20.0f-space.top)*roomH/worldH;
        for(unsigned int i=0;i<visual.main.asset->layerCount;++i){
            const NativeSpriteFrame* f=visual.main.LayerFrame(visual.main.asset->layers[i].id);
            if(!f||!f->visible)continue;
            const float cropX=Bits(f->geometry[0]),cropY=Bits(f->geometry[1]);
            const float w=Bits(f->geometry[2]),h=Bits(f->geometry[3]);
            const float offX=Bits(f->geometry[4]),offY=Bits(f->geometry[5]);
            float scaleX=Bits(f->geometry[6]),scaleY=Bits(f->geometry[7]);
            const float pivotX=Bits(f->geometry[8]),pivotY=Bits(f->geometry[9]);
            if(w<=0||h<=0)continue;
            if(scaleX==0||scaleY==0){scaleX=scaleY=1;}
            const float x=px+(offX-pivotX)*fabsf(scaleX),y=py+(offY-pivotY)*fabsf(scaleY);
            const float dw=w*fabsf(scaleX),dh=h*fabsf(scaleY);
            TexRect(gGameplayAtlas,x,y,dw,dh,cropX+(float)kGameplayDoornormalX,cropY+(float)kGameplayDoornormalY,
                w,h,(float)kGameplayAtlasWidth,(float)kGameplayAtlasHeight);
        }
    }
}
static void DrawRocks(){
    if(!gGameplayAtlas||gNormalRuntime.world.currentList<0||gNormalRuntime.world.currentList>=kNativeWorldRoomLimit)return;
    NativeWorldResident& live=gNormalRuntime.world.resident[gNormalRuntime.world.currentList];
    if(!live.room||!live.view||!live.view->background.ready)return;
    const NativeRoomBackground& background=live.view->background;
    const NativeRoomSpace& space=live.room->space;
    const float roomW=background.right-background.left,worldW=space.right-space.left;
    const float roomH=background.bottom-background.top,worldH=space.bottom-space.top;
    if(worldW<=0||worldH<=0)return;
    const float cellW=40.0f*roomW/worldW,cellH=40.0f*roomH/worldH;
    for(int i=0;i<live.room->staticGridCount;++i){
        const NativeStaticGrid& g=live.room->staticGrids[i];
        if(g.type!=1000&&g.type!=1002)continue;
        float wx,wy;if(!live.room->geometry.Position(g.grid,&wx,&wy))continue;
        const float px=background.left+(wx-space.left)*roomW/worldW;
        const float py=background.top+(wy-space.top)*roomH/worldH;
        const int sx=g.type==1002?0:(g.variant&3)*32;
        const int sy=g.type==1002?32:0;
        TexRect(gGameplayAtlas,px,py,cellW,cellH,(float)(kGameplayRocksbasementX+sx),
            (float)(kGameplayRocksbasementY+sy),32,32,(float)kGameplayAtlasWidth,(float)kGameplayAtlasHeight);
    }
}
static void DrawEnemies(){
    if(!gGameplayAtlas||gNormalRuntime.world.currentList<0||gNormalRuntime.world.currentList>=kNativeWorldRoomLimit)return;
    NativeWorldResident& live=gNormalRuntime.world.resident[gNormalRuntime.world.currentList];
    if(!live.room||!live.view||!live.view->background.ready)return;
    const NativeRoomBackground& background=live.view->background;
    const NativeRoomSpace& space=live.room->space;
    const float roomW=background.right-background.left,worldW=space.right-space.left;
    const float roomH=background.bottom-background.top,worldH=space.bottom-space.top;
    if(worldW<=0||worldH<=0)return;
    for(int i=0;i<live.enemies.count;++i){
        NativeEnemy* enemy=live.enemies.entries[i];
        if(!enemy||!enemy->ready||enemy->dead)continue;
        int sx=0,sy=0;
        if(enemy->type==10){sx=kGameplayGaperX;sy=kGameplayGaperY;}
        else if(enemy->type==13){sx=kGameplayFlyX;sy=kGameplayFlyY;}
        else if(enemy->type==14){sx=kGameplayPooterX;sy=kGameplayPooterY;}
        else if(enemy->type==208){sx=kGameplayFattyX;sy=kGameplayFattyY;}
        else continue;
        const float px=background.left+(enemy->body.position.x-space.left)*roomW/worldW;
        const float py=background.top+(enemy->body.position.y-space.top)*roomH/worldH;
        TexRect(gGameplayAtlas,px-32,py-48,64,64,(float)sx,(float)sy,64,64,
            (float)kGameplayAtlasWidth,(float)kGameplayAtlasHeight);
    }
}
static void DrawProjectiles(){
    if(!gGameplayAtlas||gNormalRuntime.world.currentList<0||gNormalRuntime.world.currentList>=kNativeWorldRoomLimit)return;
    NativeWorldResident& live=gNormalRuntime.world.resident[gNormalRuntime.world.currentList];
    if(!live.room||!live.view||!live.view->background.ready)return;
    const NativeRoomBackground& background=live.view->background;
    const NativeRoomSpace& space=live.room->space;
    const float roomW=background.right-background.left,worldW=space.right-space.left;
    const float roomH=background.bottom-background.top,worldH=space.bottom-space.top;
    if(worldW<=0||worldH<=0)return;
    for(unsigned int i=0;i<gNormalRuntime.projectiles.active.size();++i){
        const NativeProjectile& p=gNormalRuntime.projectiles.active[i];
        const float px=background.left+(p.position.x-space.left)*roomW/worldW;
        const float py=background.top+(p.position.y-space.top)*roomH/worldH;
        TexRect(gGameplayAtlas,px-16,py-16,32,32,(float)kGameplayTearsX,(float)kGameplayTearsY,32,32,
            (float)kGameplayAtlasWidth,(float)kGameplayAtlasHeight);
    }
}
static void DrawHud(){
    if(!gGameplayAtlas||!gNormalRuntime.playerState.ready)return;
    float x=10,y=10;
    const int red=gNormalRuntime.playerState.redHearts,containers=gNormalRuntime.playerState.maxRedHearts/2;
    for(int i=0;i<containers;++i){
        float sx=32,sy=0;
        if(red>=2*(i+1))sx=0;else if(red==2*i+1)sx=16;
        TexRect(gGameplayAtlas,x,y,16,16,sx+kGameplayHeartsX,sy+kGameplayHeartsY,16,16,
            (float)kGameplayAtlasWidth,(float)kGameplayAtlasHeight);
        x+=16;
    }
    const int soul=gNormalRuntime.playerState.soulHearts;
    for(int i=0;i<soul/2;++i){
        TexRect(gGameplayAtlas,x,y,16,16,(float)kGameplayHeartsX,(float)(kGameplayHeartsY+16),16,16,
            (float)kGameplayAtlasWidth,(float)kGameplayAtlasHeight);x+=16;
    }
    if(soul&1)TexRect(gGameplayAtlas,x,y,16,16,(float)(kGameplayHeartsX+16),(float)(kGameplayHeartsY+16),16,16,
        (float)kGameplayAtlasWidth,(float)kGameplayAtlasHeight);
}
static void Gameplay(){
    DrawBackground();DrawDoors();DrawRocks();DrawEnemies();DrawPlayer();DrawProjectiles();DrawHud();
    gDevice->SetTexture(0,gAtlas);
}
static void Anim(int id,int frame,float x,float y,float scale=kFrontendScale,float angle=0,const int* itemIcon=0,DWORD tint=0xffffffff,bool flipX=false,const float* extraAdd=0) {
    const FrontendAnimation& a=kAnimations[id]; if(frame<0) frame=0;
    if(a.loop) frame%=a.count; else if(frame>=a.count) frame=a.count-1;
    const FrontendFrame& f=kFrames[a.first+frame];
    float cs=cosf(angle),sn=sinf(angle);
    for(int i=0;i<f.count;++i) {
        const FrontendSprite& s=kSprites[f.first+i]; float xy[8];
        for(int k=0;k<4;++k) { float px=s.corners[k*2]*scale*(flipX?-1:1),py=s.corners[k*2+1]*scale; xy[k*2]=x+px*cs-py*sn; xy[k*2+1]=y+px*sn+py*cs; }
        const int* icon=s.itemLayer?itemIcon:0;
        float add[3];for(int c=0;c<3;++c)add[c]=s.add[c]+(extraAdd?extraAdd[c]:0);
        Quad(xy,(float)(icon?icon[0]:s.x),(float)(icon?icon[1]:s.y),(float)s.w,(float)s.h,Modulate(s.color,tint),add);
    }
}
static void Menu() {
    if(gUi.scene==UI_TITLE) {
        Anim(AN_Title,gUi.tick,0,0,1280.0f/480.0f);
        return;
    }
    if(gUi.scene==UI_SLOTS) {
        const float scale=1280.0f/480.0f;
        Anim(AN_SlotsBase,0,0,0,scale);
        const int on[]={AN_SlotsFile1,AN_SlotsFile2,AN_SlotsFile3,AN_SlotsDelete,AN_SlotsImport};
        const int off[]={AN_SlotsFile1Off,AN_SlotsFile2Off,AN_SlotsFile3Off,AN_SlotsDeleteOff,AN_SlotsImportOff};
        for(int i=0;i<5;++i) {
            bool selected=i==gUi.selection;
            int frame=selected?gUi.slotAnimTick:(gUi.slotPrevious==i?gUi.slotAnimTick:4);
            // 0091c970 colors the Delete object (1,.6,.4) while picking.
            DWORD tint=i==3 && gUi.slotMode==1?0xffff9966:0xffffffff;
            Anim(selected?on[i]:off[i],frame,0,0,scale,0,0,tint);
            if(i<3) {
                int anchorFrame=frame<5?frame:4;
                const SlotAnchor& anchor=kSlotAnchors[i][selected?0:1][anchorFrame];
                Anim(selected?AN_SlotsDrawingAnimated:AN_SlotsDrawing,selected?gUi.slotAnimTick:0,anchor.x*scale,anchor.y*scale,scale*anchor.sx,0,0,anchor.tint);
            }
        }
        if(gUi.slotMode==2) Anim(gUi.slotTick<10?AN_SlotsConfirmIn:AN_SlotsConfirm,gUi.slotTick<10?gUi.slotTick:gUi.slotChoice,0,0,scale);
        if(gUi.slotMode==3) Anim(AN_SlotsConfirmOut,gUi.slotTick,0,0,scale);
        return;
    }
    if(gUi.scene==UI_MAIN) {
        const float scale=1280.0f/480.0f;
        Anim(AN_MainBase,0,0,0,scale);
        Anim(AN_MainNew,0,0,0,scale);
        Anim(AN_MainContinue,gUi.hasContinue?0:1,0,0,scale);
        Anim(AN_MainChallenges,0,0,0,scale);
        Anim(AN_MainMods,0,0,0,scale,0,0,0xff909090); // Explicit no-mods scope.
        Anim(AN_MainStats,0,0,0,scale); Anim(AN_MainOptions,0,0,0,scale);
        Anim(AN_MainCursor,gUi.selection,0,0,scale);
        return;
    }
    if(gUi.scene==UI_CHARACTERS) {
        const float scale=1280.0f/480.0f;
        const int* art=kCharacterArt[gUi.characterPage][gUi.characterChoice];
        const NativeCharacterDef* character=NativeCharacter(gUi.characterPage,gUi.characterChoice);
        bool unlocked=NativeCharacterUnlocked(gUi.profile,gUi.characterPage,gUi.characterChoice);
        Anim(AN_CharacterBGIdle,0,0,0,scale);
        if(gUi.characterPage)Anim(AN_CharacterBGExtra,0,0,0,scale);
        Anim(gUi.characterPage?AN_CharacterBGFace1:AN_CharacterBGFace0,0,0,0,scale);
        Anim(art[0],0,0,0,scale);
        Anim(art[unlocked?1:2],0,0,0,scale);
        if(unlocked)for(int i=0;i<3;++i)if(gUi.profile.Award(character->itemAwards[i]))Anim(art[3+i],0,0,0,scale);
        NativeCharacterPortrait portraits[18];int count=NativeCharacterWheel(gUi.profile,gUi.characterPage,gUi.characterChoice,portraits);
        for(int i=0;i<count;++i) {
            const NativeCharacterPortrait& p=portraits[i];DWORD rgb=(DWORD)(p.scale*255);
            float add[3]={(1-p.scale)*231/255,(1-p.scale)*221/255,(1-p.scale)*220/255};
            Anim(kCharacterArt[gUi.characterPage][p.choice][6],NativeCharacterUnlocked(gUi.profile,gUi.characterPage,p.choice)?0:1,p.x*scale,p.y*scale,scale*p.scale,0,0,0xff000000|(rgb<<16)|(rgb<<8)|rgb,false,add);
        }
        Anim(gUi.profile.Award(341)?AN_DifficultyAll:AN_Difficulty,gUi.characterMode,365*scale,120*scale,scale);
        Anim(AN_CharacterSeed,0,350*scale,45*scale,scale);
        Anim(AN_ControllerButtons,0,415*scale,58*scale,scale);
        return;
    }
}
static void Render(bool ready) {
    gDevice->Clear(0,0,D3DCLEAR_TARGET,GameplayVisible()?0xff000000:0xffcccccc,1,0);
    gDevice->SetVertexShader(gVS);gDevice->SetPixelShader(gPS);gDevice->SetVertexDeclaration(gDecl);gDevice->SetTexture(0,gAtlas);
    gDevice->SetRenderState(D3DRS_ZENABLE,FALSE);gDevice->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    gDevice->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);gDevice->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);gDevice->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    gDevice->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);gDevice->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    gDevice->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);gDevice->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    if(ready){if(GameplayVisible())Gameplay();else Menu();}gDevice->Present(0,0,0,0);
}
VOID __cdecl main() {
    gTrace=fopen("game:\\port\\trace.log","wt");
    if(!InitGraphics())for(;;)Sleep(1000);
    bool ready=LoadAtlas()&&CheckOriginalBindings();LoadGameplayAtlas();gUi.Init(false);
    //Console-writable local save root. e:\ is the normal writable console
    //storage; a devkit/emulator without e:\ falls back to its game:// scratch.
    bool saveBound=false;
    if(CreateDirectoryA("e:\\boi360",0)||GetLastError()==ERROR_ALREADY_EXISTS)
        saveBound=gUi.BindSaveDirectory("e:\\boi360\\");
    if(!saveBound){
        CreateDirectoryA("game:\\profile",0);
        saveBound=gUi.BindSaveDirectory("game:\\profile\\");
    }
    if(!saveBound)Log("PORT save directory bind failed");
    //Platform entropy adapter; original global initialization still requires validation.
    unsigned int seed=GetTickCount();if(!seed)seed=1;gMenuRandom.Seed(seed);
    LARGE_INTEGER frequency,last,now;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&last);double accumulator=0;
    for(;;){QueryPerformanceCounter(&now);double elapsed=(double)(now.QuadPart-last.QuadPart)/frequency.QuadPart;last=now;if(elapsed>0.25)elapsed=0.25;accumulator+=elapsed;
        while(ready&&accumulator>=1.0/30){accumulator-=1.0/30;XINPUT_STATE state;ZeroMemory(&state,sizeof(state));XInputGetState(0,&state);const XINPUT_GAMEPAD& p=state.Gamepad;
            PortMenuInput input={(p.wButtons&XINPUT_GAMEPAD_DPAD_UP)!=0||p.sThumbLY>16000,(p.wButtons&XINPUT_GAMEPAD_DPAD_DOWN)!=0||p.sThumbLY< -16000,
                (p.wButtons&XINPUT_GAMEPAD_A)!=0,(p.wButtons&(XINPUT_GAMEPAD_B|XINPUT_GAMEPAD_BACK))!=0,(p.wButtons&XINPUT_GAMEPAD_START)!=0,
                (p.wButtons&XINPUT_GAMEPAD_DPAD_LEFT)!=0||p.sThumbLX< -16000,(p.wButtons&XINPUT_GAMEPAD_DPAD_RIGHT)!=0||p.sThumbLX>16000,
                (p.wButtons&XINPUT_GAMEPAD_X)!=0,(p.wButtons&XINPUT_GAMEPAD_Y)!=0};
            PortAction action=UI_NONE;
            if(!GameplayVisible())action=gUi.Step(input);
            //The native player update requests its own connected action owner.
            //Supply the real pad frame to the same shared runtime the host uses.
            static unsigned int actionFrameNumber=0;
            NativeActionFrame actionFrame;NativeFrameActions frameActions(actionFrame);
            BuildActions(p,++actionFrameNumber,&actionFrame);
            //Native profile disk format and complete run persistence remain open.
            if(action==UI_SELECT_SLOT){gUi.profile=gProfiles[gUi.selectedSlot];gUi.SlotLoaded(true,false);}
            if(action==UI_DELETE_SLOT){ZeroMemory(&gProfiles[gUi.selectedSlot],sizeof(NativeProfile));gUi.SlotDeleted(true);}
            if(action==UI_START_CHARACTER){unsigned int runSeed=0;
                //Optional local QA seed; absent in ordinary release layouts.
                FILE* seedFile=fopen("game:\\port\\target_seed.txt","rb");
                if(seedFile){if(fscanf(seedFile,"%u",&runSeed)!=1)runSeed=0;fclose(seedFile);}
                if(!runSeed)runSeed=GetTickCount();if(!runSeed)runSeed=1;
                PortSessionStart start=PortGameSession(gMenuRandom,gUi).StartNormalCharacter(runSeed,gNormalRuntime);Log("PORT normal setup result=%d seed=%u player=%d",start.result,runSeed,start.player);}
            if(gUi.characterPending){const NormalRunFlow& run=gNormalRuntime.run;
                NativeSceneFiles files={"game:\\game\\file_index.bin","game:\\game\\",run.context.stageType+1,run.context.nativeMode,0};
                NormalRunResult result=PortGameSession(gMenuRandom,gUi).AdvanceNormalSetup(gNormalRuntime,files,&frameActions);
                if(!gUi.characterPending){Log("PORT normal setup boundary phase=%d generation=%d result=%d resident=%d source=%d prerequisite=%08x",gNormalRuntime.phase,run.phase,result,gNormalRuntime.world.residentCount,gNormalRuntime.entryIssue.source,gNormalRuntime.missingNativePrerequisite);
                    if(gNormalRuntime.missingNativePrerequisite==0x6c8140&&gNormalRuntime.world.currentList>=0){NativeWorldResident& live=gNormalRuntime.world.resident[gNormalRuntime.world.currentList];
                        Log("PORT door visual stop slot=%d kind=%d variant=%d aux=%d backdrop=%d",live.doorFailureSlot,live.doorFailureKind,live.doorFailureVariant,live.doorFailureAux,live.backdrop);}}}
            if(gNormalRuntime.phase==PNR_READY){
                static bool readyLogged=false;
                if(!readyLogged){NativeWorldResident& first=gNormalRuntime.world.resident[gNormalRuntime.world.currentList];
                    Log("PORT normal ready ticks=0 playerToken=%08x doorVisualGaps=%d",gNormalRuntime.playerBody.entityToken,first.doorVisualGaps);
                    if(first.doors)for(int slot=0;slot<8;++slot)if(first.doors->attached&(1U<<slot)){
                        const NativeDoorActor& door=first.doors->actors[slot];const NativePlacedRoom* target=gNormalRuntime.world.layout.DoorTarget(gNormalRuntime.world.currentList,slot);
                        float wx=0,wy=0;if(first.room)first.room->geometry.Position(door.grid,&wx,&wy);
                        Log("PORT door slot=%d grid=%d pos=(%.1f,%.1f) target=%d definition=%d state=%d",slot,door.grid,wx,wy,
                            door.targetType,target?target->definition:-1,door.state);}
                    readyLogged=true;}
                const NormalRunFlow& run=gNormalRuntime.run;
                NativeSceneFiles files={"game:\\game\\file_index.bin","game:\\game\\",run.context.stageType+1,run.context.nativeMode,0};
                NormalRunResult tick=PortGameSession(gMenuRandom,gUi).TickNormal(gNormalRuntime,files,&frameActions);
                static unsigned int lastTickPrerequisite=0;
                if(tick==NFR_PENDING&&gNormalRuntime.missingNativePrerequisite&&gNormalRuntime.missingNativePrerequisite!=lastTickPrerequisite){
                    Log("PORT normal tick pending prerequisite=%08x",gNormalRuntime.missingNativePrerequisite);lastTickPrerequisite=gNormalRuntime.missingNativePrerequisite;
                    if(gNormalRuntime.missingNativePrerequisite==0x6f3b30)
                        Log("PORT transition issue result=%d source=%d construction=%d choice=%d type=%u variant=%u subtype=%u",
                            gNormalRuntime.entryIssue.result,gNormalRuntime.entryIssue.source,gNormalRuntime.entryIssue.construction.stage,
                            gNormalRuntime.entryIssue.construction.choice,gNormalRuntime.entryIssue.construction.type,
                            gNormalRuntime.entryIssue.construction.variant,gNormalRuntime.entryIssue.construction.subtype);}
                if(gNormalRuntime.tickFrame%30==0){
                    NativeWorldResident& live=gNormalRuntime.world.resident[gNormalRuntime.world.currentList];
                    Log("PORT normal tick=%u result=%d pos=(%.2f,%.2f) hp=%d/%d soul=%d enemies=%d clear=%d hits=%u transitions=%u",
                        gNormalRuntime.tickFrame,tick,gNormalRuntime.playerBody.position.x,gNormalRuntime.playerBody.position.y,
                        gNormalRuntime.playerState.redHearts,gNormalRuntime.playerState.maxRedHearts,gNormalRuntime.playerState.soulHearts,
                        live.enemies.count,live.room?live.room->clear:0,gNormalRuntime.playerHits,gNormalRuntime.roomTransitions);
                }
                if(tick==NFR_BAD_REQUEST)Log("PORT normal tick rejected prerequisite=%08x",gNormalRuntime.missingNativePrerequisite);
            }else if(gNormalRuntime.phase==PNR_DYING||gNormalRuntime.phase==PNR_GAME_OVER){
                static bool dyingLogged=false;
                if(!dyingLogged){Log("PORT normal death phase=%d hits=%u hp=%d/%d",gNormalRuntime.phase,gNormalRuntime.playerHits,gNormalRuntime.playerState.redHearts,gNormalRuntime.playerState.maxRedHearts);dyingLogged=true;}
                const NormalRunFlow& run=gNormalRuntime.run;
                NativeSceneFiles files={"game:\\game\\file_index.bin","game:\\game\\",run.context.stageType+1,run.context.nativeMode,0};
                PortGameSession(gMenuRandom,gUi).TickNormal(gNormalRuntime,files,&frameActions);
            }
            if(action!=UI_NONE&&action!=UI_SELECT_SLOT&&action!=UI_DELETE_SLOT&&action!=UI_START_CHARACTER)Log("PORT pending native frontend destination=%d",action);
        }
        Render(ready);
    }
}
