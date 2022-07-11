/* 
 * Copyright (C) 2004-2012 George Yunaev gyunaev@ulduzsoft.com
 *
 * This library is free software; you can redistribute it and/or modify it 
 * under the terms of the GNU Lesser General Public License as published by 
 * the Free Software Foundation; either version 3 of the License, or (at your 
 * option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT 
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public 
 * License for more details.
 */
#include <inttypes.h>
#include <arpa/inet.h>
#include <stdbool.h>

static irc_dcc_session_t * libirc_find_dcc_session (irc_session_t * session, irc_dcc_t dccid, int lock_list)
{
	irc_dcc_session_t * s, *found = 0;

	if ( lock_list )
		libirc_mutex_lock (&session->mutex_dcc);

	for ( s = session->dcc_sessions; s; s = s->next )
	{
		if ( s->id == dccid )
		{
			found = s;
			break;
		}
	}

	if ( found == 0 && lock_list )
		libirc_mutex_unlock (&session->mutex_dcc);

	return found;
}


static void libirc_dcc_destroy_nolock (irc_session_t * session, irc_dcc_t dccid)
{
	irc_dcc_session_t * dcc = libirc_find_dcc_session (session, dccid, 0);

	if ( dcc )
	{
		if ( dcc->sock >= 0 )
			socket_close (&dcc->sock);

		dcc->state = LIBIRC_STATE_REMOVED;
	}
}


static void libirc_remove_dcc_session (irc_session_t * session, irc_dcc_session_t * dcc, int lock_list)
{
	if ( dcc->sock >= 0 )
		socket_close (&dcc->sock);

	libirc_mutex_destroy (&dcc->mutex_outbuf);

	if ( lock_list )
		libirc_mutex_lock (&session->mutex_dcc);

	if ( session->dcc_sessions != dcc )
	{
		irc_dcc_session_t * s;
		for ( s = session->dcc_sessions; s; s = s->next )
		{
			if ( s->next == dcc )
			{
				s->next = dcc->next;
				break;
			}
		}
	}
	else
		session->dcc_sessions = dcc->next;

	if ( lock_list )
		libirc_mutex_unlock (&session->mutex_dcc);

	free (dcc);
}


static void libirc_dcc_add_descriptors (irc_session_t * ircsession, fd_set *in_set, fd_set *out_set, int * maxfd)
{
	irc_dcc_session_t * dcc, *dcc_next;
	time_t now = time (0);

	libirc_mutex_lock (&ircsession->mutex_dcc);

	// Preprocessing DCC list:
	// - ask DCC send callbacks for data;
	// - remove unused DCC structures
	for ( dcc = ircsession->dcc_sessions; dcc; dcc = dcc_next )
	{
		dcc_next = dcc->next;

		// Remove timed-out sessions
		if ( (dcc->state == LIBIRC_STATE_CONNECTING
			|| dcc->state == LIBIRC_STATE_INIT
			|| dcc->state == LIBIRC_STATE_LISTENING)
		&& now - dcc->timeout > ircsession->dcc_timeout )
		{
			// Inform the caller about DCC timeout.
			// Do not inform when state is LIBIRC_STATE_INIT - session
			// was initiated from someone else, and callbacks aren't set yet.
			if ( dcc->state != LIBIRC_STATE_INIT )
			{
				libirc_mutex_unlock (&ircsession->mutex_dcc);

				if ( dcc->cb_datum )
					(*dcc->cb_datum)(ircsession, dcc->id, LIBIRC_ERR_TIMEOUT, dcc->ctx);

				libirc_mutex_lock (&ircsession->mutex_dcc);
			}

			libirc_remove_dcc_session (ircsession, dcc, 0);
		}

		// Clean up unused sessions
		if ( dcc->state == LIBIRC_STATE_REMOVED )
			libirc_remove_dcc_session (ircsession, dcc, 0);
	}

	for ( dcc = ircsession->dcc_sessions; dcc; dcc = dcc->next )
	{
		switch (dcc->state)
		{
		case LIBIRC_STATE_LISTENING:
			// While listening, only in_set descriptor should be set
			libirc_add_to_set (dcc->sock, in_set, maxfd);
			break;

		case LIBIRC_STATE_CONNECTING:
			// While connection, only out_set descriptor should be set
			libirc_add_to_set (dcc->sock, out_set, maxfd);
			break;

		case LIBIRC_STATE_CONNECTED:
			// Add input descriptor if there is space in input buffer
			// and it is DCC chat (during DCC send, there is nothing to recv)
			if ( dcc->incoming_offset < sizeof(dcc->incoming_buf) - 1 )
				libirc_add_to_set (dcc->sock, in_set, maxfd);

			// Add output descriptor if there is something in output buffer
			libirc_mutex_lock (&dcc->mutex_outbuf);

			if ( dcc->outgoing_offset > 0  )
				libirc_add_to_set (dcc->sock, out_set, maxfd);

			libirc_mutex_unlock (&dcc->mutex_outbuf);
			break;

		case LIBIRC_STATE_CONFIRM_SIZE:
			/*
			 * If we're receiving file, then WE should confirm the transferred
			 * part (so we have to sent data). But if we're sending the file, 
			 * then RECEIVER should confirm the packet, so we have to receive
			 * data.
			 *
			 * We don't need to LOCK_DCC_OUTBUF - during file transfer, buffers
			 * can't change asynchronously.
			 */
			if ( dcc->outgoing_offset > 0 )
				libirc_add_to_set (dcc->sock, out_set, maxfd);
		}
	}

	libirc_mutex_unlock (&ircsession->mutex_dcc);
}


