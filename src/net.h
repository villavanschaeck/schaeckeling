struct linkedbuf {
	struct iovec iov;
	int refcount;
};

struct linkedbuf_ptr {
	struct linkedbuf_ptr *next;
	struct linkedbuf *lb;
};

struct connection {
	struct connection *next;
	int fd;
	struct linkedbuf_ptr *outbuf;
	struct linkedbuf_ptr **outbuf_tail;
	size_t inbuf_pos;
	char inbuf[64];
};

void init_net();
void *net_runner(void *);
void pre_deinit_net(void);
void deinit_net(void);
void wakeup_select(void);
// void write_client(struct connection *, char *, size_t);
// void queue_buf(struct connection *, struct linkedbuf *);
void broadcast(char *, size_t);
void client_printf(struct connection *, char *, ...);

int handle_data(struct connection *c, char *buf, size_t len);
