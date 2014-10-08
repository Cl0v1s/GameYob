#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "io.h"


#ifdef C_IO_FUNCTIONS

struct FileHandle {
    FILE* file;
};

FileHandle* file_open(const char* filename, const char* params) {
    FileHandle* h = (FileHandle*)malloc(sizeof(FileHandle));
    h->file = fopen(filename, params);

    if (h->file == NULL) {
        free(h);
        h = NULL;
    }

    return h;
}

void file_close(FileHandle* h) {
    fclose(h->file);
    free(h);
}
void file_read(void* buf, int bs, int size, FileHandle* h) {
    fread(buf, bs, size, h->file);
}
void file_write(const void* buf, int bs, int size, FileHandle* h) {
    fwrite(buf, bs, size, h->file);
}
void file_gets(char* buf, int size, FileHandle* h) {
    fgets(buf, size, h->file);
}
void file_putc(char c, FileHandle* h) {
    fputc(c, h->file);
}

void file_rewind(FileHandle* h) {
    rewind(h->file);
}
int file_tell(FileHandle* h) {
    return ftell(h->file);
}
void file_seek(FileHandle* h, int pos, int flags) {
    fseek(h->file, pos, flags);
}
int file_getSize(FileHandle* h) {
    int pos = ftell(h->file);
    fseek(h->file, 0, SEEK_END);
    int ret = ftell(h->file);
    fseek(h->file, pos, SEEK_SET);
    return ret;
}

void file_printf(FileHandle* h, const char* s, ...) {
    va_list args;
    va_start(args, s);

    char buf[512];
    vsprintf(buf, s, args);

    fputs(buf, h->file);
}


DIR* fs_opendir(const char* s) {
    return opendir(s);
}
void fs_closedir(DIR* dir) {
    return closedir(dir);
}
struct dirent* fs_readdir(DIR* d) {
    return readdir(d);
}

void fs_getcwd(char* dest, size_t maxLen) {
    getcwd(dest, maxLen);
}
void fs_chdir(const char* s) {
    chdir(s);
}

#endif
