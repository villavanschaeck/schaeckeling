/* dmxd.c */
void update_input(inputidx_t input, unsigned char value);
void flush_dmxout_sendbuf(void);
void update_websockets(int dmx1, int dmx2);
void error_step(void);


/* input.c */
int send_dmx(unsigned char *dmxbytes);
void reconnect_if_needed(void);
int init_communications(void);
void set_feedback_running(int);
void set_feedback_blackout(int);
void set_feedback_step();
