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
#include "schaeckeling.h"
#include "dmxdriver.h"
#include "net.h"
#include "colors.h"
#include "dmxd.h"


enum handle_action { HANDLE_NONE, HANDLE_RAW_VALUE, HANDLE_LED_2CH_INTENSITY, HANDLE_LED_2CH_COLOR, HANDLE_MASTER, HANDLE_BPM, HANDLE_CHASE, HANDLE_RUN, HANDLE_BLACKOUT };

struct fader_handler {
	enum handle_action action;
	union {
		struct {
			dmxchannel_t channel;
		} raw_value;
		struct {
			inputidx_t other_input;
			dmxchannel_t base_channel;
		} led_2ch;
	} data;
};

pthread_t netthr, progthr, watchdogthr;
pthread_mutex_t dmxout_sendbuf_mtx, stepmtx;
pthread_cond_t stepcond;

int watchdog_dmx_pong = 0;
int watchdog_net_pong = 0;
int watchdog_prog_pong = 0;

extern int mk2c_lost; // FIXME define a cleaner way of reconnecting. dmxd.c shouldn't care *what* has been lost.

unsigned char inputbuf[INPUT_CHANNELS];
struct fader_handler handlers[INPUT_CHANNELS];

extern int receiving_changes;

unsigned char dmxout_sendbuf[DMX_CHANNELS];
volatile int dmxout_dirty = 0;

unsigned char channel_flags[DMX_CHANNELS];
unsigned char channel_overrides[DMX_CHANNELS];
unsigned char channel_intensity[DMX_CHANNELS];

int master_blackout = -1;
int master_intensity = 255;
int program_intensity = 255;
int program_running = 1;
long programma_wait = 1000000;
struct timespec nextstep;

char *programma = NULL;
int programma_steps = 1, programma_channels = 0;

char *new_programma = NULL;
int new_programma_steps, new_programma_channels;

#define CHFLAG_IGNORE_MASTER 1
#define CHFLAG_OVERRIDE_PROGRAMMA 2

#define CHFLAG_GET_FLAG(ch, flag) (channel_flags[ch] & flag)
#define CHFLAG_SET_FLAG(ch, flag) channel_flags[ch] |= flag
#define CHFLAG_CLR_FLAG(ch, flag) channel_flags[ch] &= ~flag

#define CHFLAG_GET_IGNORE_MASTER(ch) CHFLAG_GET_FLAG(ch, CHFLAG_IGNORE_MASTER)
#define CHFLAG_GET_OVERRIDE_PROGRAMMA(ch) CHFLAG_GET_FLAG(ch, CHFLAG_OVERRIDE_PROGRAMMA)
#define CHFLAG_SET_IGNORE_MASTER(ch) CHFLAG_SET_FLAG(ch, CHFLAG_IGNORE_MASTER)
#define CHFLAG_SET_OVERRIDE_PROGRAMMA(ch) CHFLAG_SET_FLAG(ch, CHFLAG_OVERRIDE_PROGRAMMA)
#define CHFLAG_CLR_IGNORE_MASTER(ch) CHFLAG_CLR_FLAG(ch, CHFLAG_IGNORE_MASTER)
#define CHFLAG_CLR_OVERRIDE_PROGRAMMA(ch) CHFLAG_CLR_FLAG(ch, CHFLAG_OVERRIDE_PROGRAMMA)

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

static int inline
dmx_channel_to_dmxindex(dmxchannel_t channel) {
	assert(channel > 0 && channel <= DMX_CHANNELS);
	return channel-1;
}

static dmxchannel_t inline
dmxindex_to_channel(int channel) {
	assert(channel >= 0 && channel < DMX_CHANNELS);
	return channel+1;
}

static unsigned char
apply_intensity(unsigned char in, unsigned char intensity) {
	int tmp = in * intensity;
	return tmp / 255;
}

void
error_step(void) {
	pthread_mutex_lock(&stepmtx);
	pthread_cond_signal(&stepcond);
	pthread_mutex_unlock(&stepmtx);
}


void
update_websockets(int dmx1, int dmx2) {
/*
	if(dmx1) {
		char *msg = malloc(1 + DMX_CHANNELS);
		msg[0] = '1';
		memcpy(msg+1, inputbuf, DMX_CHANNELS);
		broadcast(msg, 1 + DMX_CHANNELS);
	}
*/
	if(dmx2) {
		char *msg = malloc(1 + DMX_CHANNELS);
		msg[0] = '2';
		memcpy(msg+1, dmxout_sendbuf, DMX_CHANNELS);
		broadcast(msg, 1 + DMX_CHANNELS);
	}
	if(dmx1 || dmx2) {
		wakeup_select();
	}
}

