#define _POSIX_C_SOURCE 199309L


#include "dmxdriver.h"
#include "api.h"
#include <string.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>


pthread_t readid;
unsigned char running;

extern int watchdog_dmx_pong;

void
purge_buffers(struct ftdi_context *ftdic) {
	int ret;
	ret = ftdi_usb_purge_buffers(ftdic);
	if (ret == -1) {
		fprintf(stderr, "Read buffer purge failed");
		exit(1);
	} else if (ret == -2) {
		fprintf(stderr, "Write buffer purge failed");
		exit(2);
	} else if (ret == -3) {
		fprintf(stderr, "USB device unavailable");
		exit(3);
	}
}


int
purge_receive_buffer(struct ftdi_context *ftdic) {
	int ret;
	ret = ftdi_usb_purge_rx_buffer(ftdic);
	if (ret == -1) {
		fprintf(stderr, "Read buffer purge failed");
	} else if (ret == -2) {
		fprintf(stderr, "USB device unavailable");
	}
	return ret;
}


void
send_msg_error(int ret) {
	if (ret == -666) {
		fprintf(stderr, "USB Device Unavailable for ftdi_write_data\n");
	} else if (ret < 0) {
		fprintf(stderr, "ftdi_write_data returned error %d from usb_bulk_write\n", ret);
	} else {
		fprintf(stderr, "Unexpected number of bytes written: %d\n", ret);
	}
}


int
send_msg(struct ftdi_context *ftdic, int label, unsigned char *data, int length)
{
	unsigned char end_code = MSG_END_CODE;
	int ret = 0;

	// Form Packet Header
	unsigned char header[MSG_HEADER_LENGTH];
	header[0] = MSG_START_CODE;
	header[1] = label;
	header[2] = length & OFFSET;
	header[3] = length >> BYTE_LENGTH;

	// Write The Header
	ret = ftdi_write_data(ftdic, header, MSG_HEADER_LENGTH);
	if (ret != MSG_HEADER_LENGTH) {
		send_msg_error(ret);
		return -1;
	}

	// Write The Data
	ret = ftdi_write_data(ftdic, data, length);
	if (ret != length) {
		send_msg_error(ret);
		return -1;
	}

	// Write End Code
	ret = ftdi_write_data(ftdic, &end_code, 1);
	if (ret != 1) {
		send_msg_error(ret);
		return -1;
	}

	return 0;
}


void
send_dmx(struct ftdi_context *ftdic, unsigned char *dmxbytes) {
	unsigned char my_dmx[513];
	int ret;
	
	memcpy(my_dmx + 1, dmxbytes, 512);

	// First byte has to be 0
	my_dmx[0] = 0;

	// send the array here
	ret = send_msg(ftdic, SEND_DMX_2, my_dmx, 513);
	if (ret < 0)
	{
		fprintf(stderr, "FAILED to send DMX ... exiting. FIXME THIS MESSAGE TO BE USEFUL!\n");
		// TODO fix "nice" shutdown of FTDI.
	}
}


void
receive_msg_error(int ret) {
	if (ret == -666) {
		fprintf(stderr, "USB Device Unavailable for ftdi_read_data\n");
	} else if (ret < 0) {
		fprintf(stderr, "ftdi_read_data returned error %d from libusb_bulk_transfer\n", ret);
	} else {
		fprintf(stderr, "Unexpected number of bytes read: %d\n", ret);
	}
}


/*
 * Function that reads exactly length bytes from the ftdi device.
 * Buffer must be at least length bytes in length.
 *
 */
int
read_data(struct ftdi_context *ftdic, unsigned char *buffer, int length) {
	int ret;
	int bytes_read = 0;

	while (bytes_read < length && running) {
		//fprintf(stderr, "Reading data, still %d to read.", length - bytes_read);
		ret = ftdi_read_data(ftdic, buffer + bytes_read, length - bytes_read);
		if (ret < 0) {
			receive_msg_error(ret);
			return -1;
		}
		bytes_read += ret;
	}
	if (!running) {
		pthread_exit(NULL);
	}

	assert(bytes_read == length);
	return bytes_read;
}


int
receive_msg(struct ftdi_context *ftdic, struct application_message *appmsg) {
	int ret;
	int label;
	int length = 0;
	unsigned char header[MSG_HEADER_LENGTH];
	unsigned char buffer[600];

	/* Read header */
	//fprintf(stderr, "Reading new packet header.\n");
	ret = read_data(ftdic, header, MSG_HEADER_LENGTH);
	if (ret == -1) {
		return -1;
	}

	/* Do sanity check on header contents */
	/* TODO filter on valid labels */
	/* Label 5 should have a length of 514 */
	while (1) {
		label = header[1];
		length = header[2];
		length += ((int) header[3]) << BYTE_LENGTH;

		if ((header[0] != MSG_START_CODE) || (length > 600)) {
			fprintf(stderr, "Received:\nStart code: 0x%X\nLabel:      %u\nLength:     %u\n", header[0], label, length);
			fprintf(stderr, "Header invalid. Cycling...\n");
			
			/* Move the header one byte, discarding the "wrong" MSG_START_CODE, then read another byte and repeat */
			memmove(header, header + 1, MSG_HEADER_LENGTH - 1);
			ret = read_data(ftdic, header + 3, 1);
			if (ret != 1) {
				return -1;
			}
		} else {
			//fprintf(stderr, "Header is sane. Reading %u bytes.\n", length);
			break;
		}
	}
	
	ret = read_data(ftdic, buffer, length);
	if (ret == -1) {
		return -1;
	}

	/* Check end code */
	ret = read_data(ftdic, header, 1);
	if (ret == -1) {
		return -1;
	}

	if (header[0] != MSG_END_CODE) {
		fprintf(stderr, "End code not at expected location.\n");
		return -1;
	}

	// Copy The Data read to the buffer passed
	appmsg->label = label;
	appmsg->length = length;
	memcpy(appmsg->data, buffer, length);
	return label;
}

