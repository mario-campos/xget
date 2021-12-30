#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdint.h>
#include <err.h>
#include <time.h>
#include <sys/ioctl.h>

#include "libircclient.h"

/* ansi color codes used at the dbg macros for coloured output. */

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"

/* define DBG-macros for debugging purposes if DEBUG is defined...*/

#ifdef DEBUG
#define DBG_MSG(color, stream, format, ...) do {\
		    fprintf(stream, "%sDBG:%s \"", color, KNRM);\
		    fprintf(stream, format, ##__VA_ARGS__);\
		    fprintf(stream, "\" function: %s file: %s line: %d\n",(char*) __func__, (char*)__FILE__, __LINE__);} while(0)

	#define DBG_OK(format, ...) do {\
				DBG_MSG(KGRN, stdout, format, ##__VA_ARGS__);\
		    } while(0)
	#define DBG_WARN(format, ...) do {\
				DBG_MSG(KYEL, stderr, format, ##__VA_ARGS__);\
		    } while(0)
	#define DBG_ERR(format, ...) do {\
		    	DBG_MSG(KRED, stderr, format, ##__VA_ARGS__);\
                doCleanUp();\
                exit(EXIT_FAILURE);\
			} while(0)
#else
#define DBG_MSG(color, stream, format, ...) do {} while(0)
#define DBG_OK(format, ...) do {} while(0)
#define DBG_WARN(format, ...) do {} while(0)
#define DBG_ERR(format, ...) do {} while(0)
#endif

struct dccDownloadContext {
    uint64_t completeFileSize;
    uint64_t sizeRcvd;
    uint64_t sizeNow;
    uint64_t sizeLast;
    FILE *fd;
};

struct xdccGetConfig {
    irc_session_t *session;
    char *botNick;
    char *xdccCmd;
    uint64_t flags;
    char **channelsToJoin;
    uint32_t numChannels;
    struct dccDownloadContext context;
};

#define USE_IPV6_FLAG	          0x04
#define SENDED_FLAG               0x08

#define IRC_DCC_SIZE_T_FORMAT PRIu64
#define IRC_NICK_MAX_SIZE 30
#define _FILE_OFFSET_BITS 64

struct terminalDimension {
    int cols;
};

struct xdccGetConfig cfg;

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
    char **splittedString = split(channelString, &numFound);
    if (splittedString == NULL) {
        DBG_ERR("splittedString = NULL, cant continue from here.");
    }

    for (int i = 0; i < numFound; i++) {
        char *tmp = splittedString[i];
        splittedString[i] = strip(splittedString[i]);
        free(tmp);
        DBG_OK("%d: '%s'", i, splittedString[i]);
    }

    *numChannels = numFound;

    return splittedString;
}

struct terminalDimension terminal_dimension;

void invent_nick(char *dst, size_t dst_size) {
    size_t new_size;
    char *adjectives[] = {"Pandering", "Foul", "Decrepit", "Sanguine","Illustrious",
                          "Cantankerous", "Dubious", "Auspicious", "Valorous", "Venal",
                          "Virulent", "Voracious", "Votive", "Voluptuous"};
    char *nouns[] = {"Buffoon", "Vixen", "Nincompoop", "Lad", "Phantom", "Banshee", "Jollux",
                     "Lark", "Wench", "Sobriquet", "Vexation","Violation", "Volition",
                     "Vendetta", "Veracity", "Vim"};

    // This is a weak form of entropy, but we don't need much --
    // just enough to choose a "unique" nick.
    srand(getpid());

    do {
        size_t i = rand() % (sizeof(adjectives) / sizeof(adjectives[0]));
        size_t j = rand() % (sizeof(nouns) / sizeof(nouns[0]));
        new_size = strlcpy(dst, adjectives[i], dst_size);
        new_size += strlcat(dst, nouns[j], dst_size);
    } while (new_size >= dst_size);
}

struct terminalDimension *getTerminalDimension() {
    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);

    terminal_dimension.cols = w.ws_col;
    return &terminal_dimension;
}

void printProgressBar(const int numBars, const double percentRdy) {
    const int NUM_BARS = numBars;

    putchar('[');

    for (int i = 0; i < NUM_BARS; i++) {
        if (i < (int) (NUM_BARS * percentRdy)) {
            putchar('#');
        }
        else {
            putchar('-');
        }
    }

    putchar(']');
}

int printSize(uint64_t size) {
    char *sizeNames[] = {"Byte", "KByte", "MByte", "GByte", "TByte", "PByte"};

    double temp = (double) size;
    unsigned int i = 0;

    while (temp > 1024) {
        temp /= 1024;
        i++;
    }

    int charsPrinted;

    if (i >= (sizeof (sizeNames) / sizeof (char*))) {
        charsPrinted = printf("%" IRC_DCC_SIZE_T_FORMAT " Byte", size);
    }
    else {
        charsPrinted = printf("%0.3f %s", temp, sizeNames[i]);
    }

    return charsPrinted;
}

