#include "input-config.h"
#ifndef DISABLE_NANOKONTROL
#include <sys/types.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include "nanokontroldriver.h"
#include "input.h"

void
nanokontrol2_set_led(struct nanokontrol2_context *ctx, int which, int value) {
	unsigned char msg[] = { 0xbf, which, value == 0 ? 0x00 : 0x7f };
	midi_send_buf(&ctx->midictx, msg, 3);
}

static void
nanokontrol2_switch_mode(struct nanokontrol2_context *ctx, int mode) {
	unsigned char buf[] = { 0xf0, 0x42, 0x40, 0x00, 0x01, 0x13, 0x00, 0x00, 0x00, mode, 0xf7 };
	midi_send_buf(&ctx->midictx, buf, 11);
	ctx->requested_mode = mode;
}

static int
nanokontrol2_read_data(void *_ctx, const unsigned char *buffer, int len) {
	struct nanokontrol2_context *ctx = _ctx;
	if(len < 1) {
		return 0;
	}
	int cmdlen = 0;
	int dumpoffset = -1;
	switch(buffer[0]) {
		case 0xb0:
		case 0xbf:
			cmdlen = 3;
			break;
		case 0xf0:
			if(len < 11) {
				return 0;
			}
			if(buffer[7] == 0x7f) {
				if(buffer[8] == 0x7f) {
					if(len < 12) {
						return 0;
					}
					assert(buffer[9] == 0x02);
					cmdlen = 11 + buffer[10]*128 + buffer[11] + 2;
					dumpoffset = 12;
				} else {
					cmdlen = 11 + buffer[8];
					dumpoffset = 9;
				}
				printf("supercmdlen: %d\n", cmdlen);
			} else {
				cmdlen = 11;
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
		case 0xbf:
#if TEST_NANOKONTROL
			if(buffer[1] >= 0x00 && buffer[1] < 0x08) {
				nanokontrol2_set_led(ctx, 0x40 + buffer[1], buffer[2] > 0);
				nanokontrol2_set_led(ctx, 0x30 + buffer[1], buffer[2] >= 64);
				nanokontrol2_set_led(ctx, 0x20 + buffer[1], buffer[2] == 0x7f);
			}
#else
			midi_changed(buffer[1], buffer[2]);
#endif
			break;
		case 0xf0:
			assert(buffer[cmdlen-1] == 0xf7);
			switch(buffer[7]) {
				case 0x40: // Native mode switch
					assert(buffer[8] == 0x00);
					ctx->active_mode = (buffer[9] == 02 ? NANOKONTROL2_MODE_NORMAL : NANOKONTROL2_MODE_NATIVE);
					break;
				case 0x5f: // Data dump command
					switch(buffer[8]) {
						case 0x21: // Write Completed
							assert(buffer[9] == 0x00);
							break;
						case 0x22: // Write Error
							assert(buffer[9] == 0x00);
						case 0x23: // Data Load Completed
							assert(buffer[9] == 0x00);
							break;
						case 0x24: // Data Load Error
							assert(buffer[9] == 0x00);
							break;
						case 0x42: // Mode Data
							ctx->active_mode = buffer[9];
							break;
					}
					break;
				case 0x7f: // Data dump
					switch(buffer[dumpoffset]) {
						case 0x40: // Current Scene Data Dump
							midi_print_buf("group 1: ", buffer + dumpoffset + 1 + 3, 30);
							break;
					}
					break;
			}
			break;
	}
	return cmdlen;
}

struct nanokontrol2_context *
init_nanokontrol2(char *path) {
	struct nanokontrol2_context *ctx;

	ctx = malloc(sizeof(struct nanokontrol2_context));
	assert(ctx != NULL);

	if(init_midi(&ctx->midictx, path, nanokontrol2_read_data) == NULL) {
		free(ctx);
		return NULL;
	}

	nanokontrol2_switch_mode(ctx, NANOKONTROL2_MODE_NATIVE);

	return ctx;
}

void
teardown_nanokontrol2(struct nanokontrol2_context *ctx) {
	teardown_midi(&ctx->midictx);
	free(ctx);
}

void
nanokontrol2_ask_status(struct nanokontrol2_context *ctx) {
	unsigned char buf[] = { 0xf0, 0x42, 0x40, 0x00, 0x01, 0x13, 0x00, 0x1f, 0x10, 0x00, 0xf7 };
	midi_send_buf(&ctx->midictx, buf, 11);
}

#if TEST_NANOKONTROL
int
main(int argc, char **argv) {
	struct nanokontrol2_context *ctx;

	ctx = init_nanokontrol2("/dev/snd/midiC1D0");

	nanokontrol2_switch_mode(ctx, NANOKONTROL2_MODE_NORMAL);
	nanokontrol2_ask_status(ctx);
	nanokontrol2_switch_mode(ctx, NANOKONTROL2_MODE_NATIVE);

	pthread_join(ctx->midictx.thr, NULL);

	teardown_nanokontrol2(ctx);

	return 0;
}
#endif
#endif
