#define DMX_CHANNELS 512
#define MIDI_CHANNELS 128

void update_channel(int channel, unsigned char new);
void flush_dmx2_sendbuf(void);
void update_websockets(int dmx1, int dmx2);
void error_step(void);

int send_dmx(unsigned char *dmxbytes);
void reconnect_if_needed(void);
int init_communications(void);

