/*
 * mish_telnet.c
 *
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "mish_priv.h"

#ifdef DEBUG_TELNET
#define TELCMDS
#define TELOPTS
#define TV(w) w
#else
#define TV(w)
#endif
#include <arpa/telnet.h>

/*
 * Ask remote telnet client to turn off echo, and send us the NAWS
 */
void
mish_telnet_send_init(
		mish_client_p c)
{
	const unsigned char init[] = {
			IAC, DO, TELOPT_ECHO,
			IAC, DO, TELOPT_NAWS,
			IAC, WILL, TELOPT_ECHO,
			IAC, WILL, TELOPT_SGA,
			0,
	};
	_mish_send_queue(c, (char*)init);
}

/*
 * This looks for the TELNET IAC prefix, and handles /some/ of them.
 * The one we're really interested in anyway is the NAWS aka Window Size
 * sequence.
 * All the other ones are ignored, but parsed. Ish.
 */
int
_mish_telnet_parse(
		mish_client_p c,
		uint8_t ch)
{
	TV(printf("%02x(%d) %s (seq %06x)\n",
			ch, ch, ch >= xEOF ? telcmds[ch-xEOF] : "",
					c->vts.seq);)
	/*
	 * To handle telnet sequences, we pause the VT decoder
	 */
	switch (c->vts.seq) {
		case MISH_VT_RAW:
			if (ch == IAC) {
				c->vts.seq = MISH_VT_TELNET;
				return 1;
			}
			break;
		case MISH_VT_TELNET: {
			switch (ch) {
				case WILL:
				case WONT:
				case DO:
				case DONT:
				case SB:
					c->vts.seq = c->vts.seq << 8 | ch;
					return 1;
				default:
				case SE:
					c->vts.seq = MISH_VT_RAW;
					return 1;
			}
		}	break;
		case MISH_VT_SEQ(TELNET, IAC):
			TV(printf("IAC %d %s\n\n", ch, telopts[ch]);)
			c->vts.seq = MISH_VT_RAW;
			break;	// let this one thru!!!
		case MISH_VT_SEQ(TELNET, WILL):
			TV(printf("WILL %d %s\n\n", ch, telopts[ch]);)
			c->vts.seq = MISH_VT_RAW;
			return 1;
		case MISH_VT_SEQ(TELNET, WONT):
			TV(printf("WONT %d %s\n", ch, telopts[ch]);)
			c->vts.seq = MISH_VT_RAW;
			return 1;
		case MISH_VT_SEQ(TELNET, DO):
			TV(printf("DO %d %s\n", ch, telopts[ch]);)
			c->vts.seq = MISH_VT_RAW;
			return 1;
		case MISH_VT_SEQ(TELNET, DONT):
			TV(printf("DONT %d %s\n", ch, telopts[ch]);)
			c->vts.seq = MISH_VT_RAW;
			return 1;
		case MISH_VT_SEQ(TELNET, SB):
			TV(printf("SB %d %s\n", ch, telopts[ch]);)
			c->vts.seq_want = 0;
			c->vts.p[0] = 0;
			switch (ch) {
				case TELOPT_NAWS:
					c->vts.seq = c->vts.seq << 8 | ch;
					return 1;
				default:
					if (ch == IAC) {
						c->vts.seq = MISH_VT_TELNET;
						return 1;
					}
			}
			return 1;
		case (MISH_VT_SEQ(TELNET, SB) << 8 | (TELOPT_NAWS)): {
			switch (c->vts.seq_want++) {
				case 0:
					c->vts.p[0] = 0;
					c->vts.p[1] = ch;
					return 1;
				case 1:	c->vts.p[1] = (c->vts.p[1] << 8) | ch; return 1;
				case 2:	c->vts.p[0] = ch; return 1;
				case 3:	c->vts.p[0] = (c->vts.p[0] << 8) | ch; /* fallthru */
			}
			if (c->vts.seq_want == 4) {
				TV(printf("GOT NAWS: %dx%d\n", c->vts.p[0], c->vts.p[1]);)
				c->window_size.h = c->vts.p[0];
				c->window_size.w = c->vts.p[1];
				c->flags |= MISH_CLIENT_HAS_WINDOW_SIZE |
								MISH_CLIENT_UPDATE_WINDOW;
				c->vts.seq = MISH_VT_RAW;
				c->vts.done = 1;
				return 1;
			}
		}	return 1;
	}
	return 0;
}

extern const char *__progname;

/*
 * not going to go thru all the bits regarding sockets here, you know it.
 *
 * We allocate a random port, and listen to it, then add it to the
 * main select() thread.
 */
int
mish_telnet_prepare(
		mish_p m,
		uint16_t port)
{
	m->telnet.listen = socket(AF_INET, SOCK_STREAM, 0);
	int flag = 1;

	signal(SIGPIPE, SIG_IGN);
	if (setsockopt(m->telnet.listen, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)))
		perror("SO_REUSEADDR");

	struct sockaddr_in b = {
		.sin_family = AF_INET,
		.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
	};
	// attempts to generate a pseudo random port based on the executable name
	// before having to rely on random(). This gives a chance of 'unique' ports
	// per programs.
	if (port == 0) {
		for (int i = 0; __progname[i]; i++)
			port += __progname[i] + i;
		if (port < 1024)
			port += 1024;
		port = port & 0x3fff;
	}

	int tries = 10;
	do {
		b.sin_port = htons(port);
		if (bind(m->telnet.listen, (struct sockaddr *)&b, sizeof(b)) == -1) {
			fprintf(stderr, "%s can't bind %d\n", __func__, ntohs(b.sin_port));
			perror("bind");
			port = port + (random() & 0x3ff);
			continue;
		}
		m->telnet.port = ntohs(b.sin_port);
		printf(MISH_COLOR_GREEN
				"mish: telnet port on %d"
				MISH_COLOR_RESET "\n",
				m->telnet.port);
		break;
	} while (tries-- > 0);
	if (tries) {
		if (listen(m->telnet.listen, 2) == -1) {
			perror("mish_telnet_prepare listen");
			goto error;
		}
		FD_SET(m->telnet.listen, &m->select.read);
		return 0;
	}
	fprintf(stderr, "mish: %s failed\n", __func__);
error:
	close(m->telnet.listen);
	m->telnet.listen = -1;
	return -1;
}

/*
 * Accept a new connection from the telnet port, add a new client
 */
int
mish_telnet_in_check(
		mish_p m,
		fd_set * r)
{
	if (!FD_ISSET(m->telnet.listen, r))
		return 0;

	struct sockaddr_in a = {};
	socklen_t al = sizeof(a);
	int tf = accept(m->telnet.listen, (struct sockaddr *) &a, &al);
	if (tf < 0) {
		perror(__func__);
		return -1;
	}
	/*
	 * it is necessary to have TWO file descriptors here, otherwise
	 * the FD_SET/FD_CLR logic could be confusing as using the same bits.
	 * a dup() isn't terribly expensive and guarantees we don't have to
	 * worry about it.
	 */
	mish_client_p c = mish_client_new(m, tf, dup(tf), 1 /* tty */);
	c->input.is_telnet = 1;

	printf(MISH_COLOR_GREEN
			"mish: telnet: connected."
			MISH_COLOR_RESET "\n");

	return 0;
}