static void libirc_dcc_process_descriptors (irc_session_t * ircsession, fd_set *in_set, fd_set *out_set)
{
	irc_dcc_session_t * dcc;

	/*
	 * We need to use such a complex scheme here, because on every callback
	 * a number of DCC sessions could be destroyed.
	 */
	libirc_mutex_lock (&ircsession->mutex_dcc);

	for ( dcc = ircsession->dcc_sessions; dcc; dcc = dcc->next )
	{
		if ( dcc->state == LIBIRC_STATE_LISTENING
		&& FD_ISSET (dcc->sock, in_set) )
		{
			socklen_t len = sizeof(dcc->remote_addr);

			int nsock, err = 0;

			// New connection is available; accept it.
			if ( socket_accept (&dcc->sock, &nsock, (struct sockaddr *) &dcc->remote_addr, &len) )
				err = LIBIRC_ERR_ACCEPT;

			// On success, change the active socket and change the state
			if ( err == 0 )
			{
				// close the listen socket, and replace it by a newly 
				// accepted
				socket_close (&dcc->sock);
				dcc->sock = nsock;
				dcc->state = LIBIRC_STATE_CONNECTED;
			}

			if ( err )
				libirc_dcc_destroy_nolock (ircsession, dcc->id);
		}

		if ( dcc->state == LIBIRC_STATE_CONNECTING
		&& FD_ISSET (dcc->sock, out_set) )
		{
			// Now we have to determine whether the socket is connected 
			// or the connect is failed
			struct sockaddr_in saddr;
			socklen_t slen = sizeof(saddr);
			int err = 0;

			if ( getpeername (dcc->sock, (struct sockaddr*)&saddr, &slen) < 0 )
				err = LIBIRC_ERR_CONNECT;

			// On success, change the state
			if ( err == 0 )
				dcc->state = LIBIRC_STATE_CONNECTED;

			if ( err )
				libirc_dcc_destroy_nolock (ircsession, dcc->id);
		}

		if ( dcc->state == LIBIRC_STATE_CONNECTED
		|| dcc->state == LIBIRC_STATE_CONFIRM_SIZE )
		{
			if ( FD_ISSET (dcc->sock, in_set) )
			{
				libirc_mutex_unlock (&ircsession->mutex_dcc);

				(*dcc->cb_datum)(ircsession, dcc->id, LIBIRC_ERR_OK, dcc->ctx);

				/*
				 * If the session is not terminated in callback and file-offset
				 * acknowledgements are not disabled, send the file offsets in
				 * network-byte order (big endian).
				 */
				if ( dcc->state != LIBIRC_STATE_REMOVED )
				{
					dcc->state = LIBIRC_STATE_CONFIRM_SIZE;

					if ( dcc->acknowledge )
					{
						dcc->outgoing_file_confirm_offset = htonl(dcc->file_confirm_offset);
						dcc->outgoing_offset = sizeof(dcc->outgoing_file_confirm_offset);
					}
				}

				libirc_mutex_lock (&ircsession->mutex_dcc);
			}

			/*
			 * Session might be closed (with sock = -1) after the in_set 
			 * processing, so before out_set processing we should check
			 * for this case
			 */
			if ( dcc->state == LIBIRC_STATE_REMOVED )
				continue;

			/*
			 * If we just sent the confirmation data, change state
			 * back.
			 */
			if ( dcc->state == LIBIRC_STATE_CONFIRM_SIZE )
			{
				/*
				 * If the file is already received, we should inform
				 * the caller, and close the session.
				 */
				if ( dcc->received_file_size == dcc->file_confirm_offset )
				{
					libirc_mutex_unlock (&ircsession->mutex_dcc);
					libirc_mutex_unlock (&dcc->mutex_outbuf);
					(*dcc->cb_close)(ircsession, dcc->id, LIBIRC_ERR_OK, dcc->ctx);
					libirc_dcc_destroy_nolock (ircsession, dcc->id);
				}
				else
				{
					/* Continue to receive the file */
					dcc->state = LIBIRC_STATE_CONNECTED;
				}
			}

			/*
			 * Write bit set - we can send() something, and it won't block.
			 */
			if ( FD_ISSET (dcc->sock, out_set) )
			{
				int offset, err = 0;

				/*
				 * Because in some cases outgoing_buf could be changed 
				 * asynchronously (by another thread), we should lock 
				 * it.
				 */
				libirc_mutex_lock (&dcc->mutex_outbuf);

				offset = dcc->outgoing_offset;
		
				if ( offset > 0 )
				{
					int length = socket_send (&dcc->sock, dcc->outgoing_buf, offset);

					if ( length < 0 )
						err = LIBIRC_ERR_WRITE;
					else if ( length == 0 )
						err = LIBIRC_ERR_CLOSED;
					else
					{
						if ( dcc->outgoing_offset - length != 0 )
							memmove (dcc->outgoing_buf, dcc->outgoing_buf + length, dcc->outgoing_offset - length);

						dcc->outgoing_offset -= length;
					}
				}

				libirc_mutex_unlock (&dcc->mutex_outbuf);

				/*
				 * If error arises somewhere above, we inform the caller 
				 * of failure, and destroy this session.
				 */
				if ( err )
				{
					libirc_mutex_unlock (&ircsession->mutex_dcc);
					(*dcc->cb_datum)(ircsession, dcc->id, err, dcc->ctx);
					libirc_mutex_lock (&ircsession->mutex_dcc);
					libirc_dcc_destroy_nolock (ircsession, dcc->id);
				}
			}
		}
	}

	libirc_mutex_unlock (&ircsession->mutex_dcc);
}


