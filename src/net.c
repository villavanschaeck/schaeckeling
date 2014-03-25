#include <sys/select.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sysexits.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#include <assert.h>
#include <stdarg.h>
#include <signal.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <unistd.h>
#include "net.h"

static void accept_client(int);
static int create_listen_socket(int);
static int read_client(struct connection *);
static int flush_writes(struct connection *);
static void drop_client(struct connection *);

struct connection *connhead;
fd_set socksetin, socksetout;
int maxsock = -1;
int listensock = -1;
int controlpipes[2];
// pthread_mutex_t treelock, outbuflock;
pthread_mutex_t callmtx;

#define PERSISTENT_IOVS 30
struct iovec piovs[PERSISTENT_IOVS];

extern int watchdog_net_pong;

#define FD_DUP(src, dst) memcpy(&dst, &src, sizeof(fd_set));

void
init_net() {
	// pthread_mutex_init(&outbuflock, NULL);
	// pthread_mutex_init(&treelock, NULL);
	pthread_mutex_init(&callmtx, NULL);

	FD_ZERO(&socksetin);
	FD_ZERO(&socksetout);

	listensock = create_listen_socket(1337);
	FD_SET(listensock, &socksetin);

	if(pipe(controlpipes) != 0) {
		err(1, "pipe");
	}
	FD_SET(controlpipes[0], &socksetin);

	if(controlpipes[0] > listensock) {
		maxsock = controlpipes[0] + 1;
	} else {
		maxsock = listensock + 1;
	}

#ifndef SO_NOSIGPIPE
	signal(SIGPIPE, SIG_IGN);
#endif
}

void
pre_deinit_net() {
	pthread_mutex_lock(&callmtx);
	FD_CLR(listensock, &socksetin);
	wakeup_select();
	pthread_mutex_unlock(&callmtx);
}

void
deinit_net() {
	pthread_mutex_lock(&callmtx);
	// pthread_mutex_lock(&treelock);
	close(listensock);
	struct timeval tv = { 0, 0 };
	select(maxsock, NULL, &socksetout, NULL, &tv);
	while(connhead != NULL) {
		struct connection *c = connhead;
		if(FD_ISSET(c->fd, &socksetout)) {
			printf("Attempt to flush the last data to %d\n", c->fd);
			flush_writes(c);
		}
		// pthread_mutex_lock(&outbuflock);
		if(c->outbuf != NULL) {
			printf("Truncating output buffers for %d\n", c->fd);
		}
	 	close(c->fd);
		while(c->outbuf != NULL) {
			struct linkedbuf_ptr *obp = c->outbuf;
			if(--obp->lb->refcount == 0) {
				free(obp->lb->iov.iov_base);
				free(obp->lb);
			}
			c->outbuf = obp->next;
			free(obp);
		}
		// pthread_mutex_unlock(&outbuflock);
		connhead = c->next;
		free(c);
	}
	// pthread_mutex_unlock(&treelock);
	pthread_mutex_unlock(&callmtx);
}

void
wakeup_select(void) {
	char poke = 'p';
	write(controlpipes[1], &poke, 1);
}

void *
net_runner(void *dummy) {
	pthread_mutex_lock(&callmtx);
	while(1) {
		int n;
		fd_set readset, writeset;

		FD_DUP(socksetin, readset);
		FD_DUP(socksetout, writeset);

		if(maxsock == -1) {
			maxsock = listensock;
			if(controlpipes[0] > maxsock) {
				maxsock = controlpipes[0];
			}

			// pthread_mutex_nlock(&treelock);
			struct connection *c = connhead;
			while(c != NULL) {
				if(c->fd > maxsock) {
					maxsock = c->fd;
				}
				c = c->next;
			}
			maxsock++;
			// pthread_mutex_unlock(&treelock);
		}

		pthread_mutex_unlock(&callmtx);
		n = select(maxsock, &readset, &writeset, NULL, NULL);
		pthread_mutex_lock(&callmtx);
		watchdog_net_pong = 1;
		if(n == -1) {
			if(errno != EINTR) {
				err(1, "select");
			}
			n = 0;
		}

		if(n > 0 && FD_ISSET(listensock, &readset)) {
			accept_client(listensock);
			n--;
		}

		if(n > 0 && FD_ISSET(controlpipes[0], &readset)) {
			char buf[16];
			read(controlpipes[0], buf, sizeof(buf));
			n--;
		}

		struct connection *c = connhead;
		while(c != NULL && n > 0) {
			int lost = 0;
			if(FD_ISSET(c->fd, &readset)) {
				lost = read_client(c);
				n--;
			}
			if(FD_ISSET(c->fd, &writeset)) {
				if(lost == 0) {
					lost = flush_writes(c);
				}
				n--;
			}
			if(lost != 0) {
				struct connection *deadc = c;
				c = c->next;
				drop_client(deadc);
			} else {
				c = c->next;
			}
		}

		assert(n==0);
	}
	pthread_mutex_unlock(&callmtx);
	return NULL;
}

