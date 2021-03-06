#define _POSIX_C_SOURCE 199309L


#include <pthread.h>
#include "dmxdriver.h"
#include "api.h"
#include <string.h>
#include <time.h>
#include <assert.h>

extern int watchdog_dmx_pong;

static int
purge_buffers(struct ftdi_context *ftdic) {
	int ret;
	ret = ftdi_usb_purge_buffers(ftdic);
	if (ret == -1) {
		fprintf(stderr, "purge_buffers: Read buffer purge failed\n");
	} else if (ret == -2) {
		fprintf(stderr, "purge_buffers: Write buffer purge failed\n");
	} else if (ret == -3) {
		fprintf(stderr, "purge_buffers: USB device unavailable\n");
	}
	return ret;
}


static int
purge_receive_buffer(struct ftdi_context *ftdic) {
	int ret;
	ret = ftdi_usb_purge_rx_buffer(ftdic);
	if (ret == -1) {
		fprintf(stderr, "purge_receive_buffer: Read buffer purge failed\n");
	} else if (ret == -2) {
		fprintf(stderr, "purge_receive_buffer: USB device unavailable\n");
	}
	return ret;
}


/*
 * Function that writes exactly length bytes to the ftdi device.
 * Buffer must be at least length bytes in length.
 */
static int
write_data(struct ftdi_context *ftdic, unsigned char *buffer, int length) {
	int ret;
	int bytes_written = 0;

	while (bytes_written < length) {
		ret = ftdi_write_data(ftdic, buffer + bytes_written, length - bytes_written);
		if (ret < 0) {
			if (ret == -666) {
				fprintf(stderr, "write_data: USB Device Unavailable for ftdi_write_data\n");
			} else if (ret < 0) {
				fprintf(stderr, "write_data: ftdi_write_data returned error %d from usb_bulk_write\n", ret);
			}
			return -1;
		}
		bytes_written += ret;
	}

	assert(bytes_written == length);
	return bytes_written;
}

/*
 * prepare_msg_buffer: counterpart to send_msg. Used to prepare a message buffer for
 * send_msg. Allocates enough bytes for the message header, message_size bytes, and 
 * the message_end_code. Then returns a pointer INSIDE THE BUFFER, PAST THE HEADER.
 * Also be careful of off-by-one errors, the final byte WILL be overwritten by send_msg.
 */
static unsigned char *
prepare_msg_buffer(int message_size) {
	unsigned char *buffer = (unsigned char *) malloc(MSG_HEADER_LENGTH + message_size + MSG_END_CODE_LENGTH);
	return buffer + MSG_HEADER_LENGTH;
}

/*
 * send_msg: counterpart to prepare_msg_buffer. Can only be called with a message buffer
 * received from prepare_msg_buffer. If you fail to comply, behaviour WILL BE UNDEFINED.
 * Rocks may fall. Pigs may fly. The earth may be torn asunder. Russia may invade your home country.
 *
 * After calling send_msg, the buffer passed in has been freed.
 */
static int
send_msg(struct ftdi_context *ftdic, int label, unsigned char *prepared_buffer, int length) {
	int ret = 0;

	// Decrease *prepared_buffer to "reclaim" the space allocated for the header.
	prepared_buffer -= MSG_HEADER_LENGTH;

	// Form Packet Header
	prepared_buffer[0] = MSG_START_CODE;
	prepared_buffer[1] = label;
	prepared_buffer[2] = length & OFFSET;
	prepared_buffer[3] = length >> BYTE_LENGTH;

	// Add end code
	length += MSG_HEADER_LENGTH;
	prepared_buffer[length] = MSG_END_CODE;

	// Write The Data
	length += MSG_END_CODE_LENGTH;
	ret = write_data(ftdic, prepared_buffer, length);
	if (ret != length) {
		fprintf(stderr, "send_msg: Unexpected number of bytes written: %d, should be %d\n", ret, length);
		return -1;
	}

	free(prepared_buffer);

	return 0;
}


/*
 * Send DMX data from the provided buffer dmxbytes.
 * Buffer HAS TO BE 512 bytes (or longer, only 512 bytes will be used).
 * DMX channels are 0-based in the buffer (DMX channel 1 == dmxbytes[0]).
 */