static int libirc_new_dcc_session (irc_session_t * session, unsigned long ip, unsigned short port, void * ctx, irc_dcc_session_t ** pdcc)
{
	irc_dcc_session_t * dcc = malloc (sizeof(irc_dcc_session_t));

	if ( !dcc )
		return LIBIRC_ERR_NOMEM;

	// setup
	memset (dcc, 0, sizeof(irc_dcc_session_t));

	if ( libirc_mutex_init (&dcc->mutex_outbuf) )
		goto cleanup_exit_error;

	if ( socket_create (PF_INET, SOCK_STREAM, &dcc->sock) )
		goto cleanup_exit_error;

	if ( !ip )
	{
		unsigned long arg = 1;

		setsockopt (dcc->sock, SOL_SOCKET, SO_REUSEADDR, (char*)&arg, sizeof(arg));

#if defined (ENABLE_IPV6)
		if ( session->flags & SESSIONFL_USES_IPV6 )
		{
			struct sockaddr_in6 saddr6;

			memset (&saddr6, 0, sizeof(saddr6));
			saddr6.sin6_family = AF_INET6;
			memcpy (&saddr6.sin6_addr, &session->local_addr6, sizeof(session->local_addr6));
			saddr6.sin6_port = htons (0);

			if ( bind (dcc->sock, (struct sockaddr *) &saddr6, sizeof(saddr6)) < 0 )
				goto cleanup_exit_error;
		}
		else
#endif
		{
			struct sockaddr_in saddr;
			memset (&saddr, 0, sizeof(saddr));
			saddr.sin_family = AF_INET;
			memcpy (&saddr.sin_addr, &session->local_addr, sizeof(session->local_addr));
			saddr.sin_port = htons (0);

			if ( bind (dcc->sock, (struct sockaddr *) &saddr, sizeof(saddr)) < 0 )
				goto cleanup_exit_error;
		}

		if ( listen (dcc->sock, 5) < 0 )
			goto cleanup_exit_error;

		dcc->state = LIBIRC_STATE_LISTENING;
	}
	else
	{
		// make socket non-blocking, so connect() call won't block
		if ( socket_make_nonblocking (&dcc->sock) )
			goto cleanup_exit_error;

		memset (&dcc->remote_addr, 0, sizeof(dcc->remote_addr));
		dcc->remote_addr.sin_family = AF_INET;
		dcc->remote_addr.sin_addr.s_addr = htonl(ip); // what idiot came up with idea to send IP address in host-byteorder?
		dcc->remote_addr.sin_port = htons(port);
		dcc->state = LIBIRC_STATE_INIT;
	}

	dcc->ctx = ctx;
	time (&dcc->timeout);

	// and store it
	libirc_mutex_lock (&session->mutex_dcc);

	dcc->id = session->dcc_last_id++;
	dcc->next = session->dcc_sessions;
	session->dcc_sessions = dcc;

	libirc_mutex_unlock (&session->mutex_dcc);

	*pdcc = dcc;
	return 0;

cleanup_exit_error:
	if ( dcc->sock >= 0 )
		socket_close (&dcc->sock);

	free (dcc);
	return LIBIRC_ERR_SOCKET;
}


