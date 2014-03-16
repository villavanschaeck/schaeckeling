#include "mididriver.h"

#define NANOKONTROL2_MODE_NORMAL	0
#define NANOKONTROL2_MODE_NATIVE	1

#define NANOKONTROL2_BTN_TRACK_PREV	0x3A
#define NANOKONTROL2_BTN_TRACK_NEXT	0x3B
#define NANOKONTROL2_BTN_MARKER_SET	0x3C
#define NANOKONTROL2_BTN_MARKER_PREV	0x3D
#define NANOKONTROL2_BTN_MARKER_NEXT	0x3E
#define NANOKONTROL2_BTN_CYCLE	0x2E
#define NANOKONTROL2_BTN_REWIND	0x2B
#define NANOKONTROL2_BTN_FORWARD	0x2C
#define NANOKONTROL2_BTN_STOP	0x2A
#define NANOKONTROL2_BTN_PLAY	0x29
#define NANOKONTROL2_BTN_RECORD	0x2D

#define NANOKONTROL2_CHAN_KNOB_OFFSET	0x10
#define NANOKONTROL2_CHAN_S_OFFSET	0x20
#define NANOKONTROL2_CHAN_M_OFFSET	0x30
#define NANOKONTROL2_CHAN_R_OFFSET	0x40

struct nanokontrol2_context {
	struct midi_context midictx;
	int active_mode;
	int requested_mode;
};

void nanokontrol2_set_led(struct nanokontrol2_context *ctx, int which, int value);
struct nanokontrol2_context *init_nanokontrol2(char *path);
void teardown_nanokontrol2(struct nanokontrol2_context *ctx);
void nanokontrol2_ask_status(struct nanokontrol2_context *ctx);