int
mk2_send_dmx(struct mk2_pro_context *mk2c, unsigned char *dmxbytes) {
	int ret;
	unsigned char *messagebuffer = prepare_msg_buffer(1 + DMX_PACKET_SIZE);
	if (messagebuffer == NULL) {
		fprintf(stderr, "send_dmx: Failed to allocate space for prepared buffer.\n");
		return -2;
	}

	// First byte has to be 0
	messagebuffer[0] = 0;
	memcpy(messagebuffer + 1, dmxbytes, DMX_PACKET_SIZE);

	// send the array here
	ret = send_msg(mk2c->ftdic, mk2c->device_type == ENTTEC_DMX_USB_PRO_MK2 ? SEND_DMX_2 : SEND_DMX_1, messagebuffer, 1 + DMX_PACKET_SIZE);
	if (ret < 0)
	{
		fprintf(stderr, "send_dmx: Failed to send DMX\n");
		return -1;
	}
	return 0;
}


static void
read_data_error(int ret) {
	if (ret == -666) {
		fprintf(stderr, "read_data: USB Device Unavailable for ftdi_read_data\n");
	} else if (ret < 0) {
		fprintf(stderr, "read_data: ftdi_read_data returned error %d from libusb_bulk_transfer\n", ret);
	} else {
		fprintf(stderr, "read_data: Unexpected number of bytes read: %d\n", ret);
	}
}


/*
 * Function that reads exactly length bytes from the ftdi device.
 * Buffer must be at least length bytes in length.
 *
 */
static int
read_data(struct mk2_pro_context *mk2c, unsigned char *buffer, int length) {
	int ret;
	int bytes_read = 0;

	while (bytes_read < length && mk2c->running) {
		watchdog_dmx_pong = 1;
		//fprintf(stderr, "Reading data, still %d to read.\n", length - bytes_read);
		ret = ftdi_read_data(mk2c->ftdic, buffer + bytes_read, length - bytes_read);
		if (ret < 0) {
			read_data_error(ret);
			return -1;
		}
		bytes_read += ret;
	}
	if (!mk2c->running) {
		pthread_exit(NULL);
	}

	assert(bytes_read == length);
	return bytes_read;
}


static int
receive_msg(struct mk2_pro_context *mk2c, struct application_message *appmsg) {
	int ret;
	int label;
	int length = 0;
	unsigned char header[MSG_HEADER_LENGTH];
	unsigned char buffer[600];

	/* Read header */
	//fprintf(stderr, "Reading new packet header.\n");
	ret = read_data(mk2c, header, MSG_HEADER_LENGTH);
	if (ret == -1) {
		fprintf(stderr, "receive_msg: Unable to read message header\n");
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
			fprintf(stderr, "receive_msg: Received start code 0x%X, label %u, length %u\n", header[0], label, length);
			fprintf(stderr, "receive_msg: Header invalid. Cycling...\n");
			
			/* Move the header one byte, discarding the "wrong" MSG_START_CODE, then read another byte and repeat */
			memmove(header, header + 1, MSG_HEADER_LENGTH - 1);
			ret = read_data(mk2c, header + MSG_HEADER_LENGTH - 1, 1);
			if (ret != 1) {
				return -1;
			}
		} else {
			//fprintf(stderr, "Header is sane. Reading %u bytes.\n", length);
			break;
		}
	}
	
	ret = read_data(mk2c, buffer, length);
	if (ret == -1) {
		fprintf(stderr, "receive_msg: Unable to read message data\n");
		return -1;
	}

	/* Check end code */
	ret = read_data(mk2c, header, 1);
	if (ret == -1) {
		fprintf(stderr, "receive_msg: Unable to read message end code\n");
		return -1;
	}

	if (header[0] != MSG_END_CODE) {
		fprintf(stderr, "receive_msg: End code not at expected location\n");
		return -1;
	}

	// Copy The Data read to the buffer passed
	appmsg->label = label;
	appmsg->length = length;
	memcpy(appmsg->data, buffer, length);
	return label;
}

