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
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <libgen.h>

#include "libircclient/include/libircclient.h"
#include "xget.h"

#define IRC_DCC_SIZE_T_FORMAT PRIu64

#define ANSI_TEXT_INVERT "\x1b[7m"
#define ANSI_TEXT_NORMAL "\x1b[m"
#define ANSI_CURSOR_SHOW "\x1b[?25h"
#define ANSI_CURSOR_HIDE "\x1b[?25l"

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

void event_join (irc_session_t *session, const char *event, const char *origin, const char **params, unsigned int count)
{
    assert (session);

    struct xdccGetConfig *state = irc_get_ctx (session);

    char xdcc_command[24];
    snprintf (xdcc_command, sizeof xdcc_command, "XDCC SEND #%u", state->pack);

    if ( irc_cmd_msg (session, state->botNick, xdcc_command) )
    {
        warnx ("failed to send XDCC command '%s' to nick '%s': %s", xdcc_command, state->botNick, irc_strerror(irc_errno(session)));
        irc_cmd_quit (session, NULL);
    }
}

void event_connect (irc_session_t *session, const char *event, const char *origin, const char **params, unsigned int count)
{
    assert (session);

    struct xdccGetConfig *state = irc_get_ctx (session);
    for ( uint32_t i = 0; i < state->numChannels; i++ )
    {
        irc_cmd_join (session, state->channelsToJoin[i], 0);
    }
}

void callback_dcc_recv_file (irc_session_t *session, irc_dcc_t id, int status, void *addr)
{
    assert (session);

    int nread;
    struct xdccGetConfig *cfg = irc_get_ctx (session);

    if ( status )
    {
        warnx ("failed to download file: %s", irc_strerror(status));
        irc_cmd_quit (session, NULL);
        return;
    }

    if ( (nread = irc_dcc_read (session, id, (char *)addr + cfg->currsize, cfg->filesize - cfg->currsize)) < 0 )
    {
	warnx ("irc_dcc_read: socket read error");
	return;
    }

    pthread_mutex_lock (&cfg->mutex);
    cfg->currsize += nread;
    pthread_mutex_unlock (&cfg->mutex);
}

void callback_dcc_close (irc_session_t *session, irc_dcc_t id, int status, void *addr)
{
    assert (session);

    irc_cmd_quit (session, NULL);

    struct xdccGetConfig *cfg = irc_get_ctx (session);

    if ( munmap (addr, cfg->filesize) )
	warn ("munmap");
    close (cfg->fd);
}

