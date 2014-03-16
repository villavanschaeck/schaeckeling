#include "mididriver.h"

struct usbmidi_context {
	struct midi_context midictx;
};

void usbmidi_set_led(struct usbmidi_context *ctx, int which, int value);
struct usbmidi_context *init_usbmidi(char *path);
void teardown_usbmidi(struct usbmidi_context *ctx);
void usbmidi_ask_status(struct usbmidi_context *ctx);