void
flush_dmxout_sendbuf(void) {
	pthread_mutex_lock(&dmxout_sendbuf_mtx);
	if(dmxout_dirty) {
		send_dmx(dmxout_sendbuf);
		update_websockets(1, 0);
		dmxout_dirty = 0;
	}
	pthread_mutex_unlock(&dmxout_sendbuf_mtx);
}

void
update_input(inputidx_t input, unsigned char new) {
	unsigned char intensity, color;
	dmxchannel_t dmxch;
	int dmxidx;

	inputbuf[input] = new;

	switch(handlers[input].action) {
		case HANDLE_NONE:
			break;
		case HANDLE_RAW_VALUE:
			pthread_mutex_lock(&dmxout_sendbuf_mtx);
			dmxidx = dmx_channel_to_dmxindex(handlers[input].data.raw_value.channel);
			CHFLAG_SET_IGNORE_MASTER(dmxidx);
			CHFLAG_SET_OVERRIDE_PROGRAMMA(dmxidx);
			channel_overrides[dmxidx] = new;
			dmxout_sendbuf[dmxidx] = new;
			dmxout_dirty = 1;
			pthread_mutex_unlock(&dmxout_sendbuf_mtx);
			break;
		case HANDLE_LED_2CH_INTENSITY:
		case HANDLE_LED_2CH_COLOR:
			if(handlers[input].action == HANDLE_LED_2CH_INTENSITY) {
				intensity = new;
				color = inputbuf[handlers[input].data.led_2ch.other_input];
			} else {
				intensity = inputbuf[handlers[input].data.led_2ch.other_input];
				color = new;
			}
			dmxch = handlers[input].data.led_2ch.base_channel;
			dmxidx = dmx_channel_to_dmxindex(dmxch);
			pthread_mutex_lock(&stepmtx);
			memset(channel_intensity + dmxidx, intensity, 3);
			if(color >= 252) {
				CHFLAG_CLR_OVERRIDE_PROGRAMMA(dmxidx);
				CHFLAG_CLR_OVERRIDE_PROGRAMMA(dmxidx+1);
				CHFLAG_CLR_OVERRIDE_PROGRAMMA(dmxidx+2);
			} else {
				CHFLAG_SET_OVERRIDE_PROGRAMMA(dmxidx);
				CHFLAG_SET_OVERRIDE_PROGRAMMA(dmxidx+1);
				CHFLAG_SET_OVERRIDE_PROGRAMMA(dmxidx+2);
				convert_color(color, channel_overrides + dmxidx);
			}
			pthread_cond_signal(&stepcond);
			pthread_mutex_unlock(&stepmtx);
			break;
		case HANDLE_MASTER:
			pthread_mutex_lock(&stepmtx);
			if(master_blackout == -1) {
				master_intensity = new;
				pthread_cond_signal(&stepcond);
			} else {
				master_blackout = new;
			}
			pthread_mutex_unlock(&stepmtx);
			return;
		case HANDLE_BLACKOUT:
			if(new < 64) {
				break;
			}
			pthread_mutex_lock(&stepmtx);
			if(master_blackout == -1) {
				master_blackout = master_intensity;
				master_intensity = 0;
				set_feedback_blackout(1);
			} else {
				master_intensity = master_blackout;
				master_blackout = -1;
				set_feedback_blackout(0);
			}
			pthread_cond_signal(&stepcond);
			pthread_mutex_unlock(&stepmtx);
			return;
		case HANDLE_CHASE:
			pthread_mutex_lock(&stepmtx);
			program_intensity = new;
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
		case HANDLE_RUN:
			if(new < 64) {
				break;
			}
			pthread_mutex_lock(&stepmtx);
			program_running = !program_running;
			set_feedback_running(program_running);
			if(program_running) {
				clock_gettime(CLOCK_REALTIME, &nextstep);
			}
			pthread_cond_signal(&stepcond);
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

	inputidx_t iidx;

	switch(buf[0]) {
		case 'D':
		case 'M':
			REQUIRE_MIN_LENGTH(3);
			iidx = (buf[0] == 'D' ? dmx_to_input_index(buf[1]) : midi_to_input_index(buf[1]));
			char *type = (buf[0] == 'D' ? "DMX" : "MIDI");
			int input_number = buf[1];
			if(handlers[iidx].action == HANDLE_LED_2CH_INTENSITY || handlers[iidx].action == HANDLE_LED_2CH_COLOR) {
				handlers[handlers[iidx].data.led_2ch.other_input].action = HANDLE_NONE;
			}
			switch(buf[2]) {
				case 'R':
					printf("net: Set %s channel %d to default\n", type, input_number);
					handlers[iidx].action = HANDLE_NONE;
					update_input(iidx, inputbuf[iidx]);
					break;
				case 'V':
					REQUIRE_MIN_LENGTH(4);
					printf("net: Set raw %s channel %d to channel %d\n", type, input_number, buf[3]);
					handlers[iidx].action = HANDLE_RAW_VALUE;
					handlers[iidx].data.raw_value.channel = buf[3];
					break;
				case '2':
					REQUIRE_MIN_LENGTH(5);
					int other_iidx = (buf[0] == 'D' ? dmx_to_input_index(buf[3]) : midi_to_input_index(buf[3]));
					printf("net: Set %s channel %d and %d to led 2ch [%d-%d]\n", type, input_number, buf[3], buf[4], buf[4] + 2);
					handlers[iidx].action = HANDLE_LED_2CH_INTENSITY;
					handlers[iidx].data.led_2ch.other_input = other_iidx;
					handlers[iidx].data.led_2ch.base_channel = buf[4];
					handlers[other_iidx].action = HANDLE_LED_2CH_COLOR;
					handlers[other_iidx].data.led_2ch.other_input = iidx;
					handlers[other_iidx].data.led_2ch.base_channel = buf[4];
					break;
				case 'B':
					printf("net: Set %s channel %d to bpm\n", type, input_number);
					handlers[iidx].action = HANDLE_BPM;
					break;
				case 'M':
					printf("net: Set %s channel %d to master\n", type, input_number);
					handlers[iidx].action = HANDLE_MASTER;
					break;
				case 'P':
					printf("net: Set %s channel %d to chase (program intensity)\n", type, input_number);
					handlers[iidx].action = HANDLE_CHASE;
					break;
				case 'S':
					printf("net: Set %s channel %d to program play/pause\n", type, input_number);
					handlers[iidx].action = HANDLE_RUN;
					break;
				case 'D':
					printf("net: Set %s channel %d to blackout\n", type, input_number);
					handlers[iidx].action = HANDLE_BLACKOUT;
					break;
				default:
					return -1;
			}
			break;
		case 'R':
			REQUIRE_MIN_LENGTH(1);
			for(iidx = 0; INPUT_CHANNELS > iidx; iidx++) {
				handlers[iidx].action = HANDLE_NONE;
			}
			break;
		case 'V':
			REQUIRE_MIN_LENGTH(3);
			pthread_mutex_lock(&dmxout_sendbuf_mtx);
			int dmxidx = dmx_channel_to_dmxindex(buf[1]);
			CHFLAG_SET_IGNORE_MASTER(dmxidx);
			CHFLAG_SET_OVERRIDE_PROGRAMMA(dmxidx);
			channel_overrides[dmxidx] = buf[2];
			dmxout_sendbuf[dmxidx] = buf[2];
			dmxout_dirty = 1;
			pthread_mutex_unlock(&dmxout_sendbuf_mtx);
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
		case 'P':
			REQUIRE_MIN_LENGTH(2);
			switch(buf[1]) {
				case 'N': // new
					REQUIRE_MIN_LENGTH(6);
					if(new_programma != NULL) {
						free(new_programma);
					}
					new_programma_channels = buf[2] * 256 + buf[3];
					new_programma_steps = buf[4] * 256 + buf[5];
					if(new_programma_steps < 1) {
						return -1;
					}
					new_programma = calloc(new_programma_steps, new_programma_channels);
					break;
				case 'S': // step
					if(new_programma == NULL) {
						return -1;
					}
					REQUIRE_MIN_LENGTH(4 + new_programma_channels);
					int step = buf[2] * 256 + buf[3];
					if(step >= new_programma_steps) {
						return -1;
					}
					memcpy(new_programma + step * new_programma_channels, buf + 4, new_programma_channels);
					break;
				case 'A': // activate
					if(new_programma_steps < 1) {
						return -1;
					}
					if(programma != NULL) {
						free(programma);
					}
					programma = new_programma;
					programma_steps = new_programma_steps;
					programma_channels = new_programma_channels;
					new_programma = NULL;
					new_programma_steps = -1;
					new_programma_channels = -1;
					break;
				default:
					return -1;
			}
			break;
		case 'G':
			REQUIRE_MIN_LENGTH(1);
			printf("Sending settings to %p\n", c);
			for(iidx = 0; INPUT_CHANNELS > iidx; iidx++) {
				char chdesc[2];
				chdesc[0] = input_index_is_dmx(iidx) ? 'D' : 'M';
				chdesc[1] = input_index_is_dmx(iidx) ? input_index_to_dmx(iidx) : input_index_to_midi(iidx);
				switch(handlers[iidx].action) {
					case HANDLE_NONE:
						break;
					case HANDLE_RAW_VALUE:
						client_printf(c, "%sV%c", chdesc, dmxindex_to_channel(handlers[iidx].data.raw_value.channel));
						break;
					case HANDLE_LED_2CH_INTENSITY:
						client_printf(c, "%s2%c%c", chdesc, dmxindex_to_channel(handlers[iidx].data.led_2ch.other_input), dmxindex_to_channel(handlers[iidx].data.led_2ch.base_channel));
						break;
					case HANDLE_LED_2CH_COLOR:
						// wordt geconfigt via HANDLE_LED_2CH_INTENSITY
						break;
					case HANDLE_MASTER:
						client_printf(c, "%sM", chdesc);
						break;
					case HANDLE_CHASE:
						client_printf(c, "%sP", chdesc);
						break;
					case HANDLE_BPM:
						client_printf(c, "%sB", chdesc);
						break;
					case HANDLE_RUN:
						client_printf(c, "%sS", chdesc);
						break;
					case HANDLE_BLACKOUT:
						client_printf(c, "%sD", chdesc);
						break;
				}
			}
			break;
		default:
			return -1;
	}
	if(!receiving_changes) {
		flush_dmxout_sendbuf();
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
			int dmxidx;
			pthread_mutex_lock(&dmxout_sendbuf_mtx);
			for(dmxidx = 0; DMX_CHANNELS > dmxidx; dmxidx++) {
				if(CHFLAG_GET_OVERRIDE_PROGRAMMA(dmxidx) || dmxidx >= programma_channels) {
					dmxout_sendbuf[dmxidx] = channel_overrides[dmxidx];
				} else {
					dmxout_sendbuf[dmxidx] = apply_intensity(programma[step * programma_channels + dmxidx], program_intensity);
				}
				dmxout_sendbuf[dmxidx] = apply_intensity(dmxout_sendbuf[dmxidx], channel_intensity[dmxidx]);
				if(!CHFLAG_GET_IGNORE_MASTER(dmxidx)) {
					dmxout_sendbuf[dmxidx] = apply_intensity(dmxout_sendbuf[dmxidx], master_intensity);
				}
			}
			if(mk2c_lost) {
				dmxout_dirty = 1;
				pthread_mutex_unlock(&dmxout_sendbuf_mtx);
				reconnect_if_needed();
			} else {
				send_dmx(dmxout_sendbuf);
				update_websockets(1, 0);
				pthread_mutex_unlock(&dmxout_sendbuf_mtx);
			}

			printf("[prog] pthread_cond_timedwait(&stepcond, &stepmtx, { %d, %ld });\n", (int)nextstep.tv_sec, nextstep.tv_nsec);
			int res = pthread_cond_timedwait(&stepcond, &stepmtx, &nextstep);
			if(res == ETIMEDOUT) {
				increment_timespec(&nextstep, programma_wait);
				if(program_running) {
					step++;
					set_feedback_step();
				}
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
	int iidx, dmxidx;
	pthread_mutex_lock(&dmxout_sendbuf_mtx);
	for(iidx = 0; INPUT_CHANNELS > iidx; iidx++) {
		handlers[iidx].action = HANDLE_NONE;
		inputbuf[iidx] = 255;
	}
	for(dmxidx = 0; DMX_CHANNELS > dmxidx; dmxidx++) {
		dmxout_sendbuf[dmxidx] = 0;
		channel_flags[dmxidx] = 0;
		channel_intensity[dmxidx] = 255;
		channel_overrides[dmxidx] = 0;
	}
	dmxout_dirty = 1;
	pthread_mutex_unlock(&dmxout_sendbuf_mtx);
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
	pthread_mutex_init(&dmxout_sendbuf_mtx, NULL);
	pthread_mutex_init(&stepmtx, NULL);
	pthread_cond_init(&stepcond, NULL);
	reset_vars();

	read_config_file("config.dat");

	init_communications();
	init_net();

	set_feedback_running(program_running);
	set_feedback_blackout(master_blackout != -1);

	pthread_create(&netthr, NULL, net_runner, NULL);
	pthread_create(&progthr, NULL, prog_runner, NULL);

	send_dmx(dmxout_sendbuf);

	watchdog_runner(NULL);

	pre_deinit_net();

	void *ret = NULL;
	pthread_join(netthr, &ret);
	deinit_net();
	pthread_join(progthr, &ret);
	return 0;
}
