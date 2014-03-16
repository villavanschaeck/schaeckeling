#include <sys/types.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "usbmididriver.h"

static int
usbmidi_read_data(void *_ctx, const unsigned char *buffer, int len) {
	struct usbmidi_context *ctx = _ctx;
	if(len < 1) {
		return 0;
	}
	int cmdlen = 0;
	int dumpoffset = -1;
	switch(buffer[0]) {
		case 0xb0:
			cmdlen = 3;
			break;
		case 0xf0:
			if(len < 2) {
				return 0;
			}
			for(cmdlen = 2; buffer[cmdlen-1] <= 0x7f; cmdlen++) {
				if(cmdlen >= len) {
					return 0;
				}
			}
			break;
		default:
			printf("Wat de fuck is %02x?\n", buffer[0]);
			midi_print_buf("buffer: ", buffer, len);
			abort();
	}
	if(cmdlen > len) {
		printf("Not enough data for %02x: %d of %d\n", buffer[0], len, cmdlen);
		return 0;
	}
	midi_print_buf("< ", buffer, cmdlen);
	switch(buffer[0]) {
		case 0xb0:
			midi_changed(buffer[1], buffer[2]);
			break;
		case 0xf0:
			assert(buffer[cmdlen-1] == 0xf7);
			break;
	}
	return cmdlen;
}

struct usbmidi_context *
init_nanokontrol2(char *path) {
	struct usbmidi_context *ctx;

	ctx = malloc(sizeof(struct usbmidi_context));
	assert(ctx != NULL);

	if(init_midi(&ctx->midictx, path, usbmidi_read_data) == NULL) {
		free(ctx);
		return NULL;
	}

	return ctx;
}

void
teardown_usbmidi(struct usbmidi_context *ctx) {
	teardown_midi(&ctx->midictx);
	free(ctx);
}
