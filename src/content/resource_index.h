#ifndef GENERIC_RESOURCE_INDEX_H
#define GENERIC_RESOURCE_INDEX_H
#include "game_content.h"
#include <map>
#include <vector>
// Validated bulk view for repeated original-resource queries on host and XDK.
// Loading is atomic; unknown names retain their original two-hash identities.
class GpcResourceIndex {
    std::vector<GpcEntry> entries;
    std::map<uint64_t,size_t> effective;
public:
    GpcResult Load(FILE* stream);
    GpcResult LoadBytes(const unsigned char* data,size_t size);
    GpcResult FindHashes(uint32_t a,uint32_t b,GpcEntry* output) const;
    GpcResult FindOriginal(const char* relativeName,GpcEntry* output) const;
    size_t Count() const {return entries.size();}
    const GpcEntry* At(size_t n) const {return n<entries.size()?&entries[n]:0;}
};
#endif
