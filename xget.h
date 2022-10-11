#ifndef XGET_H
#define XGET_H

#include <stdbool.h>
#include <pthread.h>

#include "libircclient/include/libircclient.h"

struct xdccGetConfig
{
	// The hostname of the IRC network to connect to.
	char *host;

	// The port number of the IRC network to connect to.
	uint16_t port;

	// The nick of the DCC sender.
	char *botNick;

	// The IRC channels to join.
	char *channelsToJoin[5];

	// The total number of IRC channels to join.
	uint32_t numChannels;

	// The name of the DCC file.
	char *filename;

	// The size of the DCC file to be sent.
	irc_dcc_size_t filesize;

	// The current size of the DCC file (as it is being sent).
	irc_dcc_size_t currsize;

	// The requested pack number.
	uint32_t pack;

	// True if the URI begins with 'ircs://'.
	bool is_ircs;

	bool has_opt_no_acknowledge;
	bool has_opt_output_document;

	// Synchronization primitives to safely share this struct between threads.
	pthread_mutex_t mutex;
	pthread_cond_t cv;
};

#endif //XGET_H
