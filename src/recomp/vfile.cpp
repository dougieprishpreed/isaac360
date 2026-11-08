// Guest stdio streams on the host (vfile.h).
#include "vfile.h"

#include <list>
#include <map>
#include <mutex>
#include <string.h>
#include <vector>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

// plain counters: a lost increment between threads only blurs a diagnostic (64-bit atomics need libatomic on PPC32)
volatile uint64_t g_opens = 0, g_bytes = 0, g_disk = 0;

enum { BLOCK = 256 << 10, CACHE_BLOCKS = 16, DIRECT = 1 << 20 };

// one per game data file, kept open for the run; blocks of BLOCK bytes, least recently used dropped
struct Reader {
#if defined(_WIN32)
    FILE* f;
#else
    int fd;
#endif
    int64_t size;
    std::mutex lock;
    std::string path;
    uint64_t gameBytes, diskBytes, reads, bigReads;   // BOI360_TRACE_READS (vf_dump_readers)
    std::list<std::pair<int64_t, std::vector<uint8_t> > > blocks;   // most recent first

    size_t raw_read(int64_t off, void* buf, size_t n) {
        g_disk += n;
        diskBytes += n;
#if defined(_WIN32)
        if (_fseeki64(f, off, SEEK_SET) != 0) return 0;
        return fread(buf, 1, n, f);
#else
        ssize_t r = pread(fd, buf, n, (off_t)off);
        return r < 0 ? 0 : (size_t)r;
#endif
    }

    const std::vector<uint8_t>& block(int64_t index) {
        for (std::list<std::pair<int64_t, std::vector<uint8_t> > >::iterator it = blocks.begin(); it != blocks.end(); ++it)
            if (it->first == index) {
                if (it != blocks.begin()) blocks.splice(blocks.begin(), blocks, it);
                return blocks.front().second;
            }
        if (blocks.size() >= CACHE_BLOCKS) {
            blocks.splice(blocks.begin(), blocks, --blocks.end());    // reuse the oldest block's storage
            blocks.front().first = index;
        } else {
            blocks.push_front(std::make_pair(index, std::vector<uint8_t>()));
        }
        std::vector<uint8_t>& data = blocks.front().second;
        int64_t off = index * BLOCK;
        size_t want = (size_t)(size - off < BLOCK ? size - off : BLOCK);
        data.resize(want);
        data.resize(raw_read(off, want ? &data[0] : 0, want));
        return data;
    }

    size_t read(int64_t off, uint8_t* out, size_t n) {
        if (off >= size) return 0;
        if ((int64_t)n > size - off) n = (size_t)(size - off);
        std::lock_guard<std::mutex> l(lock);
        gameBytes += n;
        ++reads;
        if (n >= 65536) ++bigReads;
        if (n >= DIRECT) return raw_read(off, out, n);
        size_t done = 0;
        while (done < n) {
            int64_t index = (off + (int64_t)done) / BLOCK;
            const std::vector<uint8_t>& b = block(index);
            size_t in = (size_t)(off + (int64_t)done - index * BLOCK);
            if (in >= b.size()) break;
            size_t take = b.size() - in;
            if (take > n - done) take = n - done;
            memcpy(out + done, &b[in], take);
            done += take;
        }
        return done;
    }
};

std::mutex g_readersLock;
std::map<std::string, Reader*> g_readers;

Reader* reader_for(const std::string& path) {
    std::lock_guard<std::mutex> l(g_readersLock);
    std::map<std::string, Reader*>::iterator it = g_readers.find(path);
    if (it != g_readers.end()) return it->second;
    Reader* r = new Reader;
#if defined(_WIN32)
    r->f = fopen(path.c_str(), "rb");
    if (!r->f) {
        delete r;
        return 0;
    }
    _fseeki64(r->f, 0, SEEK_END);
    r->size = _ftelli64(r->f);
#else
    r->fd = open(path.c_str(), O_RDONLY);
    if (r->fd < 0) {
        delete r;
        return 0;
    }
    r->size = (int64_t)lseek(r->fd, 0, SEEK_END);
#endif
    r->path = path;
    r->gameBytes = r->diskBytes = r->reads = r->bigReads = 0;
    g_readers[path] = r;
    return r;
}

}  // namespace

struct VFile {
    FILE* f;                            // a plain host file, or
    Reader* r;                          // a shared reader with this stream's position
    int64_t pos;
    bool eof, err;
    int unget;
};