static void
accept_client(int sock) {
	struct connection *c;
	int client;

	client = accept(sock, NULL, NULL);
	if(client < 0) {
		warn("accept()");
		return;
	}

	int on = 1;
#ifdef SO_NOSIGPIPE
	if(setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on)) == -1) {
		warn("setsockopt(SO_NOSIGPIPE)");
		// Ik wil ze niet.
		signal(SIGPIPE, SIG_IGN);
	}
#endif
	if(setsockopt(client, SOL_SOCKET, SO_KEEPALIVE, &on, sizeof(on)) == -1) {
		warn("setsockopt(SO_KEEPALIVE)");
	}

	int flags = fcntl(client, F_GETFL, 0);
	if(flags == -1) {
		warn("fcntl");
		flags = 0;
	}
	if(fcntl(client, F_SETFL, flags | O_NONBLOCK) == -1) {
		warn("fcntl(O_NONBLOCK)");
		// Gewoon hopen dat het goed gaat
	}

	c = malloc(sizeof(struct connection));
	c->fd = client;
	c->inbuf_pos = 0;
	c->outbuf = NULL;
	c->outbuf_tail = &c->outbuf;

	c->next = connhead;
	connhead = c;

	FD_SET(client, &socksetin);
	if(client >= maxsock) {
		maxsock = client+1;
	}

	printf("Welcome #%d\n", c->fd);
}

