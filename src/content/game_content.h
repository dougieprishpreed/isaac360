#ifndef GENERIC_GAME_CONTENT_H
#define GENERIC_GAME_CONTENT_H

/* C++98-compatible content index reader. No desktop or console SDK dependency. */
#include <stdint.h>
#include <stdio.h>

enum GpcResult {
    GPC_OK = 0,
    GPC_NOT_FOUND = 1,
    GPC_BAD_FORMAT = 2,
    GPC_IO_ERROR = 3
};

struct GpcEntry {
    uint16_t package;
    uint16_t flags;
    uint64_t size;
    uint32_t hash1;
    uint32_t hash2;
    unsigned char sha256[32];
    char virtualName[512];
    char objectPath[128];
};

/* Names are UTF-8 bytes; only ASCII uppercase and backslash are normalized. */
void GpcHashName(const char* name, uint32_t* hash1, uint32_t* hash2);
// Reject parent-directory components, not consecutive punctuation inside a
// legitimate filename (several original animation names end in two dots).
bool GpcValidRelativeName(const char* name);

/* Caller chooses package precedence. This function never guesses mount order.
 * index must be an open game/file_index.bin stream; ownership remains with caller.
 * A returned objectPath is relative to the game/ data directory. */
GpcResult GpcFindInPackage(FILE* index, uint16_t package,
                         uint32_t hash1, uint32_t hash2, GpcEntry* entry);

/* Matching saved program startup table 00b887a0, loop 008f7350 and
 * replacement at 00950420. Later mounts replace BOTH matching hashes.
 * Resource roots (008f8500/0a, 0094fb90) are searched in reverse order.
 * This is the saved program's default, unlocalized, no-mod setup, not a DLC
 * chooser or a claim that the observed title identifies all gameplay content. */
GpcResult GpcFindOriginalResource(FILE* index,const char* relativeName,GpcEntry* entry);

#endif
