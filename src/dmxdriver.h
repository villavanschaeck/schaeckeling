#ifndef _PRO_DRIVER_H
#define _PRO_DRIVER_H

#include <ftdi.h>
#include <stdio.h>
#include <unistd.h>

typedef void (*dmx_updated_callback_t) (int, unsigned char, unsigned char);

/**************************************************
 Function prototypes.
**************************************************/

struct ftdi_context * init_dmx_usb_mk2_pro(dmx_updated_callback_t dmx_callback);
void teardown_dmx_usb_mk2_pro(struct ftdi_context *ftdic);
void send_dmx(struct ftdi_context *ftdic, unsigned char *dmxbytes);
#endif

