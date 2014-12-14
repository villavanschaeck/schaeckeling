#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "input-config.h"
#include "schaeckeling.h"
#include "dmxdriver.h"
#include "dmxd.h"
#include "nanokontroldriver.h"
#include "usbmididriver.h"

struct mk2_pro_context *mk2c;
#ifndef DISABLE_NANOKONTROL
struct nanokontrol2_context *nanokontrol2 = NULL;
#endif
#ifndef DISABLE_USBMIDI
struct usbmidi_context *usbmidi = NULL;
#endif
volatile int mk2c_lost = 0;
volatile int nanokontrol_lost = 0;
volatile int midi_lost = 0;

volatile int receiving_changes = 0;


void
midi_changed(midichannel_t channel, unsigned char value) {
	assert(channel >= 0 && channel < MIDI_CHANNELS);
	assert(value <= 127);
	value *= 2;
	fprintf(stdout, "midi_changed(%d, %d)\n", channel, (int)value);
	receiving_changes = 1;
	update_input(midi_to_input_index(channel), value);
}


void
midi_input_completed(void) {
	receiving_changes = 0;
	flush_dmxout_sendbuf();
//	update_websockets(0, 1);
}


void
dmx_changed(dmxchannel_t channel, unsigned char old, unsigned char new) {
	assert(channel >= 0 && channel < DMX_CHANNELS);
	++channel;
	fprintf(stdout, "dmx_changed(%d, %d, %d)\n", channel, (int)old, (int)new);
	receiving_changes = 1;
	update_input(dmx_to_input_index(channel), new);
}


void
dmx_input_completed(void) {
	receiving_changes = 0;
	flush_dmxout_sendbuf();
	update_websockets(0, 1);
}


void
mk2c_error(int error) {
	fprintf(stderr, "mk2c_error: %d\n", error);
	mk2c_lost = 1;
	error_step();
}


void
nanokontrol_error(int error) {
	fprintf(stderr, "nanokontrol_error: %d\n", error);
	nanokontrol_lost = 1;
	error_step();
}


void
generic_midi_error(int error) {
	fprintf(stderr, "generic_midi_error: %d\n", error);
	midi_lost = 1;
	error_step();
}


void
reconnect_if_needed(void) {
	if (mk2c_lost) {
		if (mk2c != NULL) {
			teardown_dmx_usb_mk2_pro(mk2c);
		}
		mk2c = init_dmx_usb_mk2_pro(dmx_changed, dmx_input_completed, mk2c_error);
		if (mk2c != NULL) {
			mk2c_lost = 0;
			flush_dmxout_sendbuf();
		}
	}
	if (midi_lost) {

	}
	if (nanokontrol_lost) {

	}
}

int
init_communications(void) {
	int has_any_input = 0;
	mk2c = init_dmx_usb_mk2_pro(dmx_changed, dmx_input_completed, mk2c_error);
	if (mk2c == NULL) {
		abort(); // XXX
	}
	// TODO auto-detection of nanokontrol and generic midi.
#ifndef DISABLE_NANOKONTROL
	nanokontrol2 = init_nanokontrol2("/dev/snd/midiC1D0");
	if (nanokontrol2 != NULL) {
		has_any_input = 1;
	} else {
		fprintf(stderr, "init_communications: init_nanokontrol2 failed.");
	}
#endif
#ifndef DISABLE_USBMIDI
	usbmidi = init_usbmidi("/dev/snd/midiC1D0");
	if (usbmidi != NULL) {
		has_any_input = 1;
	} else {
		fprintf(stderr, "init_communications: init_usbmidi failed.");
	}
#endif
	if (has_any_input) {
		fprintf(stderr, "No input devices available. Defaulting to preprogrammed output.");
	}

	// Fix nanokontrol and usb-midi.
	return 0;
}


int
send_dmx(unsigned char *dmxbytes) {
	int ret = -2;
	if (mk2c != NULL) {
		ret = mk2_send_dmx(mk2c, dmxbytes);
	}
	return ret;
}

void
set_feedback_running(int running) {
#ifndef DISABLE_NANOKONTROL
	if(nanokontrol2 != NULL) {
		nanokontrol2_set_led(nanokontrol2, NANOKONTROL2_BTN_PLAY, running * 127);
		if(!running) {
			nanokontrol2_set_led(nanokontrol2, NANOKONTROL2_BTN_CYCLE, 0);
		}
	}
#endif
}

void
set_feedback_blackout(int blackout) {
#ifndef DISABLE_NANOKONTROL
	if(nanokontrol2 != NULL) {
		nanokontrol2_set_led(nanokontrol2, NANOKONTROL2_BTN_RECORD, blackout * 127);
	}
#endif
}

void
set_feedback_step() {
#ifndef DISABLE_NANOKONTROL
	if(nanokontrol2 != NULL) {
		static int prev = 0;
		prev = 127 - prev;
		nanokontrol2_set_led(nanokontrol2, NANOKONTROL2_BTN_CYCLE, prev);
	}
#endif
}
