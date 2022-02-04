#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <stdint.h>
#include <err.h>
#include <time.h>
#include <assert.h>
#include <limits.h>

#include "libircclient/include/libircclient.h"

#define IRC_DCC_SIZE_T_FORMAT PRIu64
#define IRC_NICK_MAX_SIZE 30

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
                exit(EXIT_FAILURE);\
			} while(0)
#else
#define DBG_MSG(color, stream, format, ...) do {} while(0)
#define DBG_OK(format, ...) do {} while(0)
#define DBG_WARN(format, ...) do {} while(0)
#define DBG_ERR(format, ...) do {} while(0)
#endif

struct xdccGetConfig {
    char *host;
    uint16_t port;
    char nick[IRC_NICK_MAX_SIZE];
    char botNick[IRC_NICK_MAX_SIZE];
    char xdccCmd[IRC_NICK_MAX_SIZE];
    char **channelsToJoin;
    uint32_t numChannels;
    char filename[NAME_MAX];
    uint64_t filesize;
    uint64_t currsize;
};

void
parseDccDownload(char *xdcc_nick_command, char *nick, size_t nick_size, char *xdcc_command, size_t xdcc_cmd_size)
{
    assert(xdcc_nick_command);
    assert(nick);
    assert(xdcc_command);

    char *space;
    if (!(space = strchr(xdcc_nick_command, ' '))) {
        *nick = '\0';
        *xdcc_command = '\0';
        return;
    }

    *space = '\0';
    strlcpy(nick, xdcc_nick_command, nick_size);
    strlcpy(xdcc_command, space+1, xdcc_cmd_size);
}

