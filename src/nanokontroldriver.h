#include "mididriver.h"

#define NANOKONTROL2_MODE_NORMAL	0
#define NANOKONTROL2_MODE_NATIVE	1

struct nanokontrol2_context {
	struct midi_context midictx;
	int active_mode;
	int requested_mode;
};

void nanokontrol2_set_led(struct nanokontrol2_context *ctx, int which, int value);
struct nanokontrol2_context *init_nanokontrol2(char *path);
void teardown_nanokontrol2(struct nanokontrol2_context *ctx);
void nanokontrol2_ask_status(struct nanokontrol2_context *ctx);
