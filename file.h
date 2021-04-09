#ifndef FILE_H
#define	FILE_H

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

#include "helper.h"

struct file_io_t {
    FILE *fd;
    const char *fileName;
    char *mode;
};    

typedef struct file_io_t file_io_t;
typedef void (*FileReader) (void *buffer, unsigned int bytesRead, void *ctx);

static inline size_t Read (file_io_t *fd, void *buf, size_t count) {
    size_t readBytes = fread(buf, 1, count, fd->fd);
    return readBytes;
}

static inline void Write(file_io_t *fd, const void *buf, size_t count) {
    size_t written = fwrite(buf, 1, count, fd->fd);
    if (unlikely(written != count)) {
        logprintf(LOG_ERR, "Cant write the file %s. Exiting now.", fd->fileName);
        exitPgm(EXIT_FAILURE);
    }
}
    
file_io_t* Open(const char *pathname, char *mode);
void Close(file_io_t *fd);
void Seek(file_io_t *fd, long offset, int whence);
void readFile(char *filename, FileReader callback, void *ctx);

#endif	/* FILE_H */

