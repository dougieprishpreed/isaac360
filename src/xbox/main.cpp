#include <xtl.h>
#include <d3dx9.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include "../content/game_content.h"
#include "../game/game_session.h"
#include "frontend_assets.h"
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
    gDevice->Clear(0,0,D3DCLEAR_TARGET,0xffcccccc,1,0);
    gDevice->SetVertexShader(gVS);gDevice->SetPixelShader(gPS);gDevice->SetVertexDeclaration(gDecl);gDevice->SetTexture(0,gAtlas);
    gDevice->SetRenderState(D3DRS_ZENABLE,FALSE);gDevice->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    gDevice->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);gDevice->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);gDevice->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    gDevice->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT);gDevice->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT);
    gDevice->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);gDevice->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    if(ready)Menu();gDevice->Present(0,0,0,0);
}
VOID __cdecl main() {
    gTrace=fopen("game:\\port\\trace.log","wt");
    if(!InitGraphics())for(;;)Sleep(1000);
    bool ready=LoadAtlas()&&CheckOriginalBindings();gUi.Init(false);
    //Platform entropy adapter; original global initialization still requires validation.
    unsigned int seed=GetTickCount();if(!seed)seed=1;gMenuRandom.Seed(seed);
    LARGE_INTEGER frequency,last,now;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&last);double accumulator=0;
    for(;;){QueryPerformanceCounter(&now);double elapsed=(double)(now.QuadPart-last.QuadPart)/frequency.QuadPart;last=now;if(elapsed>0.25)elapsed=0.25;accumulator+=elapsed;
        while(ready&&accumulator>=1.0/30){accumulator-=1.0/30;XINPUT_STATE state;ZeroMemory(&state,sizeof(state));XInputGetState(0,&state);const XINPUT_GAMEPAD& p=state.Gamepad;
            PortMenuInput input={(p.wButtons&XINPUT_GAMEPAD_DPAD_UP)!=0||p.sThumbLY>16000,(p.wButtons&XINPUT_GAMEPAD_DPAD_DOWN)!=0||p.sThumbLY< -16000,
                (p.wButtons&XINPUT_GAMEPAD_A)!=0,(p.wButtons&(XINPUT_GAMEPAD_B|XINPUT_GAMEPAD_BACK))!=0,(p.wButtons&XINPUT_GAMEPAD_START)!=0,
                (p.wButtons&XINPUT_GAMEPAD_DPAD_LEFT)!=0||p.sThumbLX< -16000,(p.wButtons&XINPUT_GAMEPAD_DPAD_RIGHT)!=0||p.sThumbLX>16000,
                (p.wButtons&XINPUT_GAMEPAD_X)!=0,(p.wButtons&XINPUT_GAMEPAD_Y)!=0};
            PortAction action=gUi.Step(input);
            //Native profile disk format and complete run persistence remain open.
            if(action==UI_SELECT_SLOT){gUi.profile=gProfiles[gUi.selectedSlot];gUi.SlotLoaded(true,false);}
            if(action==UI_DELETE_SLOT){ZeroMemory(&gProfiles[gUi.selectedSlot],sizeof(NativeProfile));gUi.SlotDeleted(true);}
            if(action==UI_START_CHARACTER){unsigned int runSeed=GetTickCount();if(!runSeed)runSeed=1;PortSessionStart start=PortGameSession(gMenuRandom,gUi).StartNormalCharacter(runSeed,gNormalRuntime);Log("PORT normal setup result=%d seed=%u player=%d",start.result,runSeed,start.player);}
            if(gUi.characterPending){const NormalRunFlow& run=gNormalRuntime.run;
                NativeSceneFiles files={"game:\\game\\file_index.bin","game:\\game\\",run.context.stageType+1,run.context.nativeMode,0};
                NormalRunResult result=PortGameSession(gMenuRandom,gUi).AdvanceNormalSetup(gNormalRuntime,files);
                if(!gUi.characterPending)Log("PORT normal setup boundary phase=%d generation=%d result=%d resident=%d source=%d prerequisite=%08x",gNormalRuntime.phase,run.phase,result,gNormalRuntime.world.residentCount,gNormalRuntime.entryIssue.source,gNormalRuntime.missingNativePrerequisite);}
            if(action!=UI_NONE&&action!=UI_SELECT_SLOT&&action!=UI_DELETE_SLOT&&action!=UI_START_CHARACTER)Log("PORT pending native frontend destination=%d",action);
        }
        Render(ready);
    }
}
