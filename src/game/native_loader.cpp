#include "native_loader.h"
NativeRoomResult NativeOpenLoadedStage(int stage,NativeLoadedStageView* output,int catalog) {
    if(!output || NativeStageSource(stage,catalog)<0) return NR_BAD_REQUEST;
    NativeLoadedStageView view={stage,-1,0,catalog};
    for(int i=0;i<NativeRoomCount(catalog);++i) {
        int id=NativeRoomId(catalog,i);const NativeRoomDef* data=NativeRoom(id); if(data->stage!=stage) continue;
        if(view.first<0) view.first=id;
        if(id!=view.first+view.count || data->ordinal!=view.count) return NR_BAD_REQUEST;
        ++view.count;
    }
    if(view.first<0) return NR_NO_MATCH;
    *output=view; return NR_OK;
}
int NativeLoadedStageView::Definition(int ordinal) const {
    int index=NativeRoomPoolIndex(first);
    if(index<0 || NativeRoomCatalog(first)!=catalog || count<0 || count>NativeRoomCount(catalog)-index || ordinal<0 || ordinal>=count) return -1;
    const NativeRoomDef* r=NativeRoom(first+ordinal);
    return r && r->stage==stage && r->ordinal==ordinal?first+ordinal:-1;
}
const NativeRoomDef* NativeLoadedStageView::At(int ordinal) const { return NativeRoom(Definition(ordinal)); }
unsigned int NativeLoadedRoomFlags(int stage,int type,unsigned int variant,unsigned int subtype) {
    // Saved switch007d3038: stages1..3 -> variant flag;29/30 -> subtype flag.
    // These flags do not reorder or remove records. Other loader paths are not
    // inferred from actor support or from the selected prepared package.
    if(stage>=1 && stage<=3 && (variant==603 || variant==716)) return 4;
    if((stage==29 || stage==30) && type==1 && subtype==31) return 2;
    return 0;
}
