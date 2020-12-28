#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include "helper.h"
#include "libircclient.h"

#include "argument_parser.h"

static char* usage = "usage: xdccget [-46aDqv] [-n <nickname>] [-p <port number>]\n"
                                    "[-l <login-command>] [-d <download-directory>]\n"
				    "<server> <channel(s)> <bot cmds>";

void parseArguments(int argc, char **argv, struct xdccGetConfig *cfg) {
    int opt;

    cfg->logLevel = LOG_INFO;

    while ((opt = getopt(argc, argv, "Vhqvkd:n:l:p:aD46"))) {
        switch (opt) {
        case 'V': {
            unsigned int major, minor;
            irc_get_version(&major, &minor);
            puts("xdccget-0.0.0");
            printf("libircclient-%d.%02d\n", major, minor);
            exit(0);
        }
        case 'h':
            logprintf(LOG_ERR, "%s\n", usage);
            exit(EXIT_FAILURE);

        case 'q':
            DBG_OK("setting log-level as quiet.");
            cfg->logLevel = LOG_QUIET;
            break;
    
        case 'v':
            DBG_OK("setting log-level as warn.");
            cfg->logLevel = LOG_WARN;
            break;

        case 'k':
            cfg_set_bit(cfg, ALLOW_ALL_CERTS_FLAG);
            break;
    
        case 'd':
            DBG_OK("setting target dir as %s", optarg);
            cfg->targetDir = sdsnew(optarg);
            break;
            
        case 'n':
            DBG_OK("setting nickname as %s", optarg);
            cfg->nick = sdsnew(optarg);
            break;
            
        case 'l':
             DBG_OK("setting login-command as %s", optarg);
            cfg->login_command = sdsnew(optarg);
            break;
    
        case 'p':
            cfg->port = (unsigned short) strtoul(optarg, NULL, 0);
            DBG_OK("setting port as %u", cfg->port);
            break;
    	
        case 'a':
            cfg_set_bit(cfg, ACCEPT_ALL_NICKS_FLAG);
            break;

        case 'D':
    	    cfg_set_bit(cfg, DONT_CONFIRM_OFFSETS_FLAG);
    	    break;

        case '4':
            cfg_set_bit(cfg, USE_IPV4_FLAG);
            break;
    
#ifdef ENABLE_IPV6
        case '6':
            cfg_set_bit(cfg, USE_IPV6_FLAG);
            break;
#endif
	case '?':
            logprintf(LOG_ERR, "%s\n", usage);
            exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc || (argc - optind) > 3) {
        logprintf(LOG_ERR, "%s\n", usage);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; (i + optind) <= argc; i++) {
       cfg->args[i] = argv[i + optind];
    }
}

struct dccDownload* newDccDownload(sds botNick, sds xdccCmd) {
    struct dccDownload *t = (struct dccDownload*) Malloc(sizeof (struct dccDownload));
    t->botNick = botNick;
    t->xdccCmd = xdccCmd;
    return t;
}

void freeDccDownload(struct dccDownload *t) {
    sdsfree(t->botNick);
    sdsfree(t->xdccCmd);
    FREE(t);
}

struct dccDownloadProgress* newDccProgress(char *completePath, irc_dcc_size_t complFileSize) {
    struct dccDownloadProgress *t = (struct dccDownloadProgress*) Malloc(sizeof (struct dccDownloadProgress));
    t->completeFileSize = complFileSize;
    t->sizeRcvd = 0;
    t->sizeNow = 0;
    t->sizeLast = 0;
    t->completePath = completePath;
    return t;

}

void freeDccProgress(struct dccDownloadProgress *progress) {
    sdsfree(progress->completePath);
    FREE(progress);
}

void parseDccDownload(char *dccDownloadString, sds *nick, sds *xdccCmd) {
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

    sds nickPtr = sdsnewlen(NULL, nickLen);
    sds xdccPtr = sdsnewlen(NULL, cmdLen);

    nickPtr = sdscpylen(nickPtr, dccDownloadString, nickLen - 1);
    xdccPtr = sdscpylen(xdccPtr, dccDownloadString + (spaceFound + 1), cmdLen - 1);

    *nick = nickPtr;
    *xdccCmd = xdccPtr;
}

sds* parseChannels(char *channelString, uint32_t *numChannels) {
    int numFound = 0;
    char *seperator = ",";
    sds *splittedString = sdssplitlen(channelString, strlen(channelString), seperator, strlen(seperator), &numFound);
    if (splittedString == NULL) {
        DBG_ERR("splittedString = NULL, cant continue from here.");
    }
    int i = 0;

    for (i = 0; i < numFound; i++) {
        sdstrim(splittedString[i], " \t");
        DBG_OK("%d: '%s'", i, splittedString[i]);
    }

    *numChannels = numFound;

    return splittedString;
}

struct dccDownload** parseDccDownloads(char *dccDownloadString, unsigned int *numDownloads) {
    int numFound = 0;
    int i = 0, j = 0;
    char *seperator = ",";

    sds *splittedString = sdssplitlen(dccDownloadString, strlen(dccDownloadString), seperator, strlen(seperator), &numFound);

    if (splittedString == NULL) {
        DBG_ERR("splittedString = NULL, cant continue from here.");
    }

    struct dccDownload **dccDownloadArray = (struct dccDownload**) Calloc(numFound + 1, sizeof (struct dccDownload*));

    *numDownloads = numFound;

    for (i = 0; i < numFound; i++) {
        sdstrim(splittedString[i], " \t");
        sds nick = NULL;
        sds xdccCmd = NULL;
        DBG_OK("%d: '%s'\n", i, splittedString[i]);
        parseDccDownload(splittedString[i], &nick, &xdccCmd);
        DBG_OK("%d: '%s' '%s'\n", i, nick, xdccCmd);
        if (nick != NULL && xdccCmd != NULL) {
            dccDownloadArray[j] = newDccDownload(nick, xdccCmd);
            j++;
        }
        else {
            if (nick != NULL)
                sdsfree(nick);

            if (xdccCmd != NULL)
                sdsfree(xdccCmd);
        }
        sdsfree(splittedString[i]);
    }

    FREE(splittedString);
    return dccDownloadArray;
}
