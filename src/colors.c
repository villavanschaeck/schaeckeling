#include <assert.h>

#include "colors.h"

struct mapping_entry {
	unsigned char from;
	unsigned char red;
	unsigned char green;
	unsigned char blue;
};

static struct mapping_entry mapping[] = {
	//      r    g    b
	{0,   255,   0,   0},	// Red
	{9,   255,  50,   0},	// Orange
	{18,  255, 150,   0},	// Yellow
	{27,  255, 255,   0},	// Greenish yellow
	{36,  200, 255,   0},	// Yellowish green
	{45,  100, 255,   0},	// Light green
	{54,   40, 255,   0},	// Green
	{63,    0, 255,   0},	// Dark green
	{72,    0, 255,  50},	// Turqoise
	{81,    0, 255, 150},	// Light turqoise
	{90,    0, 255, 255},	// Very light blue
	{99,    0, 150, 255},	// Light blue
	{108,   0,  50, 255},	// Blue
	{117,   0,   0, 255},	// Dark blue
	{126,  50,   0, 255},	// Lila
	{135, 150,   0, 255},	// Light pink
	{144, 255,   0, 255},	// Pink
	{153, 220,   0,  50},	// Magenta
	{162, 150,  50, 100},	// 
	{171,  50, 180, 220},	// 
	{180,  50, 220, 100},	// 
	{189, 150, 220,   0},	// 
	{198, 150,   0, 220},	// 
	{207,   0, 180, 220},	// 
	{216,   0, 220,  50},	// 
	{225, 220, 100,  50},	// Orange white
	{234, 220, 200, 100},	// White
	{243, 255, 200, 150},	// Cool white
	{252, 255, 255, 255}	// Very cool white
};

void
convert_color(unsigned char input, unsigned char *output) {
	int i;
	for(i = sizeof(mapping) / sizeof(struct mapping_entry) - 1; i >= 0; i--) {
		if(input >= mapping[i].from) {
			output[0] = mapping[i].red;
			output[1] = mapping[i].green;
			output[2] = mapping[i].blue;
			return;
		}
	}
	assert(!"reached");
}

void
convert_color_and_intensity(unsigned char color, unsigned char intensity, unsigned char *output) {
	int i;
	convert_color(color, output);
	for(i = 0; 3 > i; i++) {
		output[i] = ((int)output[i]) * ((int)intensity) / 255;
	}
}