static void
read_dmx_usb_mk2_pro(struct mk2_pro_context *mk2c) {
	int ret;
	unsigned char dmx_state[512];
	struct application_message appmsg;

	while (mk2c->running) {
		ret = receive_msg(mk2c, &appmsg);

		if (ret < 0) {
			fprintf(stderr, "read_dmx_usb_mk2_pro: Error occurred during receive. Purging receive buffer and retrying.\n");
			ret = purge_receive_buffer(mk2c->ftdic);
			if (ret != 0) {
				fprintf(stderr, "read_dmx_usb_mk2_pro: Error purging receive buffer. Signalling error and killing thread.\n");
				mk2c->running = 0;
				mk2c->error_callback(-1);
				pthread_exit(NULL);
			}
			continue;
		}

		/* TODO handle everything that is not DMX message. */
		if (appmsg.label != 5) {
			fprintf(stderr, "read_dmx_usb_mk2_pro: Unable to handle message with label %d.\n", appmsg.label);
			continue;
		}

		if ((appmsg.data[0] & 0x02) != 0) {
			fprintf(stderr, "read_dmx_usb_mk2_pro: Widget receive overrun occurred. DMX Data invalid.\n");
			continue;
		} else if ((appmsg.data[0] & 0x01) != 0) {
			fprintf(stderr, "read_dmx_usb_mk2_pro: Widget receive queue overflowed. DMX Data invalid.\n");
			continue;
		}

		if (appmsg.data[1] != 0) {
			fprintf(stderr, "read_dmx_usb_mk2_pro: WARNING: Received DMX start code not equal to 0x00: 0x%X\n", appmsg.data[1]);
		}

		for (int i = 0; i < 512; ++i) {
			if (appmsg.data[i + 2] != dmx_state[i]) {
				//fprintf(stderr, "read_dmx_usb_mk2_pro: Channel %d changed to value %d\n", i + 1, appmsg.data[i + 2]);
				mk2c->update_callback(i, dmx_state[i], appmsg.data[i+2]);
				dmx_state[i] = appmsg.data[i+2];
			}
		}
		mk2c->commit_callback();
	}
}

static void *
read_dmx_usb_mk2_pro_runner(void *mk2c) {
	read_dmx_usb_mk2_pro(mk2c);
	return NULL;
}


static int
connect_dmx_usb_mk2_pro(struct mk2_pro_context *mk2c) {
	int ret;

	/* Search for the FTDI device powering the Enttec DMX USB Mk2 Pro: vendor 0x0403, product 0x6001,
	   description DMX USB PRO Mk2, serial ENVWI3AT (but unnecessary so NULL). */
	mk2c->device_type = ENTTEC_DMX_USB_PRO_MK2;
	ret = ftdi_usb_open_desc(mk2c->ftdic, 0x0403, 0x6001, "DMX USB PRO Mk2", NULL);
	if(ret == -3) {
		mk2c->device_type = ENTTEC_DMX_USB_PRO;
		ret = ftdi_usb_open_desc(mk2c->ftdic, 0x0403, 0x6001, "DMX USB PRO", NULL);
		if(ret == -3) {
			mk2c->device_type = 0;
			ret = ftdi_usb_open_desc(mk2c->ftdic, 0x0403, 0x6001, NULL, NULL);
		}
	}
	switch (ret) {
		case -3:
			fprintf(stderr, "connect_dmx_usb_mk2_pro: USB device not found\n");
			break;
		case -4:
			fprintf(stderr, "connect_dmx_usb_mk2_pro: Unable to open device\n");
			break;
		case -5:
			fprintf(stderr, "connect_dmx_usb_mk2_pro: Unable to claim device\n");
			break;
		case -6:
			fprintf(stderr, "connect_dmx_usb_mk2_pro: Device reset failed\n");
			break;
		case -7:
			fprintf(stderr, "connect_dmx_usb_mk2_pro: Setting baud rate failed\n");
			break;
		case -8:
			fprintf(stderr, "connect_dmx_usb_mk2_pro: Getting product description failed\n");
			break;
		case -9:
			fprintf(stderr, "connect_dmx_usb_mk2_pro: Getting serial number failed\n");
			break;
		case -12:
			fprintf(stderr, "connect_dmx_usb_mk2_pro: libusb_get_device_list() failed\n");
			break;
		case -13:
			fprintf(stderr, "connect_dmx_usb_mk2_pro: libusb_get_device_descriptor() failed\n");
			break;
	}
	return ret;
}


static int
enable_second_universe(struct ftdi_context *ftdic) {
	int ret = 0;
	unsigned char *port_set_buf = prepare_msg_buffer(2);
	unsigned char *api_key_buf = prepare_msg_buffer(4);
	if (port_set_buf == NULL || api_key_buf == NULL) {
		fprintf(stderr, "enable_second_universe: Failed to allocate space for message buffer.\n");
		return -1;
	}

	memcpy(api_key_buf, APIKey, 4);
	ret = send_msg(ftdic, SET_API_KEY, api_key_buf, 4);
	if (ret != 0) {
		fprintf(stderr, "enable_second_universe: Setting API key failed.\n");
		return ret;
	}

	port_set_buf[0] = 1;
	port_set_buf[1] = 1;
	ret = send_msg(ftdic, SET_PORT_ASSIGNMENT, port_set_buf, 2);
	if (ret != 0) {
		fprintf(stderr, "enable_second_universe: Setting port assignment failed.\n");
		return ret;
	}

	return 0;
}


