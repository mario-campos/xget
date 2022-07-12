#ifndef XDCCGET_H
#define XDCCGET_H

#include <limits.h>
#include <stdbool.h>

#include "libircclient/include/libircclient.h"

#define IRC_NICK_MAX_SIZE 30

struct xdccGetConfig {
	// The hostname of the IRC network to connect to.
	char *host;

	// The port number of the IRC network to connect to.
	uint16_t port;

	// The nick of this xdccget's IRC client.
	char nick[IRC_NICK_MAX_SIZE];

	// The nick of the DCC sender.
	char *botNick;

	// The IRC channels to join.
	char *channelsToJoin[5];

	// The total number of IRC channels to join.
	uint32_t numChannels;

	// The name of the DCC file.
	char filename[NAME_MAX];

	// The size of the DCC file to be sent.
	irc_dcc_size_t filesize;

	// The current size of the DCC file (as it is being sent).
	irc_dcc_size_t currsize;

	// The requested pack number.
	uint32_t pack;

	// True if the URI begins with 'ircs://'.
	bool is_ircs;

	// True if -A/--no-acknowledge (i.e. whether to send DCC acknowledgements).
	bool no_ack;
};

#endif //XDCCGET_H