static int
create_listen_socket(int port) {
	int on = 1;
	int sock;
	struct sockaddr_in addr;
	sock = socket (PF_INET, SOCK_STREAM, 0);
	if(sock == -1) {
		err(EX_UNAVAILABLE, "socket()");
	}
	addr.sin_family = PF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(port);
	/* Enable address reuse */
	setsockopt (sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
	if(bind(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) != 0) {
		err(EX_UNAVAILABLE, "bind()");
	}
	if(listen(sock, 25) != 0) {
		err(EX_UNAVAILABLE, "listen()");
	}
	return sock;
}

static int
read_client(struct connection *c) {
	assert(c->inbuf_pos >= 0 && c->inbuf_pos <= sizeof(c->inbuf));
	int n = read(c->fd, c->inbuf + c->inbuf_pos, sizeof(c->inbuf) - c->inbuf_pos);
	switch(n) {
		case -1:
			warn("read");
#ifdef STRESS
			if(errno != ECONNRESET) {
				abort();
			}
#endif
			/* FALL THROUGH */
		case 0:
			return 1;
	}

	c->inbuf_pos += n;
	assert(c->inbuf_pos >= 0 && c->inbuf_pos <= sizeof(c->inbuf));

	int offset = 0, processed;
	do {
		processed = handle_data(c, c->inbuf + offset, c->inbuf_pos - offset);
		if(processed == -1) {
			FD_CLR(c->fd, &socksetin);
			return 1;
		}
		offset += processed;
	} while(processed > 0 && c->inbuf_pos > offset);

	c->inbuf_pos -= offset;
	if(offset > 0 && c->inbuf_pos != 0) {
		memmove(c->inbuf, c->inbuf + offset, c->inbuf_pos);
	}

	if(c->inbuf_pos == sizeof(c->inbuf)) {
		FD_CLR(c->fd, &socksetin);
		printf("#%d Buffer vol\n", c->fd);
		return 1;
	}

	return 0;
}

static void
queue_buf(struct connection *c, struct linkedbuf *lb) {
	// pthread_mutex_lock(&outbuflock);
	struct linkedbuf_ptr *obp = malloc(sizeof(struct linkedbuf_ptr));
	obp->lb = lb;
	lb->refcount++;
	obp->next = NULL;

	*c->outbuf_tail = obp;
	c->outbuf_tail = &obp->next;

	FD_SET(c->fd, &socksetout);
	// pthread_mutex_unlock(&outbuflock);
}

void
broadcast(char *buf, size_t size) {
	struct linkedbuf *ob;
	struct connection *c;
	pthread_mutex_lock(&callmtx);
	if(connhead == NULL) {
		free(buf);
		pthread_mutex_unlock(&callmtx);
		return;
	}
	ob = calloc(1, sizeof(struct linkedbuf));
	ob->iov.iov_len = size;
	ob->iov.iov_base = buf;
	ob->refcount = 0;

	c = connhead;
	while(c != NULL) {
		queue_buf(c, ob);
		c = c->next;
	}
	pthread_mutex_unlock(&callmtx);
}

static void
write_client(struct connection *c, char *buf, size_t size) {
	struct linkedbuf *ob = malloc(sizeof(struct linkedbuf));
	struct linkedbuf_ptr *obp = malloc(sizeof(struct linkedbuf_ptr));
	ob->iov.iov_len = size;
	ob->iov.iov_base = buf;
	ob->refcount = 1;

	obp->lb = ob;
	obp->next = NULL;

	*c->outbuf_tail = obp;
	c->outbuf_tail = &obp->next;

	FD_SET(c->fd, &socksetout);
}

void
client_printf(struct connection *c, char *fmt, ...) {
	char *buf;
	va_list ap;
	va_start(ap, fmt);
	int n = vasprintf(&buf, fmt, ap);
	if(n == -1) {
		err(1, "vasprintf");
	}
	va_end(ap);
	write_client(c, buf, n);
}

static int
flush_writes(struct connection *c) {
	// pthread_mutex_lock(&outbuflock);
	assert(c->outbuf != NULL);
	int i = 0;
	struct linkedbuf_ptr *obp = c->outbuf;
	while(obp != NULL && i < PERSISTENT_IOVS) {
		piovs[i] = obp->lb->iov;
		i++;
		obp = obp->next;
	}

	ssize_t n = writev(c->fd, piovs, i);
	switch(n) {
		case -1:
			if(errno == EAGAIN || errno == EPIPE) {
				return 1;
			}
			warn("writev");
#ifdef STRESS
			abort();
#endif
			/* FALL THROUGH */
		case 0:
			return 1;
	}

	do {
		obp = c->outbuf;
		if(n >= obp->lb->iov.iov_len) {
			n -= obp->lb->iov.iov_len;
			c->outbuf = obp->next;
			if(--obp->lb->refcount == 0) {
				free(obp->lb->iov.iov_base);
				free(obp->lb);
			}
			free(obp);
		} else {
			if(n > 0) {
				if(obp->lb->refcount == 1) {
					obp->lb->iov.iov_len -= n;
					memmove(obp->lb->iov.iov_base, obp->lb->iov.iov_base + n, obp->lb->iov.iov_len);
				} else {
					struct linkedbuf *ob2 = malloc(sizeof(struct linkedbuf));
					ob2->iov.iov_len = obp->lb->iov.iov_len - n;
					ob2->iov.iov_base = malloc(ob2->iov.iov_len);
					memcpy(ob2->iov.iov_base, obp->lb->iov.iov_base, ob2->iov.iov_len);
					ob2->refcount = 1;

					obp->lb->refcount--;
					obp->lb = ob2;
				}
			}
			return 0;
		}
	} while(c->outbuf != NULL);

	assert(n == 0);

	c->outbuf_tail = &c->outbuf;

	FD_CLR(c->fd, &socksetout);

	// pthread_mutex_unlock(&outbuflock);
	return 0;
}

static void
drop_client(struct connection *c) {
	FD_CLR(c->fd, &socksetin);
	FD_CLR(c->fd, &socksetout);
	maxsock = -1;

	close(c->fd);

	// pthread_mutex_lock(&outbuflock);
	struct linkedbuf_ptr *obp = c->outbuf;
	while(obp != NULL) {
		c->outbuf = obp->next;
		if(--obp->lb->refcount == 0) {
			free(obp->lb->iov.iov_base);
			free(obp->lb);
		}
		free(obp);
		obp = c->outbuf;
	}
	// pthread_mutex_unlock(&outbuflock);

	if(c == connhead) {
		connhead = c->next;
	} else {
		struct connection *c2 = connhead;
		while(c2->next != c) {
			c2 = c2->next;
			assert(c2 != NULL);
		}
		assert(c2->next == c);
		c2->next = c->next;
	}
	free(c);
}
