#define _POSIX_C_SOURCE 199309L
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include "dmxdriver.h"
#include "net.h"
#include "colors.h"

#define DMX_CHANNELS 512

enum handle_action { HANDLE_NONE, HANDLE_SINGLE_CHANNEL, HANDLE_LED_STATIC, HANDLE_LED_2CH_INTENSITY, HANDLE_LED_2CH_COLOR, HANDLE_MASTER, HANDLE_BPM };

struct fader_handler {
	enum handle_action action;
	union {
		struct {
			int channel;
		} single_channel;
		struct {
			int other_input;
			int base_channel;
		} led_2ch;
		struct {
			int base_channel;
			int num_channels;
			int offset;
		} led_static;
	} data;
};

pthread_t netthr, progthr, watchdogthr;
pthread_mutex_t dmx2_sendbuf_mtx, stepmtx;
pthread_cond_t stepcond;

int watchdog_dmx_pong = 0;
int watchdog_net_pong = 0;
int watchdog_prog_pong = 0;

unsigned char recvbuf[DMX_CHANNELS];
unsigned char dmx2_sendbuf[DMX_CHANNELS];

unsigned char old_recvbuf[DMX_CHANNELS];
unsigned char dmx2_old_sendbuf[DMX_CHANNELS];

volatile int dmx2_dirty = 0;
extern int receiving_changes;

struct fader_handler handlers[DMX_CHANNELS];

unsigned char fader_overrides[DMX_CHANNELS];
unsigned char fader_overridden[DMX_CHANNELS];

int master_divider = 1;
long programma_wait = 1000000;
struct timespec nextstep;

#include "programma.c"

static void inline
increment_timespec(struct timespec *ts, long add) {
	int i;
	assert(add > 0);
	// ts->tv_nsec += add * 1000L;
	for(i = 0; 1000 > i; i++) {
		ts->tv_nsec += add;
		ts->tv_sec += ts->tv_nsec / 1000000000L;
		ts->tv_nsec = ts->tv_nsec % 1000000000L;
	}
	assert(ts->tv_nsec >= 0);
}

static void inline
decrement_timespec(struct timespec *ts, long sub) {
	int i;
	assert(sub > 0);
	for(i = 0; 1000 > i; i++) {
		ts->tv_nsec -= sub;
		while(ts->tv_nsec < 0) {
			ts->tv_sec -= 1;
			ts->tv_nsec += 1000000000L;
		}
	}
	assert(ts->tv_nsec >= 0);
}

void
update_websockets(int dmx1, int dmx2) {
	if(dmx1) {
		char *msg = malloc(1 + DMX_CHANNELS);
		msg[0] = '1';
		memcpy(msg+1, recvbuf, DMX_CHANNELS);
		broadcast(msg, 1 + DMX_CHANNELS);
	}
	if(dmx2) {
		char *msg = malloc(1 + DMX_CHANNELS);
		msg[0] = '2';
		memcpy(msg+1, dmx2_sendbuf, DMX_CHANNELS);
		broadcast(msg, 1 + DMX_CHANNELS);
	}
	if(dmx1 || dmx2) {
		wakeup_select();
	}
}

void
flush_dmx2_sendbuf() {
	pthread_mutex_lock(&dmx2_sendbuf_mtx);
	if(dmx2_dirty) {
		send_dmx(mk2c, dmx2_sendbuf);
		update_websockets(1, 0);
		dmx2_dirty = 0;
	}
	pthread_mutex_unlock(&dmx2_sendbuf_mtx);
}

