#ifndef BOI360_PORT_DATA_H
#define BOI360_PORT_DATA_H
// Every file the game reads at run time goes through here (docs/XSDK_PLAN.md, "Data").
// * Names are relative to the data root, with '/': "game/file_index.bin", "port/audio/...".
// * The Windows binary reads loose files under its root (runtime/). The console reads one pack, boi360.pak
//   (tools/stage_console.py), opened once: on its USB drive every file open costs ~110 ms and stdio refills
//   1 KB per request, so loose files and fread would make room loads and sounds stall.
// * A pack is read with positioned reads in 256 KB pieces and never past 2 GB (xsdk's off_t is 32-bit).
// * Saves are not data: they stay plain files in the writable save folder.
#include "resource_index.h"
#include <string>
#include <vector>

// Mounts the data root. A root holding boi360.pak reads from the pack, otherwise from loose files.
bool PortDataMount(const char* root);
bool PortDataPacked();
const std::string& PortDataRoot();
// Resolves a caller's path (relative, or one under the mounted root) to the relative name the pack uses.
std::string PortDataName(const char* path);
// Whole files; false when missing. limit guards against absurd sizes.
bool PortDataRead(const char* path, std::vector<unsigned char>& out, unsigned int limit = 64u * 1024u * 1024u);
bool PortDataSize(const char* path, unsigned int* size);
// The content index (game/file_index.bin), loaded once per path and kept.
const GpcResourceIndex* PortDataContentIndex(const char* path);

// Sequential reads with seeks, buffered in 64 KB (music streams, WAV headers).
class PortDataStream {
public:
    PortDataStream();
    ~PortDataStream();
    bool Open(const char* path);
    void Close();
    bool IsOpen() const { return m_open; }
    unsigned int Size() const { return m_size; }
    unsigned int Tell() const { return m_pos; }
    bool Seek(unsigned int offset);
    bool Skip(unsigned int bytes) { return Seek(m_pos + bytes); }
    unsigned int Read(void* out, unsigned int bytes);
private:
    bool Fill();
    bool m_open;
    void* m_file;                       // loose files (FILE*)
    unsigned int m_base, m_size, m_pos; // pack entries: offset in the pack
    std::vector<unsigned char> m_buf;
    unsigned int m_bufStart, m_bufLen;
    PortDataStream(const PortDataStream&);
    PortDataStream& operator=(const PortDataStream&);
};

#endif