int irc_dcc_destroy (irc_session_t * session, irc_dcc_t dccid)
{
	// This function doesn't actually destroy the session; it just changes
	// its state to "removed" and closes the socket. The memory is actually
	// freed after the processing loop.
	irc_dcc_session_t * dcc = libirc_find_dcc_session (session, dccid, 1);

	if ( !dcc )
		return 1;

	if ( dcc->sock >= 0 )
		socket_close (&dcc->sock);

	dcc->state = LIBIRC_STATE_REMOVED;

	libirc_mutex_unlock (&session->mutex_dcc);
	return 0;
}


static void libirc_dcc_request (irc_session_t * session, const char * nick, const char * req)
{
	char filenamebuf[256];
	uint64_t size;
	uint32_t ip;
	uint16_t port;

	/*
	 * If the filename contains space characters, it will be delimited by double-quotes,
	 * which won't be scanned with `%s`.
	 */
	if (sscanf(req, "DCC SEND \"%255[^\"]\" %u %hu %"SCNu64, filenamebuf, &ip, &port, &size) == 4) {
		if ( session->callbacks.event_dcc_send_req )
		{
			irc_dcc_session_t * dcc;

			int err = libirc_new_dcc_session (session, ip, port, 0, &dcc);
			if ( err )
			{
				session->lasterror = err;
				return;
			}

            		(*session->callbacks.event_dcc_send_req) (session, nick, inet_ntoa (dcc->remote_addr.sin_addr), filenamebuf, size, dcc->id);
			dcc->received_file_size = size;
		}

		return;
	}
	else if ( sscanf (req, "DCC SEND %255s %u %hu %"SCNu64, filenamebuf, &ip, &port, &size) == 4 )
	{
		if ( session->callbacks.event_dcc_send_req )
		{
			irc_dcc_session_t * dcc;

			int err = libirc_new_dcc_session (session, ip, port, 0, &dcc);
			if ( err )
			{
				session->lasterror = err;
				return;
			}

			(*session->callbacks.event_dcc_send_req) (session, nick, inet_ntoa (dcc->remote_addr.sin_addr), filenamebuf, size, dcc->id);
			dcc->received_file_size = size;
		}

		return;
	}
#if defined (ENABLE_DEBUG)
	fprintf (stderr, "BUG: Unhandled DCC message: %s\n", req);
	abort();
#endif
}


int irc_dcc_accept (irc_session_t * session, irc_dcc_t dccid, void * ctx, irc_dcc_callback_t cb_datum, irc_dcc_callback_t cb_close, bool acknowledge)
{
	irc_dcc_session_t * dcc = libirc_find_dcc_session (session, dccid, 1);

	if ( !dcc )
		return 1;

	if ( dcc->state != LIBIRC_STATE_INIT )
	{
		session->lasterror = LIBIRC_ERR_STATE;
		libirc_mutex_unlock (&session->mutex_dcc);
		return 1;
	}

	dcc->cb_datum = cb_datum;
	dcc->cb_close = cb_close;
	dcc->ctx = ctx;
	dcc->acknowledge = acknowledge;

	// Initiate the connect
	if ( socket_connect (&dcc->sock, (struct sockaddr *) &dcc->remote_addr, sizeof(dcc->remote_addr)) )
	{
		libirc_dcc_destroy_nolock (session, dccid);
		libirc_mutex_unlock (&session->mutex_dcc);
		session->lasterror = LIBIRC_ERR_CONNECT;
		return 1;
	}

	dcc->state = LIBIRC_STATE_CONNECTING;
	libirc_mutex_unlock (&session->mutex_dcc);
	return 0;
}


int irc_dcc_decline (irc_session_t * session, irc_dcc_t dccid)
{
	irc_dcc_session_t * dcc = libirc_find_dcc_session (session, dccid, 1);

	if ( !dcc )
		return 1;

	if ( dcc->state != LIBIRC_STATE_INIT )
	{
		session->lasterror = LIBIRC_ERR_STATE;
		libirc_mutex_unlock (&session->mutex_dcc);
		return 1;
	}

	libirc_dcc_destroy_nolock (session, dccid);
	libirc_mutex_unlock (&session->mutex_dcc);
	return 0;
}

int irc_dcc_read (irc_session_t * session, irc_dcc_t dccid, char * buffer, size_t capacity)
{
	irc_dcc_session_t * dcc = libirc_find_dcc_session (session, dccid, 1);

	if ( !dcc )
		return -LIBIRC_ERR_INVAL;

	int length = socket_recv (&dcc->sock, buffer, capacity);

	if ( length < 0 )
	{
		return -LIBIRC_ERR_READ;
	}
	else
	{
	    dcc->file_confirm_offset += length;
	    return length;
	}
}