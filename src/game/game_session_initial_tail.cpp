#include "game_session.h"
#include "native_room_view.h"
#include "native_room_update.h"
#include <stdio.h>
NativeWorldResult PortGameSession::ContinueInitialGeneration(PortNormalRuntime& runtime,NativeActionSource* actions){
    NativeWorldResident& live=runtime.world.resident[runtime.world.currentList];
    unsigned int& address=live.roomInitAddress;unsigned int& next=runtime.missingNativePrerequisite;
    if(address==0x7990b0){
        if(!frontend.music.EnterStartingRoom(runtime.run,runtime.world,runtime.playerState,&next))return NWR_CONSTRUCTION_GAP;
        address=0x6f4e20;
    }
    if(address==0x6f4e20){
        if(!runtime.minimaps.EnterInitial(runtime.run,runtime.world,runtime.playerState,&next))return NWR_CONSTRUCTION_GAP;
        runtime.playerManager.resourceUi.Changed();address=0x905cd0;
    }
    if(address==0x905cd0){
        //6fab41's stage11 block and6fac60's seed effect48 do not execute
        //for this actual normal, numeric-seed first-floor selection.
        if(!runtime.playerManager.PlaceInitial(runtime.run,runtime.world,runtime.playerCreation,runtime.playerState,runtime.playerBody,&next))return NWR_CONSTRUCTION_GAP;
        address=0x88c610;
    }
    if(address==0x88c610){
        //The copy at6face9 retains indeterminate UI payload as indeterminate
        //until its original owning HUD service supplies it. It is never a
        //zero-valued gameplay input or a fabricated save field.
        unsigned int value;
        if(runtime.history.fields.Get(0x1a9f4,4,&value))runtime.history.fields.Put(0x1a9f0,value);
        else runtime.history.fields.Unwritten(0x1a9f0,4);
        if(!live.view||!live.view->FinishInitialPlacement(*live.room,runtime.playerBody,runtime.playerCreation,runtime.playerManager,runtime.history,&next))return NWR_CONSTRUCTION_GAP;
        //4092b0 drains renderer-owned deferred resources. The headless shared
        //runtime has no device resource handles to enqueue for destruction.
        if(!runtime.playerManager.EmptyLevelInitCallback()){next=0x7f2f30;return NWR_CONSTRUCTION_GAP;}
        runtime.history.fields.Put(0x18230,0,1);address=0x76c990;
    }
    if(address==0x76c990){
        //The generation caller has returned at6fad46. Only now may the
        //explicit6b4b88 call apply the initial grants and complete stat cache.
        if(!runtime.playerCreation.FinishPostGeneration(runtime.run.player,runtime.playerStartup,runtime.playerBody,runtime.playerSprites,runtime.playerState,runtime.playerManager,&next))return NWR_CONSTRUCTION_GAP;
        address=0x8a1450;
    }
    if(address==0x8a1450){
        if(!runtime.initialSave.captured&&!runtime.initialSave.CaptureBeforeFileSave(runtime.run,runtime.world,runtime.gameRandom,runtime.itemPools,runtime.nightmares,runtime.history,
            runtime.playerState,runtime.playerStartup,runtime.playerCreation,runtime.playerSprites,runtime.playerBody,runtime.playerManager,&next))return NWR_CONSTRUCTION_GAP;
        //8a14cc..d8: Main+1ea690 is the actual selected character, assigned
        //by8a1070's first argument at8a10ac (892d62 supplies the selection).
        runtime.initialSave.fields.Put(0x19c94,runtime.run.player.player);
        address=0x861d60;
    }
    if(address==0x861d60){
        runtime.initialSave.fields.Put(0x19c98,frontend.AnyEnabledMod()?1:0,1);
        address=0x8a163d; //No Steam storage interface in either offline adapter.
    }
    if(address==0x8a163d){
        //8a163d/8a1745 are New Run publication services. A native Continue
        //loads through the bootstrap but must not replace the selected file or
        //rewrite its slot metadata with the stage1 scaffold.
        if(runtime.suppressInitialNewRunPublish)address=0x906310;
        else {
            std::string primary,rerun;
            if(!frontend.LocalRunFilenames(&primary,&rerun)){next=0x8a163d;return NWR_CONSTRUCTION_GAP;}
            //Preflight the complete native byte codec before publishing anything.
            //Unsupported capture cannot truncate the previously valid local save.
            NativeSaveWriter writer;
            if(!writer.InitialGame(runtime.initialSave)){
                runtime.initialSaveFile=writer;next=writer.nextAddress;return NWR_CONSTRUCTION_GAP;
            }
            runtime.initialSaveFile=writer;
            remove(rerun.c_str()); //90bc00's actual local rerun deletion.
            FILE* file=fopen(primary.c_str(),"wb");
            if(file){const unsigned int count=(unsigned int)writer.data.size();
                const bool wrote=fwrite(&writer.data[0],1,count,file)==count;
                const bool closed=fclose(file)==0;runtime.initialSaveFileWritten=wrote&&closed;
            }
            //8a1745: the native wrapper next marks PersistentGameData dirty and
            //updates its file-slot metadata. That owner must precede906310.
            address=0x8a1745;
        }
    }
    if(address==0x8a1745){
        unsigned int checksum;
        if(!runtime.initialSave.fields.Get(0x1fb48,4,&checksum)){next=0x8a1748;return NWR_CONSTRUCTION_GAP;}
        if(!frontend.profileStorage.SaveAfterNewRun(frontend.profile,checksum,frontend.saveDirectory,frontend.selectedSlot,&next))return NWR_CONSTRUCTION_GAP;
        address=0x906310;
    }
    if(address==0x906310){
        if(!runtime.playerCreation.FinishInitialManager(runtime.playerState,runtime.playerBody,&next))return NWR_CONSTRUCTION_GAP;
        //906393's real type3 list is empty. Only variants203/221 would
        //receive a familiar update; no RNG or actor update is called here.
        for(unsigned int i=0;i<runtime.playerManager.worldTypes.size();++i)
            if(runtime.playerManager.worldTypes[i]==3){next=0x9063b2;return NWR_CONSTRUCTION_GAP;}
        runtime.history.fields.Put(0x1c2ee4,0x3f800000U);runtime.history.fields.Put(0x1c2eec,0x3d23d70aU);
        runtime.playerCreation.constructorFields.AssignBytes(0x358,0,1);
        address=0x901240;
    }
    if(address==0x901240){
        if(!runtime.pauseUi.ResetForNewRun(&next))return NWR_CONSTRUCTION_GAP;
        //6b4bff copies this vector's begin to its end. Preserve the logical
        //owner/capacity; a native allocation address is not a scalar input.
        runtime.history.native243364.clear();address=0x7ae620;
    }
    if(address==0x7ae620){
        if(!live.update||!live.update->PrepareInitialActors(runtime,actions,&next))return NWR_CONSTRUCTION_GAP;
        address=0; //The reached initial7ae620 owner returned after its actor/collision pass.
    }
    if(address==0){next=0;return NWR_OK;}
    next=address;return NWR_CONSTRUCTION_GAP;
}
