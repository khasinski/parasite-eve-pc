#ifndef PE_FILE_IO_H
#define PE_FILE_IO_H
/* Private platform boundary for checked, staged profile writes. */
#include <stdio.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
static unsigned long pe_profile_process(void) { return GetCurrentProcessId(); }
static int pe_profile_target_ok(const char *p) {
    DWORD a = GetFileAttributesA(p);
    return a == INVALID_FILE_ATTRIBUTES || !(a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY));
}
static int pe_profile_flush(FILE *f) { return !ferror(f) && !fflush(f) && !_commit(_fileno(f)); }
static int pe_file_publish_new(const char *a,const char *b){return MoveFileExA(a,b,MOVEFILE_WRITE_THROUGH)!=0;}
static int pe_profile_replace(const char *a, const char *b) {
    return MoveFileExA(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
#else
#include <unistd.h>
#include <sys/stat.h>
static unsigned long pe_profile_process(void) { return (unsigned long)getpid(); }
static int pe_profile_target_ok(const char *p) { struct stat s; return stat(p, &s) || S_ISREG(s.st_mode); }
static int pe_profile_flush(FILE *f) { return !ferror(f) && !fflush(f) && !fsync(fileno(f)); }
static int pe_profile_replace(const char *a, const char *b) { return !rename(a, b); }
static int pe_file_publish_new(const char*a,const char*b){if(link(a,b))return 0;unlink(a);return 1;}
#endif
#endif
