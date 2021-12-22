#ifndef ARGUMENT_PARSER_H
#define ARGUMENT_PARSER_H

#include "helper.h"

typedef uint64_t irc_dcc_size_t;

struct dccDownload {
    char *botNick;
    char *xdccCmd;
};

struct dccDownloadProgress {
    irc_dcc_size_t completeFileSize;
    irc_dcc_size_t sizeRcvd;
    irc_dcc_size_t sizeNow;
    irc_dcc_size_t sizeLast;
    char *completePath;
};

struct dccDownload* newDccDownload(char *botNick, char *xdccCmd);

void freeDccDownload(struct dccDownload *t);

struct dccDownloadProgress* newDccProgress(char *filename, irc_dcc_size_t complFileSize);

void freeDccProgress(struct dccDownloadProgress *progress);

void parseDccDownload (char *dccDownloadString, char **nick, char **xdccCmd);

char** parseChannels(char *channelString, uint32_t *numChannels);

struct dccDownload** parseDccDownloads(char *dccDownloadString, unsigned int *numDownloads);

#endif
