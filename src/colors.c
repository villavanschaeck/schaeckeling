#include <assert.h>

#include "colors.h"

struct mapping_entry {
	unsigned char from;
	unsigned char red;
	unsigned char green;
	unsigned char blue;
};

static struct mapping_entry mapping[] = {
	//    r    g    b
	{  0, 220, 200, 100},	// White
	{  9, 255, 200, 150},	// Cool white
	{ 18, 255, 255, 255},	// Very cool white
	{ 27, 255,   0,   0},	// Red
	{ 36, 255,  50,   0},	// Orange
	{ 45, 255, 150,   0},	// Yellow
	{ 54, 255, 255,   0},	// Greenish yellow
	{ 63, 200, 255,   0},	// Yellowish green
	{ 72, 100, 255,   0},	// Light green
	{ 81,  40, 255,   0},	// Green
	{ 90,   0, 255,   0},	// Dark green
	{ 99,   0, 255,  50},	// Turqoise
	{108,   0, 255, 150},	// Light turqoise
	{135,   0, 255, 255},	// Very light blue
	{126,   0, 150, 255},	// Light blue
	{135,   0,  50, 255},	// Blue
	{144,   0,   0, 255},	// Dark blue
	{153,  50,   0, 255},	// Lila
	{162, 150,   0, 255},	// Light pink
	{171, 255,   0, 255},	// Pink
	{180, 220,   0,  50},	// Magenta
	{189, 150,  50, 100},	// 
	{198,  50, 180, 220},	// 
	{207,  50, 220, 100},	// 
	{216, 150, 220,   0},	// 
	{225, 150,   0, 220},	// 
	{234,   0, 180, 220},	// 
	{243,   0, 220,  50},	// 
	{252,   0,   0,   0}	// Off
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
