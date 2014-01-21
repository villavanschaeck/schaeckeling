#ifndef _PRO_DRIVER_H
#define _PRO_DRIVER_H

#include <ftdi.h>
#include <stdio.h>
#include <unistd.h>

typedef void (*dmx_updated_callback_t) (int, unsigned char, unsigned char);

struct mk2_pro_context;

/**************************************************
 Function prototypes.
**************************************************/

struct mk2_pro_context * init_dmx_usb_mk2_pro(dmx_updated_callback_t dmx_callback);
void teardown_dmx_usb_mk2_pro(struct mk2_pro_context *mk2c);
void send_dmx(struct mk2_pro_context *mk2c, unsigned char *dmxbytes);
#endif
