#include "port_data.h"
#include <map>
#include <stdio.h>
#include <string.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

// Pack layout (little-endian, tools/stage_console.py):
//   "B360PAK1", u32 count, u32 nameBytes,
//   count x { u32 nameOffset, u32 nameLength, u32 offset, u32 size }  sorted by name (bytewise),
//   names, then the files' data. Names are relative to the data root, lower case, with '/'.
namespace {
const unsigned int kPiece = 256u * 1024u;        // one read never holds the drive longer than this
const unsigned int kLimit = 0x7fffffffu;          // xsdk's lseek/pread take a 32-bit off_t

struct PackEntry { unsigned int name, nameLength, offset, size; };

std::string gRoot;
bool gPacked = false;
std::vector<PackEntry> gEntries;
std::vector<char> gNames;
#if defined(_WIN32)
FILE* gPack = 0;
#else
int gPack = -1;
#endif
std::map<std::string, GpcResourceIndex*> gIndexes;

unsigned int Le32(const unsigned char* p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

bool PackReadAt(unsigned int offset, void* out, unsigned int bytes) {
    if (bytes == 0) return true;
    if (offset > kLimit || bytes > kLimit - offset) return false;
    unsigned char* dst = (unsigned char*)out;
    while (bytes) {
        unsigned int n = bytes < kPiece ? bytes : kPiece;
#if defined(_WIN32)
        if (!gPack || _fseeki64(gPack, (long long)offset, SEEK_SET) != 0 || fread(dst, 1, n, gPack) != n) return false;
#else
        if (gPack < 0) return false;
        ssize_t got = pread(gPack, dst, n, (off_t)offset);
        if (got != (ssize_t)n) return false;
#endif
        dst += n;
        offset += n;
        bytes -= n;
    }
    return true;
}

const PackEntry* PackFind(const std::string& name) {
    size_t lo = 0, hi = gEntries.size();
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        const PackEntry& e = gEntries[mid];
        int c = strncmp(&gNames[e.name], name.c_str(), e.nameLength);
        if (c == 0) c = e.nameLength == name.size() ? 0 : (e.nameLength < name.size() ? -1 : 1);
        if (c == 0) return &e;
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    return 0;
}

bool OpenPack(const std::string& path) {
#if defined(_WIN32)
    gPack = fopen(path.c_str(), "rb");
    if (!gPack) return false;
#else
    gPack = open(path.c_str(), O_RDONLY);
    if (gPack < 0) return false;
#endif
    unsigned char header[16];
    if (!PackReadAt(0, header, 16) || memcmp(header, "B360PAK1", 8) != 0) return false;
    unsigned int count = Le32(header + 8), nameBytes = Le32(header + 12);
    if (count > 1000000u || nameBytes > 64u * 1024u * 1024u) return false;
    std::vector<unsigned char> table((size_t)count * 16);
    gNames.resize(nameBytes + 1);
    if (!PackReadAt(16, table.empty() ? 0 : &table[0], (unsigned int)table.size()) ||
        !PackReadAt(16 + count * 16, &gNames[0], nameBytes))
        return false;
    gNames[nameBytes] = 0;
    gEntries.resize(count);
    for (unsigned int i = 0; i < count; ++i) {
        const unsigned char* r = &table[(size_t)i * 16];
        PackEntry& e = gEntries[i];
        e.name = Le32(r);
        e.nameLength = Le32(r + 4);
        e.offset = Le32(r + 8);
        e.size = Le32(r + 12);
        if (e.name > nameBytes || e.nameLength > nameBytes - e.name || e.offset > kLimit || e.size > kLimit - e.offset)
            return false;
    }
    return true;
}

void ClosePack() {
#if defined(_WIN32)
    if (gPack) fclose(gPack);
    gPack = 0;
#else
    if (gPack >= 0) close(gPack);
    gPack = -1;
#endif
    gEntries.clear();
    gNames.clear();
}

std::string LoosePath(const std::string& name) { return gRoot + name; }
}

bool PortDataMount(const char* root) {
    ClosePack();
    gRoot = root ? root : "";
    for (size_t i = 0; i < gRoot.size(); ++i)
        if (gRoot[i] == '\\') gRoot[i] = '/';
    if (!gRoot.empty() && gRoot[gRoot.size() - 1] != '/') gRoot += '/';
    gPacked = OpenPack(gRoot + "boi360.pak");
    if (!gPacked) ClosePack();
    return true;
}

bool PortDataPacked() { return gPacked; }
const std::string& PortDataRoot() { return gRoot; }

std::string PortDataName(const char* path) {
    std::string name = path ? path : "";
    for (size_t i = 0; i < name.size(); ++i) {
        if (name[i] == '\\') name[i] = '/';
        if (name[i] >= 'A' && name[i] <= 'Z') name[i] = (char)(name[i] - 'A' + 'a');
    }
    std::string root = gRoot;
    for (size_t i = 0; i < root.size(); ++i)
        if (root[i] >= 'A' && root[i] <= 'Z') root[i] = (char)(root[i] - 'A' + 'a');
    if (!root.empty() && name.compare(0, root.size(), root) == 0) name.erase(0, root.size());
    while (name.compare(0, 2, "./") == 0) name.erase(0, 2);
    return name;
}

bool PortDataSize(const char* path, unsigned int* size) {
    std::string name = PortDataName(path);
    if (gPacked) {
        const PackEntry* e = PackFind(name);
        if (!e) return false;
        if (size) *size = e->size;
        return true;
    }
    FILE* f = fopen(LoosePath(name).c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    if (n < 0 || (unsigned long)n > kLimit) return false;
    if (size) *size = (unsigned int)n;
    return true;
}

bool PortDataRead(const char* path, std::vector<unsigned char>& out, unsigned int limit) {
    std::string name = PortDataName(path);
    out.clear();
    if (gPacked) {
        const PackEntry* e = PackFind(name);
        if (!e || e->size > limit) return false;
        out.resize(e->size);
        return PackReadAt(e->offset, out.empty() ? 0 : &out[0], e->size);
    }
    FILE* f = fopen(LoosePath(name).c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = n >= 0 && (unsigned long)n <= limit;
    if (ok) {
        out.resize((size_t)n);
        ok = n == 0 || fread(&out[0], 1, (size_t)n, f) == (size_t)n;
    }
    fclose(f);
    if (!ok) out.clear();
    return ok;
}

const GpcResourceIndex* PortDataContentIndex(const char* path) {
    std::string name = PortDataName(path);
    std::map<std::string, GpcResourceIndex*>::iterator found = gIndexes.find(name);
    if (found != gIndexes.end()) return found->second;
    std::vector<unsigned char> bytes;
    GpcResourceIndex* index = 0;
    if (PortDataRead(name.c_str(), bytes)) {
        index = new GpcResourceIndex;
        if (bytes.empty() || index->LoadBytes(&bytes[0], bytes.size()) != GPC_OK) {
            delete index;
            index = 0;
        }
    }
    gIndexes[name] = index;          // a failure is remembered too: the caller reports it once per use
    return index;
}

PortDataStream::PortDataStream()
    : m_open(false), m_file(0), m_base(0), m_size(0), m_pos(0), m_bufStart(0), m_bufLen(0) {}
PortDataStream::~PortDataStream() { Close(); }

bool PortDataStream::Open(const char* path) {
    Close();
    std::string name = PortDataName(path);
    if (gPacked) {
        const PackEntry* e = PackFind(name);
        if (!e) return false;
        m_base = e->offset;
        m_size = e->size;
    } else {
        FILE* f = fopen(LoosePath(name).c_str(), "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n < 0 || (unsigned long)n > kLimit) {
            fclose(f);
            return false;
        }
        m_file = f;
        m_size = (unsigned int)n;
    }
    m_open = true;
    m_pos = m_bufStart = m_bufLen = 0;
    if (m_buf.size() != 64 * 1024) m_buf.resize(64 * 1024);
    return true;
}

void PortDataStream::Close() {
    if (m_file) fclose((FILE*)m_file);
    m_file = 0;
    m_open = false;
    m_base = m_size = m_pos = m_bufStart = m_bufLen = 0;
}

bool PortDataStream::Seek(unsigned int offset) {
    if (!m_open || offset > m_size) return false;
    m_pos = offset;
    return true;
}

bool PortDataStream::Fill() {
    unsigned int n = m_size - m_pos;
    if (n > (unsigned int)m_buf.size()) n = (unsigned int)m_buf.size();
    if (n == 0) return false;
    if (m_file) {
        FILE* f = (FILE*)m_file;
        if (fseek(f, (long)m_pos, SEEK_SET) != 0 || fread(&m_buf[0], 1, n, f) != n) return false;
    } else if (!PackReadAt(m_base + m_pos, &m_buf[0], n)) {
        return false;
    }
    m_bufStart = m_pos;
    m_bufLen = n;
    return true;
}

unsigned int PortDataStream::Read(void* out, unsigned int bytes) {
    if (!m_open) return 0;
    unsigned char* dst = (unsigned char*)out;
    unsigned int done = 0;
    while (done < bytes && m_pos < m_size) {
        if (m_pos < m_bufStart || m_pos >= m_bufStart + m_bufLen) {
            if (!Fill()) break;
        }
        unsigned int at = m_pos - m_bufStart;
        unsigned int n = m_bufLen - at;
        if (n > bytes - done) n = bytes - done;
        memcpy(dst + done, &m_buf[at], n);
        done += n;
        m_pos += n;
    }
    return done;
}
