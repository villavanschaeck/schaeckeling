#include <sys/types.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "mididriver.h"
#include "input.h"

void
midi_print_buf(const char *prefix, const unsigned char *buf, int len) {
	int i;
	printf("%s", prefix);
	for(i = 0; len > i; i++) {
		printf("%02x%c", buf[i], i == len-1 ? '\n' : ' ');
	}
}

int
midi_send_buf(struct midi_context *ctx, const unsigned char *buf, const int len) {
	int ret;
	midi_print_buf("> ", buf, len);
	ret = write(ctx->fd, buf, len);
	assert(ret == len);
	return 0;
}

static void
midi_reader(struct midi_context *ctx) {
	unsigned char buffer[512];
	int bufpos = 0;

	ctx->running = 1;

	while(ctx->running) {
		int ret = read(ctx->fd, buffer+bufpos, sizeof(buffer) - bufpos);
		if(ret == -1 && !ctx->running) {
			break;
		}
		assert(ret != -1);
		bufpos += ret;

		int eat;
		int did_something = 0;
		do {
			eat = ctx->eater(ctx, buffer, bufpos);
			assert(eat >= 0 && eat <= bufpos);
			if(eat == 0) {
				break;
			}
			did_something = 1;
			bufpos -= eat;
			memmove(buffer, buffer + eat, bufpos);
		} while(eat > 0);

		if(did_something) {
			midi_input_completed();
		}
	}
}

static void *
midi_read_thread(void *ctx) {
	midi_reader(ctx);
	return NULL;
}

struct midi_context *
init_midi(struct midi_context *ctx, char *path, midi_data_eater *eater) {
	assert(ctx != NULL);

	ctx->eater = eater;

	ctx->fd = open(path, O_RDWR);
	if(ctx->fd == -1) {
		return NULL;
	}

	pthread_create(&ctx->thr, NULL, midi_read_thread, ctx);

	return ctx;
}

void
teardown_midi(struct midi_context *ctx) {
	ctx->running = 0;
	close(ctx->fd);
	pthread_join(ctx->thr, NULL);
}
