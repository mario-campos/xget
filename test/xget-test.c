#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <err.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/fcntl.h>
#include <sys/wait.h>

#define IRC_MSG_MAX_SIZE 512

// TODO: delete this global variable
pid_t xget_pid;

typedef struct irc_server irc_server_t;
typedef struct irc_session irc_session_t;
typedef void (*irc_callback_t)(irc_session_t *, const char *);

struct irc_server {
    int socket_fd;
    struct addrinfo *ai;
    irc_callback_t on_accept;
    irc_callback_t on_cmd_nick;
    irc_callback_t on_cmd_user;
    irc_callback_t on_cmd_join;
    irc_callback_t on_cmd_quit;
    irc_callback_t on_cmd_privmsg;
};

struct irc_session {
    int socket_fd;
    irc_server_t *server;
    char nick[32];
    char user[32];
    union {
	struct sockaddr_storage sas;
	struct sockaddr_in sai;
    };
};

void irc_free(irc_server_t *server)
{
    freeaddrinfo(server->ai);
    close(server->socket_fd);
}

void irc_serve(irc_server_t *server)
{
    struct addrinfo hints = {
	.ai_family = AF_INET,
	.ai_socktype = SOCK_STREAM,
    };

    int errnum;
    if ((errnum = getaddrinfo("127.0.0.1", "6667", &hints, &server->ai)))
	errx(EXIT_FAILURE, "getaddrinfo: %s", gai_strerror(errnum));

    if ((server->socket_fd = socket(server->ai->ai_family, server->ai->ai_socktype, server->ai->ai_protocol)) == -1)
	err(EXIT_FAILURE, "socket");

    // bind(2) may fail with "Address already in use." In that case, try to reuse the address if possible.
    int yes = 1;
    setsockopt(server->socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    if (bind(server->socket_fd, server->ai->ai_addr, server->ai->ai_addrlen))
	err(EXIT_FAILURE, "bind");

    if (listen(server->socket_fd, 100))
	err(EXIT_FAILURE, "listen");
}

void irc_run(irc_server_t *server)
{
    irc_session_t session = {.server = server};

    socklen_t conn_sa_size = sizeof session.sas;
    if ((session.socket_fd = accept(server->socket_fd, (struct sockaddr *) &session.sas, &conn_sa_size)) == -1)
    {
	kill(xget_pid, SIGINT);
	wait(NULL);
	err(EXIT_FAILURE, "accept");
    }

    server->on_accept(&session, NULL);

    int counter;
    char nick[IRC_MSG_MAX_SIZE], user[IRC_MSG_MAX_SIZE], channel[IRC_MSG_MAX_SIZE], peer[IRC_MSG_MAX_SIZE];
    char buf[IRC_MSG_MAX_SIZE], param1[IRC_MSG_MAX_SIZE], param2[IRC_MSG_MAX_SIZE], param3[IRC_MSG_MAX_SIZE], param4[IRC_MSG_MAX_SIZE];
    char *p = buf;

    recv(session.socket_fd, buf, sizeof buf, 0);
    if (!sscanf(p, "NICK %s\r\n%n", nick, &counter))
    {
	kill(xget_pid, SIGINT);
	wait(NULL);
	errx(EXIT_FAILURE, "expected IRC 'NICK' command");
    }

    server->on_cmd_nick(&session, nick);
    p += counter;

    if (!sscanf(p, "USER %s %s %s %s\r\n%n", user, param2, param3, param4, &counter))
    {
	kill(xget_pid, SIGINT);
	wait(NULL);
	errx(EXIT_FAILURE, "expected IRC 'USER' command");
    }

    server->on_cmd_user(&session, user);

    recv(session.socket_fd, buf, sizeof buf, 0);
    sscanf(buf, "JOIN %s\r\n%n", channel, &counter);

    server->on_cmd_join(&session, channel);

    int pack;
    recv(session.socket_fd, buf, sizeof buf, 0);
    sscanf(buf, "PRIVMSG %s :XDCC SEND #%d\r\n", peer, &pack);

    server->on_cmd_privmsg(&session, peer);

    recv(session.socket_fd, buf, sizeof buf, 0);
    sscanf(buf, "QUIT %s\r\n", param1);

    server->on_cmd_quit(&session, param1);
}

void cb_accept(irc_session_t *session, const char *null)
{

}

void cb_cmd_nick(irc_session_t *session, const char *nick)
{
	strlcpy(session->nick, nick, sizeof session->nick);
}

void cb_cmd_user(irc_session_t *session, const char *user)
{
    char buf[IRC_MSG_MAX_SIZE];

    // RPL_WELCOME
    snprintf(buf, sizeof buf, ":127.0.0.1 001 %s :Welcome to libircserver %s!%s@127.0.0.1\r\n", session->nick, session->nick, user);
    send(session->socket_fd, buf, strlen(buf), 0);

    // RPL_YOURHOST
    snprintf(buf, sizeof buf, ":127.0.0.1 002 %s :Your host is %s, running version %s\r\n", session->nick, "libircserver", "0.0.1");
    send(session->socket_fd, buf, strlen(buf), 0);

    // RPL_CREATED
    snprintf(buf, sizeof buf, "127.0.0.1 003 %s :This server was created %s\r\n", session->nick, "16:43:38 Apr 23 2019");
    send(session->socket_fd, buf, strlen(buf), 0);

    // RPL_MYINFO
    snprintf(buf, sizeof buf, "127.0.0.1 004 %s %s %s BGHIRSWcdhiorswx ABCDFGIJKLMNOPQRSTXYabcdefghijklmnopqrstuvwz FIJLXYabdefghjkloqvw\r\n", session->nick, "127.0.0.1", "0.0.1");
    send(session->socket_fd, buf, strlen(buf), 0);

    // RPL_ISUPPORT
    snprintf(buf, sizeof buf, "127.0.0.1 005 %s MAXCHANNELS=30 :are supported by this server\r\n", session->nick);
    send(session->socket_fd, buf, strlen(buf), 0);

    strlcpy(session->user, user, sizeof session->user);
}

void cb_cmd_join(irc_session_t *session, const char *channel)
{
    char buf[IRC_MSG_MAX_SIZE];

    // JOIN
    snprintf(buf, sizeof buf, ":%s!%s@127.0.0.1 JOIN %s\r\n", session->nick, session->user, channel);
    send(session->socket_fd, buf, strlen(buf), 0);

    // RPL_TOPIC
    snprintf(buf, sizeof buf, ":127.0.0.1 332 %s %s :this is the topic\r\n", session->nick, channel);
    send(session->socket_fd, buf, strlen(buf), 0);

    // RPL_NAMREPLY
    snprintf(buf, sizeof buf, ":127.0.0.1 353 %s = %s :%s\r\n", session->nick, channel, session->nick);
    send(session->socket_fd, buf, strlen(buf), 0);

    // RPL_ENDOFNAMES
    snprintf(buf, sizeof buf, ":127.0.0.1 366 %s %s :End of /NAMES list\r\n", session->nick, channel);
    send(session->socket_fd, buf, strlen(buf), 0);
}

void cb_cmd_quit(irc_session_t *session, const char *message)
{
    char buf[IRC_MSG_MAX_SIZE];
    snprintf(buf, sizeof buf, ":127.0.0.1 ERROR :Quit: ");
    send(session->socket_fd, buf, strlen(buf), 0);
    close(session->socket_fd);
}

void cb_cmd_privmsg(irc_session_t *session, const char *peer)
{
    char buf[IRC_MSG_MAX_SIZE];
    struct addrinfo *res2, hints = {
	.ai_family = AF_INET,
	.ai_socktype = SOCK_STREAM,
    };

    // Set up DCC listening socket
    snprintf(buf, sizeof buf, ":%s PRIVMSG %s :\001DCC SEND %s %u %u %u\001\r\n", peer, session->nick, "file.txt", htonl(session->sai.sin_addr.s_addr), 6668, 1024);
    send(session->socket_fd, buf, strlen(buf), 0);

    int errnum;
    if ((errnum = getaddrinfo("127.0.0.1", "6668", &hints, &res2)))
	errx(EXIT_FAILURE, "getaddrinfo: %s", gai_strerror(errnum));

    int dcc_sockfd;
    if ((dcc_sockfd = socket(res2->ai_family, res2->ai_socktype, res2->ai_protocol)) == -1)
	err(EXIT_FAILURE, "socket");

    int yes = 1;
    setsockopt(dcc_sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    if (bind(dcc_sockfd, res2->ai_addr, res2->ai_addrlen))
	err(EXIT_FAILURE, "bind");

    if (listen(dcc_sockfd, 100))
	err(EXIT_FAILURE, "listen");

    int xget_dcc_sockfd;
    struct sockaddr_storage conn_sa;
    socklen_t conn_sa_size = sizeof conn_sa;
    if ((xget_dcc_sockfd = accept(dcc_sockfd, (struct sockaddr *) &conn_sa, &conn_sa_size)) == -1)
    {
	kill(xget_pid, SIGINT);
	wait(NULL);
	err(EXIT_FAILURE, "accept");
    }

    char file_buffer[1025] = {0};
    memset(file_buffer, 'A', 1024);
    send(xget_dcc_sockfd, file_buffer, strlen(file_buffer), 0);

    unlink("file.txt");
    close(xget_dcc_sockfd);
    close(dcc_sockfd);
    freeaddrinfo(res2);
}

int main(int argc, char *argv[])
{
    irc_server_t server = {
	.on_accept = cb_accept,
	.on_cmd_nick = cb_cmd_nick,
	.on_cmd_user = cb_cmd_user,
	.on_cmd_join = cb_cmd_join,
	.on_cmd_privmsg = cb_cmd_privmsg,
	.on_cmd_quit = cb_cmd_quit,
    };

    irc_serve(&server);

    // xget should be in the same directory as this executable, so modify argv[0] to form a path to xget.
    char *s = rindex(argv[0], 'x');
    strcpy(s, "xget");

    // TODO: randomize arguments (within spec) to test xget's input handling/parsing.
    char *xget_argv[] = {
	    argv[0],
	    "-A",
	    "irc://localhost/#ch",
	    "bot",
	    "send",
	    "42",
	    NULL,
    };

    if ((xget_pid = fork()) == -1)
	err(EXIT_FAILURE, "fork");
    if (0 == xget_pid) {
	int fd = open("/dev/null", O_RDONLY);
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	execvp(argv[0], xget_argv);
    }

    irc_run(&server);
    irc_free(&server);

    int xget_exit;
    wait(&xget_exit);
    if (xget_exit == 0) puts("PASS");
    exit(xget_exit);
}