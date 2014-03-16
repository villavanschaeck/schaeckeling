#include <pthread.h>

typedef int (midi_data_eater)(void *ctx, const unsigned char *buffer, int len);

struct midi_context {
	int fd;
	pthread_t thr;
	int running;
	midi_data_eater *eater;
};

void midi_print_buf(const char *prefix, const unsigned char *buf, int len);
int midi_send_buf(struct midi_context *ctx, const unsigned char *buf, const int len);
struct midi_context * init_midi(struct midi_context *ctx, char *path, midi_data_eater *eater);
void teardown_midi(struct midi_context *ctx);
