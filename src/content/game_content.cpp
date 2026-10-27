#include "game_content.h"
#include <string.h>

static uint16_t GpcU16(const unsigned char* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t GpcU32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t GpcU64(const unsigned char* p) {
    return (uint64_t)GpcU32(p) | ((uint64_t)GpcU32(p + 4) << 32);
}

void GpcHashName(const char* name, uint32_t* hash1, uint32_t* hash2) {
    uint32_t a = 0x1505U;
    uint32_t b = 0x5bb2220eU;
    const unsigned char* p = (const unsigned char*)name;
    while (*p) {
        unsigned char c = *p++;
        if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + ('a' - 'A'));
        if (c == '\\') c = '/';
        a = a * 33U + (uint32_t)c;
        b = (b ^ (uint32_t)c) * 0x1000193U;
    }
    *hash1 = a;
    *hash2 = b;
}

static bool GpcSafeObjectPath(const char* path) {
    if (strncmp(path, "objects/", 8) != 0) return false;
    unsigned int componentLength = 0;
    for (const char* p = path; *p; ++p) {
        char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.') {
            if (++componentLength > 36) return false;
            if (c == '.' && p[1] == '.') return false;
        } else if (c == '/' && componentLength != 0) {
            componentLength = 0;
        } else {
            return false;
        }
    }
    return componentLength != 0;
}

GpcResult GpcFindInPackage(FILE* index, uint16_t package,
                         uint32_t hash1, uint32_t hash2, GpcEntry* entry) {
    if (!index || !entry) return GPC_BAD_FORMAT;
    if (fseek(index, 0, SEEK_SET) != 0) return GPC_IO_ERROR;
    unsigned char header[12];
    if (fread(header, 1, sizeof(header), index) != sizeof(header)) return GPC_IO_ERROR;
    if (memcmp(header, "GPCIDX1\0", 8) != 0) return GPC_BAD_FORMAT;
    uint32_t count = GpcU32(header + 8);
    if (count > 1000000U) return GPC_BAD_FORMAT;
    for (uint32_t i = 0; i < count; ++i) {
        unsigned char record[56];
        if (fread(record, 1, sizeof(record), index) != sizeof(record)) return GPC_IO_ERROR;
        uint16_t nameLength = GpcU16(record + 4);
        uint16_t pathLength = GpcU16(record + 6);
        if (nameLength == 0 || nameLength >= sizeof(entry->virtualName) ||
            pathLength == 0 || pathLength >= sizeof(entry->objectPath)) return GPC_BAD_FORMAT;
        if (fread(entry->virtualName, 1, nameLength, index) != nameLength ||
            fread(entry->objectPath, 1, pathLength, index) != pathLength) return GPC_IO_ERROR;
        entry->virtualName[nameLength] = 0;
        entry->objectPath[pathLength] = 0;
        if (!GpcSafeObjectPath(entry->objectPath)) return GPC_BAD_FORMAT;
        uint32_t actual1, actual2;
        GpcHashName(entry->virtualName, &actual1, &actual2);
        if (!(GpcU16(record + 2) & 1U) &&
            (actual1 != GpcU32(record + 16) || actual2 != GpcU32(record + 20))) return GPC_BAD_FORMAT;
        if (GpcU16(record) == package && GpcU32(record + 16) == hash1 && GpcU32(record + 20) == hash2) {
            entry->package = GpcU16(record);
            entry->flags = GpcU16(record + 2);
            entry->size = GpcU64(record + 8);
            entry->hash1 = hash1;
            entry->hash2 = hash2;
            memcpy(entry->sha256, record + 24, 32);
            return GPC_OK;
        }
    }
    return GPC_NOT_FOUND;
}

bool GpcValidRelativeName(const char* name) {
    if(!name || !*name || strlen(name)>450 || strchr(name,':') || *name=='/' || *name=='\\') return false;
    const char* start=name;
    for(const char* p=name;;++p) if(!*p || *p=='/' || *p=='\\') {
        if(p-start==2 && start[0]=='.' && start[1]=='.') return false;
        if(!*p) return true;
        start=p+1;
    }
}
GpcResult GpcFindOriginalResource(FILE* index,const char* name,GpcEntry* entry) {
    if(!index || !entry || !GpcValidRelativeName(name)) return GPC_BAD_FORMAT;
    // Exact saved startup registration order, independent of package numbering.
    static const uint16_t mounts[]={7,8,9,10,11,19,20,21,1,3,2,4,6,5,12,13,14,15,16,17,18};
    static const char* roots[]={"resources-dlc3/","resources/",""};
    uint32_t hashes[3][2]; char path[512];
    for(int r=0;r<3;++r) { sprintf(path,"%s%s",roots[r],name); GpcHashName(path,&hashes[r][0],&hashes[r][1]); }
    if(fseek(index,0,SEEK_SET)!=0) return GPC_IO_ERROR;
    unsigned char header[12];
    if(fread(header,1,12,index)!=12) return GPC_IO_ERROR;
    if(memcmp(header,"GPCIDX1\0",8) || GpcU32(header+8)>1000000U) return GPC_BAD_FORMAT;
    int best=-1; GpcEntry chosen; memset(&chosen,0,sizeof(chosen));
    for(uint32_t i=0;i<GpcU32(header+8);++i) {
        unsigned char record[56]; GpcEntry candidate;
        if(fread(record,1,56,index)!=56) return GPC_IO_ERROR;
        uint16_t nl=GpcU16(record+4),pl=GpcU16(record+6);
        if(!nl || nl>=sizeof(candidate.virtualName) || !pl || pl>=sizeof(candidate.objectPath)) return GPC_BAD_FORMAT;
        if(fread(candidate.virtualName,1,nl,index)!=nl || fread(candidate.objectPath,1,pl,index)!=pl) return GPC_IO_ERROR;
        candidate.virtualName[nl]=candidate.objectPath[pl]=0;
        if(!GpcSafeObjectPath(candidate.objectPath)) return GPC_BAD_FORMAT;
        candidate.package=GpcU16(record); candidate.flags=GpcU16(record+2);
        candidate.hash1=GpcU32(record+16); candidate.hash2=GpcU32(record+20);
        uint32_t a,b; GpcHashName(candidate.virtualName,&a,&b);
        if(!(candidate.flags&1) && (a!=candidate.hash1 || b!=candidate.hash2)) return GPC_BAD_FORMAT;
        for(int r=0;r<3;++r) if(candidate.hash1==hashes[r][0] && candidate.hash2==hashes[r][1]) {
            for(int m=0;m<21;++m) if(candidate.package==mounts[m]) {
                int rank=(2-r)*21+m;
                if(rank==best) return GPC_BAD_FORMAT; // Ambiguous duplicate within a mount.
                if(rank>best) { candidate.size=GpcU64(record+8); memcpy(candidate.sha256,record+24,32); chosen=candidate; best=rank; }
            }
        }
    }
    if(best<0) return GPC_NOT_FOUND;
    *entry=chosen; return GPC_OK;
}