int printETA(double seconds) {
    int charsPrinted = 0;
    if (seconds <= 60) {
        charsPrinted = printf("%.0fs", seconds);
    }
    else {
        double mins = seconds / 60;
        double hours = mins / 60;
        double remainMins = mins - ((unsigned int) hours) * 60;
        double days = hours / 24;
        double remainHours = hours - ((unsigned int) days) * 24;
        double remainSeconds = seconds - ((unsigned int) mins) *60;

        if (days >= 1) {
            charsPrinted += printf("%.0fd", days);
        }

        if (remainHours >= 1) {
            charsPrinted += printf("%.0fh", remainHours);
        }

        charsPrinted += printf("%.0fm%.0fs", remainMins, remainSeconds);
    }
    return charsPrinted;
}

void outputProgress() {
    struct terminalDimension *terminalDimension = getTerminalDimension();
    /* see comments below how these "numbers" are calculated */
    int progBarLen = terminalDimension->cols - (8 + 14 + 1 + 14 + 1 + 14 + 3 + 13 /* +1 for windows...*/);

    cfg.context.sizeLast = cfg.context.sizeNow;
    cfg.context.sizeNow = cfg.context.sizeRcvd;

    uint64_t temp = (cfg.context.completeFileSize == 0) ? 0 : cfg.context.sizeRcvd * 1000000L / cfg.context.completeFileSize;
    double curProcess = (double) temp / 1000000;
    uint64_t curSpeed = cfg.context.sizeNow - cfg.context.sizeLast;

    int printedChars = progBarLen + 2;

    printProgressBar(progBarLen, curProcess);
    /* 8 chars -->' 75.30% ' */
    printedChars += printf(" %.2f%% ", curProcess * 100);
    /* 14 chars --> '1001.132 MByte' */
    printedChars += printSize(cfg.context.sizeRcvd);
    /* 1 char */
    printedChars += printf("/");
    /* 14 chars --> '1001.132 MByte' */
    printedChars += printSize(cfg.context.completeFileSize);
    /* 1 char */
    printedChars += printf("|");
    /* 14 chars --> '1001.132 MByte' */
    printedChars += printSize(curSpeed);
    /* 3 chars */
    printedChars += printf("/s|");

    /*calc ETA - max 13 chars */
    uint64_t remainingSize = cfg.context.completeFileSize - cfg.context.sizeRcvd;
    if (remainingSize > 0 && curSpeed > 0) {
        double etaSeconds = ((double) remainingSize / (double) curSpeed);
        printedChars += printETA(etaSeconds);
    }
    else {
        printedChars += printf("---");
    }

    /* fill remaining columns of terminal with spaces, in ordner to clean the output... */

    int j;
    for (j = printedChars; j < terminalDimension->cols - 1; j++) {
        printf(" ");
    }
}

void join_channels(irc_session_t *session) {
    for (uint32_t i = 0; i < cfg.numChannels; i++) {
        DBG_OK("Joining channel '%s'", cfg.channelsToJoin[i]);
        irc_cmd_join (session, cfg.channelsToJoin[i], 0);
    }
}

void send_xdcc_requests(irc_session_t *session) {
    int err;
    if (!(cfg.flags & SENDED_FLAG)) {
        DBG_OK("Sending XDCC command '%s' to nick '%s'", cfg.xdccCmd, cfg.botNick);
        if ((err = irc_cmd_msg(session, cfg.botNick, cfg.xdccCmd))) {
            warnx("failed to send XDCC command '%s' to nick '%s': %s", cfg.xdccCmd, cfg.botNick, irc_strerror(err));
        }
        cfg.flags |= SENDED_FLAG;
    }
}

void event_mode(irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count) {
    if (count > 1) {
        if (strcmp(params[1], "+v") == 0) {
            send_xdcc_requests(session);
        }
    }

}

void event_umode(irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count) {
    if (strcmp(params[0], "+r") == 0) {
        join_channels(session);
    }
}


void event_join (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
    irc_cmd_user_mode (session, "+i");
    send_xdcc_requests(session);
}


void event_connect (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
    join_channels(session);
}

// This callback is used when we receive a file from the remote party

void callback_dcc_recv_file(irc_session_t * session, irc_dcc_t id, int status, void * ctx, const char * data, unsigned int length) {
    if (status) {
        DBG_ERR("File sent error: %d\nerror desc: %s", status, irc_strerror(status));
        return;
    }

    if (!data) {
        DBG_OK("callback_dcc_recv_file called with data = NULL!");
        return;
    }

    cfg.context.sizeRcvd += length;
    fwrite(data, 1, length, cfg.context.fd);

    if (cfg.context.sizeRcvd == cfg.context.completeFileSize) {
        outputProgress();

        fclose(cfg.context.fd);

        irc_cmd_quit(cfg.session, "Goodbye!");
    }
}