char*
strip(char *s)
{
    assert(s);

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

char**
split(char *s, int *count)
{
    assert(s);

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

char**
parseChannels(char *channelString, uint32_t *numChannels)
{
    assert(channelString);

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

void
invent_nick(char *dst, size_t dst_size)
{
    assert(dst);
    assert(dst_size);

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

void
event_join(irc_session_t *session, const char *event, const char *origin, const char **params, unsigned int count)
{
    assert(session);

    struct xdccGetConfig *state = irc_get_ctx(session);
    DBG_OK("Sending XDCC command '%s' to nick '%s'", state->xdccCmd, state->botNick);
    if (irc_cmd_msg(session, state->botNick, state->xdccCmd)) {
        warnx("failed to send XDCC command '%s' to nick '%s': %s", state->xdccCmd, state->botNick, irc_strerror(irc_errno(session)));
        irc_cmd_quit(session, NULL);
    }
}

void
event_connect(irc_session_t *session, const char *event, const char *origin, const char **params, unsigned int count)
{
    assert(session);

    struct xdccGetConfig *state = irc_get_ctx(session);
    for (uint32_t i = 0; i < state->numChannels; i++) {
        DBG_OK("Joining channel '%s'", state->channelsToJoin[i]);
        irc_cmd_join(session, state->channelsToJoin[i], 0);
    }
}

void
print_progress(struct xdccGetConfig *cfg, unsigned int chunk_size_bytes)
{
    assert(cfg);

    double delta;
    static int this_or_that = 0;
    static struct timespec that_time, this_time;

    this_or_that = !this_or_that;
    if (this_or_that) {
	clock_gettime(CLOCK_MONOTONIC, &this_time);
	delta = (double)(this_time.tv_nsec - that_time.tv_nsec) / 1.0e9;
    } else {
	clock_gettime(CLOCK_MONOTONIC, &that_time);
	delta = (double)(that_time.tv_nsec - this_time.tv_nsec) / 1.0e9;
    }

    double percentage = (double)cfg->currsize / (double)cfg->filesize * 100.0;
    double throughput = ((double)chunk_size_bytes / 1024.0) / (cfg->currsize == chunk_size_bytes ? 1.0 : delta);
    printf("\r%s\t%3.f%%\t%6.1f KB/s", cfg->filename, percentage, throughput);
}

void
callback_dcc_recv_file(irc_session_t *session, irc_dcc_t id, int status, void *fstream, const char *data, unsigned int length)
{
    assert(session);
    assert(fstream);

    if (status) {
        warnx("failed to download file: %s", irc_strerror(status));
        irc_cmd_quit(session, NULL);
        return;
    }
    if (!data) {
        irc_cmd_quit(session, NULL);
        return;
    }

    fwrite(data, sizeof(char), length, fstream);

    struct xdccGetConfig *cfg = irc_get_ctx(session);
    cfg->currsize += length;
    print_progress(cfg, length);
}

void
event_dcc_send_req(irc_session_t *session, const char *nick, const char *addr, const char *filename, uint64_t size, irc_dcc_t dccid)
{
    assert(session);
    DBG_OK("DCC send [%d] requested from '%s' (%s): %s (%" IRC_DCC_SIZE_T_FORMAT " bytes)", dccid, nick, addr, filename, size);

    // The forward-slash (/) and backslash (\) characters are invalid in file names.
    // If they are present, replace each one with an underscore (_).
    char *c;
    while ((c = strchr(filename, '/')) || (c = strchr(filename, '\\')))
        *c = '_';

    FILE *fstream = fopen(filename, "wb");
    if (!fstream) {
        warn("fopen");
        irc_cmd_quit(session, NULL);
        return;
    }

    irc_dcc_accept(session, dccid, fstream, callback_dcc_recv_file);

    struct xdccGetConfig *cfg = irc_get_ctx(session);
    strlcpy(&cfg->filename[0], filename, sizeof(cfg->filename));
    cfg->filesize = size;
}

static char* usage = "usage: xdccget [-p <port>] <server> <channel(s)> <XDCC command>";

int
main(int argc, char **argv)
{
    struct xdccGetConfig cfg = {0};
    cfg.port = 6667;

    int opt;
    while ((opt = getopt(argc, argv, "Vhp:")) != -1) {
        switch (opt) {
            case 'V': {
                unsigned int major, minor;
                irc_get_version(&major, &minor);
                printf("xdccget-0.0.0\nlibircclient-%u.%02u\n", major, minor);
                return EXIT_SUCCESS;
            }
            case 'h':
                puts(usage);
                return EXIT_SUCCESS;

            case 'p':
                cfg.port = (uint16_t)strtoul(optarg, NULL, 0);
                DBG_OK("Port number: %u", cfg.port);
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

    cfg.host = argv[0];

    cfg.channelsToJoin = parseChannels(argv[1], &cfg.numChannels);
    parseDccDownload(argv[2], cfg.botNick, sizeof(cfg.botNick), cfg.xdccCmd, sizeof(cfg.xdccCmd));
    DBG_OK("Parsed XDCC sender as \"%s\" and XDCC command as \"%s\"", cfg.botNick, cfg.xdccCmd);

    irc_callbacks_t callbacks = {0};
    callbacks.event_connect = event_connect;
    callbacks.event_join = event_join;
    callbacks.event_dcc_send_req = event_dcc_send_req;

    irc_session_t *session = irc_create_session(&callbacks);
    if (!session) errx(EXIT_FAILURE, "failed to create IRC session object");

    irc_set_ctx(session, &cfg);

    invent_nick(cfg.nick, sizeof(cfg.nick));
    DBG_OK("IRC nick: '%s'", cfg.nick);

    if (irc_connect(session, cfg.host, cfg.port, 0, cfg.nick, 0, 0)) {
        irc_destroy_session(session);
        errx(EXIT_FAILURE, "failed to establish TCP connection to %s:%u: %s", cfg.host, cfg.port, irc_strerror(irc_errno(session)));
    }

    if (irc_run(session)) {
        if (irc_errno(session) != LIBIRC_ERR_TERMINATED && irc_errno(session) != LIBIRC_ERR_CLOSED) {
            irc_destroy_session(session);
            errx(EXIT_FAILURE, "failed to start IRC session: %s", irc_strerror(irc_errno(session)));
        }
    }

    irc_destroy_session(session);
}
