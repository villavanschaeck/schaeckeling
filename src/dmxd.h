void update_input(inputidx_t input, unsigned char value);
void flush_dmxout_sendbuf(void);
void update_websockets(int dmx1, int dmx2);
void error_step(void);

int send_dmx(unsigned char *dmxbytes);
void reconnect_if_needed(void);
int init_communications(void);