void
update_channel(int channel, unsigned char new) {
	int ch;
	unsigned char intensity, color;

	recvbuf[channel] = new;

	switch(handlers[channel].action) {
		case HANDLE_NONE:
			break;
		case HANDLE_SINGLE_CHANNEL:
			pthread_mutex_lock(&dmx2_sendbuf_mtx);
			dmx2_sendbuf[handlers[channel].data.single_channel.channel] = new;
			fader_overridden[handlers[channel].data.single_channel.channel] = (new > 0);
			fader_overrides[handlers[channel].data.single_channel.channel] = new;
			dmx2_dirty = 1;
			pthread_mutex_unlock(&dmx2_sendbuf_mtx);
			break;
		case HANDLE_LED_STATIC:
			pthread_mutex_lock(&dmx2_sendbuf_mtx);
			for(ch = handlers[channel].data.led_static.base_channel; handlers[channel].data.led_static.base_channel + handlers[channel].data.led_static.num_channels > ch; ch++) {
				dmx2_sendbuf[ch] = 0;
				fader_overridden[ch] = (new > 0);
				fader_overrides[ch] = 0;
			}
			ch = handlers[channel].data.led_static.base_channel + handlers[channel].data.led_static.offset;
			dmx2_sendbuf[ch] = new;
			fader_overrides[ch] = new;
			dmx2_dirty = 1;
			pthread_mutex_unlock(&dmx2_sendbuf_mtx);
			break;
		case HANDLE_LED_2CH_INTENSITY:
		case HANDLE_LED_2CH_COLOR:
			if(handlers[channel].action == HANDLE_LED_2CH_INTENSITY) {
				intensity = new;
				color = recvbuf[handlers[channel].data.led_2ch.other_input];
			} else {
				intensity = recvbuf[handlers[channel].data.led_2ch.other_input];
				color = new;
			}
			int ch = handlers[channel].data.led_2ch.base_channel;
			pthread_mutex_lock(&dmx2_sendbuf_mtx);
			if(color < 9) {
				memset(fader_overridden + ch, 0, 3);
			} else {
				convert_color_and_intensity(color, intensity, fader_overrides + ch);
				memcpy(dmx2_sendbuf + ch, fader_overrides + ch, 3);
				memset(fader_overridden + ch, 1, 3);
				dmx2_dirty = 1;
			}
			pthread_mutex_unlock(&dmx2_sendbuf_mtx);
			break;
		case HANDLE_MASTER:
			pthread_mutex_lock(&stepmtx);
			master_divider = 256 - new;
			pthread_cond_signal(&stepcond);
			pthread_mutex_unlock(&stepmtx);
			return;
		case HANDLE_BPM:
			printf("[dmx] pthread_mutex_lock(&stepmtx);\n");
			pthread_mutex_lock(&stepmtx);
			// BPM range: 30 - 180
			long new_wait = 1000000 * 60 / (30 + ((180 - 30) * new / 255));
			if(programma_wait > new_wait) {
				decrement_timespec(&nextstep, programma_wait - new_wait);
			}
			programma_wait = new_wait;
			printf("[dmx] pthread_cond_signal(&stepcond);\n");
			pthread_cond_signal(&stepcond);
			printf("[dmx] pthread_mutex_unlock(&stepmtx);\n");
			pthread_mutex_unlock(&stepmtx);
			return;
	}
}




