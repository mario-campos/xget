/*
	xdccget -- download files from xdcc via cmd line
 */

#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <inttypes.h>
#include <sys/stat.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <sys/ioctl.h>

#include "libircclient.h"

#define LOG_ERR   0
#define LOG_QUIET 1
#define LOG_WARN  2
#define LOG_INFO  3


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
		    	exitPgm(EXIT_FAILURE);\
			} while(0)
#else
#define DBG_MSG(color, stream, format, ...) do {} while(0)
#define DBG_OK(format, ...) do {} while(0)
#define DBG_WARN(format, ...) do {} while(0)
#define DBG_ERR(format, ...) do {} while(0)
#endif

/* define macro for free that checks if ptr is null and sets ptr after free to null. */

struct xdccGetConfig {
    irc_session_t *session;
    uint32_t logLevel;
    struct dccDownload **dccDownloadArray;
    uint32_t numDownloads;
    uint64_t flags;

    char *ircServer;
    char **channelsToJoin;
    char *targetDir;
    char *nick;
    char *login_command;
    char *args[3];

    uint32_t numChannels;
    uint16_t port;
};

#define OUTPUT_FLAG               0x01
#define ALLOW_ALL_CERTS_FLAG      0x02
#define USE_IPV4_FLAG             0x03
#define USE_IPV6_FLAG	          0x04
#define SENDED_FLAG               0x06
#define ACCEPT_ALL_NICKS_FLAG     0x07
#define DONT_CONFIRM_OFFSETS_FLAG 0x08

#define IRC_DCC_SIZE_T_FORMAT PRIu64
#define NICKLEN 20

struct terminalDimension {
    int rows;
    int cols;
};

struct dccDownloadContext {
    struct dccDownloadProgress *progress;
    FILE *fd;
};

typedef uint64_t irc_dcc_size_t;

static struct xdccGetConfig cfg;

