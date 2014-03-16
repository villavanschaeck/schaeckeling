#include <sys/types.h>
#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/*
struct mk2_pro_context * init_dmx_usb_mk2_pro(dmx_update_callback_t update_callback, dmx_commit_callback_t commit_callback, dmx_error_callback_t error_callback);
void teardown_dmx_usb_mk2_pro(struct mk2_pro_context *mk2c);
int send_dmx(struct mk2_pro_context *mk2c, unsigned char *dmxbytes);
*/

#define NANOKONTROL2_MODE_NORMAL	0
#define NANOKONTROL2_MODE_NATIVE	1

struct nanokontrol2_context {
	int fd;
	pthread_t thr;
	int running;
	int active_mode;
	int requested_mode;
};

static void
print_midi(const char *prefix, const unsigned char *buf, int len) {
	int i;
	printf("%s", prefix);
	for(i = 0; len > i; i++) {
		printf("%02x%c", buf[i], i == len-1 ? '\n' : ' ');
	}
}

static void
nanokontrol2_send_midi(struct nanokontrol2_context *ctx, const unsigned char *buf, const int len) {
	int ret;
	print_midi("> ", buf, len);
	ret = write(ctx->fd, buf, len);
	assert(ret == len);
}

void
nanokontrol2_set_led(struct nanokontrol2_context *ctx, int which, int value) {
	unsigned char msg[] = { 0xbf, which, value == 0 ? 0x00 : 0x7f };
	nanokontrol2_send_midi(ctx, msg, 3);
}

static int
nanokontrol2_read_data(struct nanokontrol2_context *ctx, const unsigned char *buffer, int len) {
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
			print_midi("buffer: ", buffer, len);
			abort();
	}
	if(cmdlen > len) {
		printf("Not enough data for %02x: %d of %d\n", buffer[0], len, cmdlen);
		return 0;
	}
	print_midi("< ", buffer, cmdlen);
	switch(buffer[0]) {
		case 0xb0:
		case 0xbf:
			if(buffer[1] >= 0x00 && buffer[1] < 0x08) {
				nanokontrol2_set_led(ctx, 0x40 + buffer[1], buffer[2] > 0);
				nanokontrol2_set_led(ctx, 0x30 + buffer[1], buffer[2] >= 64);
				nanokontrol2_set_led(ctx, 0x20 + buffer[1], buffer[2] == 0x7f);
			}
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
							print_midi("group 1: ", buffer + dumpoffset + 1 + 3, 30);
							break;
					}
					break;
			}
			break;
	}
	return cmdlen;
}

static void
nanokontrol2_reader(struct nanokontrol2_context *ctx) {
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
		do {
			eat = nanokontrol2_read_data(ctx, buffer, bufpos);
			assert(eat >= 0 && eat <= bufpos);
			if(eat == 0) {
				break;
			}
			bufpos -= eat;
			memmove(buffer, buffer + eat, bufpos);
		} while(eat > 0);
	}
}

static void *
read_thread(void *ctx) {
	nanokontrol2_reader(ctx);
	return NULL;
}

struct nanokontrol2_context *
init_nanokontrol2(char *path) {
	struct nanokontrol2_context *ctx;

	ctx = malloc(sizeof(struct nanokontrol2_context));
	assert(ctx != NULL);

	ctx->fd = open(path, O_RDWR);
	assert(ctx->fd != -1);

	pthread_create(&ctx->thr, NULL, read_thread, ctx);

	return ctx;
}

void
teardown_nanokontrol2(struct nanokontrol2_context *ctx) {
	ctx->running = 0;
	close(ctx->fd);
	pthread_join(ctx->thr, NULL);
	free(ctx);
}

static void
nanokontrol2_switch_mode(struct nanokontrol2_context *ctx, int mode) {
	unsigned char buf[] = { 0xf0, 0x42, 0x40, 0x00, 0x01, 0x13, 0x00, 0x00, 0x00, mode, 0xf7 };
	nanokontrol2_send_midi(ctx, buf, 11);
	ctx->requested_mode = mode;
}

void
nanokontrol2_ask_status(struct nanokontrol2_context *ctx) {
	unsigned char buf[] = { 0xf0, 0x42, 0x40, 0x00, 0x01, 0x13, 0x00, 0x1f, 0x10, 0x00, 0xf7 };
	nanokontrol2_send_midi(ctx, buf, 11);
}

int
main(int argc, char **argv) {
	struct nanokontrol2_context *ctx;

	ctx = init_nanokontrol2("/dev/snd/midiC1D0");

	nanokontrol2_switch_mode(ctx, NANOKONTROL2_MODE_NORMAL);
	nanokontrol2_ask_status(ctx);
	nanokontrol2_switch_mode(ctx, NANOKONTROL2_MODE_NATIVE);

/*
	sleep(3);
	unsigned char rstbuf[] = { 0xf0, 0x42, 0x40, 0x00, 0x01, 0x13, 0x00, 0xff, 0x00, 0x00, 0xf7 };
	nanokontrol2_send_midi(ctx, rstbuf, 11);
*/

	//pthread_join(ctx->thr, NULL);
	sleep(5);

	teardown_nanokontrol2(ctx);

	return 0;
}
