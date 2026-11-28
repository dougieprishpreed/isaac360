// Addresses in the game's isaac-ng.exe (Repentance+ v1.9.7.17) that the runtime calls, patches or reads, found from
// v1.7.5's with tools/recomp/port_addresses.py (and by hand where the code changed). 0: not found yet, and the
// feature that needs it stays off. translate.py's HOST and PATCHES tables hold the hooked functions' own.
#pragma once
#include <stdint.h>

namespace game {
// the Game object's pointer, and the boss intro ("versus" screen) in it: its sprite, and Sprite::Load (shims_warm.cpp)
// (the transition F_82ee40, "[Frame %d] Starting room transition", at Game+0x1B83C as its callers pass it)
const uint32_t kGamePtr = 0xc71678, kIntro = 0x1b83c, kIntroSprite = 0x38, kSpriteLoaded = 0x109;
const uint32_t kSpriteLoad = 0x40bd50;
// the game's own load time totals (ms): ANM2 loading and entity spawns (shims_gl.cpp's room lines)
const uint32_t kAnm2Total = 0, kSpawnTotal = 0;
// the archive reader (hle_archive.cpp): the stream's vtable (KAGE::Filesys::ArchivedFile) and its Seek, its next
// block, an entry's ISAAC keystream (seed, round, a block XORed) and miniz's tinfl_decompress
const uint32_t kStreamVtable = 0xba05bc, kArchiveSeek = 0xa694a0, kArchiveNextBlock = 0xa68cf0;
const uint32_t kIsaacSeed = 0xaa93e0, kIsaacRound = 0xaa94a0, kIsaacXor = 0xa89d70, kTinfl = 0xa85710;
// zlib's inflate and libpng's row unfiltering (hle_inflate.cpp, hle_png.cpp: their checks call the game's)
const uint32_t kInflate = 0xaaf9d0, kPngFilterRow = 0xab2d80;
// stb_vorbis: vorbis_pump_first_frame (hle_vorbis.cpp)
const uint32_t kVorbisPumpFirstFrame = 0xaa50a0;
// the engine's lock object (KAGE::Mutex): its vtable (shims_kernel32.cpp's lock and reference count hooks)
const uint32_t kMutexVtable = 0xb81c0c;
// stb_vorbis_get_samples_short_interleaved (hle_vorbis.cpp's ISAAC360_VORBIS_CHECK)
const uint32_t kVorbisGetSamples = 0xaa7fb0;
// the player's vtable and Entity::SetColor (shims_looks.cpp's ISAAC360_TRACE_LOOKS)
const uint32_t kPlayerVtable = 0, kSetColor = 0;
// the engine's reference count add-ref (ISAAC360_BENCH)
const uint32_t kRefcountAdd = 0x40c690;
// the job worker's loop (0xA9E950, Repentance+'s worker thread): the return address of its queue lock's lock call;
// the worker object holds its flags at +0 (bit 1: stop), the queued count at +0x1C and the lock object at +0x20
// (shims_kernel32.cpp's idle wait). 0: not in this version.
const uint32_t kJobWorkerLockReturn = 0xa9e9b7;
}  // namespace game