void
read_dmx_usb_mk2_pro(struct thread_arguments *thread_args) {
	struct ftdi_context *ftdic = thread_args->ftdic;
	dmx_updated_callback_t dmx_callback;
	int ret;
	unsigned char dmx_state[512];
	struct application_message appmsg;

	dmx_callback = thread_args->dmx_callback;
	free(thread_args);

	while (running) {
		watchdog_dmx_pong = 1;
		ret = receive_msg(ftdic, &appmsg);

		if (ret < 0) {
			fprintf(stderr, "Error occurred during receive. Purging receive buffer and retrying.\n");
			ret = purge_receive_buffer(ftdic);
			continue;
		}

		/* TODO handle everything that is not DMX message. */
		if (appmsg.label != 5) {
			fprintf(stderr, "Unable to handle message with label %d.\n", appmsg.label);
			continue;
		}

		if ((appmsg.data[0] & 0x02) != 0) {
			fprintf(stderr, "Widget receive overrun occurred. DMX Data invalid.\n");
			continue;
		} else if ((appmsg.data[0] & 0x01) != 0) {
			fprintf(stderr, "Widget receive queue overflowed. DMX Data invalid.\n");
			continue;
		}

		if (appmsg.data[1] != 0) {
			fprintf(stderr, "Received DMX start code not equal to 0x00: 0x%X\n", appmsg.data[1]);
		}

		for (int i = 0; i < 512; ++i) {
			if (appmsg.data[i + 2] != dmx_state[i]) {
				fprintf(stderr, "Channel %d changed to value %d\n", i + 1, appmsg.data[i + 2]);
				/* todo callback */
				dmx_callback(i, dmx_state[i], appmsg.data[i+2]);
				dmx_state[i] = appmsg.data[i+2];
			}
		}
	}
}

static void *
read_dmx_usb_mk2_pro_runner(void *thread_args) {
	read_dmx_usb_mk2_pro(thread_args);
	return NULL;
}


int
connect_dmx_usb_mk2_pro(struct ftdi_context *ftdic) {
	int ret;

	/* Search for the FTDI device powering the Enttec DMX USB Mk2 Pro: vendor 0x0403, product 0x6001,
	   description DMX USB PRO Mk2, serial ENVWI3AT (but unnecessary so NULL). */
	ret = ftdi_usb_open_desc(ftdic, 0x0403, 0x6001, "DMX USB PRO Mk2", NULL);
	switch (ret) {
		case -3:
			fprintf(stderr, "USB device not found\n");
			break;
		case -4:
			fprintf(stderr, "Unable to open device\n");
			break;
		case -5:
			fprintf(stderr, "Unable to claim device\n");
			break;
		case -6:
			fprintf(stderr, "Device reset failed\n");
			break;
		case -7:
			fprintf(stderr, "Setting baud rate failed\n");
			break;
		case -8:
			fprintf(stderr, "Getting product description failed\n");
			break;
		case -9:
			fprintf(stderr, "Getting serial number failed\n");
			break;
		case -12:
			fprintf(stderr, "libusb_get_device_list() failed\n");
			break;
		case -13:
			fprintf(stderr, "libusb_get_device_descriptor() failed\n");
			break;
	}
	return ret;
}


int
enable_second_universe(struct ftdi_context *ftdic) {
	unsigned char port_set[] = { 1, 1};
	int ret = 0;

	ret = send_msg(ftdic, SET_API_KEY, APIKey, 4);
	if (ret != 0) {
		fprintf(stderr, "Setting API key failed.");
		return ret;
	}

	ret = send_msg(ftdic, SET_PORT_ASSIGNMENT, port_set, 2);
	if (ret != 0) {
		fprintf(stderr, "Setting port assignment failed.");
		return ret;
	}

	return 0;
}


/**
 * on_change: 0 if we want to receive every DMX message, 1 if we want to receive only changes.
 */
int
set_dmx_recv_mode(struct ftdi_context *ftdic, unsigned char on_change) {
	int ret;

	ret = send_msg(ftdic, RECEIVE_DMX_ON_CHANGE_1, &on_change, 1);
	if (ret != 0) {
		fprintf(stderr, "Setting receive mode failed.");
	}

	return ret;
}


struct ftdi_context *
init_dmx_usb_mk2_pro(dmx_updated_callback_t dmx_callback) {
	int ret;
	struct ftdi_context *ftdic;
	struct thread_arguments *thread_args;

	thread_args = malloc(sizeof(struct thread_arguments));
	if (thread_args == NULL) {
		fprintf(stderr, "Error allocating memory for thread_arguments.");
		exit(1);
	}

	ftdic = ftdi_new();
	if (ftdic == NULL) {
		fprintf(stderr, "ftdi_init failed\n");
		return NULL;
	}

	ret = connect_dmx_usb_mk2_pro(ftdic);
	if (ret != 0) {
		ftdi_free(ftdic);
		return NULL;
	}

	purge_buffers(ftdic);

	ret = enable_second_universe(ftdic);
	ret = set_dmx_recv_mode(ftdic, 0);

	thread_args->ftdic = ftdic;
	thread_args->dmx_callback = dmx_callback;
	running = 1;
	pthread_create(&readid, NULL, read_dmx_usb_mk2_pro_runner, thread_args);

	return ftdic;
}


void
teardown_dmx_usb_mk2_pro(struct ftdi_context *ftdic) {
	running = 0;
	pthread_join(readid, NULL);
	purge_buffers(ftdic);
	ftdi_usb_close(ftdic);
	ftdi_free(ftdic);
	return;
}

