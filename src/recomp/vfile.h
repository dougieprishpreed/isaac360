// Host side of the game's stdio streams (shims_crt*.cpp). The game reopens its archives (resources/packed/*.a)
// for every entry it reads, 22,500 opens during start-up; an open on the console's USB costs ~110 ms. So
// read-only binary opens of game data share one reader per file, opened once and read in 256 KB blocks (the
// USB's good request size), each stream keeping its own position. Everything else is a plain host file.
#ifndef BOI360_RECOMP_VFILE_H
#define BOI360_RECOMP_VFILE_H
#include <stdint.h>
#include <stdio.h>
#include <string>

struct VFile;

VFile* vf_open(const std::string& hostPath, const std::string& mode, bool shared);
VFile* vf_wrap(FILE* f);               // stdin/stdout/stderr
void vf_close(VFile* f);
size_t vf_read(VFile* f, void* buf, size_t size, size_t n);
size_t vf_write(VFile* f, const void* buf, size_t size, size_t n);
int vf_seek(VFile* f, int64_t off, int whence);
int64_t vf_tell(VFile* f);
int vf_getc(VFile* f);
int vf_ungetc(int ch, VFile* f);
char* vf_gets(char* buf, int n, VFile* f);
int vf_putc(int ch, VFile* f);
int vf_puts(const char* s, VFile* f);
int vf_eof(VFile* f);
int vf_error(VFile* f);
void vf_clearerr(VFile* f);
void vf_flush(VFile* f);
// waits until the writer thread has done every queued open, write, flush and close (files only written go
// through it): before anything looks at the files again
void vf_drain();
// totals since start (the console's watchdog): opens, bytes the game read, bytes read from the drive
void vf_stats(uint64_t* opens, uint64_t* bytes, uint64_t* disk);
// per shared reader: reads, bytes the game read, bytes read from the drive (BOI360_TRACE_READS)
void vf_dump_readers(void (*line)(const char*));

#endif
