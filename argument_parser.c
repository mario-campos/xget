#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include "helper.h"
#include "libircclient.h"

struct dccDownload* newDccDownload(char *botNick, char *xdccCmd) {
    struct dccDownload *t = (struct dccDownload*)malloc(sizeof (struct dccDownload));
    t->botNick = botNick;
    t->xdccCmd = xdccCmd;
    return t;
}

void freeDccDownload(struct dccDownload *t) {
    free(t->botNick);
    free(t->xdccCmd);
    free(t);
}

struct dccDownloadProgress* newDccProgress(char *completePath, irc_dcc_size_t complFileSize) {
    struct dccDownloadProgress *t = (struct dccDownloadProgress*)malloc(sizeof (struct dccDownloadProgress));
    t->completeFileSize = complFileSize;
    t->sizeRcvd = 0;
    t->sizeNow = 0;
    t->sizeLast = 0;
    t->completePath = completePath;
    return t;

}

void freeDccProgress(struct dccDownloadProgress *progress) {
    free(progress->completePath);
    free(progress);
}

void parseDccDownload(char *dccDownloadString, char **nick, char **xdccCmd) {
    size_t i;
    size_t strLen = strlen(dccDownloadString);
    size_t spaceFound = 0;

    for (i = 0; i < strLen; i++) {
        if (dccDownloadString[i] == ' ') {
            spaceFound = i;
            break;
        }
    }

    size_t nickLen = spaceFound + 1;
    size_t cmdLen = (strLen - spaceFound) + 1;

    DBG_OK("nickLen = %zu, cmdLen = %zu", nickLen, cmdLen);

    char nickPtr[nickLen];
    char xdccPtr[cmdLen];

    strlcpy(nickPtr, dccDownloadString, sizeof(nickPtr));
    strlcpy(xdccPtr, dccDownloadString + (spaceFound + 1), sizeof(xdccPtr));

    *nick = strdup(nickPtr);
    *xdccCmd = strdup(xdccPtr);
}

char *strip(char *s) {
    char *it;
    char *separated = s;
    while ((it = strsep(&separated, " \t")) != NULL) {
        if (*it != '\0') {
            s = strdup(it);
            break;
        }
    }
    return s;
}

char **split(char *s, int *count) {
    char *it, **parts, **head;
    *count = 0;
    head = parts = calloc(10, sizeof(char*));
    while ((it = strsep(&s, ",")) != NULL) {
        if (*it != '\0') {
            *parts = strdup(it);
            parts++;
            (*count)++;
        }
    }
    *parts = NULL;
    return head;
}

char** parseChannels(char *channelString, uint32_t *numChannels) {
    int numFound = 0;
    char *seperator = ",";
    char **splittedString = split(channelString, &numFound);
    if (splittedString == NULL) {
        DBG_ERR("splittedString = NULL, cant continue from here.");
    }
    int i = 0;

    for (i = 0; i < numFound; i++) {
        char *tmp = splittedString[i];
        splittedString[i] = strip(splittedString[i]);
        free(tmp);
        DBG_OK("%d: '%s'", i, splittedString[i]);
    }

    *numChannels = numFound;

    return splittedString;
}

struct dccDownload** parseDccDownloads(char *dccDownloadString, unsigned int *numDownloads) {
    int numFound = 1;
    int i = 0, j = 0;
    char *seperator = ",";

    struct dccDownload **dccDownloadArray = (struct dccDownload**)calloc(numFound + 1, sizeof (struct dccDownload*));

    *numDownloads = numFound;

    for (i = 0; i < numFound; i++) {
        char *nick = NULL;
        char *xdccCmd = NULL;
        DBG_OK("%d: '%s'\n", i, dccDownloadString);
        parseDccDownload(dccDownloadString, &nick, &xdccCmd);
        DBG_OK("%d: '%s' '%s'\n", i, nick, xdccCmd);
        if (nick != NULL && xdccCmd != NULL) {
            dccDownloadArray[j] = newDccDownload(nick, xdccCmd);
            j++;
        }
        else {
            if (nick != NULL)
                free(nick);

            if (xdccCmd != NULL)
                free(xdccCmd);
        }
    }

    return dccDownloadArray;
}