void event_dcc_send_req(irc_session_t *session, const char *nick, const char *addr, const char *filename, unsigned long size, unsigned int dccid)
{
    DBG_OK("DCC send [%d] requested from '%s' (%s): %s (%" IRC_DCC_SIZE_T_FORMAT " bytes)", dccid, nick, addr, filename, size);

    if (irc_dcc_accept(session, dccid, NULL, callback_dcc_recv_file)) {
        warnx("failed to accept DCC request: %s", irc_strerror(irc_errno(session)));
        irc_disconnect(session);
        return;
    }

    // The forward-slash (/) and backslash (\) characters are invalid in file names.
    // If they are present, replace each one with an underscore (_).
    char *c;
    while ((c = strchr(filename, '/')) || (c = strchr(filename, '\\')))
        *c = '_';

    cfg.context.completeFileSize = size;
    cfg.context.fd = fopen(filename, "wb");
}

static char* usage = "usage: xdccget [-46aD] [-n <nick>] [-p <port>] <server> <channel(s)> <XDCC command>";

int main(int argc, char **argv)
{
    uint16_t port = 6667;
    char nick[IRC_NICK_MAX_SIZE] = {0};

    int opt;
    while ((opt = getopt(argc, argv, "Vhkn:p:a46")) != -1) {
        switch (opt) {
            case 'V': {
                unsigned int major, minor;
                irc_get_version(&major, &minor);
                printf("xdccget-0.0.0\nlibircclient-%d.%02d\n", major, minor);
                return EXIT_SUCCESS;
            }
            case 'h':
                puts(usage);
                return EXIT_SUCCESS;

            case 'p':
                port = (uint16_t)strtoul(optarg, NULL, 0);
                DBG_OK("Port number: %u", port);
                break;

            case '4':
                cfg.flags &= ~USE_IPV6_FLAG;
                break;

            case '6':
                cfg.flags |= USE_IPV6_FLAG;
                break;

            case '?':
            default:
                fputs(usage, stderr);
                return EXIT_FAILURE;
        }
    }
    argc -= optind;
    argv += optind;

    if (argc != 3) {
        fputs(usage, stderr);
        return EXIT_FAILURE;
    }

    char *host = argv[0];
    char *channels = argv[1];
    char *xdccCommand = argv[2];

    cfg.channelsToJoin = parseChannels(channels, &cfg.numChannels);
    parseDccDownload(xdccCommand, &cfg.botNick, &cfg.xdccCmd);
    DBG_OK("Parsed XDCC sender as \"%s\" and XDCC command as \"%s\"", cfg.botNick, cfg.xdccCmd);

    irc_callbacks_t callbacks;
    bzero(&callbacks, sizeof(callbacks));
    callbacks.event_connect = event_connect;
    callbacks.event_join = event_join;
    callbacks.event_dcc_send_req = event_dcc_send_req;
    callbacks.event_umode = event_umode;
    callbacks.event_mode = event_mode;

    cfg.session = irc_create_session(&callbacks);
    if (!cfg.session) {
        warn("failed to create IRC session object");
        for (size_t i = 0; i < cfg.numChannels; i++)
            free(cfg.channelsToJoin[i]);
        free(cfg.botNick);
        free(cfg.xdccCmd);
        free(cfg.channelsToJoin);
        return EXIT_FAILURE;
    }

    if (!strlen(nick)) invent_nick(nick, sizeof(nick));
    DBG_OK("IRC nick: '%s'", nick);

    int irc_err;
    if (cfg.flags & USE_IPV6_FLAG) {
        irc_err = irc_connect6(cfg.session, host, port, 0, nick, 0, 0);
    }
    else {
        irc_err = irc_connect(cfg.session, host, port, 0, nick, 0, 0);
    }

    if (irc_err) {
        warnx( "error: could not connect to server %s:%u: %s", host, port, irc_strerror(irc_errno(cfg.session)));
        irc_destroy_session(cfg.session);
        for (size_t i = 0; i < cfg.numChannels; i++)
            free(cfg.channelsToJoin[i]);
        free(cfg.botNick);
        free(cfg.xdccCmd);
        free(cfg.channelsToJoin);
        return EXIT_FAILURE;
    }

    if (irc_run(cfg.session) != 0) {
        if (irc_errno(cfg.session) != LIBIRC_ERR_TERMINATED && irc_errno(cfg.session) != LIBIRC_ERR_CLOSED) {
            warnx("error: could not connect or I/O error at server %s:%u: %s\n", host, port, irc_strerror(irc_errno(cfg.session)));
            irc_destroy_session(cfg.session);
            for (size_t i = 0; i < cfg.numChannels; i++)
                free(cfg.channelsToJoin[i]);
            fclose(cfg.context.fd);
            free(cfg.botNick);
            free(cfg.xdccCmd);
            free(cfg.channelsToJoin);
            return EXIT_FAILURE;
        }
    }

    irc_destroy_session(cfg.session);
    for (size_t i1 = 0; i1 < cfg.numChannels; i1++)
        free(cfg.channelsToJoin[i1]);
    fclose(cfg.context.fd);
    free(cfg.botNick);
    free(cfg.xdccCmd);
    free(cfg.channelsToJoin);
}
