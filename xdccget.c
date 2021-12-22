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

#include "helper.h"

#define NICKLEN 20

static struct xdccGetConfig cfg;

static uint32_t numActiveDownloads = 0;
static uint32_t finishedDownloads = 0;
static struct dccDownloadContext **downloadContext = NULL;
static struct dccDownloadProgress *curDownload = NULL;

struct xdccGetConfig *getCfg() {
    return &cfg;
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
        freeDccDownload(cfg.dccDownloadArray[i]);
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

            freeDccProgress(current_context->progress);
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

void output_all_progesses() {
    unsigned int i;

    if (numActiveDownloads < 1) {
        printf("Please wait until the download is started!\r");
    }
    else {
        for (i = 0; i < numActiveDownloads; i++) {
            outputProgress(downloadContext[i]->progress);

            if (numActiveDownloads != 1) {
                printf("\n");
            }
        }
    }

    fflush(stdout);

    if (numActiveDownloads == 1) {
        /* send \r so that we override this line the next time...*/
        printf("\r");
    }
}

void output_handler (int signum) {
    alarm(1);
    cfg_set_bit(getCfg(), OUTPUT_FLAG);
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

    if (unlikely(progress->sizeRcvd == progress->completeFileSize)) {
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

void callback_dcc_resume_file (irc_session_t * session, irc_dcc_t dccid, int status, void * ctx, const char * data, irc_dcc_size_t length) {
    struct dccDownloadContext *context = (struct dccDownloadContext*) ctx;

    DBG_OK("got to callback_dcc_resume_file\n");
    fseek(context->fd, length, SEEK_SET);
    DBG_OK("before irc_dcc_accept!\n");

    struct dccDownloadProgress *tdp = context->progress;
    tdp->sizeRcvd = length;

    int ret = irc_dcc_accept (session, dccid, ctx, callback_dcc_recv_file);

    if (ret != 0) {
        logprintf(LOG_ERR, "Could not connect to bot\nError was: %s\n", irc_strerror(irc_errno(cfg.session)));
        exitPgm(EXIT_FAILURE);
    }

    DBG_OK("after irc_dcc_accept!\n");
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

    struct dccDownloadProgress *progress = newDccProgress(fileName, size);
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

void print_output_callback (irc_session_t *session) {
    if (unlikely(cfg_get_bit(getCfg(), OUTPUT_FLAG))) {
        output_all_progesses();
        cfg_clear_bit(getCfg(), OUTPUT_FLAG);
    }
}

/*
 * This function configures the libircclient event loop.
 */
void initCallbacks(irc_callbacks_t *callbacks) {
    memset (callbacks, 0, sizeof(*callbacks));

    callbacks->event_connect = event_connect;
    callbacks->event_join = event_join;
    callbacks->event_dcc_send_req = recvFileRequest;
    callbacks->event_ctcp_rep = dump_event;
    callbacks->event_ctcp_action = dump_event;
    callbacks->event_unknown = dump_event;
    callbacks->event_privmsg = dump_event;
    callbacks->event_notice = event_notice;
    callbacks->event_umode = event_umode;
    callbacks->event_mode = event_mode;
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

static char* usage = "usage: xdccget [-46aDqv] [-n <nickname>] [-p <port number>]\n"
                     "[-l <login-command>] [-d <download-directory>]\n"
                     "<server> <channel(s)> <bot cmds>";

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
                logprintf(LOG_ERR, "%s\n", usage);
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
        logprintf(LOG_ERR, "%s\n", usage);
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
    initCallbacks(&callbacks);
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
