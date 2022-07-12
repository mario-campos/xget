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
#include <regex.h>
#include <errno.h>
#include <getopt.h>

#include "libircclient/include/libircclient.h"
#include "xdccget.h"

#define IRC_DCC_SIZE_T_FORMAT PRIu64

/*
 * Match Groups:
 * 1. The entire matched string.
 * 2. The scheme ("irc" or "ircs").
 * 3. The hostname or IP address.
 * 4. [optional] The ':' and port number.
 * 5. [optional] The port number.
 * 6. Between one and five channels.
 * 7. The last channel (if more than one).
 */
#define IRC_URI_REGEX \
    "(ircs?)://([[:alnum:]\\.-]{3,})" \
    "(:([0-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?" \
    "/(#[[:alnum:]_-]+(,#[[:alnum:]_-]+){0,4})"

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

    char xdcc_command[24];
    snprintf(xdcc_command, sizeof(xdcc_command), "XDCC SEND #%u", state->pack);

    if (irc_cmd_msg(session, state->botNick, xdcc_command)) {
        warnx("failed to send XDCC command '%s' to nick '%s': %s", xdcc_command, state->botNick, irc_strerror(irc_errno(session)));
        irc_cmd_quit(session, NULL);
    }
}

void
event_connect(irc_session_t *session, const char *event, const char *origin, const char **params, unsigned int count)
{
    assert(session);

    struct xdccGetConfig *state = irc_get_ctx(session);
    for (uint32_t i = 0; i < state->numChannels; i++) {
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
callback_dcc_recv_file(irc_session_t *session, irc_dcc_t id, int status, void *fstream)
{
    assert(session);
    assert(fstream);

    int nread;
    char buf[1024];
    struct xdccGetConfig *cfg = irc_get_ctx(session);

    if (status) {
        warnx("failed to download file: %s", irc_strerror(status));
        irc_cmd_quit(session, NULL);
        return;
    }

    if ((nread = irc_dcc_read(session, id, buf, sizeof(buf))) < 0) {
	warnx("irc_dcc_read: socket read error");
	return;
    }

    fwrite(buf, sizeof(char), nread, fstream);

    cfg->currsize += nread;
    print_progress(cfg, nread);
}

void
callback_dcc_close(irc_session_t *session, irc_dcc_t id, int status, void *fstream)
{
    assert(session);
    assert(fstream);

    irc_cmd_quit(session, NULL);
}

void
event_dcc_send_req(irc_session_t *session, const char *nick, const char *addr, const char *filename, uint64_t size, irc_dcc_t dccid)
{
    assert(session);

    FILE *fstream = fopen(filename, "wb");
    if (!fstream) {
        warn("fopen");
        irc_cmd_quit(session, NULL);
        return;
    }

    struct xdccGetConfig *cfg = irc_get_ctx(session);
    strlcpy(&cfg->filename[0], filename, sizeof(cfg->filename));
    cfg->filesize = size;

    irc_dcc_accept(session, dccid, fstream, callback_dcc_recv_file, callback_dcc_close, !cfg->no_ack);
}

void
usage(int exit_status) {
    fputs("usage: xdccget [-A|--no-acknowledge] <uri> <nick> send <pack>\n", stderr);
    exit(exit_status);
}

static const struct option long_options[] = {
    {"no-acknowledge", no_argument, 0, 'A'},
    {"version",        no_argument, 0, 'V'},
    {"help",           no_argument, 0, 'h'},
    {NULL,             0,           0,  0 },
};

int
main(int argc, char **argv)
{
    struct xdccGetConfig cfg = {0};

    int opt;
    while ((opt = getopt_long(argc, argv, "AVh", long_options, NULL)) != -1) {
        switch (opt) {
	    case 'A':
		cfg.no_ack = true;
		break;
            case 'V': {
                unsigned int major, minor;
                irc_get_version(&major, &minor);
                printf("xdccget-0.0.0\nlibircclient-%u.%02u\n", major, minor);
                return EXIT_SUCCESS;
            }
            case 'h':
                usage(EXIT_SUCCESS);
            case '?':
            default:
                usage(EXIT_FAILURE);
        }
    }
    argc -= optind;
    argv += optind;

    // At the moment, only the XDCC "send" command is supported.
    if (argc != 4 || strcmp(argv[2], "send"))
	usage(EXIT_FAILURE);

    regex_t re;
    int regex_errno = regcomp(&re, IRC_URI_REGEX, REG_EXTENDED);
    assert(0 == regex_errno);

    regmatch_t matches[6];
    if ((regex_errno = regexec(&re, argv[0], sizeof(matches) / sizeof(matches[0]), matches, 0))) {
	assert(REG_NOMATCH == regex_errno);
        usage(EXIT_FAILURE);
    }

    regfree(&re);

    cfg.is_ircs = matches[1].rm_eo == 4;

    // Capture the IRC server hostname or IP address. If TLS is to be used, libircclient
    // requires the hostname to be prepended with a '#' character.
    if (cfg.is_ircs) argv[0][--matches[2].rm_so] = '#';
    argv[0][matches[2].rm_eo] = '\0';
    cfg.host = &argv[0][matches[2].rm_so];

    if (matches[3].rm_so < 0)
	cfg.port = cfg.is_ircs ? 6697 : 6667;
    else
	// atoi(3) is no longer recommended, but, in this case, I think it's appropriate
	// because the string has been validated by the regular-expression pattern
	// and atoi(3) handles mixed-text, like '6667/', better than strtonum(3).
    	cfg.port = atoi(&argv[0][matches[4].rm_so]);

    cfg.channelsToJoin[0] = &argv[0][matches[5].rm_so];
    cfg.numChannels = 1;

    // If other IRC channels were supplied, capture those as well.
    char *sep = cfg.channelsToJoin[0];
    while ((sep = strchr(sep, ','))) {
	if (cfg.numChannels >= sizeof(cfg.channelsToJoin) / sizeof(cfg.channelsToJoin[0])) break;
	*sep = '\0';
	cfg.channelsToJoin[cfg.numChannels++] = ++sep;
    }

    cfg.botNick = argv[1];
    cfg.pack = strtonum(argv[3], 1, UINT32_MAX, NULL);
    if (errno)
	errx(EXIT_FAILURE, "invalid pack number: %s", argv[3]);

    irc_callbacks_t callbacks = {0};
    callbacks.event_connect = event_connect;
    callbacks.event_join = event_join;
    callbacks.event_dcc_send_req = event_dcc_send_req;

    irc_session_t *session = irc_create_session(&callbacks);
    if (!session) errx(EXIT_FAILURE, "failed to create IRC session object");

    irc_set_ctx(session, &cfg);

    invent_nick(cfg.nick, sizeof(cfg.nick));

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
