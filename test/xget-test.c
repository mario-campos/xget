#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netdb.h>
#include <err.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/fcntl.h>

#define IRC_MSG_MAX_SIZE 512

typedef struct {
    int socket_fd;
    struct addrinfo *ai;
} irc_session_t;

void irc_free(irc_session_t *session)
{
    freeaddrinfo(session->ai);
    close(session->socket_fd);
}

void irc_serve(irc_session_t *session)
{
    struct addrinfo hints = {
	.ai_family = AF_INET,
	.ai_socktype = SOCK_STREAM,
    };

    int errnum;
    if ((errnum = getaddrinfo("127.0.0.1", "6667", &hints, &session->ai)))
	errx(EXIT_FAILURE, "getaddrinfo: %s", gai_strerror(errnum));

    if ((session->socket_fd = socket(session->ai->ai_family, session->ai->ai_socktype, session->ai->ai_protocol)) == -1)
	err(EXIT_FAILURE, "socket");

    // bind(2) may fail with "Address already in use." In that case, try to reuse the address if possible.
    int yes = 1;
    setsockopt(session->socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    if (bind(session->socket_fd, session->ai->ai_addr, session->ai->ai_addrlen))
	err(EXIT_FAILURE, "bind");

    if (listen(session->socket_fd, 100))
	err(EXIT_FAILURE, "listen");
}

int main(int argc, char *argv[])
{
    int errnum;
    irc_session_t session;

    struct addrinfo *res2, hints = {
	    .ai_family = AF_INET,
	    .ai_socktype = SOCK_STREAM,
    };
    irc_serve(&session);

    struct sockaddr_in *saddr = (struct sockaddr_in *) session.ai->ai_addr;

    // Set up DCC listening socket

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

    pid_t xget_pid;
    if ((xget_pid = fork()) == -1)
	err(EXIT_FAILURE, "fork");
    if (0 == xget_pid) {
	int fd = open("/dev/null", O_RDONLY);
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	dup2(fd, STDERR_FILENO);
	execvp(argv[0], xget_argv);
    }

    int xget_sockfd;
    struct sockaddr_storage conn_sa;
    socklen_t conn_sa_size = sizeof conn_sa;
    if ((xget_sockfd = accept(session.socket_fd, (struct sockaddr *) &conn_sa, &conn_sa_size)) == -1)
    {
	kill(xget_pid, SIGINT);
	wait(NULL);
	err(EXIT_FAILURE, "accept");
    }

    int counter;
    char nick[IRC_MSG_MAX_SIZE], user[IRC_MSG_MAX_SIZE], channel[IRC_MSG_MAX_SIZE], peer[IRC_MSG_MAX_SIZE];
    char buf[IRC_MSG_MAX_SIZE], param1[IRC_MSG_MAX_SIZE], param2[IRC_MSG_MAX_SIZE], param3[IRC_MSG_MAX_SIZE], param4[IRC_MSG_MAX_SIZE];
    char *p = buf;

    recv(xget_sockfd, buf, sizeof buf, 0);
    if (!sscanf(p, "NICK %s\r\n%n", nick, &counter))
    {
	kill(xget_pid, SIGINT);
	wait(NULL);
	errx(EXIT_FAILURE, "expected IRC 'NICK' command");
    }

    p += counter;

    if (!sscanf(p, "USER %s %s %s %s\r\n%n", user, param2, param3, param4, &counter))
    {
	kill(xget_pid, SIGINT);
	wait(NULL);
	errx(EXIT_FAILURE, "expected IRC 'USER' command");
    }

    // RPL_WELCOME
    snprintf(buf, sizeof buf, ":127.0.0.1 001 %s :Welcome to libircserver %s!%s@127.0.0.1\r\n", nick, nick, user);
    send(xget_sockfd, buf, strlen(buf), 0);

    // RPL_YOURHOST
    snprintf(buf, sizeof buf, ":127.0.0.1 002 %s :Your host is %s, running version %s\r\n", nick, "libircserver", "0.0.1");
    send(xget_sockfd, buf, strlen(buf), 0);

    // RPL_CREATED
    snprintf(buf, sizeof buf, "127.0.0.1 003 %s :This server was created %s\r\n", nick, "16:43:38 Apr 23 2019");
    send(xget_sockfd, buf, strlen(buf), 0);

    // RPL_MYINFO
    snprintf(buf, sizeof buf, "127.0.0.1 004 %s %s %s BGHIRSWcdhiorswx ABCDFGIJKLMNOPQRSTXYabcdefghijklmnopqrstuvwz FIJLXYabdefghjkloqvw\r\n", nick, "127.0.0.1", "0.0.1");
    send(xget_sockfd, buf, strlen(buf), 0);

    // RPL_ISUPPORT
    snprintf(buf, sizeof buf, "127.0.0.1 005 %s MAXCHANNELS=30 :are supported by this server\r\n", nick);
    send(xget_sockfd, buf, strlen(buf), 0);

    recv(xget_sockfd, buf, sizeof buf, 0);
    sscanf(buf, "JOIN %s\r\n%n", channel, &counter);

    // JOIN
    snprintf(buf, sizeof buf, ":%s!%s@127.0.0.1 JOIN %s\r\n", nick, user, channel);
    send(xget_sockfd, buf, strlen(buf), 0);

    // RPL_TOPIC
    snprintf(buf, sizeof buf, ":127.0.0.1 332 %s %s :this is the topic\r\n", nick, channel);
    send(xget_sockfd, buf, strlen(buf), 0);

    // RPL_NAMREPLY
    snprintf(buf, sizeof buf, ":127.0.0.1 353 %s = %s :%s\r\n", nick, channel, nick);
    send(xget_sockfd, buf, strlen(buf), 0);

    // RPL_ENDOFNAMES
    snprintf(buf, sizeof buf, ":127.0.0.1 366 %s %s :End of /NAMES list\r\n", nick, channel);
    send(xget_sockfd, buf, strlen(buf), 0);

    int pack;
    recv(xget_sockfd, buf, sizeof buf, 0);
    sscanf(buf, "PRIVMSG %s :XDCC SEND #%d\r\n", peer, &pack);

    snprintf(buf, sizeof buf, ":%s PRIVMSG %s :\001DCC SEND %s %u %u %u\001\r\n", peer, nick, "file.txt", htonl(saddr->sin_addr.s_addr), 6668, 1024);
    send(xget_sockfd, buf, strlen(buf), 0);

    int xget_dcc_sockfd;
    if ((xget_dcc_sockfd = accept(dcc_sockfd, (struct sockaddr *) &conn_sa, &conn_sa_size)) == -1)
    {
	kill(xget_pid, SIGINT);
	wait(NULL);
	err(EXIT_FAILURE, "accept");
    }

    char file_buffer[1025] = {0};
    memset(file_buffer, 'A', 1024);
    send(xget_dcc_sockfd, file_buffer, strlen(file_buffer), 0);

    recv(xget_sockfd, buf, sizeof buf, 0);
    sscanf(buf, "QUIT %s\r\n", param1);

    snprintf(buf, sizeof buf, ":127.0.0.1 ERROR :Quit: ");
    send(xget_sockfd, buf, strlen(buf), 0);

    unlink("file.txt");

    freeaddrinfo(res2);
    close(xget_dcc_sockfd);
    close(dcc_sockfd);
    close(xget_sockfd);
    irc_free(&session);

    int xget_exit;
    wait(&xget_exit);
    if (xget_exit == 0) puts("PASS");
    exit(xget_exit);
}