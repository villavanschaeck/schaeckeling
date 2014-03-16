#ifndef _PRO_DRIVER_H
#define _PRO_DRIVER_H

#include <ftdi.h>
#include <stdio.h>
#include <unistd.h>

typedef void (*dmx_update_callback_t) (int, unsigned char, unsigned char);
typedef void (*dmx_commit_callback_t) ();
typedef void (*dmx_error_callback_t) (int);

struct mk2_pro_context;

/**************************************************
 Function prototypes.
**************************************************/

struct mk2_pro_context * init_dmx_usb_mk2_pro(dmx_update_callback_t update_callback, dmx_commit_callback_t commit_callback, dmx_error_callback_t error_callback);
void teardown_dmx_usb_mk2_pro(struct mk2_pro_context *mk2c);
int mk2_send_dmx(struct mk2_pro_context *mk2c, unsigned char *dmxbytes);
#endif