void event_dcc_send_req (irc_session_t *session, const char *nick, const char *addr, const char *filename, irc_dcc_size_t size, irc_dcc_t dccid)
{
    assert (session);
    struct xdccGetConfig *cfg = irc_get_ctx (session);

    if ( !cfg->has_opt_output_document )
    {
	// Check that the file's name is only a file name and not a path. DCC senders
	// should not be sending file paths as file names, and we should not be opening
	// untrusted files outside the current working directory.
	if ( strcmp (basename((char *)filename), filename) )
	{
	    warnx ("DCC sender sent a file path as the name: '%s'", filename);
	    irc_cmd_quit (session, NULL);
	    return;
	}
	else
	{
	    cfg->filename = strdup(filename);
	}
    }

    int fd = open (cfg->filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if ( fd < 0 )
    {
        warn ("open");
        irc_cmd_quit (session, NULL);
        return;
    }

    // The file must be allocated to its final size in order for the mmap(2) below to succeed.
    ftruncate (fd, size);

    void *maddr = mmap (NULL, size, PROT_WRITE, MAP_SHARED, fd, 0);
    if ( maddr == MAP_FAILED )
    {
	warn ("mmap");
	irc_cmd_quit (session, NULL);
	return;
    }

    if ( madvise (maddr, size, MADV_SEQUENTIAL) < 0 )
    {
	warn ("madvise");
    }

    pthread_mutex_lock (&cfg->mutex);
    cfg->fd = fd;
    cfg->filesize = size;
    pthread_mutex_unlock (&cfg->mutex);
    pthread_cond_signal (&cfg->cv);

    irc_dcc_accept (session, dccid, maddr, callback_dcc_recv_file, callback_dcc_close, !cfg->has_opt_no_acknowledge);
}

void usage (int exit_status)
{
    fputs ("usage: xget [-A|--no-acknowledge] [-O|--output-document] <uri> <nick> send <pack>\n", stderr);
    exit (exit_status);
}

char * unit (size_t size)
{
    if ( size < 1024 )
        return "B";
    if ( size < 1024 * 1024 )
        return "KiB";
    if ( size < 1024 * 1024 * 1024 )
        return "MiB";
    return "GiB";
}

void * thread_progress (void *arg)
{
    struct xdccGetConfig *cfg = arg;

    size_t stat_len;
    char line_buffer[1024], stat_buffer[60];

    // Get a timestamp to calculate the Time To Download (TTD) and average throughput.
    time_t start_time = time (NULL);

    // Get terminal's dimensions (rows, columns).
    struct winsize ws;
    ioctl (STDOUT_FILENO, TIOCGWINSZ, &ws);

    // Wait until the download size is known.
    pthread_mutex_lock (&cfg->mutex);
    // Program defensively against spurious wake-ups.
    while ( !cfg->filesize ) pthread_cond_wait (&cfg->cv, &cfg->mutex);
    irc_dcc_size_t total_size = cfg->filesize;
    pthread_mutex_unlock (&cfg->mutex);

    size_t name_len = strlen (cfg->filename);

    double humanscaled_total_size = total_size;
    while ( humanscaled_total_size > 1024 ) humanscaled_total_size /= 1024;

    irc_dcc_size_t this_size = 0;
    while ( this_size != total_size )
    {
	sleep (1);

	pthread_mutex_lock (&cfg->mutex);
	irc_dcc_size_t size_delta = cfg->currsize - this_size;
	this_size = cfg->currsize;
	pthread_mutex_unlock (&cfg->mutex);

	int progress_percentage = (this_size * 100) / total_size;

	// Translate the progress percentage relative to the terminal's width (in columns).
	// This percentage will be used for displaying the "progress bar" across the line.
	int percentage_width = (progress_percentage * ws.ws_col) / 100;

	double humanscaled_this_size = this_size;
	while ( humanscaled_this_size > 1024 ) humanscaled_this_size /= 1024;

	double humanscaled_size_delta = size_delta;
	while ( humanscaled_size_delta > 1024 ) humanscaled_size_delta /= 1024;

	int eta = (total_size - this_size) / size_delta;
	int eta_hours = eta / 3600;
	int eta_minutes = (eta - eta_hours * 3600) / 60;
	int eta_seconds = (eta - eta_hours * 3600) % 60;

	stat_len = snprintf (stat_buffer, sizeof stat_buffer, "%d%%   %.1f %s / %.1f %s   %.1f %s/s   %02d:%02d:%02d",
			    progress_percentage, humanscaled_this_size, unit (this_size), humanscaled_total_size,
			    unit (total_size), humanscaled_size_delta, unit (size_delta), eta_hours,
			    eta_minutes, eta_seconds);

	if ( name_len + stat_len + 1 > ws.ws_col )
	{
            int limit = ws.ws_col - stat_len - 4;
	    snprintf (line_buffer, sizeof line_buffer, "%.*s... ", limit, cfg->filename);
        }
	else
	{
            int limit = ws.ws_col - stat_len - name_len;
            snprintf (line_buffer, sizeof line_buffer, "%s%.*s", cfg->filename, limit,
	    "                                                                                                     ");
	}

	strncat (line_buffer, stat_buffer, sizeof line_buffer - strlen (line_buffer) - 1);
	printf (ANSI_CURSOR_HIDE ANSI_TEXT_INVERT "\r%.*s" ANSI_TEXT_NORMAL "%s", percentage_width, line_buffer, line_buffer + percentage_width);
	fflush (stdout);
    }

    time_t ttd = time (NULL) - start_time;
    int ttd_hours = ttd / 3600;
    int ttd_minutes = (ttd - ttd_hours * 3600) / 60;
    int ttd_seconds = (ttd - ttd_hours * 3600) % 60;

    size_t avg_throughput = total_size / ttd;
    double humanscaled_avg_throughput = avg_throughput;
    while ( humanscaled_avg_throughput > 1024 ) humanscaled_avg_throughput /= 1024;

    stat_len = snprintf (stat_buffer, sizeof stat_buffer, "100%%   %.1f %s   %.1f %s/s   %02d:%02d:%02d",
			humanscaled_total_size, unit (total_size), humanscaled_avg_throughput, unit (avg_throughput),
			ttd_hours, ttd_minutes, ttd_seconds);

    if ( name_len + stat_len + 1 > ws.ws_col )
    {
	int limit = ws.ws_col - stat_len - 4;
	snprintf (line_buffer, sizeof line_buffer, "%.*s... ", limit, cfg->filename);
    }
    else
    {
	int limit = ws.ws_col - stat_len - name_len;
	snprintf (line_buffer, sizeof line_buffer, "%s%.*s", cfg->filename, limit,
	"                                                                                                     ");
    }

    printf (ANSI_TEXT_NORMAL "\r%s%s\n" ANSI_CURSOR_SHOW, line_buffer, stat_buffer);
    fflush (stdout);

    return NULL;
}

int main (int argc, char **argv)
{
    struct xdccGetConfig cfg = {
	    .mutex = PTHREAD_MUTEX_INITIALIZER,
	    .cv = PTHREAD_COND_INITIALIZER,
    };

    const struct option long_options[] = {
	{"output-document", required_argument, 0, 'O'},
	{"no-acknowledge",  no_argument,       0, 'A'},
	{"version",         no_argument,       0, 'V'},
	{"help",            no_argument,       0, 'h'},
	{NULL,              0,                 0,  0 },
    };

    int opt;
    while ( (opt = getopt_long (argc, argv, "O:AVh", long_options, NULL)) != -1 )
    {
        switch ( opt )
	{
	    case 'O':
		cfg.has_opt_output_document = true;
		cfg.filename = optarg;
		break;
	    case 'A':
		cfg.has_opt_no_acknowledge = true;
		break;
            case 'V': {
                unsigned int major, minor;
                irc_get_version (&major, &minor);
                printf ("xget-0.0.0\nlibircclient-%u.%02u\n", major, minor);
                return EXIT_SUCCESS;
            }
            case 'h':
                usage (EXIT_SUCCESS);
            case '?':
            default:
                usage (EXIT_FAILURE);
        }
    }
    argc -= optind;
    argv += optind;

    // At the moment, only the XDCC "send" command is supported.
    if ( argc != 4 || strcmp (argv[2], "send") )
	usage (EXIT_FAILURE);

    regex_t re;
    int regex_errno = regcomp (&re, IRC_URI_REGEX, REG_EXTENDED);
    assert (0 == regex_errno);

    regmatch_t matches[6];
    if ( (regex_errno = regexec (&re, argv[0], sizeof matches / sizeof matches[0], matches, 0)) )
    {
	assert (REG_NOMATCH == regex_errno);
        usage (EXIT_FAILURE);
    }

    regfree (&re);

    cfg.is_ircs = matches[1].rm_eo == 4;

    // Capture the IRC server hostname or IP address. If TLS is to be used, libircclient
    // requires the hostname to be prepended with a '#' character.
    if ( cfg.is_ircs ) argv[0][--matches[2].rm_so] = '#';
    argv[0][matches[2].rm_eo] = '\0';
    cfg.host = &argv[0][matches[2].rm_so];

    if ( matches[3].rm_so < 0 )
	cfg.port = cfg.is_ircs ? 6697 : 6667;
    else
	// atoi(3) is no longer recommended, but, in this case, I think it's appropriate
	// because the string has been validated by the regular-expression pattern
	// and atoi(3) handles mixed-text, like '6667/', better than strtonum(3).
    	cfg.port = atoi (&argv[0][matches[4].rm_so]);

    cfg.channelsToJoin[0] = &argv[0][matches[5].rm_so];
    cfg.numChannels = 1;

    // If other IRC channels were supplied, capture those as well.
    char *sep = cfg.channelsToJoin[0];
    while ( (sep = strchr (sep, ',')) )
    {
	if ( cfg.numChannels >= sizeof cfg.channelsToJoin / sizeof cfg.channelsToJoin[0] ) break;
	*sep = '\0';
	cfg.channelsToJoin[cfg.numChannels++] = ++sep;
    }

    cfg.botNick = argv[1];
    cfg.pack = strtoul (argv[3], NULL, 10);
    if ( errno )
	errx (EXIT_FAILURE, "invalid pack number: %s", argv[3]);

    irc_callbacks_t callbacks = {0};
    callbacks.event_connect = event_connect;
    callbacks.event_join = event_join;
    callbacks.event_dcc_send_req = event_dcc_send_req;

    irc_session_t *session = irc_create_session (&callbacks);
    if ( !session ) errx (EXIT_FAILURE, "failed to create IRC session object");

    irc_set_ctx (session, &cfg);

    char nick[20];
    snprintf (nick, sizeof nick, "xget[%d]", getpid());

    if ( irc_connect (session, cfg.host, cfg.port, 0, nick, 0, 0) )
    {
        irc_destroy_session (session);
        errx (EXIT_FAILURE, "failed to establish TCP connection to %s:%u: %s", cfg.host, cfg.port, irc_strerror(irc_errno(session)));
    }

    int errnum;
    pthread_t display_thread;
    if ( (errnum = pthread_create (&display_thread, NULL, thread_progress, &cfg)) )
    {
	irc_destroy_session (session);
	errc (EXIT_FAILURE, errnum, "pthread_create: ");
    }

    if ( irc_run (session) )
    {
        if ( irc_errno (session) != LIBIRC_ERR_TERMINATED && irc_errno (session) != LIBIRC_ERR_CLOSED )
	{
            irc_destroy_session (session);
            errx (EXIT_FAILURE, "failed to start IRC session: %s", irc_strerror(irc_errno(session)));
        }
    }

    irc_destroy_session (session);

    if ( (errnum = pthread_join (display_thread, NULL)) )
    {
	errc (EXIT_FAILURE, errnum, "pthread_join: ");
    }
}