void vf_dump_readers(void (*line)(const char*)) {
    std::lock_guard<std::mutex> l(g_readersLock);
    for (std::map<std::string, Reader*>::iterator it = g_readers.begin(); it != g_readers.end(); ++it) {
        Reader* r = it->second;
        char buf[512];
        snprintf(buf, sizeof(buf), "  %s: %llu reads (%llu of 64 KB+), game %llu KB, drive %llu KB, file %llu KB",
                 r->path.c_str(), (unsigned long long)r->reads, (unsigned long long)r->bigReads,
                 (unsigned long long)(r->gameBytes >> 10), (unsigned long long)(r->diskBytes >> 10),
                 (unsigned long long)(r->size >> 10));
        line(buf);
    }
}

void vf_stats(uint64_t* opens, uint64_t* bytes, uint64_t* disk) {
    *opens = g_opens;
    *bytes = g_bytes;
    *disk = g_disk;
}

VFile* vf_open(const std::string& hostPath, const std::string& mode, bool shared) {
    ++g_opens;
    VFile* v = new VFile;
    v->f = 0;
    v->r = 0;
    v->pos = 0;
    v->eof = v->err = false;
    v->unget = -1;
    bool readOnlyBinary = mode.find('r') != std::string::npos && mode.find('b') != std::string::npos &&
                          mode.find('+') == std::string::npos;
    if (shared && readOnlyBinary) v->r = reader_for(hostPath);
    else v->f = fopen(hostPath.c_str(), mode.c_str());
    if (!v->f && !v->r) {
        delete v;
        return 0;
    }
    return v;
}

VFile* vf_wrap(FILE* f) {
    VFile* v = new VFile;
    v->f = f;
    v->r = 0;
    v->pos = 0;
    v->eof = v->err = false;
    v->unget = -1;
    return v;
}

void vf_close(VFile* v) {
    if (v->f && v->f != stdin && v->f != stdout && v->f != stderr) fclose(v->f);
    delete v;
}

size_t vf_read(VFile* v, void* buf, size_t size, size_t n) {
    if (!size || !n) return 0;
    g_bytes += (uint64_t)size * n;
    if (v->f) return fread(buf, size, n, v->f);
    size_t want = size * n, got = 0;
    uint8_t* out = (uint8_t*)buf;
    if (v->unget >= 0) {                // the byte put back sits at the position
        out[got++] = (uint8_t)v->unget;
        v->unget = -1;
        v->pos += 1;
    }
    size_t r = v->r->read(v->pos, out + got, want - got);
    v->pos += (int64_t)r;
    got += r;
    if (got < want) v->eof = true;
    return got / size;
}

size_t vf_write(VFile* v, const void* buf, size_t size, size_t n) {
    if (v->f) return fwrite(buf, size, n, v->f);
    v->err = true;
    return 0;
}

int vf_seek(VFile* v, int64_t off, int whence) {
    if (v->f) {
#if defined(_WIN32)
        return _fseeki64(v->f, off, whence);
#else
        return fseeko(v->f, (off_t)off, whence);
#endif
    }
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? v->pos : v->r->size;
    if (base + off < 0) return -1;
    v->pos = base + off;
    v->unget = -1;
    v->eof = false;
    return 0;
}

int64_t vf_tell(VFile* v) {
    if (v->f) {
#if defined(_WIN32)
        return _ftelli64(v->f);
#else
        return (int64_t)ftello(v->f);
#endif
    }
    return v->pos;
}

int vf_getc(VFile* v) {
    if (v->f) return fgetc(v->f);
    uint8_t ch;
    return vf_read(v, &ch, 1, 1) == 1 ? ch : EOF;
}

int vf_ungetc(int ch, VFile* v) {
    if (v->f) return ungetc(ch, v->f);
    if (ch == EOF || v->unget >= 0 || v->pos == 0) return EOF;
    // the byte goes back in front of the position (the reader's data is not changed)
    v->unget = ch & 0xff;
    v->pos -= 1;
    v->eof = false;
    return ch;
}

char* vf_gets(char* buf, int n, VFile* v) {
    if (v->f) return fgets(buf, n, v->f);
    int i = 0;
    while (i < n - 1) {
        int ch = vf_getc(v);
        if (ch == EOF) break;
        buf[i++] = (char)ch;
        if (ch == '\n') break;
    }
    if (i == 0) return 0;
    buf[i] = 0;
    return buf;
}

int vf_putc(int ch, VFile* v) { return v->f ? fputc(ch, v->f) : EOF; }
int vf_puts(const char* s, VFile* v) { return v->f ? fputs(s, v->f) : EOF; }
int vf_eof(VFile* v) { return v->f ? feof(v->f) : v->eof ? 1 : 0; }
int vf_error(VFile* v) { return v->f ? ferror(v->f) : v->err ? 1 : 0; }
void vf_clearerr(VFile* v) {
    if (v->f) clearerr(v->f);
    v->eof = v->err = false;
}
void vf_flush(VFile* v) {
    if (v->f) fflush(v->f);
}
