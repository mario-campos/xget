#include <stdio.h>
#include "file.h"

#define FILE_READ_BUFFER_SIZE BUFSIZ

file_io_t *Open(const char *pathname, char *mode) {
    file_io_t *fd = Malloc(sizeof(file_io_t));

    fd->fd = NULL;
    if (str_equals(mode, "w")) {
        fopen_s(&fd->fd, pathname, "wb");
    } else if (str_equals(mode, "a")) {
        fopen_s(&fd->fd, pathname, "ab");
    } else if (str_equals(mode, "r")) {
        fd->fd = fopen(pathname, "rb");
    }

    if (fd->fd == NULL) {
        logprintf(LOG_ERR, "Cant open the file %s. Exiting now.", pathname);
        free(fd);
        exitPgm(EXIT_FAILURE);
    }

    fd->fileName = pathname;
    fd->mode = mode;

    return fd;
}

void Close(file_io_t *fd) {
    if (fd == NULL) return;
    if (fd->fd == NULL) return;
    int ret = fclose(fd->fd);
    if (ret != 0) {
        logprintf(LOG_ERR, "Cant close the file %s. Exiting now.",
                  fd->fileName);
        exitPgm(EXIT_FAILURE);
    }

    free(fd);
}

void Seek(file_io_t *fd, long offset, int whence) {
    int ret;
    ret = fseek(fd->fd, offset, whence);
    if (ret == -1) {
        logprintf(LOG_ERR, "Cant fseek the file %s. Exiting now.",
                  fd->fileName);
        exitPgm(EXIT_FAILURE);
    }
}

void readFile(char *filename, FileReader callback, void *ctx) {
    char buffer[FILE_READ_BUFFER_SIZE + 1];
    size_t bytesRead;
    file_io_t *file;

    file = Open(filename, "r");

    do {
        bytesRead = Read(file, buffer, FILE_READ_BUFFER_SIZE);
        callback(buffer, bytesRead, ctx);
    } while (bytesRead == FILE_READ_BUFFER_SIZE);

    Close(file);
}