int
handle_data(struct connection *c, char *buf_s, size_t len) {
	unsigned char *buf = (unsigned char *)buf_s;
	int processed = 0;
#define REQUIRE_MIN_LENGTH(x) if(x > len) { return 0; } processed = x
	assert(len > 0);

	int ch;

	switch(buf[0]) {
		case 'F':
			REQUIRE_MIN_LENGTH(3);
			ch = buf[1] - 1;
			if(handlers[ch].action == HANDLE_LED_2CH_INTENSITY || handlers[ch].action == HANDLE_LED_2CH_COLOR) {
				handlers[handlers[ch].data.led_2ch.other_input].action = HANDLE_NONE;
			}
			switch(buf[2]) {
				case 'R':
					printf("net: Set fader %d to default\n", ch+1);
					handlers[ch].action = HANDLE_NONE;
					update_channel(ch, recvbuf[ch]);
					break;
				case 'C':
					REQUIRE_MIN_LENGTH(4);
					printf("net: Set fader %d to single channel %d\n", ch+1, buf[3]);
					handlers[ch].action = HANDLE_SINGLE_CHANNEL;
					handlers[ch].data.single_channel.channel = buf[3]-1;
					break;
				case 'L':
					REQUIRE_MIN_LENGTH(6);
					printf("net: Set fader %d to led static [%d/%d-%d]\n", ch+1, buf[3] + buf[5], buf[3], buf[3] + buf[4]);
					handlers[ch].action = HANDLE_LED_STATIC;
					handlers[ch].data.led_static.base_channel = buf[3]-1;
					handlers[ch].data.led_static.num_channels = buf[4];
					handlers[ch].data.led_static.offset = buf[5];
					break;
				case '2':
					REQUIRE_MIN_LENGTH(5);
					printf("net: Set fader %d and %d to led 2ch [%d-%d]\n", ch+1, buf[3]-1, buf[4]-1, buf[4]-1 + 2);
					handlers[ch].action = HANDLE_LED_2CH_INTENSITY;
					handlers[ch].data.led_2ch.other_input = buf[3]-1;
					handlers[ch].data.led_2ch.base_channel = buf[4]-1;
					handlers[buf[3]-1].action = HANDLE_LED_2CH_COLOR;
					handlers[buf[3]-1].data.led_2ch.other_input = ch;
					handlers[buf[3]-1].data.led_2ch.base_channel = buf[4]-1;
					break;
				case 'B':
					printf("net: Set fader %d to bpm\n", ch);
					handlers[ch].action = HANDLE_BPM;
					break;
				case 'M':
					printf("net: Set fader %d to master\n", ch);
					handlers[ch].action = HANDLE_MASTER;
					break;
				default:
					return -1;
			}
			break;
		case 'R':
			REQUIRE_MIN_LENGTH(1);
			for(ch = 0; DMX_CHANNELS > ch; ch++) {
				handlers[ch].action = HANDLE_NONE;
			}
			break;
		case 'B':
			REQUIRE_MIN_LENGTH(2);
			programma_wait = 1000000 * 60 / buf[1];
			break;
		case 'S':
			REQUIRE_MIN_LENGTH(1);
			pthread_mutex_lock(&stepmtx);
			clock_gettime(CLOCK_REALTIME, &nextstep);
			pthread_cond_signal(&stepcond);
			pthread_mutex_unlock(&stepmtx);
			break;
		case 'G':
			REQUIRE_MIN_LENGTH(1);
			printf("Sending settings to %p\n", c);
			for(ch = 0; DMX_CHANNELS > ch; ch++) {
				switch(handlers[ch].action) {
					case HANDLE_NONE:
						break;
					case HANDLE_SINGLE_CHANNEL:
						client_printf(c, "F%cC%c", ch+1, handlers[ch].data.single_channel.channel+1);
						break;
					case HANDLE_LED_STATIC:
						client_printf(c, "F%cL%c%c%c", ch+1, handlers[ch].data.led_static.base_channel+1, handlers[ch].data.led_static.num_channels, handlers[ch].data.led_static.offset);
						break;
					case HANDLE_LED_2CH_INTENSITY:
						client_printf(c, "F%c2%c%c", ch+1, handlers[ch].data.led_2ch.other_input+1, handlers[ch].data.led_2ch.base_channel+1);
						break;
					case HANDLE_LED_2CH_COLOR:
						// wordt geconfigt via HANDLE_LED_2CH_INTENSITY
						break;
					case HANDLE_MASTER:
						client_printf(c, "F%cM", ch+1);
						break;
					case HANDLE_BPM:
						client_printf(c, "F%cB", ch+1);
						break;
				}
			}
			break;
		default:
			return -1;
	}
	if(!receiving_changes && mk2c != NULL) {
		flush_dmx2_sendbuf();
	}
	return processed;
}
#undef REQUIRE_MIN_LENGTH