static uint32_t numActiveDownloads = 0;
static uint32_t finishedDownloads = 0;
static struct dccDownloadContext **downloadContext = NULL;
static struct dccDownloadProgress *curDownload = NULL;

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

    struct dccDownload **dccDownloadArray = (struct dccDownload**)calloc(numFound + 1, sizeof (struct dccDownload*));

    *numDownloads = numFound;

    for (i = 0; i < numFound; i++) {
        char *nick = NULL;
        char *xdccCmd = NULL;
        DBG_OK("%d: '%s'", i, dccDownloadString);
        parseDccDownload(dccDownloadString, &nick, &xdccCmd);
        DBG_OK("%d: '%s' '%s'", i, nick, xdccCmd);
        if (nick != NULL && xdccCmd != NULL) {
            dccDownloadArray[j] = malloc(sizeof(struct dccDownload));
            dccDownloadArray[j]->botNick = nick;
            dccDownloadArray[j]->xdccCmd = xdccCmd;
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

struct terminalDimension terminal_dimension;


static inline void set_bit(uint64_t *x, int bitNum) {
    *x |= (1L << bitNum);
}

static inline int get_bit(uint64_t *x, int bitNum) {
    int bit = 0;
    bit = (*x >> bitNum) & 1L;
    return bit;
}

void cfg_set_bit(struct xdccGetConfig *config, int bitNum);
int cfg_get_bit(struct xdccGetConfig *config, int bitNum);

inline void cfg_set_bit(struct xdccGetConfig *config, int bitNum) {
    set_bit(&config->flags, bitNum);
}

inline int cfg_get_bit(struct xdccGetConfig *config, int bitNum) {
    return get_bit(&config->flags, bitNum);
}

static inline void logprintf_line (FILE *stream, char *color_code, char *prefix, char *formatString, va_list va_alist) {
    fprintf(stream, "%s[%s] - ", color_code, prefix);
    vfprintf(stream, formatString, va_alist);
    fprintf(stream, "%s\n", KNRM);
}

void logprintf(int logLevel, char *formatString, ...) {
    va_list va_alist;

    if (cfg.logLevel == LOG_QUIET) {
        return;
    }

    va_start(va_alist, formatString);

    switch (logLevel) {
        case LOG_INFO:
            if (cfg.logLevel >= LOG_INFO) {
                logprintf_line(stdout, KGRN, "Info", formatString, va_alist);
            }
            break;
        case LOG_WARN:
            if (cfg.logLevel >= LOG_WARN) {
                logprintf_line(stderr, KYEL, "Warning", formatString, va_alist);
            }
            break;
        case LOG_ERR:
            logprintf_line(stderr, KRED, "Error", formatString, va_alist);
            break;
        default:
            DBG_WARN("logprintf called with unknown log-level. using normal logging.");
            vfprintf(stdout, formatString, va_alist);
            fprintf(stdout, "\n");
            break;
    }

    va_end(va_alist);
}

void initRand() {
    time_t t = time(NULL);

    if (t == ((time_t) -1)) {
        DBG_ERR("time failed");
    }

    srand((unsigned int) t);
}

int rand_range(int low, int high) {
    if (high == 0) {
        return 0;
    }
    return (rand() % high + low);
}

void createRandomNick(int nickLen, char *nick) {
    char *possibleChars = "abcdefghiklmnopqrstuvwxyzABCDEFGHIJHKLMOPQRSTUVWXYZ";
    size_t numChars = strlen(possibleChars);
    int i;

    if (nick == NULL) {
        DBG_WARN("nick = NULL!");
        return;
    }

    for (i = 0; i < nickLen; i++) {
        nick[i] = possibleChars[rand_range(0, numChars - 1)];
    }

}

struct terminalDimension *getTerminalDimension() {
    struct winsize w;
    ioctl(0, TIOCGWINSZ, &w);

    terminal_dimension.rows = w.ws_row;
    terminal_dimension.cols = w.ws_col;
    return &terminal_dimension;
}

void printProgressBar(const int numBars, const double percentRdy) {
    const int NUM_BARS = numBars;
    int i = 0;

    putchar('[');

    for (i = 0; i < NUM_BARS; i++) {
        if (i < (int) (NUM_BARS * percentRdy)) {
            putchar('#');
        }
        else {
            putchar('-');
        }
    }

    putchar(']');
}

int printSize(irc_dcc_size_t size) {
    char *sizeNames[] = {"Byte", "KByte", "MByte", "GByte", "TByte", "PByte"};

    double temp = (double) size;
    unsigned int i = 0;

    while (temp > 1024) {
        temp /= 1024;
        i++;
    }

    int charsPrinted = 0;

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

void outputProgress(struct dccDownloadProgress *progress) {
    struct terminalDimension *terminalDimension = getTerminalDimension();
    /* see comments below how these "numbers" are calculated */
    int progBarLen = terminalDimension->cols - (8 + 14 + 1 + 14 + 1 + 14 + 3 + 13 /* +1 for windows...*/);

    progress->sizeLast = progress->sizeNow;
    progress->sizeNow = progress->sizeRcvd;

    irc_dcc_size_t temp = (progress->completeFileSize == 0) ? 0 : progress->sizeRcvd * 1000000L / progress->completeFileSize;
    double curProcess = (double) temp / 1000000;
    irc_dcc_size_t curSpeed = progress->sizeNow - progress->sizeLast;

    int printedChars = progBarLen + 2;

    printProgressBar(progBarLen, curProcess);
    /* 8 chars -->' 75.30% ' */
    printedChars += printf(" %.2f%% ", curProcess * 100);
    /* 14 chars --> '1001.132 MByte' */
    printedChars += printSize(progress->sizeRcvd);
    /* 1 char */
    printedChars += printf("/");
    /* 14 chars --> '1001.132 MByte' */
    printedChars += printSize(progress->completeFileSize);
    /* 1 char */
    printedChars += printf("|");
    /* 14 chars --> '1001.132 MByte' */
    printedChars += printSize(curSpeed);
    /* 3 chars */
    printedChars += printf("/s|");

    /*calc ETA - max 13 chars */
    irc_dcc_size_t remainingSize = progress->completeFileSize - progress->sizeRcvd;
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

/*
 * Close IRC sessions and deallocate memory.
 */
void doCleanUp() {
    uint32_t i;
    if (cfg.session)
        irc_destroy_session(cfg.session);

    for (i = 0; i < cfg.numChannels; i++) {
        free(cfg.channelsToJoin[i]);
    }

    for (i = 0; cfg.dccDownloadArray[i]; i++) {
        free(cfg.dccDownloadArray[i]->botNick);
        free(cfg.dccDownloadArray[i]->xdccCmd);
        free(cfg.dccDownloadArray[i]);
    }

    for (i = 0; i < cfg.numDownloads && downloadContext[i]; i++) {
        struct dccDownloadContext *current_context = downloadContext[i];
        struct dccDownloadProgress *current_progress = current_context->progress;

        if (current_progress != NULL) {
            bool finishedDownloading = current_progress->sizeRcvd == current_progress->completeFileSize;

            if (!finishedDownloading) {
                fclose(current_context->fd);
                current_context->fd = NULL;
            }

            free(current_context->progress->completePath);
            free(current_context->progress);
        }

        free(downloadContext[i]);
    }

    free(cfg.targetDir);
    free(cfg.nick);
    free(cfg.login_command);
    free(cfg.dccDownloadArray);
    free(cfg.channelsToJoin);
    free(downloadContext);
}

void exitPgm(int retCode) {
    doCleanUp();
    exit(retCode);
}

void interrupt_handler(int signum) {
    if (cfg.session && irc_is_connected(cfg.session)) {
        irc_cmd_quit(cfg.session, "Goodbye!");
    }
    else {
        exitPgm(0);
    }
}

void output_handler (int signum) {
    alarm(1);
    cfg_set_bit(&cfg, OUTPUT_FLAG);
}

void dump_event (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
    char *param_string = calloc(1024, sizeof(char));
    unsigned int cnt;

    for (cnt = 0; cnt < count; cnt++) {
        if (cnt)
            strlcat(param_string, "|", 1024);

        char *message_without_color_codes = irc_color_strip_from_mirc(params[cnt]);
        strlcat(param_string, message_without_color_codes, 1024);
        free(message_without_color_codes);
    }

    logprintf(LOG_INFO, "Event \"%s\", origin: \"%s\", params: %d [%s]", event, origin ? origin : "NULL", cnt, param_string);
    free(param_string);
}

static void join_channels(irc_session_t *session) {
    for (uint32_t i = 0; i < cfg.numChannels; i++) {
        logprintf(LOG_INFO, "joining %s\n", cfg.channelsToJoin[i]);
        irc_cmd_join (session, cfg.channelsToJoin[i], 0);
    }
}

static void send_xdcc_requests(irc_session_t *session) {
    int i;
    if (!cfg_get_bit(&cfg, SENDED_FLAG)) {
        for (i = 0; cfg.dccDownloadArray[i] != NULL; i++) {
            char *botNick = cfg.dccDownloadArray[i]->botNick;
            char *xdccCommand = cfg.dccDownloadArray[i]->xdccCmd;

            logprintf(LOG_INFO, "/msg %s %s\n", botNick, xdccCommand);
            bool cmdSendingFailed = irc_cmd_msg(session, botNick, xdccCommand) == 1;

            if (cmdSendingFailed) {
                logprintf(LOG_ERR, "Cannot send xdcc command to bot!");
            }
        }

        cfg_set_bit(&cfg, SENDED_FLAG);
    }
}

void event_notice(irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count) {
    dump_event(session, event, origin, params, count);
}

void event_mode(irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count) {
    if (cfg.login_command != NULL && count > 1) {
        if (strcmp(params[1], "+v") == 0) {
            send_xdcc_requests(session);
        }
    }

}

void event_umode(irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count) {
    if (cfg.login_command != NULL) {
        if (strcmp(params[0], "+r") == 0) {
            join_channels(session);
        }
    }
}


void event_join (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
    irc_cmd_user_mode (session, "+i");

    if (cfg.login_command == NULL) {
        send_xdcc_requests(session);
    }
}

static void send_login_command(irc_session_t *session) {
    if (strlen(cfg.login_command) >= 9) {
        char *user = strdup(cfg.login_command);
        char *auth_command = strdup(&cfg.login_command[9]);
        user[9] = '\0';

        logprintf(LOG_INFO, "sending login-command: %s", cfg.login_command);

        bool cmdSendingFailed = irc_cmd_msg(session, user, auth_command) == 1;

        if (cmdSendingFailed) {
            logprintf(LOG_ERR, "Cannot send command to authenticate!");
        }

        free(user);
        free(auth_command);
    } else {
        logprintf(LOG_ERR, "the login-command is too short. cant send this login-command.");
    }
}


void event_connect (irc_session_t * session, const char * event, const char * origin, const char ** params, unsigned int count)
{
    dump_event (session, event, origin, params, count);

    if (cfg.login_command != NULL) {
        send_login_command(session);
    }
    else {
        join_channels(session);
    }
}

// This callback is used when we receive a file from the remote party

void callback_dcc_recv_file(irc_session_t * session, irc_dcc_t id, int status, void * ctx, const char * data, unsigned int length) {
    if (data == NULL) {
        DBG_WARN("callback_dcc_recv_file called with data = NULL!");
        return;
    }

    if (ctx == NULL) {
        DBG_WARN("callback_dcc_recv_file called with ctx = NULL!");
        return;
    }

    if (length == 0) {
        DBG_WARN("callback_dcc_recv_file called with length = 0!");
        return;
    }

    if (status) {
        DBG_ERR("File sent error: %d\nerror desc: %s", status, irc_strerror(status));
        return;
    }

    struct dccDownloadContext *context = (struct dccDownloadContext*) ctx;
    struct dccDownloadProgress *progress = context->progress;

    progress->sizeRcvd += length;
    fwrite(data, 1, length, context->fd);

    if (progress->sizeRcvd == progress->completeFileSize) {
        alarm(0);
        outputProgress(progress);
        printf("\nDownload completed!\n");
        fflush(NULL);

        fclose(context->fd);
        context->fd = NULL;

        finishedDownloads++;

        if (finishedDownloads == numActiveDownloads) {
            irc_cmd_quit(cfg.session, "Goodbye!");
        }
    }
}

void recvFileRequest (irc_session_t *session, const char *nick, const char *addr, const char *filename, unsigned long size, unsigned int dccid)
{
    DBG_OK("DCC send [%d] requested from '%s' (%s): %s (%" IRC_DCC_SIZE_T_FORMAT " bytes)", dccid, nick, addr, filename, size);

    char *fileName = strdup(filename);

    /* chars / and \ are not permitted to appear in a valid filename. if someone wants to send us such a file
       then something is definately wrong. so just exit pgm then and print error msg to user.*/
    if (strchr(fileName, '/') || strchr(fileName, '\\')) {
        /* filename contained bad chars. print msg and exit...*/
        logprintf(LOG_ERR, "Someone wants to send us a file that contains / or \\. This is not permitted.\nFilename was: %s", fileName);
        exitPgm(EXIT_FAILURE);
    }

    struct dccDownloadProgress *progress = malloc(sizeof(*progress));
    progress->completeFileSize = size;
    progress->sizeRcvd = 0;
    progress->sizeNow = 0;
    progress->sizeLast = 0;
    progress->completePath = fileName;
    curDownload = progress;

    struct dccDownloadContext *context = malloc(sizeof(struct dccDownloadContext));
    downloadContext[numActiveDownloads] = context;
    numActiveDownloads++;
    context->progress = progress;

    DBG_OK("nick at recvFileReq is %s", nick);

    if (access(filename, F_OK) == 0) {
        struct stat st;

        if (stat(filename, &st) != 0) {
            logprintf(LOG_ERR, "Cant stat the file %s. Exiting now.", filename);
            exitPgm(EXIT_FAILURE);
        }

        context->fd = fopen(filename, "ab");

        off_t fileSize = st.st_size;

        if (size == (irc_dcc_size_t) fileSize) {
            logprintf(LOG_ERR, "file %s is already downloaded, exit pgm now.", filename);
            exitPgm(EXIT_FAILURE);
        }

        /* file already exists but is empty. so accept it, rather than resume... */
        if (fileSize == 0) {
            goto accept_flag;
        }
    }
    else {
        int ret;
        context->fd = fopen(filename, "wb");
        logprintf(LOG_INFO, "file %s does not exist. creating file and downloading it now.", filename);
accept_flag:
        ret = irc_dcc_accept(session, dccid, context, callback_dcc_recv_file);
        if (ret != 0) {
            logprintf(LOG_ERR, "Could not connect to bot\nError was: %s\n", irc_strerror(irc_errno(cfg.session)));
            exitPgm(EXIT_FAILURE);
        }
    }
}

/*
 * `init_signal` is a wrapper for `sigaction`; i.e. it defines
 * a signal handler for the given signal number.
 */
void init_signal(int signum, void (*handler) (int)) {
    struct sigaction act;
    int ret;

    memset(&act, 0, sizeof(act));
    sigemptyset (&act.sa_mask);

    act.sa_handler = handler;
    act.sa_flags = SA_RESTART;

    ret = sigaction(signum, &act, NULL);
    if (ret == -1) {
        logprintf(LOG_ERR, "could not set up signal %d", signum);
        exitPgm(EXIT_FAILURE);
    }
}

static char* usage = "usage: xdccget [-46aDqv] [-n <nick>] [-p <port>] [-l <login command>] [-d <path>] <server> <channel(s)> <XDCC command>";

void parseArguments(int argc, char **argv) {
    int opt;

    cfg.logLevel = LOG_INFO;

    while ((opt = getopt(argc, argv, "Vhqvkd:n:l:p:aD46")) != -1) {
        switch (opt) {
            case 'V': {
                unsigned int major, minor;
                irc_get_version(&major, &minor);
                puts("xdccget-0.0.0");
                printf("libircclient-%d.%02d\n", major, minor);
                exit(0);
            }
            case 'h':
                puts(usage);
                exit(EXIT_FAILURE);

            case 'q':
                DBG_OK("setting log-level as quiet.");
                cfg.logLevel = LOG_QUIET;
                break;

            case 'v':
                DBG_OK("setting log-level as warn.");
                cfg.logLevel = LOG_WARN;
                break;

            case 'k':
                cfg_set_bit(&cfg, ALLOW_ALL_CERTS_FLAG);
                break;

            case 'd':
                DBG_OK("setting target dir as %s", optarg);
                cfg.targetDir = strdup(optarg);
                break;

            case 'n':
                DBG_OK("setting nickname as %s", optarg);
                cfg.nick = strdup(optarg);
                break;

            case 'l':
                DBG_OK("setting login-command as %s", optarg);
                cfg.login_command = strdup(optarg);
                break;

            case 'p':
                cfg.port = (unsigned short) strtoul(optarg, NULL, 0);
                DBG_OK("setting port as %u", cfg.port);
                break;

            case 'a':
                cfg_set_bit(&cfg, ACCEPT_ALL_NICKS_FLAG);
                break;

            case 'D':
                cfg_set_bit(&cfg, DONT_CONFIRM_OFFSETS_FLAG);
                break;

            case '4':
                cfg_set_bit(&cfg, USE_IPV4_FLAG);
                break;

#ifdef ENABLE_IPV6
            case '6':
                cfg_set_bit(&cfg, USE_IPV6_FLAG);
                break;
#endif
            case '?':
                logprintf(LOG_ERR, "%s\n", usage);
                exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc || (argc - optind) > 3) {
        puts(usage);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; (i + optind) < argc; i++) {
        cfg.args[i] = argv[i + optind];
    }
}

int main (int argc, char **argv)
{
    int ret = -1;

    initRand();

    memset(&cfg, 0, sizeof(struct xdccGetConfig));

    cfg.logLevel = LOG_WARN;
    cfg.port = 6667;

    cfg.targetDir = getcwd(NULL, 0);
    if (cfg.targetDir == NULL)
        DBG_ERR("cannot get current working directory to download file.");

    parseArguments(argc, argv);

    cfg.ircServer = cfg.args[0];

    cfg.channelsToJoin = parseChannels(cfg.args[1], &cfg.numChannels);
    cfg.dccDownloadArray = parseDccDownloads(cfg.args[2], &cfg.numDownloads);

    downloadContext = calloc(cfg.numDownloads, sizeof(struct downloadContext*));

    init_signal(SIGINT, interrupt_handler);
    init_signal(SIGALRM, output_handler);

    irc_callbacks_t callbacks;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.event_connect = event_connect;
    callbacks.event_join = event_join;
    callbacks.event_dcc_send_req = recvFileRequest;
    callbacks.event_ctcp_rep = dump_event;
    callbacks.event_ctcp_action = dump_event;
    callbacks.event_unknown = dump_event;
    callbacks.event_privmsg = dump_event;
    callbacks.event_notice = event_notice;
    callbacks.event_umode = event_umode;
    callbacks.event_mode = event_mode;
    cfg.session = irc_create_session (&callbacks);

    if (!cfg.session) {
        logprintf(LOG_ERR, "Could not create session\n");
        exitPgm(EXIT_FAILURE);
    }

    if (cfg.nick == NULL) {
        cfg.nick = malloc(NICKLEN);
        createRandomNick(NICKLEN, cfg.nick);
    }

    logprintf(LOG_INFO, "nick is %s\n", cfg.nick);
    irc_option_set(cfg.session, LIBIRC_OPTION_DEBUG);

    if (cfg_get_bit(&cfg, USE_IPV4_FLAG)) {
        ret = irc_connect(cfg.session, cfg.ircServer, cfg.port, 0, cfg.nick, 0, 0);
    }
#ifdef ENABLE_IPV6
    else if (cfg_get_bit(&cfg, USE_IPV6_FLAG)) {
        ret = irc_connect6(cfg.session, cfg.ircServer, cfg.port, 0, cfg.nick, 0, 0);
    }
#endif
    else {
        ret = irc_connect(cfg.session, cfg.ircServer, cfg.port, 0, cfg.nick, 0, 0);
    }

    if (ret != 0) {
        logprintf(LOG_ERR, "Could not connect to server %s and port %u.\nError was: %s\n", cfg.ircServer, cfg.port, irc_strerror(irc_errno(cfg.session)));
        exitPgm(EXIT_FAILURE);
    }

    alarm(1);

    ret = irc_run (cfg.session);

    if (ret != 0) {
        if (irc_errno(cfg.session) != LIBIRC_ERR_TERMINATED && irc_errno(cfg.session) != LIBIRC_ERR_CLOSED) {
            logprintf(LOG_ERR, "Could not connect or I/O error at server %s and port %u\nError was:%s\n", cfg.ircServer, cfg.port, irc_strerror(irc_errno(cfg.session)));
            exitPgm(EXIT_FAILURE);
        }
    }

    doCleanUp();
    return EXIT_SUCCESS;
}