/**
 * on_change: 0 if we want to receive every DMX message, 1 if we want to receive only changes.
 */
static int
set_dmx_recv_mode(struct ftdi_context *ftdic, unsigned char on_change) {
	int ret;

	unsigned char *on_change_buf = prepare_msg_buffer(1);

	if (on_change_buf == NULL) {
		fprintf(stderr, "set_dmx_recv_mode: Failed to allocate space for message buffer.\n");
		return -1;
	}

	on_change_buf[0] = on_change;
	ret = send_msg(ftdic, RECEIVE_DMX_ON_CHANGE_1, on_change_buf, 1);
	if (ret != 0) {
		fprintf(stderr, "set_dmx_recv_mode: Setting receive mode failed.\n");
	}

	return ret;
}


struct mk2_pro_context *
init_dmx_usb_mk2_pro(dmx_update_callback_t update_callback, dmx_commit_callback_t commit_callback, dmx_error_callback_t error_callback) {
	int ret;
	struct mk2_pro_context *mk2c;

	fprintf(stderr, "init_dmx_usb_mk2_pro: Initializing and connecting.\n");

	mk2c = malloc(sizeof(struct mk2_pro_context));
	if (mk2c == NULL) {
		fprintf(stderr, "init_dmx_usb_mk2_pro: Error allocating memory for mk2_pro_context.\n");
		return NULL;
	}

	mk2c->ftdic = ftdi_new();
	if (mk2c->ftdic == NULL) {
		free(mk2c);
		fprintf(stderr, "init_dmx_usb_mk2_pro: ftdi_init failed\n");
		return NULL;
	}

	ret = connect_dmx_usb_mk2_pro(mk2c);
	if (ret != 0) {
		fprintf(stderr, "init_dmx_usb_mk2_pro: Failed to connect\n");
		goto error;
	}

	ret = purge_buffers(mk2c->ftdic);
	if (ret != 0) {
		fprintf(stderr, "init_dmx_usb_mk2_pro: Failed to purge buffers\n");
		goto error;
	}

	if(mk2c->device_type == 2) {
		ret = enable_second_universe(mk2c->ftdic);
		if(ret != 0) {
			fprintf(stderr, "init_dmx_usb_mk2_pro: Failed to enable second universe\n");
			goto error;
		}
		ret = set_dmx_recv_mode(mk2c->ftdic, 0);
		if(ret != 0) {
			fprintf(stderr, "init_dmx_usb_mk2_pro: Failed to set dmx receive mode\n");
			goto error;
		}
	}

	mk2c->update_callback = update_callback;
	mk2c->commit_callback = commit_callback;
	mk2c->error_callback = error_callback;
	mk2c->running = 1;
	ret = pthread_create(&mk2c->readid, NULL, read_dmx_usb_mk2_pro_runner, mk2c);
	if (ret != 0) {
		fprintf(stderr, "init_dmx_usb_mk2_pro: %s\n", strerror(ret));
		goto error;
	}

	fprintf(stderr, "init_dmx_usb_mk2_pro: Connected successfully\n");
	return mk2c;

error:
	ftdi_free(mk2c->ftdic);
	free(mk2c);
	return NULL;
}


void
teardown_dmx_usb_mk2_pro(struct mk2_pro_context *mk2c) {
	int ret;
	fprintf(stderr, "teardown_dmx_usb_mk2_pro: starting teardown / disconnect.\n");
	mk2c->running = 0;
	ret = pthread_join(mk2c->readid, NULL);
	if (ret != 0) {
		fprintf(stderr, "teardown_dmx_usb_mk2_pro: %s\n", strerror(ret));
	}
	purge_buffers(mk2c->ftdic);
	ftdi_usb_close(mk2c->ftdic);
	ftdi_free(mk2c->ftdic);
	free(mk2c);
	fprintf(stderr, "teardown_dmx_usb_mk2_pro: teardown / disconnect completed.\n");
}
