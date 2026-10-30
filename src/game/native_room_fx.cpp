#include "native_room_fx.h"
#include "normal_run.h"
#include <algorithm>
#include <string.h>
struct NativeRoomFxRayDef {NativeSpriteSheet texture;unsigned int opacity,speed;};
struct NativeRoomFxRayGroupDef {unsigned int values[7];int first,count;bool matchesFirstChapter;};
#include "native_room_fx_data.h"
namespace {
float F(unsigned int n){float f;memcpy(&f,&n,4);return f;}
float Add(float a,float b){volatile float f=a+b;return f;}
float Sub(float a,float b){volatile float f=a-b;return f;}
float Mul(float a,float b){volatile float f=a*b;return f;}
float Div(float a,float b){volatile float f=a/b;return f;}
//Native796de0 returns a rounded float; the caller's x87 multiply/add is
//rounded only at FSTP. Keep that boundary distinct from scalar SSE operands.
float RandomBetween(NativeRng& r,float lo,float hi){float f;r.Float(&f);volatile float v=(float)((double)f*(double)Sub(hi,lo)+(double)lo);return v;}
}
const NativeSpriteAsset* NativePreparedRoomVeins(){return &kRoomVeinsSprite;}
void NativeRoomFxParams::Reset(){
    for(int i=0;i<6;++i)modifier[i]=(i<3||i==5)?1.0f:0;
    native18=false;
    for(int i=0;i<4;++i){native1c[i]=i==3?F(0x3f19999a):F(0x3d23d70a);native2c[i]=1;}
    shadowAlpha=0.5f;native40[0]=F(0x3d75c28f);native40[1]=F(0x3da3d70a);native40[2]=F(0x3e051eb8);native40[3]=1;
    for(int i=0;i<3;++i)native50[i]=1;
    for(int i=0;i<12;++i)native5c[i]=i==0?F(0x3f19999a):i<5?1.0f:0;
}
NativeRoomFx::NativeRoomFx():begun(false),ready(false),nextAddress(0),initialSeed(0),definition(-1),stage(0),stageType(0),selectedRay(-1),shadingTexture(0),shadowAlpha(0){
    random.Default();memset(&lighting,0,sizeof(lighting));memset(rays,0,sizeof(rays));memset(groups,0,sizeof(groups));memset(edges,0,sizeof(edges));memset(colorModifier,0,sizeof(colorModifier));parameters.Reset();
}
bool NativeRoomFx::PrepareInitial(const NormalRunFlow& run,NativeMt& global){
    const NativePlacedRoom* selected=run.layout.ByCell(run.currentGrid,run.placement.dimension);
    const NativeRoomDef* d=selected?NativeRoom(selected->definition):0;
    if(!d||run.phase!=NRF_NEED_ROOM_ENTRY||run.currentGrid!=run.previousGrid||run.context.stage!=1||run.context.nativeMode!=0||
       run.context.challenge||run.context.stageType<0||run.context.stageType>1||d->shape!=1||d->type!=1||selected->visitedCount||!global.Valid())return false;
    if(ready)return definition==selected->definition&&stageType==run.context.stageType;
    //A stopped native initialization retains the already consumed parent
    //draw and child cursor. Reinvocation cannot replay any partial FX work.
    if(begun)return false;
    const NativeSpriteAsset* asset=NativePreparedSprite("gfx/Veins.anm2");
    if(!asset)return false;
    if(!begun){
        //7a0ce3 ->79ed70 ->8d55c0. This draw PRECEDES main-pass factories,
        //door seeds and perimeter walls; it is not a room/award RNG clone.
        initialSeed=global.Next();random.Set(initialSeed,13);begun=true;
        definition=selected->definition;stage=run.context.stage;stageType=run.context.stageType;
        nextAddress=0x8d58f3;
    }
    //All other fxlayers.xml records fail their genuine stage/type conditions
    //at8d6890. NativeFX7 selects shape1 textures in original XML order.
    NativeRng local;local.Set(selected->seeds.spawn,35);unsigned int n;
    local.Int(5,&n);lighting.texture=kRoomLightingTextures+n;
    lighting.layer=4;lighting.parallax=0.025f;
    lighting.velocity.x=RandomBetween(local,-10,10);lighting.velocity.y=RandomBetween(local,-10,10);
    lighting.position.x=RandomBetween(local,0,(float)lighting.texture->width);
    lighting.position.y=RandomBetween(local,0,(float)lighting.texture->height);
    float f;local.Float(&f);lighting.flipX=f<0.5f;local.Float(&f);lighting.flipY=f<0.5f;
    //8d76b1 draws one phase per fxRay child BEFORE7b04 checks the stage
    //match. Unmatched6ab groups still load their textures and draw phases;
    //their temporary owners are then discarded, not appended to live groups.
    int group=0,ray=0;
    for(unsigned int g=0;g<sizeof(kRoomRayGroups)/sizeof(kRoomRayGroups[0]);++g){
        const NativeRoomFxRayGroupDef& src=kRoomRayGroups[g];
        if(src.matchesFirstChapter){NativeRoomFxRayGroup& out=groups[group++];
            out.minimum=F(src.values[0]);out.maximum=F(src.values[1]);out.topParallax=F(src.values[2]);out.bottomParallax=F(src.values[3]);
            out.length=F(src.values[4]);out.spacing=F(src.values[5]);out.perspective=F(src.values[6]);out.first=ray;out.count=src.count;}
        for(int j=0;j<src.count;++j){int i=src.first+j;const NativeRoomFxRayDef& def=kRoomRayDefs[i];
            random.Float(&f);if(src.matchesFirstChapter){NativeRoomFxRay& out=rays[ray++];out.texture=&def.texture;out.opacity=F(def.opacity);out.speed=F(def.speed);out.phase=Mul(f,F(kRoomRayPhaseScale));}}
    }
    //8d7bd9 resets the temporary owner from descriptor spawn for selection.
    local.Set(selected->seeds.spawn,35);local.Int(2,&n);selectedRay=(int)n;
    local.Int(3,&n);if(n)selectedRay=-1;
    colorModifier[0]=colorModifier[1]=colorModifier[2]=colorModifier[5]=1;colorModifier[3]=colorModifier[4]=0;
    shadowAlpha=F(kRoomShadowAlpha);
    parameters.Reset();parameters.shadowAlpha=shadowAlpha;
    nextAddress=0x8d7d5a;
    for(int i=0;i<2;++i)if(veins[i].Load("gfx/Veins.anm2")!=NSPR_OK)return false;
    if(veins[0].Play("Vein1")!=NSPR_OK||veins[1].Play("Vein2")!=NSPR_OK)return false;
    //8d7e90..8d808d: ten positions around the pre-grid physical rectangle.
    NativeRoomGrid grid;NativeRoomSpace space;
    if(!grid.Init(d->shape,d->width,d->height)||!space.Init(d->shape,grid,run.floor.plan.configStage,d->type))return false;
    float w=Sub(space.right,space.left),h=Sub(space.bottom,space.top);
    for(int i=0;i<10;++i){float along=Mul(Add(Add(h,h),Add(w,w)),Div((float)i,10));NativeRoomFxEdge& e=edges[i];
        if(w>along){e.position.x=Add(Mul(Div(along,w),w),space.left);e.position.y=space.top;e.rotation=180;}
        else if(Add(h,w)>along){along=Sub(along,w);e.position.x=space.right;e.position.y=Add(Mul(Div(along,h),h),space.top);e.rotation=-90;}
        else if(Add(Add(w,w),h)>along){along=Sub(Sub(along,w),h);e.position.x=Add(Mul(Div(along,w),w),space.left);e.position.y=space.bottom;e.rotation=0;}
        else {along=Sub(Sub(along,Add(w,w)),h);e.position.x=space.left;e.position.y=Add(Mul(Div(along,h),h),space.top);e.rotation=90;}
        random.Next(&n);e.variant=(int)(n&1);
    }
    //79f81b..843 selects shading.png for shape1, backdrop!=11.79f8db
    //creates its original texture surface; retain that binding without a GPU.
    shadingTexture=&kRoomShadingTexture;
    nextAddress=0;ready=true;return true;
}
void NativeRoomFx::Swap(NativeRoomFx& b){
    std::swap(begun,b.begun);std::swap(ready,b.ready);std::swap(nextAddress,b.nextAddress);std::swap(initialSeed,b.initialSeed);
    std::swap(definition,b.definition);std::swap(stage,b.stage);std::swap(stageType,b.stageType);std::swap(selectedRay,b.selectedRay);std::swap(random,b.random);
    std::swap(lighting,b.lighting);for(int i=0;i<8;++i)std::swap(rays[i],b.rays[i]);for(int i=0;i<2;++i){std::swap(groups[i],b.groups[i]);veins[i].Swap(b.veins[i]);}
    std::swap(shadingTexture,b.shadingTexture);
    for(int i=0;i<10;++i)std::swap(edges[i],b.edges[i]);std::swap(shadowAlpha,b.shadowAlpha);for(int i=0;i<6;++i)std::swap(colorModifier[i],b.colorModifier[i]);
    std::swap(parameters,b.parameters);
}
