#include "resource_index.h"
#include <string.h>
#include <set>
#include <stdio.h>
namespace {
unsigned int Le(const unsigned char* p,int n) {unsigned int v=0;for(int i=0;i<n;++i)v|=(unsigned int)p[i]<<(8*i);return v;}
uint64_t Key(unsigned int a,unsigned int b) {return ((uint64_t)a<<32)|b;}
int Rank(unsigned int package) {
    static const unsigned int mounts[]={7,8,9,10,11,19,20,21,1,3,2,4,6,5,12,13,14,15,16,17,18};
    for(int i=0;i<21;++i)if(mounts[i]==package)return i;return -1;
}
bool Safe(const char* p) {
    if(strncmp(p,"objects/",8))return false;
    unsigned int length=0;
    for(;*p;++p) {
        if((*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='.') {if(++length>36||(*p=='.'&&p[1]=='.'))return false;}
        else if(*p=='/'&&length)length=0;else return false;
    }
    return length!=0;
}
}
GpcResult GpcResourceIndex::Load(FILE* stream) {
    if(!stream)return GPC_BAD_FORMAT;
    if(fseek(stream,0,SEEK_END))return GPC_IO_ERROR;
    long size=ftell(stream);if(size<12||fseek(stream,0,SEEK_SET))return GPC_IO_ERROR;
    std::vector<unsigned char> bytes((size_t)size);
    if(fread(&bytes[0],1,bytes.size(),stream)!=bytes.size())return GPC_IO_ERROR;
    return LoadBytes(&bytes[0],bytes.size());
}
GpcResult GpcResourceIndex::LoadBytes(const unsigned char* data,size_t size) {
    if(!data||size<12)return GPC_IO_ERROR;
    const unsigned char* header=data;size_t at=12;
    if(memcmp(header,"GPCIDX1\0",8)||Le(header+8,4)>1000000)return GPC_BAD_FORMAT;
    GpcResourceIndex next;std::set<std::pair<unsigned int,uint64_t> > seen;
    for(unsigned int i=0;i<Le(header+8,4);++i) {
        GpcEntry e;memset(&e,0,sizeof(e));
        if(size-at<56)return GPC_IO_ERROR;
        const unsigned char* r=data+at;at+=56;
        unsigned int nl=Le(r+4,2),pl=Le(r+6,2);
        if(!nl||nl>=sizeof(e.virtualName)||!pl||pl>=sizeof(e.objectPath))return GPC_BAD_FORMAT;
        if(size-at<nl+pl)return GPC_IO_ERROR;
        memcpy(e.virtualName,data+at,nl);memcpy(e.objectPath,data+at+nl,pl);at+=nl+pl;
        if(strlen(e.virtualName)!=nl||strlen(e.objectPath)!=pl||!Safe(e.objectPath))return GPC_BAD_FORMAT;
        e.package=(uint16_t)Le(r,2);e.flags=(uint16_t)Le(r+2,2);
        e.hash1=Le(r+16,4);e.hash2=Le(r+20,4);e.size=(uint64_t)Le(r+8,4)|((uint64_t)Le(r+12,4)<<32);memcpy(e.sha256,r+24,32);
        unsigned int a,b;GpcHashName(e.virtualName,&a,&b);
        if(!(e.flags&1)&&(a!=e.hash1||b!=e.hash2))return GPC_BAD_FORMAT;
        uint64_t key=Key(e.hash1,e.hash2);
        if(!seen.insert(std::make_pair((unsigned int)e.package,key)).second)return GPC_BAD_FORMAT;
        int rank=Rank(e.package);
        std::map<uint64_t,size_t>::iterator found=next.effective.find(key);
        if(rank>=0&&(found==next.effective.end()||rank>Rank(next.entries[found->second].package)))next.effective[key]=next.entries.size();
        next.entries.push_back(e);
    }
    if(at!=size)return GPC_BAD_FORMAT;
    entries.swap(next.entries);effective.swap(next.effective);return GPC_OK;
}
GpcResult GpcResourceIndex::FindHashes(uint32_t a,uint32_t b,GpcEntry* out) const {
    if(!out)return GPC_BAD_FORMAT;
    std::map<uint64_t,size_t>::const_iterator at=effective.find(Key(a,b));
    if(at==effective.end())return GPC_NOT_FOUND;*out=entries[at->second];return GPC_OK;
}
GpcResult GpcResourceIndex::FindOriginal(const char* name,GpcEntry* out) const {
    if(!out||!GpcValidRelativeName(name))return GPC_BAD_FORMAT;
    static const char* roots[]={"resources-dlc3/","resources/",""};char full[512];
    for(int i=0;i<3;++i) {
        sprintf(full,"%s%s",roots[i],name);unsigned int a,b;GpcHashName(full,&a,&b);
        GpcResult result=FindHashes(a,b,out);if(result!=GPC_NOT_FOUND)return result;
    }
    return GPC_NOT_FOUND;
}