void *
prog_runner(void *dummy) {
	pthread_mutex_lock(&stepmtx);
	clock_gettime(CLOCK_REALTIME, &nextstep);
	while(1) {
		int step = 0;
		while(programma_steps > step) {
			int ch;
			pthread_mutex_lock(&dmx2_sendbuf_mtx);
			printf("program_step: ");
			for(ch = 0; programma_channels > ch; ch++) {
				dmx2_sendbuf[ch] = master_divider ? (fader_overridden[ch] ? fader_overrides[ch] : programma[step][ch]) / master_divider : 0;
				printf("%d: %03d; ", ch, dmx2_sendbuf[ch]);
			}
			printf("\n");
			if(mk2c_lost) {
				dmx2_dirty = 1;
				pthread_mutex_unlock(&dmx2_sendbuf_mtx);
				reconnect_if_needed();
			} else {
				send_dmx(mk2c, dmx2_sendbuf);
				update_websockets(1, 0);
				pthread_mutex_unlock(&dmx2_sendbuf_mtx);
			}

			printf("[prog] pthread_cond_timedwait(&stepcond, &stepmtx, { %d, %ld });\n", (int)nextstep.tv_sec, nextstep.tv_nsec);
			int res = pthread_cond_timedwait(&stepcond, &stepmtx, &nextstep);
			if(res == ETIMEDOUT) {
				increment_timespec(&nextstep, programma_wait);
				step++;
			}
			watchdog_prog_pong = 1;
		}
	}
	pthread_mutex_unlock(&stepmtx);
	return NULL;
}

void *
watchdog_runner(void *dummy) {
	int ok = 1;
	pid_t parent;
	while(1) {
		watchdog_dmx_pong = 0;
		watchdog_net_pong = 0;
		watchdog_prog_pong = 0;
		wakeup_select();

		sleep(5);

		if(!watchdog_dmx_pong) {
			fprintf(stderr, "Watchdog: DMX thread not responding\n");
			ok = 0;
		}
		if(!watchdog_net_pong) {
			fprintf(stderr, "Watchdog: Network thread not responding\n");
			ok = 0;
		}
		if(!watchdog_prog_pong) {
			fprintf(stderr, "Watchdog: Program thread not responding\n");
			ok = 0;
		}
		if(ok) {
			parent = getppid();
			if(parent != 1) {
				kill(parent, SIGWINCH);
			}
		}
	}
	return NULL;
}

void
reset_vars() {
	int ch;
	pthread_mutex_lock(&dmx2_sendbuf_mtx);
	for(ch = 0; DMX_CHANNELS > ch; ch++) {
		handlers[ch].action = HANDLE_NONE;
		recvbuf[ch] = 0;
		dmx2_sendbuf[ch] = 0;
		old_recvbuf[ch] = 0;
		dmx2_old_sendbuf[ch] = 0;
		fader_overridden[ch] = 0;
		fader_overrides[ch] = 0;
	}
	dmx2_dirty = 1;
	pthread_mutex_unlock(&dmx2_sendbuf_mtx);
}

int
read_config_file(char *filename) {
	struct stat st;
	int offset = 0, processed = 0, ret = 1;
	int fd = open(filename, O_RDONLY);
	if(fd == -1) {
		warn("open");
		return 0;
	}
	if(fstat(fd, &st) != 0) {
		warn("fstat");
		return 0;
	}
	char *cfg = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if(cfg == MAP_FAILED) {
		close(fd);
		warn("mmap");
		return 0;
	}
	do {
		processed = handle_data(NULL, cfg + offset, st.st_size - offset);
		if(processed < 0) {
			fprintf(stderr, "Parsing config failed on position %d\n", offset);
			ret = 0;
			break;
		}
		offset += processed;
	} while(processed > 0 && st.st_size > offset);

	munmap(cfg, st.st_size);
	close(fd);
	return ret;
}

int
main(int argc, char **argv) {
	pthread_mutex_init(&dmx2_sendbuf_mtx, NULL);
	pthread_mutex_init(&stepmtx, NULL);
	pthread_cond_init(&stepcond, NULL);
	reset_vars();

	read_config_file("config.dat");

	init_communications();
	init_net();

	pthread_create(&netthr, NULL, net_runner, NULL);
	pthread_create(&progthr, NULL, prog_runner, NULL);

	send_dmx(mk2c, dmx2_sendbuf);

	watchdog_runner(NULL);

	pre_deinit_net();

	void *ret = NULL;
	pthread_join(netthr, &ret);
	deinit_net();
	pthread_join(progthr, &ret);
	return 0;
}
